#!/usr/bin/env python3
"""Restore JPEG files from RA6M5 UART $JPG hex dump logs."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


DEFAULT_LOG = Path(__file__).resolve().parent / "logs" / "capture_log.txt.txt"
DEFAULT_OUTPUT = Path(__file__).resolve().parent / "output" / "capture.jpg"
DEFAULT_DATASET_DIR = Path(__file__).resolve().parents[3] / "fruit_dataset"
LOG_SUFFIXES = {".txt", ".log"}
CLASS_NAME_PATTERN = re.compile(r"^(?P<class>a|b|c|u|unk|unknown)[_-]?(?P<index>\d+)$", re.IGNORECASE)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Restore JPEG files from UART log lines like $JPG,DATA,<offset>,<hex>."
    )
    parser.add_argument(
        "input_path",
        type=Path,
        nargs="?",
        default=DEFAULT_LOG,
        help="UART log text file or a directory containing logs.",
    )
    parser.add_argument(
        "output_file",
        type=Path,
        nargs="?",
        default=DEFAULT_OUTPUT,
        help=(
            "Output JPEG path for single-file mode. If omitted and the log name is like "
            "A001.txt, output is written to fruit_dataset/A/A001.jpg."
        ),
    )
    parser.add_argument(
        "--dataset-dir",
        type=Path,
        default=DEFAULT_DATASET_DIR,
        help="Dataset root used for auto output. Default: ../../fruit_dataset from project root.",
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


def infer_dataset_output_file(log_file: Path, dataset_dir: Path) -> Path | None:
    match = CLASS_NAME_PATTERN.match(log_file.stem)

    if match is None:
        return None

    class_name = match.group("class").lower()

    if class_name in {"a", "b", "c"}:
        class_dir = class_name.upper()
    else:
        class_dir = "unknown"

    return dataset_dir / class_dir / f"{log_file.stem}.jpg"


def iter_log_files(log_dir: Path) -> list[Path]:
    return sorted(
        path
        for path in log_dir.iterdir()
        if path.is_file() and path.suffix.lower() in LOG_SUFFIXES
    )


def restore_one(log_file: Path, output_file: Path) -> None:
    jpeg = restore_jpeg_from_log(log_file)
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_bytes(jpeg)
    print(f"OK: {log_file.name} -> {output_file} ({len(jpeg)} bytes)")


def restore_directory(log_dir: Path, dataset_dir: Path) -> None:
    converted_count = 0
    skipped_count = 0

    for log_file in iter_log_files(log_dir):
        output_file = infer_dataset_output_file(log_file, dataset_dir)

        if output_file is None:
            skipped_count += 1
            print(f"SKIP: {log_file.name} does not look like A001/B001/C001/unknown001.")
            continue

        restore_one(log_file, output_file)
        converted_count += 1

    print(f"DONE: converted {converted_count} log(s), skipped {skipped_count} file(s).")


def main() -> None:
    args = parse_args()
    input_path = args.input_path
    output_file = args.output_file
    output_was_default = output_file == DEFAULT_OUTPUT

    if not input_path.exists():
        raise SystemExit(f"Input path not found: {input_path}")

    if input_path.is_dir():
        restore_directory(input_path, args.dataset_dir)
        return

    if output_was_default:
        inferred_output = infer_dataset_output_file(input_path, args.dataset_dir)

        if inferred_output is not None:
            output_file = inferred_output

    restore_one(input_path, output_file)


if __name__ == "__main__":
    main()
