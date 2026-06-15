# crclink

CRC-protected, line-based transport framing for serial-style embedded links.

## Features
- JSON line framing with a trailing crc key.
- Text line framing with trailing CRC suffix.
- CRC-16/XMODEM validation.

## Install

```bash
uv add crclink
```

## Development

```bash
uv sync
uv run pytest
```

## Spec
Project requirements are maintained in specs.md.
