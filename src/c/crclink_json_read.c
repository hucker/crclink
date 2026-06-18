/* crclink_json_read.c -- see crclink_json_read.h for the API and contract. */
#include "crclink_json_read.h"

#define JSMN_STATIC
#include "jsmn.h"

#include "crc16_xmodem.h"

#include <stdlib.h>
#include <string.h>

#ifndef CRCLINK_JSON_MAX_TOKENS
#define CRCLINK_JSON_MAX_TOKENS 16
#endif

/* Parse json[0..len) and return the token index of key's value at the top level,
 * or -1 if not found / parse error. A value's whole subtree is skipped by byte
 * range (the next sibling is the first token starting at or after this value's
 * end), so nested values between { and the key are stepped over correctly,
 * iteratively, at any depth, no recursion. */
static int find_value_n(const char *json, size_t len, const char *key, jsmntok_t *toks) {
    jsmn_parser p;
    jsmn_init(&p);
    int n = jsmn_parse(&p, json, len, toks, CRCLINK_JSON_MAX_TOKENS);
    if (n < 1 || toks[0].type != JSMN_OBJECT) {
        return -1;
    }

    size_t keylen = strlen(key);
    int i = 1;
    while (i < n) {
        const jsmntok_t *k = &toks[i];
        if (k->type != JSMN_STRING || i + 1 >= n) {
            break;
        }
        int vi = i + 1;
        if ((size_t)(k->end - k->start) == keylen && memcmp(json + k->start, key, keylen) == 0) {
            return vi;
        }
        int end = toks[vi].end;
        i = vi + 1;
        while (i < n && toks[i].start < end) {
            i++; /* skip this value's whole subtree */
        }
    }
    return -1;
}

/* Copy a JSON string span (json[start..end)) into out, unescaping, with a NUL.
 * Returns the length, or -1 if it would not fit (out needs room for the NUL). */
static int copy_unescaped(const char *src, int len, char *out, size_t outcap) {
    size_t o = 0;
    for (int i = 0; i < len; i++) {
        char ch = src[i];
        if (ch == '\\' && i + 1 < len) {
            char e = src[++i];
            switch (e) {
                case '"': ch = '"'; break;
                case '\\': ch = '\\'; break;
                case '/': ch = '/'; break;
                case 'b': ch = '\b'; break;
                case 'f': ch = '\f'; break;
                case 'n': ch = '\n'; break;
                case 'r': ch = '\r'; break;
                case 't': ch = '\t'; break;
                case 'u': {
                    /* \uXXXX: decode 4 hex digits; emit one byte if it fits
                     * (the builder only ever emits \u00xx for control bytes),
                     * otherwise a '?' placeholder. */
                    if (i + 4 < len) {
                        unsigned cp = 0;
                        int ok = 1;
                        for (int h = 1; h <= 4; h++) {
                            char d = src[i + h];
                            cp <<= 4;
                            if (d >= '0' && d <= '9')
                                cp |= (unsigned)(d - '0');
                            else if (d >= 'a' && d <= 'f')
                                cp |= (unsigned)(d - 'a' + 10);
                            else if (d >= 'A' && d <= 'F')
                                cp |= (unsigned)(d - 'A' + 10);
                            else {
                                ok = 0;
                                break;
                            }
                        }
                        if (ok) {
                            i += 4;
                            ch = (cp <= 0xFF) ? (char)cp : '?';
                        } else {
                            ch = e; /* malformed \u, copy the 'u' literally */
                        }
                    } else {
                        ch = e;
                    }
                    break;
                }
                default: ch = e; break; /* unknown escape: copy the char as-is */
            }
        }
        if (o + 1 >= outcap) {
            return -1; /* no room for this byte plus the NUL */
        }
        out[o++] = ch;
    }
    out[o] = '\0';
    return (int)o;
}

/* Parse exactly len hex digits at s into *out. Returns 0, or -1 if not 4 hex. */
static int parse_hex4(const char *s, int len, uint16_t *out) {
    if (len != 4) {
        return -1;
    }
    uint16_t v = 0;
    for (int i = 0; i < 4; i++) {
        char d = s[i];
        v = (uint16_t)(v << 4);
        if (d >= '0' && d <= '9') {
            v |= (uint16_t)(d - '0');
        } else if (d >= 'a' && d <= 'f') {
            v |= (uint16_t)(d - 'a' + 10);
        } else if (d >= 'A' && d <= 'F') {
            v |= (uint16_t)(d - 'A' + 10);
        } else {
            return -1;
        }
    }
    *out = v;
    return 0;
}

