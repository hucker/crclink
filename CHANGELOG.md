# Changelog

## v0.1.0 (2026-06-17)

Initial release. crclink frames CRC-protected lines for serial-style embedded links and ships both sides of the wire: a Python host library and a C firmware companion. Both ends compute CRC-16/XMODEM over the same coverage, so a frame built on one verifies on the other.

### Python (host)

- Framing API: `encode_json_frame` / `decode_json_frame` add and check a trailing `crc` key on a JSON line; `encode_text_frame` / `decode_text_frame` add and check a trailing CRC suffix on a text line, with a configurable prefix (e.g. `0x`).
- `crclink` CLI: `encode-json`, `encode-text`, `decode-json`, `decode-text`, and `verify-file`, which checks a whole file (or stdin) line by line, auto-detects JSON vs text per line, reports each line `ok` or `FAIL`, and exits non-zero if any line fails.
- Typed errors for decode and CRC failures.
- CRC-16/XMODEM is computed through [crcglot](https://github.com/hucker/crcglot) (`>=0.21.0`), not reimplemented, so the CRC definition lives in one place and no extra runtime packages come along.

### C (device)

- No-heap, no-runtime-dependency firmware companion under `src/c/`.
- JSON frame builder that streams a CRC-stamped frame to a per-byte serial sink, with typed adders (int, bool, float, string, plus typed and polymorphic lists) and nested objects.
- Flat-JSON reader for incoming commands: verify the CRC, then read fields by key with length-aware, type-checked getters and nested access via `get_raw`.
- Unity test suite that cross-checks the C frames byte-for-byte against the Python encoder.

### Packaging

- Pure-Python wheel (`py3-none-any`), Python `>=3.11`, ships `py.typed`.
- Built and uploaded to PyPI from CI via OIDC Trusted Publishing on a version tag.
