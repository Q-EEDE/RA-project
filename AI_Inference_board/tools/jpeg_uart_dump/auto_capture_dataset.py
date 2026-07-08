#!/usr/bin/env python3
"""Capture JPEG dumps from the RA6M5 debug UART into dataset folders.

Two usage modes are supported:
1. Active mode: use --host-port to send "$TRIG" to the inference board.
2. Passive mode: omit --host-port and press the board capture button manually.
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from pathlib import Path


DEFAULT_DATASET_DIR = Path(__file__).resolve().parents[3] / "fruit_dataset"
DEFAULT_LOG_DIR = Path(__file__).resolve().parent / "logs"
CLASS_DIRS = {
    "a": "A",
    "A": "A",
    "b": "B",
    "B": "B",
    "c": "C",
    "C": "C",
    "u": "unknown",
    "U": "unknown",
    "unk": "unknown",
    "UNK": "unknown",
    "unknown": "unknown",
    "UNKNOWN": "unknown",
}


class CaptureError(RuntimeError):
    """Raised when a UART JPEG frame is incomplete or invalid."""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Listen to SCI9 debug UART $JPG lines and save captured JPEGs "
            "to fruit_dataset/A, B, C, or unknown."
        )
    )
    parser.add_argument("--list-ports", action="store_true", help="List available serial ports and exit.")
    parser.add_argument("--debug-port", help="Serial port connected to SCI9 debug/JPEG output, for example COM6.")
    parser.add_argument("--host-port", help="Optional serial port connected to SCI2 host link for sending $TRIG.")
    parser.add_argument("--baud", type=int, default=115200, help="UART baud rate. Default: 115200.")
    parser.add_argument(
        "--class",
        dest="class_label",
        required=False,
        choices=sorted(CLASS_DIRS),
        help="Dataset class: A, B, C, or unknown.",
    )
    parser.add_argument("--count", type=int, default=1, help="Number of images to save. Use 0 for endless capture.")
    parser.add_argument("--interval", type=float, default=1.0, help="Delay after each capture trigger, in seconds.")
    parser.add_argument("--timeout", type=float, default=30.0, help="Timeout for one JPEG frame, in seconds.")
    parser.add_argument("--retry", type=int, default=1, help="Retry count for each image if capture fails.")
    parser.add_argument(
        "--ready-timeout",
        type=float,
        default=8.0,
        help="Time to keep reading status lines after one JPEG is saved, in seconds.",
    )
    parser.add_argument("--start-index", type=int, help="First file index. Default: next available index.")
    parser.add_argument("--prefix", help="Output file prefix. Default: A, B, C, or unknown.")
    parser.add_argument("--dataset-dir", type=Path, default=DEFAULT_DATASET_DIR, help="Dataset root folder.")
    parser.add_argument("--save-log", action="store_true", help="Also save the raw $JPG/$DBG lines as .log files.")
    parser.add_argument("--log-dir", type=Path, default=DEFAULT_LOG_DIR, help="Folder for --save-log output.")
    parser.add_argument("--echo", action="store_true", help="Print $DBG/$CLS/$ERR lines while capturing.")
    parser.add_argument("--trigger-command", default="$TRIG", help="Host command sent before each capture.")
    return parser.parse_args()


def import_serial_module():
    try:
        import serial  # type: ignore
        from serial.tools import list_ports  # type: ignore
    except ModuleNotFoundError as exc:
        raise SystemExit(
            "Missing Python package: pyserial\n"
            "Install it first:\n"
            "  python -m pip install pyserial"
        ) from exc

    return serial, list_ports


def list_serial_ports(list_ports_module) -> None:
    ports = list(list_ports_module.comports())

    if not ports:
        print("No serial ports found.")
        return

    for port in ports:
        description = port.description or ""
        print(f"{port.device}\t{description}")


def normalize_class_dir(class_label: str) -> str:
    return CLASS_DIRS[class_label.lower()]


def default_prefix_for_class(class_dir: str) -> str:
    return class_dir if class_dir != "unknown" else "unknown"


def find_next_index(output_dir: Path, prefix: str) -> int:
    pattern = re.compile(rf"^{re.escape(prefix)}(?P<index>\d+)\.jpg$", re.IGNORECASE)
    max_index = 0

    if output_dir.exists():
        for path in output_dir.iterdir():
            match = pattern.match(path.name)
            if match:
                max_index = max(max_index, int(match.group("index")))

    return max_index + 1


def parse_decimal(text: str, field_name: str) -> int:
    try:
        value = int(text, 10)
    except ValueError as exc:
        raise CaptureError(f"Invalid {field_name}: {text!r}") from exc

    if value < 0:
        raise CaptureError(f"{field_name} must not be negative.")

    return value


def rebuild_contiguous_bytes(chunks: dict[int, bytes], expected_len: int) -> bytes:
    if not chunks:
        raise CaptureError("No $JPG,DATA lines were received.")

    output = bytearray()
    expected_offset = 0

    for offset in sorted(chunks):
        chunk = chunks[offset]

        if offset != expected_offset:
            raise CaptureError(f"Missing JPEG data at offset {expected_offset}; next offset is {offset}.")

        output.extend(chunk)
        expected_offset += len(chunk)

    if len(output) != expected_len:
        raise CaptureError(f"JPEG length {len(output)} does not match expected length {expected_len}.")

    return bytes(output)


def validate_jpeg(jpeg: bytes) -> None:
    if len(jpeg) < 4:
        raise CaptureError("JPEG is too short.")

    if jpeg[:2] != b"\xff\xd8":
        raise CaptureError("JPEG does not start with FF D8.")

    if jpeg[-2:] != b"\xff\xd9":
        raise CaptureError("JPEG does not end with FF D9.")


def read_jpeg_from_debug_uart(debug_serial, timeout_s: float, echo: bool) -> tuple[bytes, list[str]]:
    deadline = time.monotonic() + timeout_s
    expected_len: int | None = None
    chunks: dict[int, bytes] = {}
    raw_lines: list[str] = []

    while time.monotonic() < deadline:
        raw_line = debug_serial.readline()

        if not raw_line:
            continue

        line = raw_line.decode("ascii", errors="ignore").strip()

        if not line:
            continue

        raw_lines.append(line)

        if echo and (line.startswith("$DBG,") or line.startswith("$CLS,") or line.startswith("$ERR,")):
            print(line)

        if not line.startswith("$JPG,"):
            continue

        parts = line.split(",", 3)

        if len(parts) < 3:
            continue

        record_type = parts[1]

        if record_type == "BEGIN":
            expected_len = parse_decimal(parts[2], "BEGIN length")
            chunks.clear()
            deadline = time.monotonic() + timeout_s
            print(f"Receiving JPEG: {expected_len} bytes")
        elif record_type == "DATA":
            if expected_len is None:
                continue

            if len(parts) != 4:
                raise CaptureError("$JPG,DATA line is incomplete.")

            offset = parse_decimal(parts[2], "DATA offset")
            hex_text = parts[3].strip()

            if (len(hex_text) == 0) or ((len(hex_text) % 2) != 0):
                raise CaptureError(f"Invalid hex data at offset {offset}.")

            try:
                chunks[offset] = bytes.fromhex(hex_text)
            except ValueError as exc:
                raise CaptureError(f"DATA contains non-hex characters at offset {offset}.") from exc
        elif record_type == "END":
            if expected_len is None:
                continue

            end_len = parse_decimal(parts[2], "END length")

            if end_len != expected_len:
                raise CaptureError(f"BEGIN length {expected_len} does not match END length {end_len}.")

            jpeg = rebuild_contiguous_bytes(chunks, expected_len)
            validate_jpeg(jpeg)
            return jpeg, raw_lines

    raise CaptureError(f"Timed out after {timeout_s:.1f} seconds waiting for a complete JPEG.")


def save_raw_log(log_dir: Path, stem: str, raw_lines: list[str]) -> None:
    log_dir.mkdir(parents=True, exist_ok=True)
    (log_dir / f"{stem}.log").write_text("\n".join(raw_lines) + "\n", encoding="utf-8")


def send_trigger(host_serial, command: str) -> None:
    if host_serial is None:
        print("Passive mode: press the board capture button once.")
        return

    frame = command.rstrip("\r\n") + "\r\n"
    host_serial.write(frame.encode("ascii"))
    host_serial.flush()
    print(f"Sent host trigger: {command}")


def wait_after_capture(debug_serial, timeout_s: float, echo: bool) -> None:
    deadline = time.monotonic() + timeout_s

    while time.monotonic() < deadline:
        raw_line = debug_serial.readline()

        if not raw_line:
            continue

        line = raw_line.decode("ascii", errors="ignore").strip()

        if not line:
            continue

        if echo and (line.startswith("$DBG,") or line.startswith("$CLS,") or line.startswith("$ERR,")):
            print(line)

        if line.startswith("$DBG,BUTTON,READY"):
            return

        if line.startswith("$DBG,BUTTON,CAPTURE,OK"):
            return


def main() -> None:
    args = parse_args()
    serial_module, list_ports_module = import_serial_module()

    if args.list_ports:
        list_serial_ports(list_ports_module)
        return

    if not args.debug_port:
        raise SystemExit("Missing --debug-port. Use --list-ports to check available COM ports.")

    if not args.class_label:
        raise SystemExit("Missing --class. Choose A, B, C, or unknown.")

    if args.count < 0:
        raise SystemExit("--count must be 0 or a positive integer.")

    class_dir = normalize_class_dir(args.class_label)
    prefix = args.prefix or default_prefix_for_class(class_dir)
    output_dir = args.dataset_dir / class_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    next_index = args.start_index if args.start_index is not None else find_next_index(output_dir, prefix)

    with serial_module.Serial(args.debug_port, args.baud, timeout=0.2) as debug_serial:
        host_serial = None

        try:
            if args.host_port:
                host_serial = serial_module.Serial(args.host_port, args.baud, timeout=0.2, write_timeout=2.0)

            debug_serial.reset_input_buffer()

            saved_count = 0

            while (args.count == 0) or (saved_count < args.count):
                stem = f"{prefix}{next_index:03d}"
                output_file = output_dir / f"{stem}.jpg"

                for attempt in range(args.retry + 1):
                    try:
                        print(f"\nCapture target: {output_file}")
                        send_trigger(host_serial, args.trigger_command)
                        jpeg, raw_lines = read_jpeg_from_debug_uart(debug_serial, args.timeout, args.echo)
                    except CaptureError as exc:
                        print(f"Capture failed: {exc}")
                        if attempt >= args.retry:
                            raise

                        print("Retrying...")
                        continue

                    output_file.write_bytes(jpeg)

                    if args.save_log:
                        save_raw_log(args.log_dir, stem, raw_lines)

                    print(f"Saved: {output_file} ({len(jpeg)} bytes)")
                    wait_after_capture(debug_serial, args.ready_timeout, args.echo)
                    next_index += 1
                    saved_count += 1
                    break

                if args.interval > 0:
                    time.sleep(args.interval)
        except KeyboardInterrupt:
            print("\nStopped by user.")
        finally:
            if host_serial is not None:
                host_serial.close()


if __name__ == "__main__":
    try:
        main()
    except CaptureError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
