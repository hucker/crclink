/* crclink_json.h -- build a crclink CRC-framed JSON line into a fixed buffer.
 *
 * Streaming, allocation-free JSON object builder for constrained firmware.
 * Call json_start once, add fields, then json_end to stamp the CRC and close:
 *
 *     char s[CRCLINK_JSON_CAP];
 *     json_start(s);
 *     json_str_add(s, "msg", "hi");
 *     json_int_add(s, "v", 12);
 *     json_int_list_add(s, "xs", 3, 1, 2, 3);   // count, then the values
 *     json_end(s);   // -> {"msg":"hi","v":12,"xs":[1,2,3],"crc":"...."}
 *
 * The buffer MUST be declared with capacity CRCLINK_JSON_CAP (override the
 * macro before including to change it). Every function bounds-checks against
 * that capacity and returns 0 on success or -1 if the field would overflow,
 * leaving the buffer a valid partial frame. Check the return value when
 * completeness matters.
 *
 * The frame is a single-level JSON object, matching crclink's Python encoder:
 * the CRC (crc16-xmodem) covers the bytes from the opening '{' up to and
 * including the comma before "crc". Uses the crcglot-generated crc16_xmodem()
 * from crc16_xmodem.h.
 */
#ifndef CRCLINK_JSON_H
#define CRCLINK_JSON_H

#include <stddef.h>

#ifndef CRCLINK_JSON_CAP
#define CRCLINK_JSON_CAP 100
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Open the object. Writes "{" into s. Returns 0, or -1 if CRCLINK_JSON_CAP < 2. */
int json_start(char *s);

/* Add "key":"value" (value is JSON-escaped). Returns 0 on success, -1 on overflow. */
int json_str_add(char *s, const char *key, const char *value);

/* Add "key":value for an integer. Returns 0 on success, -1 on overflow. */
int json_int_add(char *s, const char *key, long value);

/* Add "key":[v0,v1,...] from count int arguments. The ints are read with the
 * default argument promotion, so pass plain int values. Returns 0, -1 on overflow. */
int json_int_list_add(char *s, const char *key, size_t count, ...);

/* Add "key":<json_object> verbatim, for a nested object the caller already
 * built (no escaping, no validation). Returns 0 on success, -1 on overflow. */
int json_dict_add(char *s, const char *key, const char *json_object);

/* Stamp the CRC over the current buffer and close the object. Returns the
 * final frame length on success, or -1 on overflow. */
int json_end(char *s);

#ifdef __cplusplus
}
#endif

/* Convenience macro so the count is filled in for you, up to 16 values:
 *     JSON_INT_LIST_ADD(s, "xs", 1, 2, 3, 4, 5)
 */
#define JSON_INT_LIST_ADD(s, key, ...) \
    json_int_list_add((s), (key), JSON_NARGS(__VA_ARGS__), __VA_ARGS__)
#define JSON_NARGS(...) \
    JSON_NARGS_(__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
#define JSON_NARGS_(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, N, ...) N

#endif /* CRCLINK_JSON_H */
