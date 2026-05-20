#!/usr/bin/env python3
"""Restore a JPEG file from RA6M5 UART $JPG hex dump logs."""

from __future__ import annotations

import argparse
from pathlib import Path


DEFAULT_LOG = Path(__file__).resolve().parent / "logs" / "capture_log.txt.txt"
DEFAULT_OUTPUT = Path(__file__).resolve().parent / "output" / "capture.jpg"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Restore capture.jpg from UART log lines like $JPG,DATA,<offset>,<hex>."
    )
    parser.add_argument(
        "log_file",
        type=Path,
        nargs="?",
        default=DEFAULT_LOG,
        help="UART log text file.",
    )
    parser.add_argument(
        "output_file",
        type=Path,
        nargs="?",
        default=DEFAULT_OUTPUT,
        help="Output JPEG path. Default: tools/jpeg_uart_dump/output/capture.jpg",
    )
    return parser.parse_args()


def restore_jpeg_from_log(log_file: Path) -> bytes:
    expected_len: int | None = None
    end_len: int | None = None
    chunks: dict[int, bytes] = {}

    with log_file.open("r", encoding="utf-8", errors="ignore") as file:
        for line_number, raw_line in enumerate(file, start=1):
            line = raw_line.strip()

            if not line.startswith("$JPG,"):
                continue

            parts = line.split(",", 3)

            if len(parts) < 3:
                continue

            record_type = parts[1]

            if record_type == "BEGIN":
                expected_len = parse_decimal(parts[2], line_number, "BEGIN length")
            elif record_type == "END":
                end_len = parse_decimal(parts[2], line_number, "END length")
            elif record_type == "DATA":
                if len(parts) != 4:
                    raise ValueError(f"Line {line_number}: DATA line is incomplete.")

                offset = parse_decimal(parts[2], line_number, "DATA offset")
                hex_text = parts[3].strip()

                if len(hex_text) == 0:
                    raise ValueError(f"Line {line_number}: DATA hex string is empty.")

                if (len(hex_text) % 2) != 0:
                    raise ValueError(f"Line {line_number}: DATA hex length is not even.")

                try:
                    chunk = bytes.fromhex(hex_text)
                except ValueError as exc:
                    raise ValueError(f"Line {line_number}: DATA contains non-hex characters.") from exc

                chunks[offset] = chunk

    if expected_len is None:
        raise ValueError("Missing $JPG,BEGIN,<len> line.")

    if end_len is None:
        raise ValueError("Missing $JPG,END,<len> line.")

    if expected_len != end_len:
        raise ValueError(f"BEGIN length {expected_len} does not match END length {end_len}.")

    jpeg = rebuild_contiguous_bytes(chunks, expected_len)
    validate_jpeg(jpeg)
    return jpeg


def parse_decimal(text: str, line_number: int, field_name: str) -> int:
    try:
        value = int(text, 10)
    except ValueError as exc:
        raise ValueError(f"Line {line_number}: invalid {field_name}: {text!r}.") from exc

    if value < 0:
        raise ValueError(f"Line {line_number}: {field_name} must not be negative.")

    return value


def rebuild_contiguous_bytes(chunks: dict[int, bytes], expected_len: int) -> bytes:
    if not chunks:
        raise ValueError("No $JPG,DATA lines found.")

    output = bytearray()
    expected_offset = 0

    for offset in sorted(chunks):
        chunk = chunks[offset]

        if offset != expected_offset:
            raise ValueError(f"Missing or out-of-order data at offset {expected_offset}; next offset is {offset}.")

        output.extend(chunk)
        expected_offset += len(chunk)

    if len(output) != expected_len:
        raise ValueError(f"Rebuilt JPEG length {len(output)} does not match expected length {expected_len}.")

    return bytes(output)


def validate_jpeg(jpeg: bytes) -> None:
    if len(jpeg) < 4:
        raise ValueError("JPEG is too short.")

    if jpeg[0:2] != b"\xff\xd8":
        raise ValueError("JPEG does not start with FF D8.")

    if jpeg[-2:] != b"\xff\xd9":
        raise ValueError("JPEG does not end with FF D9.")


def main() -> None:
    args = parse_args()
    log_file = args.log_file
    output_file = args.output_file

    if not log_file.exists():
        raise SystemExit(f"Log file not found: {log_file}")

    jpeg = restore_jpeg_from_log(log_file)
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_bytes(jpeg)

    print(f"OK: wrote {len(jpeg)} bytes to {output_file}")


if __name__ == "__main__":
    main()
