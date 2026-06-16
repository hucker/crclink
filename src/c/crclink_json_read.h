/**
 * @file crclink_json_read.h
 * @brief Read fields from a flat crclink JSON frame body.
 *
 * Typed getters for a single-level JSON object, for a command arriving over a
 * serial link, e.g. `{"cmd":"reboot","n":3,"crc":"...."}`. The CRC is assumed to
 * have been checked already; these only extract values by key. Numbers are
 * converted for you (no string detour). Parsing is built on a vendored jsmn
 * (MIT) and uses no heap.
 *
 * @code
 * char cmd[20];
 * if (crclink_json_get_str(line, "cmd", cmd, sizeof cmd) >= 0
 *     && strcmp(cmd, "reboot") == 0) { ... }
 * long n;
 * crclink_json_get_int(line, "n", &n);
 * @endcode
 *
 * @p json must be NUL-terminated. Each getter parses on its own, which is cheap
 * for short command lines. Values may be scalars, or one-level arrays/objects of
 * scalars; a value nested deeper than that makes a getter return -1 (fail
 * closed). The token budget is CRCLINK_JSON_MAX_TOKENS (default 16; raise it
 * before including for wider frames; it sizes a stack array of ~16 bytes each).
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

#ifdef __cplusplus
}
#endif

#endif /* CRCLINK_JSON_READ_H */
