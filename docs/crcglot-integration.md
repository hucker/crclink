# crclink ↔ crcglot integration

How crclink consumes crcglot for its crc16-xmodem layer. Handoff reference for the crclink project.

Status: crcglot **0.21.0** is live on PyPI (2026-06-15). It ships the provenance feature described below. Pin `crcglot>=0.21.0`.

## The boundary

crcglot owns the CRC math and its own metadata; crclink owns the JSON line framing. Do not reimplement or hardcode crc16-xmodem's parameters in crclink: ask crcglot. The two halves of crclink integrate asymmetrically:

- **Python host**: runtime dependency on crcglot, call its engine.
- **C firmware**: vendor crcglot-generated source (a micro cannot import a Python package).

Both sides are the same crc16-xmodem, so they interoperate by construction.

## Host (Python): depend on crcglot, do not vendor

crcglot is pure-stdlib (zero runtime deps) and uses its C extension automatically when present, so a runtime dependency is cheap and fast.

- Pin it: `uv add "crcglot>=0.21.0"`.
- Compute via the engine, never a hand-rolled loop:

  ```python
  from crcglot import compute

  crc = compute(prefix_bytes, "crc16-xmodem")   # -> int
  ```

- If crclink ever needs to display or validate the parameters, read them from crcglot rather than a local dict (this is the library-boundary rule: crcglot owns its own knowledge):

  ```python
  from crcglot import ALGORITHMS

  p = ALGORITHMS["crc16-xmodem"]   # .width .poly .init .refin .refout .xorout .check
  assert p.check == 0x31C3
  ```

## Firmware (C): generate and vendor

Generate the verified C pair from crcglot and vendor it into the firmware tree:

```bash
uv run crcglot c crc16-xmodem --small file=crc16_xmodem   # -> crc16_xmodem.h / crc16_xmodem.c
```

- `--small` (bitwise) for the smallest flash footprint; `--table` if you have ~512 B of ROM to spare and want speed. Both are crc16-xmodem and pass the same check value.
- Treat the files as generated output: regenerate when you bump crcglot, do not hand-edit. The header's `Reproduce with crcglot` block records the exact parameters so the next regeneration is unambiguous.
- The generated `.c` embeds `crc16_xmodem_self_test()` (returns 0 on success). Call it once on the target to catch a compiler / width / endianness mismatch.

## The framing layer (crclink's own code)

crclink owns this thin wrapper around `crc16_xmodem(...)` (firmware) and `compute(...)` (host). The full scheme is in the crclink design digest; in brief:

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

- `crc16_xmodem(b"123456789") == 0x31C3` asserted on **both** ends: the firmware's `_self_test()` already does this; the host asserts `compute(b"123456789", "crc16-xmodem") == 0x31C3`.
- A few real frames round-tripped through the host's `reframe()` must equal the original wire bytes.
- One frame carrying a float as a hex / base64 string blob, to lock the no-bare-floats mitigation.

## Pinning and CI

- crclink pins `crcglot>=0.21.0`.
- A crclink CI test asserts the boundary holds: the vendored C's check value and the host's `compute("123456789")` both equal `0x31C3`. If they diverge, the vendored C is stale and must be regenerated from the pinned crcglot.

## crc16-xmodem parameters (reference only)

For reference. Read these from crcglot at runtime rather than copying them into crclink code.

| Field  | Value  |
| ------ | ------ |
| width  | 16     |
| poly   | 0x1021 |
| init   | 0x0000 |
| refin  | false  |
| refout | false  |
| xorout | 0x0000 |
| check  | 0x31C3 |
