/* crclink_json.c -- see crclink_json.h for the API and frame contract. */
#include "crclink_json.h"

#include "crc16_xmodem.h"

#include <stdio.h>

/* Emit one prefix byte: send to the sink, then fold into the running CRC.
 * No-op once err latches; the CRC is updated only for a byte the sink accepted,
 * so it always covers exactly the bytes that reached the output. */
static void emit(crclink_json_t *j, char byte) {
    if (j->err) {
        return;
    }
    uint8_t b = (uint8_t)byte;
    if (j->sink(j->ctx, b)) {
        j->err = -1;
        return;
    }
    j->crc = crc16_xmodem_update(j->crc, &b, 1);
    j->total++;
}

/* Emit one trailer byte: send to the sink WITHOUT updating the CRC. */
static void emit_raw(crclink_json_t *j, char byte) {
    if (j->err) {
        return;
    }
    if (j->sink(j->ctx, (uint8_t)byte)) {
        j->err = -1;
        return;
    }
    j->total++;
}

static void emit_str(crclink_json_t *j, const char *text) {
    for (; *text; text++) {
        emit(j, *text);
    }
}

static void emit_raw_str(crclink_json_t *j, const char *text) {
    for (; *text; text++) {
        emit_raw(j, *text);
    }
}

/* Emit a string with JSON escaping for the structural and control bytes. */
static void emit_escaped(crclink_json_t *j, const char *text) {
    for (; *text; text++) {
        unsigned char c = (unsigned char)*text;
        switch (c) {
            case '"': emit_str(j, "\\\""); break;
            case '\\': emit_str(j, "\\\\"); break;
            case '\b': emit_str(j, "\\b"); break;
            case '\f': emit_str(j, "\\f"); break;
            case '\n': emit_str(j, "\\n"); break;
            case '\r': emit_str(j, "\\r"); break;
            case '\t': emit_str(j, "\\t"); break;
            default:
                if (c < 0x20) {
                    char u[7];
                    snprintf(u, sizeof u, "\\u%04x", c);
                    emit_str(j, u);
                } else {
                    emit(j, (char)c);
                }
        }
    }
}

int crclink_json_buf_sink(void *ctx, uint8_t byte) {
    crclink_json_buf_t *b = (crclink_json_buf_t *)ctx;
    if (b->len + 1 >= b->cap) { /* reserve one byte for the NUL terminator */
        return -1;
    }
    b->buf[b->len++] = (char)byte;
    b->buf[b->len] = '\0';
    return 0;
}

int crclink_json_start(crclink_json_t *j, crclink_json_sink sink, void *ctx) {
    j->sink = sink;
    j->ctx = ctx;
    j->crc = crc16_xmodem_init();
    j->err = 0;
    j->total = 0;
    j->list_first = 0;
    emit(j, '{');
    return j->err ? -1 : 0;
}

int crclink_json_start_buf(crclink_json_t *j, crclink_json_buf_t *b, char *buf, size_t cap) {
    b->buf = buf;
    b->cap = cap;
    b->len = 0;
    if (cap > 0) {
        buf[0] = '\0';
    }
    return crclink_json_start(j, crclink_json_buf_sink, b);
}

int crclink_json_str_add(crclink_json_t *j, const char *key, const char *value) {
    emit(j, '"');
    emit_str(j, key);
    emit_str(j, "\":\"");
    emit_escaped(j, value);
    emit_str(j, "\",");
    return j->err ? -1 : 0;
}

int crclink_json_int_add(crclink_json_t *j, const char *key, long value) {
    char num[24];
    snprintf(num, sizeof num, "%ld", value);
    emit(j, '"');
    emit_str(j, key);
    emit_str(j, "\":");
    emit_str(j, num);
    emit(j, ',');
    return j->err ? -1 : 0;
}

int crclink_json_bool_add(crclink_json_t *j, const char *key, int value) {
    emit(j, '"');
    emit_str(j, key);
    emit_str(j, "\":");
    emit_str(j, value ? "true" : "false");
    emit(j, ',');
    return j->err ? -1 : 0;
}

/* Emit the inter-element comma for a scoped list (none before the first). */
static void list_sep(crclink_json_t *j) {
    if (!j->list_first) {
        emit(j, ',');
    }
    j->list_first = 0;
}

int crclink_json_list_open(crclink_json_t *j, const char *key) {
    emit(j, '"');
    emit_str(j, key);
    emit_str(j, "\":[");
    j->list_first = 1;
    return j->err ? -1 : 0;
}

int crclink_json_list_int(crclink_json_t *j, long value) {
    char num[24];
    snprintf(num, sizeof num, "%ld", value);
    list_sep(j);
    emit_str(j, num);
    return j->err ? -1 : 0;
}

int crclink_json_list_bool(crclink_json_t *j, int value) {
    list_sep(j);
    emit_str(j, value ? "true" : "false");
    return j->err ? -1 : 0;
}

int crclink_json_list_str(crclink_json_t *j, const char *value) {
    list_sep(j);
    emit(j, '"');
    emit_escaped(j, value);
    emit(j, '"');
    return j->err ? -1 : 0;
}

int crclink_json_list_close(crclink_json_t *j) {
    emit_str(j, "],");
    return j->err ? -1 : 0;
}

/* Typed homogeneous-list convenience: open, append each element, close. */
int crclink_json_int_list_add(crclink_json_t *j, const char *key, const int *values, size_t count) {
    crclink_json_list_open(j, key);
    for (size_t i = 0; i < count; i++) {
        crclink_json_list_int(j, values[i]);
    }
    return crclink_json_list_close(j);
}

int crclink_json_bool_list_add(crclink_json_t *j, const char *key, const int *values,
                               size_t count) {
    crclink_json_list_open(j, key);
    for (size_t i = 0; i < count; i++) {
        crclink_json_list_bool(j, values[i]);
    }
    return crclink_json_list_close(j);
}

int crclink_json_str_list_add(crclink_json_t *j, const char *key, const char *const *values,
                              size_t count) {
    crclink_json_list_open(j, key);
    for (size_t i = 0; i < count; i++) {
        crclink_json_list_str(j, values[i]);
    }
    return crclink_json_list_close(j);
}

int crclink_json_dict_add(crclink_json_t *j, const char *key, const char *json_object) {
    emit(j, '"');
    emit_str(j, key);
    emit_str(j, "\":");
    emit_str(j, json_object);
    emit(j, ',');
    return j->err ? -1 : 0;
}

int crclink_json_end(crclink_json_t *j) {
    if (j->err) {
        return -1;
    }
    uint16_t crc = crc16_xmodem_finalize(j->crc);
    char trailer[16];
    snprintf(trailer, sizeof trailer, "\"crc\":\"%04x\"}", crc);
    emit_raw_str(j, trailer);
    return j->err ? -1 : (int)j->total;
}
