# crclink Specification

## Purpose
crclink is a Python package for CRC-protected line transport over serial-style links for embedded systems.

Primary transport is a compact JSON line where CRC is carried in a final key.

## Scope
- Package for PyPI publishing.
- Baseline folder layout:
  - docs/
  - src/
  - tests/
  - scripts/
- Git-based workflow.
- uv for environment, dependency, build, and publish workflow.

## Protocol Requirements

### JSON Frame
A message is one compact JSON object on one line with crc as the final key.

Example:
{"t":1234,"v":42,"crc":"31c3"}

CRC coverage rule:
- CRC covers bytes from opening { up to but not including the "crc" key.
- The trailing comma before the crc key is included in CRC coverage for non-empty payload objects.
- Empty payload object coverage prefix is {.

### Text Line
Also support text line framing where CRC is appended to message text as either:
- <body><space><4-hex>
- <body><space>0x<4-hex>

CRC covers body bytes only (before final separator space).

## CRC Algorithm
Use CRC-16/XMODEM (crc16-xmodem):
- width: 16
- poly: 0x1021
- init: 0x0000
- refin: false
- refout: false
- xorout: 0x0000
- check("123456789"): 0x31C3

Implementation requirement:
- Use crcglot as a development dependency.
- Generate crc16-xmodem implementation using crcglot tooling.
- Keep runtime dependency set minimal.

## API Baseline
Provide baseline encode/decode/verify functionality for:
- JSON framing
- Text framing (plain 4-hex and 0x-prefixed form)

Include clear validation errors for malformed frames and CRC mismatch.

## Documentation and Style
- Google-style docstrings for public functions.
- Keep comments concise and useful.

## Testing Requirements
- pytest test suite.
- Coverage gate: 90% minimum.
- Tests should use class-based organization.
- Non-trivial tests should include AAA comments:
  - Arrange
  - Act
  - Assert

## Release Utility Scripts
Provide scripts folder with baseline utilities:
- scripts/release_prep.py
- scripts/release_publish.py

These can start as baseline scaffolding and evolve with release process.

## Planned Commit Sequence
1. Initial baseline project scaffold and core implementation.
2. Initial test cases.

## Open Design Questions
- Additional framing mode beyond JSON and text suffix modes.
- String escaping policy for constrained firmware emitters.
- Strict canonical ordering policy for host-constructed frames.

## CLI Utility
Consider a future CLI utility for encoding/decoding frames for testing and debugging purposes. This can be a separate module or script that leverages the core API. Use argparse for the cli and make sure the cli is exposed in the package entry points for easy access.

The main use case is for decoding a json line or text line and determining if it is valid OR encoding a line.
