# crclink ↔ crcglot integration

How crclink consumes crcglot for its crc16-xmodem layer. Handoff reference for the crclink project.

Status: crcglot **0.22.0** is live on PyPI (2026-06-17). It stamps the generating crcglot version into the output and ships the provenance feature described below. Pin `crcglot>=0.22.0`.

## The boundary

crcglot owns the CRC math and its own metadata; crclink owns the JSON line framing. Do not reimplement or hardcode crc16-xmodem's parameters in crclink: generate them from crcglot. Both halves of crclink integrate the same way, by vendoring generated source:

- **Python host**: vendor a crcglot-generated, self-contained module (`src/crclink/crc16_xmodem.py`); no runtime dependency on crcglot.
- **C firmware**: vendor crcglot-generated source (a micro cannot import a Python package).

Both sides are the same crc16-xmodem, so they interoperate by construction.

## Host (Python): generate and vendor

The host vendors a self-contained crc16-xmodem module that crcglot generates, so the wheel has no runtime dependency. crcglot is a dev-only code generator, pinned `crcglot>=0.22.0`.

Generate the module and vendor it into the package:

```bash
uv run crcglot python --small --comment google --naming snake crc16-xmodem > src/crclink/crc16_xmodem.py
```

- Treat the file as generated output: regenerate when you bump crcglot, do not hand-edit. Its docstring carries a `Reproduce with crcglot` block recording the exact parameters.
- Compute via the generated `crc16_xmodem(data)`, never a hand-rolled loop:

  ```python
  from crclink.crc16_xmodem import crc16_xmodem

  crc = crc16_xmodem(prefix_bytes)   # -> int
  ```

- The generated module embeds `crc16_xmodem_self_test()` (returns True on success). The host suite calls it to catch a build or width mismatch, the same role the firmware's C `_self_test()` plays.

## Firmware (C): generate and vendor

Generate the verified C pair from crcglot and vendor it into the firmware tree:

```bash
uv run crcglot c crc16-xmodem --small file=crc16_xmodem   # -> crc16_xmodem.h / crc16_xmodem.c
```

- `--small` (bitwise) for the smallest flash footprint; `--table` if you have ~512 B of ROM to spare and want speed. Both are crc16-xmodem and pass the same check value.
- Treat the files as generated output: regenerate when you bump crcglot, do not hand-edit. The header's `Reproduce with crcglot` block records the exact parameters so the next regeneration is unambiguous.
- The generated `.c` embeds `crc16_xmodem_self_test()` (returns 0 on success). Call it once on the target to catch a compiler / width / endianness mismatch.

## The framing layer (crclink's own code)

crclink owns this thin wrapper around `crc16_xmodem(...)`, the generated CRC on both host and firmware. The full scheme is in the crclink design digest; in brief:

- **Wire format**: one compact JSON object per line, CRC as the final field: `{"t":1234,"v":42,"crc":"31c3"}`.
- **CRC range**: from the opening `{` up to (not including) the final `"crc"` key. Build the body ending in the comma, CRC that prefix, append `"crc":"<hex>"}`.
- **Firmware is the canonical-form authority**: it hand-builds the line with `snprintf` and CRCs the bytes it emits. The host reproduces those exact bytes when it wants to recompute from a parsed dict.
- **Host verify**: CRC the literal received bytes up to the `"crc"` key, compare to the parsed hex. No re-serialization needed to verify.
- **Host reframe-from-dict** (the round-trip nice-to-have): canonical-serialize with `json.dumps(body, separators=(",", ":"), ensure_ascii=False)`, preserve key order, no bare floats. The invariant to test: `reframe(parsed_dict) == original_wire_bytes`.

The firmware C build loop, for reference:

```c
char buf[256];
int n = 0;
n += snprintf(buf + n, sizeof buf - n, "{\"t\":%lu,\"v\":%d,", ts, val);
uint16_t crc = crc16_xmodem((const uint8_t *)buf, n);   /* CRC the prefix as built */
n += snprintf(buf + n, sizeof buf - n, "\"crc\":\"%04x\"}", crc);
```

## Use the C `const` record for device diagnostics

crcglot's generated C emits a public, linkable provenance record. This is directly useful for crclink's device-comms purpose:

```c
const crcglot_provenance_t crc16_xmodem_provenance = {
    .algorithm = "crc16-xmodem",
    .target    = "c",
    .variant   = "bitwise",
    .comment   = "plain",
    .symbol    = "crc16_xmodem",
    .naming    = "snake",
};
```

The firmware can report `crc16_xmodem_provenance.algorithm` / `.variant` over the monitor channel, so a host can confirm both ends agree on the CRC configuration at runtime rather than by convention. It carries only request-derived values (no tool version), so generated output stays a pure function of the request. Being a public symbol it never trips `-Wunused-const-variable` under `-Werror`; `-Wl,--gc-sections` strips it when unreferenced, and `-DCRCGLOT_NO_PROVENANCE` omits it on a toolchain without section GC.

## Cross-end test vectors (pin both ends together)

- `crc16_xmodem(b"123456789") == 0x31C3` asserted on **both** ends: the firmware's `_self_test()` already does this; the host asserts `crc16_xmodem(b"123456789") == 0x31C3` against the vendored module and runs its `crc16_xmodem_self_test()`.
- A few real frames round-tripped through the host's `reframe()` must equal the original wire bytes.
- One frame carrying a float as a hex / base64 string blob, to lock the no-bare-floats mitigation.

## Pinning and CI

- crclink pins `crcglot>=0.22.0` as a dev-only code generator; the wheel has no runtime dependencies.
- A crclink test asserts the vendored copy is intact: the host's `crc16_xmodem("123456789")` equals `0x31C3` and `crc16_xmodem_self_test()` passes, matching the firmware's C check value. This catches a corrupted or wrongly hand-edited vendored file. It does not, on its own, prove the file is freshly regenerated: crc16-xmodem's check value is fixed, so a stale-but-still-correct copy also passes. Regenerating from the pinned crcglot is a manual step at bump time; nothing triggers it automatically.

## crc16-xmodem parameters (reference only)

For reference. These live in crcglot and in the generated module's header, not as a hand-written table in crclink code.

| Field  | Value  |
| ------ | ------ |
| width  | 16     |
| poly   | 0x1021 |
| init   | 0x0000 |
| refin  | false  |
| refout | false  |
| xorout | 0x0000 |
| check  | 0x31C3 |
