/* Unity test suite for the crclink flat JSON reader (crclink_json_read).
 *
 * The input frames are real frames from crclink's Python encoder
 * (crclink.encode_json_frame), so this is the read side of the round trip:
 * Python encodes, C reads the fields back. Built with -DCRCLINK_JSON_FLOATS so
 * the float getter is exercised.
 */
#include "unity.h"

#include "crclink_json_read.h"

#include <string.h>

/* Golden frames (the trailing "crc" is present but ignored by the reader). */
static const char *CMD = "{\"cmd\":\"do something\",\"crc\":\"21f9\"}";
static const char *INTS = "{\"n\":12,\"neg\":-5,\"crc\":\"5a8a\"}";
static const char *BOOLS = "{\"flag\":true,\"off\":false,\"crc\":\"6cb9\"}";
static const char *FLOAT = "{\"x\":3.14,\"crc\":\"e5d1\"}";
static const char *ESC = "{\"q\":\"a\\\"b\\\\c\",\"crc\":\"f0fb\"}";
static const char *TAB = "{\"t\":\"\\t\",\"crc\":\"aaf8\"}";
static const char *EMPTY = "{\"crc\":\"cffc\"}";

void setUp(void) {}
void tearDown(void) {}

void test_get_str(void) {
    char out[32];
    int len = crclink_json_get_str(CMD, "cmd", out, sizeof out);
    TEST_ASSERT_EQUAL_INT((int)strlen("do something"), len);
    TEST_ASSERT_EQUAL_STRING("do something", out);
}

void test_get_int(void) {
    long n = 0;
    TEST_ASSERT_EQUAL_INT(0, crclink_json_get_int(INTS, "n", &n));
    TEST_ASSERT_EQUAL_INT(12, n);
    TEST_ASSERT_EQUAL_INT(0, crclink_json_get_int(INTS, "neg", &n));
    TEST_ASSERT_EQUAL_INT(-5, n);
}

void test_get_bool(void) {
    int b = -1;
    TEST_ASSERT_EQUAL_INT(0, crclink_json_get_bool(BOOLS, "flag", &b));
    TEST_ASSERT_EQUAL_INT(1, b);
    TEST_ASSERT_EQUAL_INT(0, crclink_json_get_bool(BOOLS, "off", &b));
    TEST_ASSERT_EQUAL_INT(0, b);
}

void test_get_float(void) {
    double x = 0.0;
    TEST_ASSERT_EQUAL_INT(0, crclink_json_get_float(FLOAT, "x", &x));
    TEST_ASSERT_TRUE(x > 3.139 && x < 3.141); /* 3.14 without Unity double config */
}

void test_unescapes_quote_and_backslash(void) {
    char out[32];
    int len = crclink_json_get_str(ESC, "q", out, sizeof out);
    TEST_ASSERT_EQUAL_INT(5, len);
    TEST_ASSERT_EQUAL_STRING("a\"b\\c", out); /* value: a"b\c */
}

void test_unescapes_short_escape(void) {
    char out[8];
    int len = crclink_json_get_str(TAB, "t", out, sizeof out);
    TEST_ASSERT_EQUAL_INT(1, len);
    TEST_ASSERT_EQUAL_CHAR('\t', out[0]);
}

void test_missing_key_returns_negative(void) {
    char out[16];
    long n;
    TEST_ASSERT_LESS_THAN_INT(0, crclink_json_get_str(CMD, "nope", out, sizeof out));
    TEST_ASSERT_LESS_THAN_INT(0, crclink_json_get_int(CMD, "nope", &n));
}

void test_wrong_type_returns_negative(void) {
    char out[16];
    long n;
    TEST_ASSERT_LESS_THAN_INT(0, crclink_json_get_int(CMD, "cmd", &n)); /* cmd is a string */
    TEST_ASSERT_LESS_THAN_INT(0,
                              crclink_json_get_str(INTS, "n", out, sizeof out)); /* n is an int */
}

void test_buffer_too_small_returns_negative(void) {
    char out[4]; /* "do something" will not fit */
    TEST_ASSERT_LESS_THAN_INT(0, crclink_json_get_str(CMD, "cmd", out, sizeof out));
}

void test_skips_nested_value_to_reach_later_key(void) {
    /* The walk skips a nested value (the "arr" array of objects) to reach the
       top-level "x":5; the inner x:99 is not top-level and must not be returned. */
    const char *frame = "{\"arr\":[{\"x\":99}],\"x\":5,\"crc\":\"0000\"}";
    long v = 0;
    TEST_ASSERT_EQUAL_INT(0, crclink_json_get_int(frame, "x", &v));
    TEST_ASSERT_EQUAL_INT(5, v);
}

