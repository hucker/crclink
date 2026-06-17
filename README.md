# crclink

[![PyPI](https://img.shields.io/pypi/v/crclink "Latest release on PyPI")](https://pypi.org/project/crclink/) ![license](https://img.shields.io/badge/license-MIT-blue "MIT license") ![Py 3.11](https://img.shields.io/badge/Py%203.11-passing-brightgreen "34 tests pass on CPython 3.11.0") ![Py 3.12](https://img.shields.io/badge/Py%203.12-passing-brightgreen "34 tests pass on CPython 3.12.7") ![Py 3.13](https://img.shields.io/badge/Py%203.13-passing-brightgreen "34 tests pass on CPython 3.13.11") ![Py 3.14](https://img.shields.io/badge/Py%203.14-passing-brightgreen "34 tests pass on CPython 3.14.0") ![unity](https://img.shields.io/badge/unity-32%20passed-brightgreen "C Unity suite: builder 15 + reader 17, 0 failures") ![ruff](https://img.shields.io/badge/ruff-passing-brightgreen "ruff: 0 lint errors") ![ty](https://img.shields.io/badge/ty-passing-brightgreen "ty: 0 type errors")

CRC-protected JSON and text line framing for serial-style embedded links.

crclink ships **both sides of the wire**: a Python library for the host and a C firmware companion for the device, so the same frames build, verify, and decode on either end.

- **Host (Python)**: this package, with no runtime dependencies. Encode, decode, and verify frames, plus a `crclink` CLI. Install with `uv add crclink`.
- **Device (C)**: a no-heap, no-runtime-dependency implementation under [src/c/](src/c/). It builds and CRC-stamps frames straight to a serial sink and verifies/reads incoming flat-JSON commands. See [src/c/README.md](src/c/README.md).

Both sides compute CRC-16/XMODEM over the same coverage, so a frame built on one verifies on the other; the test suites cross-check both directions.

## Features

- JSON line framing with a trailing crc key.
- Text line framing with trailing CRC suffix.
- CRC-16/XMODEM validation.
- Matching Python (host) and C (device) implementations.

## CRC engine

crclink does not reimplement CRC-16/XMODEM. It vendors a self-contained module that [crcglot](https://github.com/hucker/crcglot) generates ([src/crclink/crc16_xmodem.py](src/crclink/crc16_xmodem.py)), so the wheel installs with no runtime dependencies. crcglot owns the algorithm and its parameters and emits the code; crclink ships the generated copy and regenerates it whenever crcglot is bumped. The C firmware vendors the same crc16-xmodem the same way, so both ends stay in lockstep.

crcglot is a build-time code generator here (a dev dependency, `crcglot>=0.22.0`), not something the package imports at run time. See [docs/crcglot-integration.md](docs/crcglot-integration.md) for the integration details: both the host and the firmware vendor generated code, plus the cross-end test vectors.

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

The firmware speaks the same frames both ways: verify an incoming command, read its fields by key, then build and CRC-stamp the reply. Numbers are converted for you, with no heap and no runtime dependencies. Here a host sends the monitor command `mem byte 0x1234` and the device returns the byte at that address (`cmd: uint32 -> v: uint8`):

```c
#include "crclink_json.h"        // build the reply frame
#include "crclink_json_read.h"   // read the incoming command

void handle_line(const char *line) {              // {"cmd":"mem byte 0x1234","crc":"5993"}
    if (crclink_json_verify(line) != 0) return;   // bad CRC: drop the frame
    char cmd[32];
    if (crclink_json_get_str(line, "cmd", cmd, sizeof cmd) < 0) return;

    uint32_t addr;                                // pull the address out (your parser)
    if (sscanf(cmd, "mem byte %" SCNx32, &addr) != 1) return;

    uint8_t value = *(volatile uint8_t *)(uintptr_t)addr;   // read the address
    crclink_json_t j;
    crclink_json_start(&j, uart_sink, NULL);      // your per-byte serial sink
    crclink_json_int_add(&j, "v", value);
    crclink_json_end(&j);                         // -> {"v":42,"crc":"37c2"}
}
```

The reply streams out a byte at a time through your sink and decodes on the host with `crclink.decode_json_frame`. See [src/c/README.md](src/c/README.md) for the builder and reader APIs in full, failure handling, and filling a C struct from a command.

## Install

```bash
uv add crclink
```

## Development

```bash
uv sync
uv run pytest
```

Test across the supported Python versions with tox, which builds each env with uv (not pip):

```bash
uvx --with tox-uv tox run            # every version plus the ruff/ty lint env
uvx --with tox-uv tox run -e py312   # one version
```
