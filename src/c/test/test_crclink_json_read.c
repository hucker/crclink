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
static const char *CMD   = "{\"cmd\":\"do something\",\"crc\":\"21f9\"}";
static const char *INTS  = "{\"n\":12,\"neg\":-5,\"crc\":\"5a8a\"}";
static const char *BOOLS = "{\"flag\":true,\"off\":false,\"crc\":\"6cb9\"}";
static const char *FLOAT = "{\"x\":3.14,\"crc\":\"e5d1\"}";
static const char *ESC   = "{\"q\":\"a\\\"b\\\\c\",\"crc\":\"f0fb\"}";
static const char *TAB   = "{\"t\":\"\\t\",\"crc\":\"aaf8\"}";

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
    TEST_ASSERT_TRUE(x > 3.139 && x < 3.141);  /* 3.14 without Unity double config */
}

void test_unescapes_quote_and_backslash(void) {
    char out[32];
    int len = crclink_json_get_str(ESC, "q", out, sizeof out);
    TEST_ASSERT_EQUAL_INT(5, len);
    TEST_ASSERT_EQUAL_STRING("a\"b\\c", out);  /* value: a"b\c */
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
    TEST_ASSERT_LESS_THAN_INT(0, crclink_json_get_int(CMD, "cmd", &n));   /* cmd is a string */
    TEST_ASSERT_LESS_THAN_INT(0, crclink_json_get_str(INTS, "n", out, sizeof out));  /* n is an int */
}

void test_buffer_too_small_returns_negative(void) {
    char out[4];  /* "do something" will not fit */
    TEST_ASSERT_LESS_THAN_INT(0, crclink_json_get_str(CMD, "cmd", out, sizeof out));
}

void test_nesting_beyond_flat_fails_closed(void) {
    /* A value nested deeper than one level is outside the flat contract. The
       reader must fail closed (-1), not step into it and return an inner value
       (regression: after_value used to mis-size nested values). */
    const char *deep = "{\"arr\":[{\"x\":99}],\"x\":5,\"crc\":\"0000\"}";
    long v = 0;
    TEST_ASSERT_LESS_THAN_INT(0, crclink_json_get_int(deep, "x", &v));
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
    RUN_TEST(test_nesting_beyond_flat_fails_closed);
    return UNITY_END();
}
