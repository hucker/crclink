/**
 * @file crclink_json_read.h
 * @brief Read fields from a flat crclink JSON frame body.
 *
 * Typed getters for a single-level JSON object, for a command arriving over a
 * serial link, e.g. `{"cmd":"reboot","n":3,"crc":"...."}`. Check the frame with
 * crclink_json_verify first, then extract values by key. Numbers are converted
 * for you (no string detour). Parsing is built on a vendored jsmn (MIT) and uses
 * no heap.
 *
 * @code
 * if (crclink_json_verify(line) != 0) return;   // bad CRC: drop the frame
 * char cmd[20];
 * if (crclink_json_get_str(line, "cmd", cmd, sizeof cmd) >= 0
 *     && strcmp(cmd, "reboot") == 0) { ... }
 * long n;
 * crclink_json_get_int(line, "n", &n);
 * @endcode
 *
 * @p json must be NUL-terminated (or use the _n variants with an explicit
 * length). Each getter parses on its own, cheap for short command lines. The
 * scalar getters return a top-level scalar by key and skip over any nested
 * values in between; for a nested object/array value, get its byte span with
 * crclink_json_get_raw and re-read that span with the _n getters (one object
 * level per call, no recursion). The token budget is CRCLINK_JSON_MAX_TOKENS
 * (default 16; raise it in your build for nested or wide frames; it sizes a
 * stack array of ~16 bytes per token).
 *
 * @note Floating-point support is opt-in: define CRCLINK_JSON_FLOATS to compile
 *       crclink_json_get_float (it pulls in strtod, which is costly on a
 *       soft-float target). Integers and the rest never need it.
 */
#ifndef CRCLINK_JSON_READ_H
#define CRCLINK_JSON_READ_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Verify a frame's CRC against its covered prefix.
 *
 * crclink always appends `"crc":"XXXX"}` as the final field, so this checks that
 * fixed trailer and computes crc16-xmodem over everything before it (the opening
 * '{' up to and including the comma before "crc"). It does not parse the payload,
 * so it verifies any frame shape, flat or nested, and matches crclink's Python
 * decoder. Call it before the getters on a frame from an untrusted link.
 *
 * @param frame NUL-terminated frame text; a trailing CR/LF is tolerated.
 * @return 0 if the CRC matches, or -1 if the trailer is malformed or the CRC
 *         does not match.
 */
int crclink_json_verify(const char *frame);

/**
 * @brief Copy the string value of @p key into @p out, JSON-unescaped and NUL-terminated.
 *
 * @param json   NUL-terminated flat JSON object text.
 * @param key    key to look up.
 * @param out    destination buffer.
 * @param outcap capacity of @p out (must leave room for the value plus a NUL).
 * @return copied length (>=0) on success, or -1 if the key is absent, its value
 *         is not a string, or it would not fit in @p outcap.
 */
int crclink_json_get_str(const char *json, const char *key, char *out, size_t outcap);

/**
 * @brief Read the integer value of @p key.
 *
 * @param json NUL-terminated flat JSON object text.
 * @param key  key to look up.
 * @param out  receives the parsed value on success.
 * @return 0 on success, or -1 if the key is absent or its value is not an integer.
 */
int crclink_json_get_int(const char *json, const char *key, long *out);

/**
 * @brief Read the boolean value of @p key.
 *
 * @param json NUL-terminated flat JSON object text.
 * @param key  key to look up.
 * @param out  receives 0 or 1 on success.
 * @return 0 on success, or -1 if the key is absent or its value is not true/false.
 */
int crclink_json_get_bool(const char *json, const char *key, int *out);

#ifdef CRCLINK_JSON_FLOATS
/**
 * @brief Read the floating-point value of @p key.
 *
 * Compiled only when CRCLINK_JSON_FLOATS is defined.
 *
 * @param json NUL-terminated flat JSON object text.
 * @param key  key to look up.
 * @param out  receives the parsed value on success.
 * @return 0 on success, or -1 if the key is absent or its value is not a number.
 */
int crclink_json_get_float(const char *json, const char *key, double *out);
#endif

/* --- Nested reads ---
 * Get a value's raw byte span with crclink_json_get_raw, then re-read that span
 * (which is valid JSON) with the _n getters, which take an explicit length
 * instead of requiring NUL-termination. One object level per call, no recursion.
 * Reaching nested values needs CRCLINK_JSON_MAX_TOKENS large enough for the
 * whole frame; raise it in your build. */

/**
 * @brief Byte span of @p key's value, any type (including a nested object/array).
 * @param json  NUL-terminated JSON object text.
 * @param key   key to look up.
 * @param start receives a pointer into @p json at the value's first byte.
 * @param len   receives the value's length in bytes (the span is valid JSON).
 * @return 0 on success, or -1 if the key is absent.
 */
int crclink_json_get_raw(const char *json, const char *key, const char **start, int *len);

/** @brief Length-aware crclink_json_get_raw for a sub-span (no NUL needed). */
int crclink_json_get_raw_n(const char *json, size_t len, const char *key, const char **start,
                           int *out_len);

/** @brief Length-aware crclink_json_get_str for a sub-span (no NUL needed). */
int crclink_json_get_str_n(const char *json, size_t len, const char *key, char *out, size_t outcap);

/** @brief Length-aware crclink_json_get_int for a sub-span. */
int crclink_json_get_int_n(const char *json, size_t len, const char *key, long *out);

/** @brief Length-aware crclink_json_get_bool for a sub-span. */
int crclink_json_get_bool_n(const char *json, size_t len, const char *key, int *out);

#ifdef CRCLINK_JSON_FLOATS
/** @brief Length-aware crclink_json_get_float for a sub-span (CRCLINK_JSON_FLOATS only). */
int crclink_json_get_float_n(const char *json, size_t len, const char *key, double *out);
#endif

#ifdef __cplusplus
}
#endif

#endif /* CRCLINK_JSON_READ_H */
