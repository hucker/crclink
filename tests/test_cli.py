"""CLI tests for crclink."""

from __future__ import annotations

import json

from crclink.cli import main


class TestCliJson:
    """CLI behavior for JSON encode/decode commands."""

    def test_encode_json_prints_frame(self, capsys) -> None:
        """encode-json should emit a CRC-protected JSON frame."""
        # Arrange
        argv = ["encode-json", '{"t":1,"v":2}']

        # Act
        exit_code = main(argv)
        output = capsys.readouterr().out.strip()

        # Assert
        assert exit_code == 0, "encode-json should return zero exit code"
        assert '"crc":"' in output, "encoded JSON output must include crc field"
        assert output.startswith('{"t":1,"v":2,'), "encoded JSON should preserve payload content"

    def test_decode_json_outputs_result_object(self, capsys) -> None:
        """decode-json should return a result payload with ok=true."""
        # Arrange
        encode_code = main(["encode-json", '{"t":123}'])
        frame = capsys.readouterr().out.strip()
        assert encode_code == 0, "setup encode-json command should succeed"

        # Act
        exit_code = main(["decode-json", frame])
        output = capsys.readouterr().out.strip()
        result = json.loads(output)

        # Assert
        assert exit_code == 0, "decode-json should return zero exit code"
        assert result["ok"] is True, "decode-json result should mark frame as valid"
        assert result["mode"] == "json", "decode-json should report json mode"
        assert result["payload"] == {"t": 123}, "decoded payload should match encoded payload"

    def test_decode_json_returns_error_for_invalid_frame(self, capsys) -> None:
        """decode-json should return exit code 1 on CRC failure."""
        # Arrange
        bad_frame = '{"t":1,"crc":"0000"}'

        # Act
        exit_code = main(["decode-json", bad_frame])
        output = capsys.readouterr().out.strip()

        # Assert
        assert exit_code == 1, "decode-json should return non-zero for invalid frame"
        assert output.startswith("error:"), "decode-json failures should print error prefix"


class TestCliText:
    """CLI behavior for text encode/decode commands."""

    def test_encode_text_plain_suffix(self, capsys) -> None:
        """encode-text should emit plain 4-hex suffix by default."""
        # Arrange
        argv = ["encode-text", "PING"]

        # Act
        exit_code = main(argv)
        output = capsys.readouterr().out.strip()

        # Assert
        assert exit_code == 0, "encode-text should return zero exit code"
        assert output.startswith("PING "), "output should keep original body prefix"
        assert len(output.split(" ")[-1]) == 4, "default suffix should be 4 hex chars"

    def test_encode_text_with_0x_prefix(self, capsys) -> None:
        """encode-text with --with-0x-prefix should emit prefixed suffix."""
        # Arrange
        argv = ["encode-text", "PING", "--with-0x-prefix"]

        # Act
        exit_code = main(argv)
        output = capsys.readouterr().out.strip()

        # Assert
        assert exit_code == 0, "encode-text should return zero exit code"
        assert output.endswith(output.split(" ")[-1]), "output should have trailing CRC token"
        assert output.split(" ")[-1].startswith("0x"), "CRC token should include 0x prefix"

    def test_decode_text_outputs_result_object(self, capsys) -> None:
        """decode-text should parse and validate a generated text frame."""
        # Arrange
        encode_code = main(["encode-text", "HELLO"])
        line = capsys.readouterr().out.strip()
        assert encode_code == 0, "setup encode-text command should succeed"

        # Act
        exit_code = main(["decode-text", line])
        output = capsys.readouterr().out.strip()
        result = json.loads(output)

        # Assert
        assert exit_code == 0, "decode-text should return zero exit code"
        assert result["ok"] is True, "decode-text result should mark frame as valid"
        assert result["mode"] == "text", "decode-text should report text mode"
        assert result["body"] == "HELLO", "decoded text body should match original"

    def test_decode_text_returns_error_for_invalid_crc(self, capsys) -> None:
        """decode-text should fail on CRC mismatch."""
        # Arrange
        bad_line = "HELLO 0000"

        # Act
        exit_code = main(["decode-text", bad_line])
        output = capsys.readouterr().out.strip()

        # Assert
        assert exit_code == 1, "decode-text should return non-zero for invalid line"
        assert output.startswith("error:"), "decode-text failures should print error prefix"
