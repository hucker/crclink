"""Command-line interface for crclink."""

from __future__ import annotations

import argparse
import json
import sys
from collections.abc import Iterable, Sequence

from .errors import CrclinkError
from .frame import (
    FrameFormatError,
    decode_json_frame,
    decode_text_frame,
    encode_json_frame,
    encode_text_frame,
)


def build_parser() -> argparse.ArgumentParser:
    """Build the CLI argument parser.

    Returns:
        Configured top-level parser.
    """
    parser = argparse.ArgumentParser(prog="crclink", description="Encode/decode CRC-framed lines")
    subparsers = parser.add_subparsers(dest="command", required=True)

    encode_json = subparsers.add_parser("encode-json", help="Encode a JSON payload frame")
    encode_json.add_argument(
        "payload",
        help='JSON object payload (without crc key), for example \'{"t":1,"v":2}\'',
    )

    decode_json = subparsers.add_parser("decode-json", help="Decode and verify a JSON frame")
    decode_json.add_argument("frame", help="JSON frame to decode")

    encode_text = subparsers.add_parser("encode-text", help="Encode a text frame")
    encode_text.add_argument("body", help="Text body to encode")
    encode_text.add_argument(
        "--prefix",
        default="",
        help="Text before the CRC suffix; use 0x for 0xHHHH (default: bare HHHH)",
    )

    decode_text = subparsers.add_parser("decode-text", help="Decode and verify a text frame")
    decode_text.add_argument("line", help="Text frame to decode")

    verify_file = subparsers.add_parser(
        "verify-file", help="Verify every frame line in a file (or stdin)"
    )
    verify_file.add_argument("path", help="File to read, or - for standard input")
    verify_file.add_argument(
        "--format",
        choices=("auto", "json", "text"),
        default="auto",
        help="Frame format per line; auto picks json for lines starting with '{' (default: auto)",
    )
    verify_file.add_argument(
        "--quiet",
        action="store_true",
        help="Print only failing lines and the final summary",
    )

    return parser


def _verify_lines(lines: Iterable[str], fmt: str, quiet: bool) -> int:
    """Verify each non-blank line of an iterable of frame lines.

    Args:
        lines: Source lines (file handle, stdin, or any string iterable).
        fmt: "json", "text", or "auto" (decide per line by a leading '{').
        quiet: When true, print only failing lines and the summary.

    Returns:
        Exit code: 0 if every checked line verified, 1 if any failed.
    """
    checked = 0
    failed = 0
    for lineno, raw in enumerate(lines, start=1):
        line = raw.rstrip("\r\n")
        if not line.strip():
            continue  # blank lines are not frames; skip without counting

        checked += 1
        line_fmt = "json" if (fmt == "auto" and line.lstrip().startswith("{")) else fmt
        if line_fmt == "auto":
            line_fmt = "text"

        try:
            if line_fmt == "json":
                decode_json_frame(line)
            else:
                decode_text_frame(line)
        except CrclinkError as exc:
            failed += 1
            print(f"line {lineno}: FAIL [{line_fmt}] {exc}")
        else:
            if not quiet:
                print(f"line {lineno}: ok [{line_fmt}]")

    print(f"verified {checked - failed}/{checked} line(s)")
    return 1 if failed else 0


def _verify_file(path: str, fmt: str, quiet: bool) -> int:
    """Verify every frame line in a file, streaming line by line.

    Args:
        path: File path, or "-" to read standard input.
        fmt: "json", "text", or "auto".
        quiet: When true, print only failing lines and the summary.

    Returns:
        Exit code from verification, or 1 if the file cannot be read.
    """
    try:
        if path == "-":
            return _verify_lines(sys.stdin, fmt, quiet)
        with open(path, encoding="utf-8") as handle:
            return _verify_lines(handle, fmt, quiet)
    except OSError as exc:
        print(f"error: {exc}")
        return 1


def main(argv: Sequence[str] | None = None) -> int:
    """Run crclink CLI.

    Args:
        argv: Optional argument vector. If omitted, reads from sys.argv.

    Returns:
        Process exit code.
    """
    parser = build_parser()
    args = parser.parse_args(argv)

    try:
        if args.command == "encode-json":
            payload = json.loads(args.payload)
            if not isinstance(payload, dict):
                raise FrameFormatError("payload must be a JSON object")
            print(encode_json_frame(payload).decode("utf-8"))
            return 0

        if args.command == "decode-json":
            decoded = decode_json_frame(args.frame)
            result = {
                "ok": True,
                "mode": "json",
                "payload": decoded.payload,
                "claimed_crc": f"{decoded.claimed_crc:04x}",
                "computed_crc": f"{decoded.computed_crc:04x}",
            }
            print(json.dumps(result, separators=(",", ":"), ensure_ascii=False))
            return 0

        if args.command == "encode-text":
            print(encode_text_frame(args.body, prefix=args.prefix))
            return 0

        if args.command == "decode-text":
            decoded = decode_text_frame(args.line)
            result = {
                "ok": True,
                "mode": "text",
                "body": decoded.body,
                "claimed_crc": f"{decoded.claimed_crc:04x}",
                "computed_crc": f"{decoded.computed_crc:04x}",
            }
            print(json.dumps(result, separators=(",", ":"), ensure_ascii=False))
            return 0

        if args.command == "verify-file":
            return _verify_file(args.path, args.format, args.quiet)

        parser.error("unknown command")
        return 2

    except (json.JSONDecodeError, CrclinkError) as exc:
        print(f"error: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
