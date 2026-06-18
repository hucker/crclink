"""CLI tests for crclink."""

from __future__ import annotations

import json
from importlib.metadata import version

import pytest

from crclink import encode_json_frame, encode_text_frame
from crclink.cli import main


class TestCliVersion:
    """CLI --version flag."""

    def test_version_flag_prints_package_version(self, capsys) -> None:
        """`crclink --version` prints 'crclink <version>' and exits 0."""
        # Act / Assert (argparse's version action prints, then exits)
        with pytest.raises(SystemExit) as exc:
            main(["--version"])
        output = capsys.readouterr().out.strip()

        # Assert
        assert exc.value.code == 0, "--version should exit with code 0"
        expected = f"crclink {version('crclink')}"
        assert output == expected, "--version should print 'crclink <version>'"


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

    def test_encode_text_with_prefix(self, capsys) -> None:
        """encode-text with --prefix 0x should emit a prefixed suffix."""
        # Arrange
        argv = ["encode-text", "PING", "--prefix", "0x"]

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


class TestCliVerifyFile:
    """verify-file reads a file line by line and verifies every frame.

    Frames are built with the encoders so the fixtures carry correct CRCs
    without hardcoding hex values.
    """

    def test_all_valid_json_lines(self, tmp_path, capsys) -> None:
        """A JSON-lines file of good frames should verify fully."""
        # Arrange
        frames = [encode_json_frame({"t": 1}).decode(), encode_json_frame({"v": 2}).decode()]
        path = tmp_path / "frames.jsonl"
        path.write_text("\n".join(frames) + "\n", encoding="utf-8")

        # Act
        exit_code = main(["verify-file", str(path), "--format", "json"])
        output = capsys.readouterr().out

        # Assert
        assert exit_code == 0, "all-valid json file should return zero exit code"
        assert "verified 2/2 line(s)" in output, "summary should report 2 of 2 verified"

    def test_all_valid_text_lines(self, tmp_path, capsys) -> None:
        """A text-lines file of good frames should verify fully."""
        # Arrange
        lines = [encode_text_frame("PING"), encode_text_frame("SET mode=2")]
        path = tmp_path / "frames.txt"
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")

        # Act
        exit_code = main(["verify-file", str(path), "--format", "text"])
        output = capsys.readouterr().out

        # Assert
        assert exit_code == 0, "all-valid text file should return zero exit code"
        assert "verified 2/2 line(s)" in output, "summary should report 2 of 2 verified"

    def test_reports_failing_line(self, tmp_path, capsys) -> None:
        """A bad line should fail verification and set a non-zero exit code."""
        # Arrange
        content = encode_text_frame("PING") + "\n" + "PING 0000\n"  # second line: wrong CRC
        path = tmp_path / "mixed.txt"
        path.write_text(content, encoding="utf-8")

        # Act
        exit_code = main(["verify-file", str(path), "--format", "text"])
        output = capsys.readouterr().out

        # Assert
        assert exit_code == 1, "a failing line should produce a non-zero exit code"
        assert "line 2: FAIL" in output, "the failing line number should be reported"
        assert "verified 1/2 line(s)" in output, "summary should report 1 of 2 verified"

    def test_auto_format_detects_per_line(self, tmp_path, capsys) -> None:
        """auto format should pick json or text per line by the leading brace."""
        # Arrange
        content = encode_json_frame({"t": 1}).decode() + "\n" + encode_text_frame("PING") + "\n"
        path = tmp_path / "both.lines"
        path.write_text(content, encoding="utf-8")

        # Act
        exit_code = main(["verify-file", str(path)])  # default --format auto
        output = capsys.readouterr().out

        # Assert
        assert exit_code == 0, "auto-detected mixed file should verify fully"
        assert "[json]" in output, "json line should be detected as json"
        assert "[text]" in output, "text line should be detected as text"

    def test_blank_lines_are_skipped(self, tmp_path, capsys) -> None:
        """Blank and whitespace-only lines should not count toward the total."""
        # Arrange
        content = encode_text_frame("A") + "\n\n   \n" + encode_text_frame("B") + "\n"
        path = tmp_path / "gaps.txt"
        path.write_text(content, encoding="utf-8")

        # Act
        exit_code = main(["verify-file", str(path), "--format", "text"])
        output = capsys.readouterr().out

        # Assert
        assert exit_code == 0, "file with blank lines should still verify"
        assert "verified 2/2 line(s)" in output, "blank lines should be skipped, not counted"

    def test_quiet_suppresses_ok_lines(self, tmp_path, capsys) -> None:
        """--quiet should print the summary but not per-line ok messages."""
        # Arrange
        path = tmp_path / "frames.txt"
        path.write_text(encode_text_frame("PING") + "\n", encoding="utf-8")

        # Act
        exit_code = main(["verify-file", str(path), "--format", "text", "--quiet"])
        output = capsys.readouterr().out

        # Assert
        assert exit_code == 0, "quiet run of a valid file should return zero"
        assert "ok" not in output, "quiet mode should suppress per-line ok output"
        assert "verified 1/1 line(s)" in output, "quiet mode should still print the summary"

    def test_reads_stdin_with_dash(self, capsys, monkeypatch) -> None:
        """A path of '-' should verify frames read from standard input."""
        # Arrange
        import io

        content = encode_text_frame("PING") + "\n" + encode_text_frame("PONG") + "\n"
        monkeypatch.setattr("sys.stdin", io.StringIO(content))

        # Act
        exit_code = main(["verify-file", "-", "--format", "text"])
        output = capsys.readouterr().out

        # Assert
        assert exit_code == 0, "valid frames on stdin should return zero"
        assert "verified 2/2 line(s)" in output, "stdin frames should be counted and verified"

    def test_missing_file_errors(self, tmp_path, capsys) -> None:
        """A missing path should print an error and return a non-zero exit code."""
        # Arrange
        missing = tmp_path / "does-not-exist.txt"

        # Act
        exit_code = main(["verify-file", str(missing)])
        output = capsys.readouterr().out

        # Assert
        assert exit_code == 1, "missing file should return a non-zero exit code"
        assert output.startswith("error:"), "missing file should print an error prefix"
