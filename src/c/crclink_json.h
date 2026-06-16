/**
 * @file crclink_json.h
 * @brief Stream a crclink CRC-framed JSON line to any output sink.
 *
 * Allocation-free JSON object builder for constrained firmware. Each emitted
 * byte is handed to a per-byte sink callback (a fixed buffer, a UART putc, ...)
 * and folded into a running crc16-xmodem, so a whole frame never has to live in
 * RAM. Call crclink_json_start, add fields, then crclink_json_end to stamp the
 * CRC and close.
 *
 * @code
 * // into a fixed buffer
 * char s[100];
 * crclink_json_t j;
 * crclink_json_buf_t b;
 * crclink_json_start_buf(&j, &b, s, sizeof s);
 * crclink_json_str_add(&j, "msg", "hi");
 * crclink_json_int_add(&j, "v", 12);
 * int xs[] = {1, 2, 3};
 * crclink_json_int_list_add(&j, "xs", xs, 3);
 * crclink_json_end(&j);   // s -> {"msg":"hi","v":12,"xs":[1,2,3],"crc":"...."}
 *
 * // straight out a serial port
 * int uart_sink(void *ctx, uint8_t byte) { uart_putc(byte); return 0; }
 * crclink_json_t j;
 * crclink_json_start(&j, uart_sink, NULL);
 * crclink_json_str_add(&j, "msg", "hi");
 * crclink_json_end(&j);   // bytes stream out as they are built
 * @endcode
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

/**
 * @brief Per-byte output sink.
 *
 * @param ctx  opaque cookie passed through from crclink_json_start.
 * @param byte the byte to write.
 * @return 0 on success, non-zero on failure (full buffer, UART error). Each call
 *         must be all-or-nothing.
 */
typedef int (*crclink_json_sink)(void *ctx, uint8_t byte);

/**
 * @brief Builder state. Initialize with crclink_json_start / crclink_json_start_buf;
 *        treat the fields as private.
 */
typedef struct {
    crclink_json_sink sink;
    void             *ctx;
    uint16_t          crc;    /**< running crc16-xmodem over the prefix */
    int               err;    /**< sticky: set on the first failed sink write */
    size_t            total;  /**< bytes emitted so far (the frame length) */
} crclink_json_t;

/**
 * @brief A fixed-buffer sink target.
 *
 * @p buf must hold @p cap bytes; the frame is kept NUL-terminated, so up to
 * cap-1 frame bytes fit. @p len is the current frame length.
 */
typedef struct {
    char  *buf;
    size_t cap;
    size_t len;
} crclink_json_buf_t;

/**
 * @brief Sink function backing crclink_json_buf_t.
 * @param ctx pointer to a crclink_json_buf_t.
 * @param byte the byte to append.
 * @return 0 on success, -1 if the buffer is full.
 */
int crclink_json_buf_sink(void *ctx, uint8_t byte);

/**
 * @brief Begin a frame writing to a caller-supplied sink.
 * @param j    builder state to initialize.
 * @param sink per-byte output sink.
 * @param ctx  opaque cookie passed to @p sink.
 * @return 0, or -1 if the opening '{' could not be emitted.
 */
int crclink_json_start(crclink_json_t *j, crclink_json_sink sink, void *ctx);

/**
 * @brief Begin a frame writing into a fixed buffer.
 *
 * Initializes @p b and wires it as the sink.
 *
 * @param j   builder state to initialize.
 * @param b   buffer-sink state to initialize.
 * @param buf destination buffer.
 * @param cap capacity of @p buf in bytes.
 * @return 0, or -1 on immediate overflow.
 */
int crclink_json_start_buf(crclink_json_t *j, crclink_json_buf_t *b, char *buf, size_t cap);

/**
 * @brief Add a "key":"value" field (the value is JSON-escaped).
 * @return 0, or -1 once the frame has overflowed.
 */
int crclink_json_str_add(crclink_json_t *j, const char *key, const char *value);

/**
 * @brief Add a "key":value field for an integer.
 * @return 0, or -1 once the frame has overflowed.
 */
int crclink_json_int_add(crclink_json_t *j, const char *key, long value);

/**
 * @brief Add a "key":[v0,...] field from a count-element int array.
 *
 * @p count 0 yields []. @p values must point to @p count ints when count > 0.
 *
 * @return 0, or -1 once the frame has overflowed.
 */
int crclink_json_int_list_add(crclink_json_t *j, const char *key, const int *values, size_t count);

/**
 * @brief Add a "key":<json_object> field verbatim (no escaping, no validation).
 * @return 0, or -1 once the frame has overflowed.
 */
int crclink_json_dict_add(crclink_json_t *j, const char *key, const char *json_object);

/**
 * @brief Stamp the CRC over the emitted prefix and close the object.
 * @return the frame length on success, or -1 if the frame overflowed (no trailer
 *         is emitted).
 */
int crclink_json_end(crclink_json_t *j);

#ifdef __cplusplus
}
#endif

#endif /* CRCLINK_JSON_H */
