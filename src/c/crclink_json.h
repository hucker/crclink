/* crclink_json.h -- stream a crclink CRC-framed JSON line to any output sink.
 *
 * Allocation-free JSON object builder for constrained firmware. Each emitted
 * byte is handed to a per-byte sink callback (a fixed buffer, a UART putc, ...)
 * and folded into a running crc16-xmodem, so a whole frame never has to live in
 * RAM. Call crclink_json_start, add fields, then crclink_json_end to stamp the
 * CRC and close.
 *
 *     // into a fixed buffer
 *     char s[100];
 *     crclink_json_t j;
 *     crclink_json_buf_t b;
 *     crclink_json_start_buf(&j, &b, s, sizeof s);
 *     crclink_json_str_add(&j, "msg", "hi");
 *     crclink_json_int_add(&j, "v", 12);
 *     int xs[] = {1, 2, 3};
 *     crclink_json_int_list_add(&j, "xs", xs, 3);
 *     crclink_json_end(&j);   // s -> {"msg":"hi","v":12,"xs":[1,2,3],"crc":"...."}
 *
 *     // straight out a serial port
 *     int uart_sink(void *ctx, uint8_t byte) { uart_putc(byte); return 0; }
 *     crclink_json_t j;
 *     crclink_json_start(&j, uart_sink, NULL);
 *     crclink_json_str_add(&j, "msg", "hi");
 *     crclink_json_end(&j);   // bytes stream out as they are built
 *
 * Each function returns 0 on success (crclink_json_end returns the frame length)
 * and -1 once any sink write has failed. Failure is sticky: after it every call
 * is a no-op and crclink_json_end emits no trailer, so a truncated frame carries
 * no "crc" key and is rejected by the decoder rather than passing with a
 * partial-prefix CRC. Check the return value when completeness matters.
 *
 * The frame is a single-level JSON object, byte-identical to crclink's Python
 * encoder: the CRC (crc16-xmodem) covers the bytes from the opening '{' up to
 * and including the comma before "crc". Uses the crcglot-generated CRC from
 * crc16_xmodem.h.
 */
#ifndef CRCLINK_JSON_H
#define CRCLINK_JSON_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Per-byte output sink. Write one byte; return 0 on success, non-zero on
 * failure (full buffer, UART error). Each call must be all-or-nothing. */
typedef int (*crclink_json_sink)(void *ctx, uint8_t byte);

/* Builder state. Initialize with crclink_json_start / crclink_json_start_buf;
 * treat the fields as private. */
typedef struct {
    crclink_json_sink sink;
    void             *ctx;
    uint16_t          crc;    /* running crc16-xmodem over the prefix */
    int               err;    /* sticky: set on the first failed sink write */
    size_t            total;  /* bytes emitted so far (the frame length) */
} crclink_json_t;

/* A fixed-buffer sink target. buf must hold cap bytes; the frame is kept
 * NUL-terminated, so up to cap-1 frame bytes fit. len is the frame length. */
typedef struct {
    char  *buf;
    size_t cap;
    size_t len;
} crclink_json_buf_t;

/* Sink function backing crclink_json_buf_t (ctx is a crclink_json_buf_t *). */
int crclink_json_buf_sink(void *ctx, uint8_t byte);

/* Begin a frame writing to a caller-supplied sink. Returns 0, or -1 if the
 * opening '{' could not be emitted. */
int crclink_json_start(crclink_json_t *j, crclink_json_sink sink, void *ctx);

/* Begin a frame writing into buf[0..cap). Initializes b and wires it as the
 * sink. Returns 0, or -1 on immediate overflow. */
int crclink_json_start_buf(crclink_json_t *j, crclink_json_buf_t *b, char *buf, size_t cap);

/* Add "key":"value" (value is JSON-escaped). Returns 0, -1 once overflowed. */
int crclink_json_str_add(crclink_json_t *j, const char *key, const char *value);

/* Add "key":value for an integer. Returns 0, -1 once overflowed. */
int crclink_json_int_add(crclink_json_t *j, const char *key, long value);

/* Add "key":[v0,...] from a count-element int array (count 0 yields []). values
 * must point to count ints when count > 0. Returns 0, -1 once overflowed. */
int crclink_json_int_list_add(crclink_json_t *j, const char *key, const int *values, size_t count);

/* Add "key":<json_object> verbatim (no escaping, no validation). Returns 0, -1. */
int crclink_json_dict_add(crclink_json_t *j, const char *key, const char *json_object);

/* Stamp the CRC over the emitted prefix and close the object. Returns the frame
 * length on success, or -1 if the frame overflowed (no trailer is emitted). */
int crclink_json_end(crclink_json_t *j);

#ifdef __cplusplus
}
#endif

#endif /* CRCLINK_JSON_H */
