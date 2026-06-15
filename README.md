# crclink

CRC-protected, line-based transport framing for serial-style embedded links.

## Features
- JSON line framing with a trailing crc key.
- Text line framing with trailing CRC suffix.
- CRC-16/XMODEM validation.

## CRC engine

crclink computes CRC-16/XMODEM through [crcglot](https://github.com/hucker/crcglot) rather than reimplementing it. crcglot owns the algorithm and its parameters; crclink just names the algorithm and calls the engine (`crcglot.compute(data, "crc16-xmodem")`), so the CRC definition lives in one place. crcglot is pure-stdlib, so depending on it at runtime pulls in no extra packages. crclink pins `crcglot>=0.21.0`.

See [docs/crcglot-integration.md](docs/crcglot-integration.md) for the integration details: the host vs. firmware split and the cross-end test vectors.

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
