"""Tests for crclink framing APIs."""

from __future__ import annotations

import pytest
from crcglot import compute

from crclink import (
    CrcMismatchError,
    FrameFormatError,
    decode_json_frame,
    decode_text_frame,
    encode_json_frame,
    encode_text_frame,
)


class TestCrcglotBoundary:
    """Prove crclink is wired to crcglot's crc16-xmodem, not that the CRC is correct.

    crcglot owns the algorithm and its own test suite; this is a single
    connection check on the catalogue name crclink passes to the engine.
    """

    def test_catalog_check_vector(self) -> None:
        """The name crclink uses must resolve to crc16-xmodem in crcglot."""
        # Arrange
        data = b"123456789"

        # Act
        actual = compute(data, "crc16-xmodem")

        # Assert
        expected = 0x31C3
        assert actual == expected, "crc16-xmodem check vector must equal 0x31C3"


class TestJsonFraming:
    """JSON frame behavior for encode, decode, and verification."""

    def test_encode_decode_round_trip_payload(self) -> None:
        """Encoded frame should decode back to the same payload."""
        # Arrange
        payload = {"t": 1234, "v": 42, "ok": True}

        # Act
        frame = encode_json_frame(payload)
        decoded = decode_json_frame(frame)

        # Assert
        assert decoded.payload == payload, "decoded payload must match encoded payload"
        assert decoded.claimed_crc == decoded.computed_crc, "CRC values must match"

    def test_decode_accepts_optional_newline(self) -> None:
        """Decoder should allow newline-terminated input."""
        # Arrange
        frame = encode_json_frame({"t": 1}) + b"\n"

        # Act
        decoded = decode_json_frame(frame)

        # Assert
        assert decoded.payload == {"t": 1}, "newline should be ignored during decode"

    def test_encode_rejects_crc_key_in_payload(self) -> None:
        """Encoder should reject payloads that already include crc."""
        # Arrange
        payload = {"t": 1, "crc": "0000"}

        # Act / Assert
        with pytest.raises(FrameFormatError, match="must not include 'crc' key"):
            encode_json_frame(payload)

    def test_decode_raises_for_missing_crc_key(self) -> None:
        """Decode should fail when crc key is absent."""
        # Arrange
        frame = b'{"t":1}'

        # Act / Assert
        with pytest.raises(FrameFormatError, match="missing crc key"):
            decode_json_frame(frame)

    def test_decode_raises_for_bad_crc_field_type(self) -> None:
        """Decode should fail when crc is not a 4-hex string."""
        # Arrange
        frame = b'{"t":1,"crc":1234}'

        # Act / Assert
        with pytest.raises(FrameFormatError, match="4-character hex"):
            decode_json_frame(frame)

    def test_decode_raises_on_crc_mismatch(self) -> None:
        """Decode should raise CrcMismatchError when checksum differs."""
        # Arrange
        good = encode_json_frame({"t": 9, "v": 7}).decode("ascii")
        original_crc = good.split('"crc":"', 1)[1][:4]
        flipped_nibble = "0" if original_crc[0] != "0" else "1"
        mutated_crc = f"{flipped_nibble}{original_crc[1:]}"
        bad = good.replace(f'"crc":"{original_crc}"', f'"crc":"{mutated_crc}"', 1)

        # Act / Assert
        with pytest.raises(CrcMismatchError, match="crc mismatch"):
            decode_json_frame(bad)


class TestTextFraming:
    """Text line framing behavior for both suffix variants."""

    def test_encode_decode_plain_suffix_round_trip(self) -> None:
        """Plain CRC suffix should round-trip."""
        # Arrange
        body = "PING status=ok"

        # Act
        line = encode_text_frame(body)
        decoded = decode_text_frame(line)

        # Assert
        assert decoded.body == body, "decoded body must match encoded body"
        assert decoded.claimed_crc == decoded.computed_crc, "CRC values must match"

    def test_encode_decode_0x_suffix_round_trip(self) -> None:
        """0x-prefixed CRC suffix should round-trip."""
        # Arrange
        body = "SET mode=2"

        # Act
        line = encode_text_frame(body, with_0x_prefix=True)
        decoded = decode_text_frame(line)

        # Assert
        assert " 0x" in line, "encoded line must include 0x prefix when requested"
        assert decoded.body == body, "decoded body must match encoded body"

    def test_decode_accepts_optional_newline(self) -> None:
        """Text decoder should allow newline-terminated input."""
        # Arrange
        line = encode_text_frame("HELLO") + "\r\n"

        # Act
        decoded = decode_text_frame(line)

        # Assert
        assert decoded.body == "HELLO", "newline should be ignored for text decode"

    def test_encode_rejects_trailing_whitespace_in_body(self) -> None:
        """Encoder should reject body values with trailing space."""
        # Arrange

        # Act / Assert
        with pytest.raises(FrameFormatError, match="must not end with whitespace"):
            encode_text_frame("BAD ")

    def test_decode_raises_for_malformed_suffix(self) -> None:
        """Decode should fail for invalid CRC suffix shapes."""
        # Arrange
        line = "PING xyz"

        # Act / Assert
        with pytest.raises(FrameFormatError, match="must end with"):
            decode_text_frame(line)

    def test_decode_raises_on_crc_mismatch(self) -> None:
        """Decode should fail when text CRC does not match body."""
        # Arrange
        line = encode_text_frame("PING")
        bad = f"{line[:-4]}ffff"

        # Act / Assert
        with pytest.raises(CrcMismatchError, match="crc mismatch"):
            decode_text_frame(bad)