int crclink_json_verify(const char *frame) {
    size_t len = strlen(frame);
    while (len > 0 && (frame[len - 1] == '\n' || frame[len - 1] == '\r')) {
        len--; /* tolerate a trailing CR/LF from the link */
    }

    /* crclink always ends a frame with the fixed 13-byte trailer "crc":"XXXX"},
     * because the crc is the last top-level field. Check that trailer, parse the
     * 4-hex value, and CRC everything before it. This does not parse the payload,
     * so flat and nested frames verify the same way. */
    const size_t trailer = 13; /* "crc":"XXXX"} */
    if (len < trailer + 1 || frame[0] != '{') {
        return -1;
    }
    const char *t = frame + (len - trailer);
    uint16_t claimed;
    if (t[0] != '"' || t[1] != 'c' || t[2] != 'r' || t[3] != 'c' || t[4] != '"' || t[5] != ':' ||
        t[6] != '"' || t[11] != '"' || t[12] != '}' || parse_hex4(t + 7, 4, &claimed) != 0) {
        return -1;
    }
    /* Prefix: '{' up to and including the comma before the trailer (or just '{'
     * for an empty object), matching the Python decoder's coverage. */
    uint16_t computed = crc16_xmodem((const uint8_t *)frame, len - trailer);
    return (computed == claimed) ? 0 : -1;
}

int crclink_json_get_str_n(const char *json, size_t len, const char *key, char *out,
                           size_t outcap) {
    if (outcap == 0) {
        return -1;
    }
    jsmntok_t toks[CRCLINK_JSON_MAX_TOKENS];
    int vi = find_value_n(json, len, key, toks);
    if (vi < 0 || toks[vi].type != JSMN_STRING) {
        return -1;
    }
    return copy_unescaped(json + toks[vi].start, toks[vi].end - toks[vi].start, out, outcap);
}

int crclink_json_get_str(const char *json, const char *key, char *out, size_t outcap) {
    return crclink_json_get_str_n(json, strlen(json), key, out, outcap);
}

int crclink_json_get_int_n(const char *json, size_t len, const char *key, long *out) {
    jsmntok_t toks[CRCLINK_JSON_MAX_TOKENS];
    int vi = find_value_n(json, len, key, toks);
    if (vi < 0 || toks[vi].type != JSMN_PRIMITIVE) {
        return -1;
    }
    const char *s = json + toks[vi].start;
    if (*s != '-' && (*s < '0' || *s > '9')) {
        return -1; /* true / false / null */
    }
    *out = strtol(s, NULL, 10);
    return 0;
}

int crclink_json_get_int(const char *json, const char *key, long *out) {
    return crclink_json_get_int_n(json, strlen(json), key, out);
}

int crclink_json_get_bool_n(const char *json, size_t len, const char *key, int *out) {
    jsmntok_t toks[CRCLINK_JSON_MAX_TOKENS];
    int vi = find_value_n(json, len, key, toks);
    if (vi < 0 || toks[vi].type != JSMN_PRIMITIVE) {
        return -1;
    }
    const char *s = json + toks[vi].start;
    int tlen = toks[vi].end - toks[vi].start;
    if (tlen == 4 && memcmp(s, "true", 4) == 0) {
        *out = 1;
        return 0;
    }
    if (tlen == 5 && memcmp(s, "false", 5) == 0) {
        *out = 0;
        return 0;
    }
    return -1;
}

int crclink_json_get_bool(const char *json, const char *key, int *out) {
    return crclink_json_get_bool_n(json, strlen(json), key, out);
}

/* Return the byte span of key's value (any type, including a nested {...}/[...]).
 * The span is valid JSON, so a nested object can be re-read with the _n getters. */
int crclink_json_get_raw_n(const char *json, size_t len, const char *key, const char **start,
                           int *out_len) {
    jsmntok_t toks[CRCLINK_JSON_MAX_TOKENS];
    int vi = find_value_n(json, len, key, toks);
    if (vi < 0) {
        return -1;
    }
    *start = json + toks[vi].start;
    *out_len = toks[vi].end - toks[vi].start;
    return 0;
}

int crclink_json_get_raw(const char *json, const char *key, const char **start, int *out_len) {
    return crclink_json_get_raw_n(json, strlen(json), key, start, out_len);
}
