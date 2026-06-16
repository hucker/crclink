/* crclink_json_read.c -- see crclink_json_read.h for the API and contract. */
#include "crclink_json_read.h"

#define JSMN_STATIC
#include "jsmn.h"

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
