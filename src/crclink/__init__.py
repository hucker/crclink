"""crclink package."""

from .cli import main
from .errors import CrclinkError, CrcMismatchError, FrameFormatError
from .frame import (
    DecodedJsonFrame,
    DecodedTextFrame,
    decode_json_frame,
    decode_text_frame,
    encode_json_frame,
    encode_text_frame,
)

__all__ = [
    "CrcMismatchError",
    "CrclinkError",
    "FrameFormatError",
    "DecodedJsonFrame",
    "DecodedTextFrame",
    "main",
    "encode_json_frame",
    "decode_json_frame",
    "encode_text_frame",
    "decode_text_frame",
]
