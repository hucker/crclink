# crclink

CRC-protected, line-based transport framing for serial-style embedded links.

## Features
- JSON line framing with a trailing crc key.
- Text line framing with trailing CRC suffix.
- CRC-16/XMODEM validation.

## CRC engine

crclink computes CRC-16/XMODEM through [crcglot](https://github.com/hucker/crcglot) rather than reimplementing it. crcglot owns the algorithm and its parameters; crclink just names the algorithm and calls the engine (`crcglot.compute(data, "crc16-xmodem")`), so the CRC definition lives in one place. crcglot is pure-stdlib, so depending on it at runtime pulls in no extra packages. crclink pins `crcglot>=0.21.0`.

See [docs/crcglot-integration.md](docs/crcglot-integration.md) for the integration details: the host vs. firmware split and the cross-end test vectors.

## CLI

Installing crclink puts a `crclink` command on your path. It encodes and decodes single frames, and verifies a whole file of them.

```bash
# Encode (prints the framed line)
crclink encode-json '{"t":1234,"v":42}'        # -> {"t":1234,"v":42,"crc":"1352"}
crclink encode-text "PING"                       # -> PING e0e7
crclink encode-text "PING" --prefix 0x           # -> PING 0xe0e7

# Decode and verify one frame (prints a JSON result, exit 1 on a bad CRC)
crclink decode-json '{"t":1234,"v":42,"crc":"1352"}'
crclink decode-text "PING e0e7"

# Verify every frame in a file, line by line
crclink verify-file frames.jsonl --format json   # a file of JSON lines
crclink verify-file log.txt --format text         # a file of text lines
crclink verify-file mixed.lines                    # auto-detect each line
cat frames | crclink verify-file -                 # read from stdin
```

`verify-file` skips blank lines, reports each line as `ok` or `FAIL` with its number and reason, prints a `verified X/Y` summary, and exits non-zero if any line fails (so it slots into a shell pipeline). With `auto` (the default) a line starting with `{` is treated as JSON and anything else as text. Use `--quiet` to print only failures and the summary.

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
