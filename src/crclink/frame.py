"""JSON and text framing helpers for crclink."""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from typing import Any

from crcglot import compute

from .errors import CrcMismatchError, FrameFormatError

#: crcglot catalogue name for the CRC this transport uses. crcglot owns the
#: parameters; crclink only names the algorithm and asks the engine.
_ALGORITHM = "crc16-xmodem"

_TEXT_SUFFIX_RE = re.compile(r"^(?P<body>.*) (?P<crc>(?:0x)?[0-9a-fA-F]{4})$")


def _crc(data: bytes) -> int:
    """Compute the transport CRC over data via crcglot's engine."""
    return compute(data, _ALGORITHM)


@dataclass(frozen=True)
class DecodedJsonFrame:
    """Decoded JSON frame details.

    Attributes:
        payload: JSON object content without the crc key.
        raw: Raw line bytes excluding trailing newline.
        claimed_crc: Parsed CRC from the frame.
        computed_crc: CRC computed over covered bytes.
    """

    payload: dict[str, Any]
    raw: bytes
    claimed_crc: int
    computed_crc: int


@dataclass(frozen=True)
class DecodedTextFrame:
    """Decoded text frame details.

    Attributes:
        body: Message body before CRC suffix.
        raw: Raw line text excluding trailing newline.
        claimed_crc: Parsed CRC from the frame.
        computed_crc: CRC computed from body bytes.
    """

    body: str
    raw: str
    claimed_crc: int
    computed_crc: int


def encode_json_frame(payload: dict[str, Any]) -> bytes:
    """Encode a payload dict into a CRC-protected JSON line.

    Args:
        payload: Payload mapping. It must not contain the crc key.

    Returns:
        Encoded frame bytes without a trailing newline.

    Raises:
        FrameFormatError: If payload contains a crc key.

    Examples:
        >>> encode_json_frame({"t": 1234, "v": 42}).decode("ascii")
        '{"t":1234,"v":42,"crc":"1352"}'
    """
    if "crc" in payload:
        raise FrameFormatError("payload must not include 'crc' key")

    body_text = json.dumps(payload, separators=(",", ":"), ensure_ascii=False)
    prefix = "{" if body_text == "{}" else f"{body_text[:-1]},"
    prefix_bytes = prefix.encode("utf-8")
    crc = _crc(prefix_bytes)
    trailer = f'"crc":"{crc:04x}"}}'.encode("ascii")
    return prefix_bytes + trailer


def decode_json_frame(frame: bytes | str) -> DecodedJsonFrame:
    """Decode and verify a CRC-protected JSON frame.

    Args:
        frame: Frame text or bytes, with optional trailing newline.

    Returns:
        DecodedJsonFrame with payload and CRC details.

    Raises:
        FrameFormatError: If frame is malformed.
        CrcMismatchError: If CRC verification fails.

    Examples:
        >>> d = decode_json_frame('{"t":1,"crc":"ee8e"}')
        >>> d.payload["t"]
        1
    """
    raw = frame.encode("utf-8") if isinstance(frame, str) else frame
    raw = raw.rstrip(b"\r\n")

    if not raw.startswith(b"{") or not raw.endswith(b"}"):
        raise FrameFormatError("json frame must be a JSON object line")

    marker_index = raw.rfind(b'"crc"')
    if marker_index == -1:
        raise FrameFormatError("json frame missing crc key")

    prefix_bytes = raw[:marker_index]
    if prefix_bytes and prefix_bytes[-1:] == b":":
        raise FrameFormatError("invalid crc key position")

    try:
        parsed = json.loads(raw.decode("utf-8"))
    except json.JSONDecodeError as exc:
        raise FrameFormatError("invalid json frame") from exc

    if not isinstance(parsed, dict):
        raise FrameFormatError("json frame root must be an object")

    crc_text = parsed.get("crc")
    if not isinstance(crc_text, str) or not re.fullmatch(r"[0-9a-fA-F]{4}", crc_text):
        raise FrameFormatError("crc field must be a 4-character hex string")

    claimed_crc = int(crc_text, 16)
    computed_crc = _crc(prefix_bytes)
    if computed_crc != claimed_crc:
        raise CrcMismatchError(
            f"crc mismatch: expected {claimed_crc:04x}, computed {computed_crc:04x}"
        )

    payload = {k: v for k, v in parsed.items() if k != "crc"}
    return DecodedJsonFrame(
        payload=payload,
        raw=raw,
        claimed_crc=claimed_crc,
        computed_crc=computed_crc,
    )


def encode_text_frame(body: str, prefix: str = "") -> str:
    """Encode a text line with trailing CRC suffix.

    Args:
        body: Message body.
        prefix: Text placed before the 4-hex CRC. The default "" gives a bare
            HHHH suffix; "0x" gives 0xHHHH. Only "" and "0x" round-trip through
            decode_text_frame.

    Returns:
        Encoded line text without trailing newline.

    Raises:
        FrameFormatError: If body ends with whitespace.

    Examples:
        >>> encode_text_frame("PING")
        'PING e0e7'
        >>> encode_text_frame("PING", prefix="0x")
        'PING 0xe0e7'
    """
    if body.endswith((" ", "\t")):
        raise FrameFormatError("text body must not end with whitespace")

    crc = _crc(body.encode("utf-8"))
    suffix = f"{prefix}{crc:04x}"
    return f"{body} {suffix}"


def decode_text_frame(line: str) -> DecodedTextFrame:
    """Decode and verify a text line with trailing CRC suffix.

    Args:
        line: Text line with optional trailing newline.

    Returns:
        DecodedTextFrame with body and CRC details.

    Raises:
        FrameFormatError: If line is malformed.
        CrcMismatchError: If CRC verification fails.

    Examples:
        >>> decode_text_frame("PING e0e7").body
        'PING'
    """
    raw = line.rstrip("\r\n")
    match = _TEXT_SUFFIX_RE.fullmatch(raw)
    if not match:
        raise FrameFormatError("text frame must end with '<space><crc4>' or '<space>0x<crc4>'")

    body = match.group("body")
    crc_token = match.group("crc")
    if crc_token.lower().startswith("0x"):
        crc_token = crc_token[2:]

    claimed_crc = int(crc_token, 16)
    computed_crc = _crc(body.encode("utf-8"))
    if computed_crc != claimed_crc:
        raise CrcMismatchError(
            f"crc mismatch: expected {claimed_crc:04x}, computed {computed_crc:04x}"
        )

    return DecodedTextFrame(
        body=body,
        raw=raw,
        claimed_crc=claimed_crc,
        computed_crc=computed_crc,
    )
