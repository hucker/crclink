"""Command-line interface for crclink."""

from __future__ import annotations

import argparse
import json
from collections.abc import Sequence

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
        help="JSON object payload (without crc key), for example '{\"t\":1,\"v\":2}'",
    )

    decode_json = subparsers.add_parser("decode-json", help="Decode and verify a JSON frame")
    decode_json.add_argument("frame", help="JSON frame to decode")

    encode_text = subparsers.add_parser("encode-text", help="Encode a text frame")
    encode_text.add_argument("body", help="Text body to encode")
    encode_text.add_argument(
        "--with-0x-prefix",
        action="store_true",
        help="Emit CRC suffix as 0xHHHH instead of HHHH",
    )

    decode_text = subparsers.add_parser("decode-text", help="Decode and verify a text frame")
    decode_text.add_argument("line", help="Text frame to decode")

    return parser


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
            print(encode_text_frame(args.body, with_0x_prefix=args.with_0x_prefix))
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

        parser.error("unknown command")
        return 2

    except (json.JSONDecodeError, CrclinkError) as exc:
        print(f"error: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
