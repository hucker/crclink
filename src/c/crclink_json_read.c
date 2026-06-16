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

/* Index just past a value token, or -1 if the value nests deeper than this flat
 * reader supports (the caller then fails closed). A scalar is one token; a flat
 * array of S scalars is 1 + S; a flat object of S scalar pairs is 1 + 2*S.
 * jsmn's size is the immediate child/pair count, not the recursive span, so a
 * value with a non-scalar child cannot be sized here without recursion: reject
 * it rather than mis-step into it. */
static int after_value(const jsmntok_t *toks, int n, int vi) {
    jsmntok_t v = toks[vi];
    int children;
    if (v.type == JSMN_ARRAY) {
        children = v.size;
    } else if (v.type == JSMN_OBJECT) {
        children = 2 * v.size;
    } else {
        return vi + 1; /* scalar */
    }
    int i = vi + 1;
    for (int c = 0; c < children; c++) {
        if (i >= n) {
            return -1; /* truncated token stream */
        }
        if (toks[i].type == JSMN_ARRAY || toks[i].type == JSMN_OBJECT) {
            return -1; /* deeper than this flat reader supports: fail closed */
        }
        i++;
    }
    return i;
}

/* Parse json and return the token index of key's value, or -1 if not found. */
static int find_value(const char *json, const char *key, jsmntok_t *toks) {
    jsmn_parser p;
    jsmn_init(&p);
    int n = jsmn_parse(&p, json, strlen(json), toks, CRCLINK_JSON_MAX_TOKENS);
    if (n < 1 || toks[0].type != JSMN_OBJECT) {
        return -1;
    }

    size_t keylen = strlen(key);
    int i = 1;
    while (i + 1 < n) {
        const jsmntok_t *k = &toks[i];
        if (k->type != JSMN_STRING) {
            break; /* malformed for a flat object */
        }
        int vi = i + 1;
        if ((size_t)(k->end - k->start) == keylen && memcmp(json + k->start, key, keylen) == 0) {
            return vi;
        }
        i = after_value(toks, n, vi);
        if (i < 0) {
            return -1; /* value too deep to step over: fail closed */
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

int crclink_json_get_str(const char *json, const char *key, char *out, size_t outcap) {
    if (outcap == 0) {
        return -1;
    }
    jsmntok_t toks[CRCLINK_JSON_MAX_TOKENS];
    int vi = find_value(json, key, toks);
    if (vi < 0 || toks[vi].type != JSMN_STRING) {
        return -1;
    }
    return copy_unescaped(json + toks[vi].start, toks[vi].end - toks[vi].start, out, outcap);
}

int crclink_json_get_int(const char *json, const char *key, long *out) {
    jsmntok_t toks[CRCLINK_JSON_MAX_TOKENS];
    int vi = find_value(json, key, toks);
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

int crclink_json_get_bool(const char *json, const char *key, int *out) {
    jsmntok_t toks[CRCLINK_JSON_MAX_TOKENS];
    int vi = find_value(json, key, toks);
    if (vi < 0 || toks[vi].type != JSMN_PRIMITIVE) {
        return -1;
    }
    const char *s = json + toks[vi].start;
    int len = toks[vi].end - toks[vi].start;
    if (len == 4 && memcmp(s, "true", 4) == 0) {
        *out = 1;
        return 0;
    }
    if (len == 5 && memcmp(s, "false", 5) == 0) {
        *out = 0;
        return 0;
    }
    return -1;
}

#ifdef CRCLINK_JSON_FLOATS
int crclink_json_get_float(const char *json, const char *key, double *out) {
    jsmntok_t toks[CRCLINK_JSON_MAX_TOKENS];
    int vi = find_value(json, key, toks);
    if (vi < 0 || toks[vi].type != JSMN_PRIMITIVE) {
        return -1;
    }
    const char *s = json + toks[vi].start;
    if (*s != '-' && (*s < '0' || *s > '9')) {
        return -1;
    }
    *out = strtod(s, NULL);
    return 0;
}
#endif
