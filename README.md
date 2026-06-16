# crclink

CRC-protected, line-based transport framing for serial-style embedded links.

crclink ships **both sides of the wire**: a Python library for the host and a C firmware companion for the device, so the same frames build, verify, and decode on either end.

- **Host (Python)**: this package. Encode, decode, and verify frames, plus a `crclink` CLI. Install with `uv add crclink`.
- **Device (C)**: a no-heap, no-runtime-dependency implementation under [src/c/](src/c/). It builds and CRC-stamps frames straight to a serial sink and verifies/reads incoming flat-JSON commands. See [src/c/README.md](src/c/README.md).

Both sides compute CRC-16/XMODEM over the same coverage, so a frame built on one verifies on the other; the test suites cross-check both directions.

## Features

- JSON line framing with a trailing crc key.
- Text line framing with trailing CRC suffix.
- CRC-16/XMODEM validation.
- Matching Python (host) and C (device) implementations.

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

## On the device (C)

The firmware receives the same frames: verify the CRC, then read fields by key. Numbers are converted for you, with no heap and no runtime dependencies.

```c
#include "crclink_json_read.h"

void handle_line(const char *line) {              // {"cmd":"get_voltage 1","crc":"9585"}
    if (crclink_json_verify(line) != 0) return;   // bad CRC: drop the frame
    char cmd[32];
    if (crclink_json_get_str(line, "cmd", cmd, sizeof cmd) >= 0) {
        dispatch(cmd);                            // cmd == "get_voltage 1"
    }
}
```

See [src/c/README.md](src/c/README.md) for transmitting frames, failure handling, and filling a C struct from a command.

## Install

```bash
uv add crclink
```

## Development

```bash
uv sync
uv run pytest
```
