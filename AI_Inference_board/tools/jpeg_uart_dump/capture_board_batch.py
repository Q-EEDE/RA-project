#!/usr/bin/env python3
"""Batch capture board JPEG dumps from the debug UART.

This script is intentionally small and tolerant for manual dataset capture:
each image gets its own timeout window, and a timeout does not stop the run.
"""

from __future__ import annotations

import argparse
import datetime as dt
import re
import sys
import time
from pathlib import Path


CLASS_DIRS = {
    "a": "A",
    "A": "A",
    "b": "B",
    "B": "B",
    "c": "C",
    "C": "C",
    "unknown": "unknown",
    "UNKNOWN": "unknown",
    "u": "unknown",
    "U": "unknown",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Capture JPEGs from RA debug UART into a dataset folder.")
    parser.add_argument("--debug-port", required=True, help="Debug/JPEG UART port, for example COM5.")
    parser.add_argument("--baud", type=int, default=115200, help="UART baud rate. Default: 115200.")
    parser.add_argument("--class", dest="class_label", required=True, choices=sorted(CLASS_DIRS))
    parser.add_argument("--count", type=int, default=1, help="Number of valid images to save.")
    parser.add_argument("--timeout", type=float, default=45.0, help="Timeout for one image window, in seconds.")
    parser.add_argument(
        "--max-timeouts",
        type=int,
        default=0,
        help="Stop after this many timeout windows. Use 0 to keep waiting until enough images are saved.",
    )
    parser.add_argument(
        "--dataset-dir",
        type=Path,
        default=Path(__file__).resolve().parents[3] / "fruit_dataset_balanced",
        help="Dataset root folder.",
    )
    parser.add_argument("--prefix", help="Output file prefix. Default: <class>_board_.")
    return parser.parse_args()


def import_serial_module():
    try:
        import serial  # type: ignore
    except ModuleNotFoundError as exc:
        raise SystemExit("Missing Python package: pyserial. Install with: python -m pip install pyserial") from exc

    return serial


def find_next_index(output_dir: Path, prefix: str) -> int:
    pattern = re.compile(rf"^{re.escape(prefix)}(?P<index>\d+)", re.IGNORECASE)
    max_index = 0

    if output_dir.exists():
        for path in output_dir.glob(f"{prefix}*.jpg"):
            match = pattern.match(path.name)
            if match:
                max_index = max(max_index, int(match.group("index")))

    return max_index + 1


def rebuild_payload(chunks: dict[int, bytes], total: int) -> tuple[bytes, list[int]]:
    payload = bytearray()
    missing: list[int] = []
    offset = 0

    while offset < total:
        chunk = chunks.get(offset)
        if chunk is None:
            missing.append(offset)
            offset += 32
            continue
        payload.extend(chunk)
        offset += len(chunk)

    return bytes(payload[:total]), missing


def read_one_jpeg(ser, timeout_s: float) -> tuple[bytes, str, str]:
    deadline = time.monotonic() + timeout_s
    text_buffer = ""
    collecting = False
    total: int | None = None
    chunks: dict[int, bytes] = {}
    last_inf = ""
    last_cls = ""

    while time.monotonic() < deadline:
        raw = ser.read(2048)
        if not raw:
            continue

        text_buffer += raw.decode("ascii", errors="ignore")

        while "\n" in text_buffer:
            line, text_buffer = text_buffer.split("\n", 1)
            line = line.strip("\r\x00 ")

            if not line:
                continue

            if line.startswith("$DBG,BUTTON") or line.startswith("$DBG,CAP") or line.startswith("$DBG,JDEC"):
                print(line, flush=True)
            elif line.startswith("$DBG,INF"):
                last_inf = line
                print(line, flush=True)
            elif line.startswith("$CLS"):
                last_cls = line
                print(line, flush=True)

            if line.startswith("$JPG,BEGIN,"):
                try:
                    total = int(line.split(",", 2)[2])
                except ValueError:
                    continue
                chunks.clear()
                collecting = True
                deadline = time.monotonic() + timeout_s
                print(line, flush=True)
                continue

            if collecting and line.startswith("$JPG,DATA,"):
                parts = line.split(",", 3)
                if len(parts) != 4:
                    continue
                try:
                    chunks[int(parts[2])] = bytes.fromhex(parts[3])
                except ValueError:
                    continue
                continue

            if collecting and line.startswith("$JPG,END,"):
                try:
                    end_total = int(line.split(",", 2)[2])
                except ValueError:
                    continue
                print(line, flush=True)

                if total is None or end_total != total:
                    raise RuntimeError("BEGIN/END JPEG length mismatch.")

                payload, missing = rebuild_payload(chunks, total)

                if missing:
                    raise RuntimeError(f"JPEG has missing chunks: {len(missing)}")

                if not (payload.startswith(b"\xff\xd8") and payload.endswith(b"\xff\xd9")):
                    raise RuntimeError("JPEG marker check failed.")

                return payload, last_inf, last_cls

    raise TimeoutError(f"Timed out after {timeout_s:.1f}s waiting for one complete JPEG.")


def main() -> int:
    args = parse_args()
    if args.count <= 0:
        raise SystemExit("--count must be positive.")

    serial = import_serial_module()
    class_dir = CLASS_DIRS[args.class_label.lower()]
    prefix = args.prefix or f"{class_dir}_board_"
    output_dir = args.dataset_dir / class_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    next_index = find_next_index(output_dir, prefix)

    print(f"PORT={args.debug_port}", flush=True)
    print(f"CLASS={class_dir}", flush=True)
    print(f"OUTPUT_DIR={output_dir}", flush=True)
    print(f"START_INDEX={next_index}", flush=True)
    print(f"TARGET_COUNT={args.count}", flush=True)
    print("Press the board capture button once for each image.", flush=True)

    saved = 0
    attempts = 0
    timeouts = 0

    with serial.Serial(args.debug_port, args.baud, timeout=0.2) as ser:
        ser.reset_input_buffer()

        while saved < args.count:
            attempts += 1
            print(f"\nWAIT image {saved + 1}/{args.count}, index {next_index:03d}, timeout {args.timeout:.0f}s", flush=True)

            try:
                jpeg, inf_line, cls_line = read_one_jpeg(ser, args.timeout)
            except TimeoutError as exc:
                timeouts += 1
                print(f"TIMEOUT: {exc}", flush=True)
                if args.max_timeouts and timeouts >= args.max_timeouts:
                    print(f"STOP max_timeouts={args.max_timeouts}, saved={saved}", flush=True)
                    return 2
                continue
            except RuntimeError as exc:
                print(f"BAD_CAPTURE: {exc}", flush=True)
                continue

            timeouts = 0
            timestamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
            output_file = output_dir / f"{prefix}{next_index:03d}_{timestamp}.jpg"
            output_file.write_bytes(jpeg)

            saved += 1
            print(f"SAVED {saved}/{args.count}: {output_file} ({len(jpeg)} bytes)", flush=True)
            if inf_line:
                print(f"INF={inf_line}", flush=True)
            if cls_line:
                print(f"CLS={cls_line}", flush=True)

            next_index += 1

    print(f"DONE saved={saved}, attempts={attempts}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
