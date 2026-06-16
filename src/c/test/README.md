# C tests (Unity)

Unit tests for the C JSON frame builder, using [Unity](https://github.com/ThrowTheSwitch/Unity).

```sh
make test     # build and run
make clean
```

CI runs `make -C src/c/test test` on every push (see `.github/workflows/ci.yml`).

## Layout

- `unity/` — vendored Unity v2.6.0 (`unity.c`, `unity.h`, `unity_internals.h`), MIT-licensed. To update, replace these three files from the upstream `src/` directory.
- `test_crclink_json.c` — the suite. Expected frame strings are golden values from crclink's Python encoder (`crclink.encode_json_frame`), so a failure also flags any drift from byte-for-byte parity with the host. Regenerate a golden with, e.g.:

  ```sh
  uv run python -c "from crclink import encode_json_frame; print(encode_json_frame({'msg':'hi','v':12}).decode())"
  ```

The suite links the real crcglot-generated `crc16_xmodem.c`, so it also exercises `crc16_xmodem_self_test()`.
