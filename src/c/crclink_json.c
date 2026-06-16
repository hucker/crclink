/* crclink_json.c -- see crclink_json.h for the API and frame contract. */
#include "crclink_json.h"

#include "crc16_xmodem.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Append text if it fits the capacity; return 0 on success, -1 on overflow.
 * On overflow the buffer is left unchanged (the size check precedes the copy). */
static int put(char *s, const char *text) {
    size_t len = strlen(s);
    size_t add = strlen(text);
    if (len + add + 1 > CRCLINK_JSON_CAP) {
        return -1;
    }
    memcpy(s + len, text, add + 1);
    return 0;
}

/* Append a string with JSON escaping for the structural and control bytes. */
static int put_escaped(char *s, const char *text) {
    for (; *text; text++) {
        unsigned char c = (unsigned char)*text;
        char buf[7];
        const char *piece;
        switch (c) {
            case '"':  piece = "\\\""; break;
            case '\\': piece = "\\\\"; break;
            case '\n': piece = "\\n";  break;
            case '\r': piece = "\\r";  break;
            case '\t': piece = "\\t";  break;
            default:
                if (c < 0x20) {
                    snprintf(buf, sizeof buf, "\\u%04x", c);
                } else {
                    buf[0] = (char)c;
                    buf[1] = '\0';
                }
                piece = buf;
        }
        if (put(s, piece)) {
            return -1;
        }
    }
    return 0;
}

int json_start(char *s) {
    if (CRCLINK_JSON_CAP < 2) {
        return -1;
    }
    s[0] = '{';
    s[1] = '\0';
    return 0;
}

int json_str_add(char *s, const char *key, const char *value) {
    size_t mark = strlen(s);
    if (put(s, "\"") || put(s, key) || put(s, "\":\"") || put_escaped(s, value) || put(s, "\",")) {
        s[mark] = '\0';  /* roll back any partial write */
        return -1;
    }
    return 0;
}

int json_int_add(char *s, const char *key, long value) {
    char num[24];
    snprintf(num, sizeof num, "%ld", value);
    size_t mark = strlen(s);
    if (put(s, "\"") || put(s, key) || put(s, "\":") || put(s, num) || put(s, ",")) {
        s[mark] = '\0';
        return -1;
    }
    return 0;
}

int json_int_list_add(char *s, const char *key, size_t count, ...) {
    size_t mark = strlen(s);
    if (put(s, "\"") || put(s, key) || put(s, "\":[")) {
        s[mark] = '\0';
        return -1;
    }

    va_list ap;
    va_start(ap, count);
    int failed = 0;
    for (size_t i = 0; i < count; i++) {
        char num[24];
        snprintf(num, sizeof num, "%d", va_arg(ap, int));
        if (put(s, num) || (i + 1 < count && put(s, ","))) {
            failed = 1;
            break;
        }
    }
    va_end(ap);

    if (failed || put(s, "],")) {
        s[mark] = '\0';
        return -1;
    }
    return 0;
}

int json_dict_add(char *s, const char *key, const char *json_object) {
    size_t mark = strlen(s);
    if (put(s, "\"") || put(s, key) || put(s, "\":") || put(s, json_object) || put(s, ",")) {
        s[mark] = '\0';
        return -1;
    }
    return 0;
}

int json_end(char *s) {
    /* The CRC covers the buffer as built: '{' through the trailing comma
     * before "crc" (or just '{' for an empty object). */
    size_t mark = strlen(s);
    uint16_t crc = crc16_xmodem((const uint8_t *)s, mark);
    char trailer[16];
    snprintf(trailer, sizeof trailer, "\"crc\":\"%04x\"}", crc);
    if (put(s, trailer)) {
        s[mark] = '\0';
        return -1;
    }
    return (int)strlen(s);
}