void test_reads_nested_via_get_raw(void) {
    /* MCP-shaped frame: get the nested params span, then read its fields, then
       descend once more into params.arguments. Built at MAX_TOKENS=32. */
    const char *frame = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\","
                        "\"params\":{\"name\":\"set\",\"arguments\":{\"ch\":2,\"on\":true}},"
                        "\"crc\":\"638c\"}";
    /* top-level scalars read past the nested params */
    long id = 0;
    char method[16];
    TEST_ASSERT_EQUAL_INT(0, crclink_json_get_int(frame, "id", &id));
    TEST_ASSERT_EQUAL_INT(1, id);
    TEST_ASSERT_TRUE(crclink_json_get_str(frame, "method", method, sizeof method) >= 0);
    TEST_ASSERT_EQUAL_STRING("tools/call", method);

    /* descend into params, then params.arguments */
    const char *params;
    int plen;
    TEST_ASSERT_EQUAL_INT(0, crclink_json_get_raw(frame, "params", &params, &plen));
    char name[16];
    TEST_ASSERT_TRUE(crclink_json_get_str_n(params, (size_t)plen, "name", name, sizeof name) >= 0);
    TEST_ASSERT_EQUAL_STRING("set", name);

    const char *args;
    int alen;
    TEST_ASSERT_EQUAL_INT(0,
                          crclink_json_get_raw_n(params, (size_t)plen, "arguments", &args, &alen));
    long ch = 0;
    int on = 0;
    TEST_ASSERT_EQUAL_INT(0, crclink_json_get_int_n(args, (size_t)alen, "ch", &ch));
    TEST_ASSERT_EQUAL_INT(2, ch);
    TEST_ASSERT_EQUAL_INT(0, crclink_json_get_bool_n(args, (size_t)alen, "on", &on));
    TEST_ASSERT_EQUAL_INT(1, on);
}

void test_verify_accepts_valid_frames(void) {
    /* Real frames from crclink.encode_json_frame: C verifies the Python CRC. */
    TEST_ASSERT_EQUAL_INT(0, crclink_json_verify(CMD));
    TEST_ASSERT_EQUAL_INT(0, crclink_json_verify(INTS));
    TEST_ASSERT_EQUAL_INT(0, crclink_json_verify(EMPTY));
}

void test_verify_rejects_tampered_payload(void) {
    /* CMD payload altered (capital G) but the original crc kept: must fail. */
    const char *bad = "{\"cmd\":\"do somethinG\",\"crc\":\"21f9\"}";
    TEST_ASSERT_LESS_THAN_INT(0, crclink_json_verify(bad));
}

void test_verify_rejects_wrong_crc(void) {
    const char *bad = "{\"cmd\":\"do something\",\"crc\":\"0000\"}";
    TEST_ASSERT_LESS_THAN_INT(0, crclink_json_verify(bad));
}

void test_verify_rejects_frame_without_crc(void) {
    TEST_ASSERT_LESS_THAN_INT(0, crclink_json_verify("{\"cmd\":\"x\"}"));
}

void test_verify_accepts_nested_frame(void) {
    /* The crc is the trailing field, so verification is independent of payload
       structure: an MCP-shaped nested frame verifies just like a flat one. */
    const char *nested = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\","
                         "\"params\":{\"name\":\"x\",\"arguments\":{}},\"crc\":\"4485\"}";
    TEST_ASSERT_EQUAL_INT(0, crclink_json_verify(nested));
    /* A frame the C builder itself emits via dict_add (nested object) verifies too. */
    TEST_ASSERT_EQUAL_INT(0, crclink_json_verify("{\"sub\":{\"k\":1},\"crc\":\"5544\"}"));
}

void test_verify_tolerates_trailing_newline(void) {
    TEST_ASSERT_EQUAL_INT(0,
                          crclink_json_verify("{\"cmd\":\"do something\",\"crc\":\"21f9\"}\r\n"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_get_str);
    RUN_TEST(test_get_int);
    RUN_TEST(test_get_bool);
    RUN_TEST(test_get_float);
    RUN_TEST(test_unescapes_quote_and_backslash);
    RUN_TEST(test_unescapes_short_escape);
    RUN_TEST(test_missing_key_returns_negative);
    RUN_TEST(test_wrong_type_returns_negative);
    RUN_TEST(test_buffer_too_small_returns_negative);
    RUN_TEST(test_skips_nested_value_to_reach_later_key);
    RUN_TEST(test_reads_nested_via_get_raw);
    RUN_TEST(test_verify_accepts_valid_frames);
    RUN_TEST(test_verify_rejects_tampered_payload);
    RUN_TEST(test_verify_rejects_wrong_crc);
    RUN_TEST(test_verify_rejects_frame_without_crc);
    RUN_TEST(test_verify_accepts_nested_frame);
    RUN_TEST(test_verify_tolerates_trailing_newline);
    return UNITY_END();
}
