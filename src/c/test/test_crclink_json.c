/* Unity test suite for the crclink C JSON frame builder.
 *
 * The expected frame strings are golden values produced by crclink's Python
 * encoder (crclink.encode_json_frame), so these tests double as a cross-language
 * parity check: if the C output drifts from Python, the string compare fails.
 * Regenerate goldens with, e.g.:
 *     uv run python -c "from crclink import encode_json_frame; \
 *         print(encode_json_frame({'msg':'hi','v':12}).decode())"
 */
#include "unity.h"

#include "crc16_xmodem.h"
#include "crclink_json.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_crc_self_test_passes(void) { TEST_ASSERT_EQUAL_INT(0, crc16_xmodem_self_test()); }

void test_simple_frame(void) {
    char s[100];
    crclink_json_t j;
    crclink_json_buf_t b;
    crclink_json_start_buf(&j, &b, s, sizeof s);
    crclink_json_str_add(&j, "msg", "hi");
    crclink_json_int_add(&j, "v", 12);
    int len = crclink_json_end(&j);

    TEST_ASSERT_EQUAL_STRING("{\"msg\":\"hi\",\"v\":12,\"crc\":\"0256\"}", s);
    TEST_ASSERT_EQUAL_INT((int)strlen(s), len); /* end() returns frame length */
}

void test_int_list(void) {
    char s[100];
    crclink_json_t j;
    crclink_json_buf_t b;
    int xs[] = {1, 2, 3, 4, 5};
    crclink_json_start_buf(&j, &b, s, sizeof s);
    crclink_json_int_list_add(&j, "xs", xs, 5);
    crclink_json_end(&j);

    TEST_ASSERT_EQUAL_STRING("{\"xs\":[1,2,3,4,5],\"crc\":\"87e0\"}", s);
}

void test_empty_object(void) {
    char s[100];
    crclink_json_t j;
    crclink_json_buf_t b;
    crclink_json_start_buf(&j, &b, s, sizeof s);
    crclink_json_end(&j);

    TEST_ASSERT_EQUAL_STRING("{\"crc\":\"cffc\"}", s);
}

void test_bool(void) {
    char s[100];
    crclink_json_t j;
    crclink_json_buf_t b;
    crclink_json_start_buf(&j, &b, s, sizeof s);
    crclink_json_bool_add(&j, "flag", 1);
    crclink_json_bool_add(&j, "off", 0);
    crclink_json_end(&j);

    TEST_ASSERT_EQUAL_STRING("{\"flag\":true,\"off\":false,\"crc\":\"6cb9\"}", s);
}

void test_str_list(void) {
    char s[100];
    crclink_json_t j;
    crclink_json_buf_t b;
    const char *tags[] = {"a", "b", "c"};
    crclink_json_start_buf(&j, &b, s, sizeof s);
    crclink_json_str_list_add(&j, "tags", tags, 3);
    crclink_json_end(&j);

    TEST_ASSERT_EQUAL_STRING("{\"tags\":[\"a\",\"b\",\"c\"],\"crc\":\"3faa\"}", s);
}

void test_bool_list(void) {
    char s[100];
    crclink_json_t j;
    crclink_json_buf_t b;
    int flags[] = {1, 0, 1};
    crclink_json_start_buf(&j, &b, s, sizeof s);
    crclink_json_bool_list_add(&j, "flags", flags, 3);
    crclink_json_end(&j);

    TEST_ASSERT_EQUAL_STRING("{\"flags\":[true,false,true],\"crc\":\"6f9c\"}", s);
}

void test_polymorphic_list(void) {
    char s[100];
    crclink_json_t j;
    crclink_json_buf_t b;
    crclink_json_start_buf(&j, &b, s, sizeof s);
    crclink_json_list_open(&j, "items");
    crclink_json_list_int(&j, 1);
    crclink_json_list_str(&j, "two");
    crclink_json_list_bool(&j, 1);
    crclink_json_list_close(&j);
    crclink_json_end(&j);

    TEST_ASSERT_EQUAL_STRING("{\"items\":[1,\"two\",true],\"crc\":\"40a9\"}", s);
}

void test_string_escaping_quote_and_backslash(void) {
    char s[100];
    crclink_json_t j;
    crclink_json_buf_t b;
    crclink_json_start_buf(&j, &b, s, sizeof s);
    crclink_json_str_add(&j, "q", "a\"b\\c"); /* value: a"b\c */
    crclink_json_end(&j);

    TEST_ASSERT_EQUAL_STRING("{\"q\":\"a\\\"b\\\\c\",\"crc\":\"f0fb\"}", s);
}

void test_backspace_uses_short_escape(void) {
    char s[100];
    crclink_json_t j;
    crclink_json_buf_t b;
    crclink_json_start_buf(&j, &b, s, sizeof s);
    crclink_json_str_add(&j, "m", "\b"); /* backspace -> \b, not  */
    crclink_json_end(&j);

    TEST_ASSERT_EQUAL_STRING("{\"m\":\"\\b\",\"crc\":\"3f86\"}", s);
}

void test_nested_dict_verbatim(void) {
    char s[100];
    crclink_json_t j;
    crclink_json_buf_t b;
    int xs[] = {1, 2, 3, 4, 5};
    crclink_json_start_buf(&j, &b, s, sizeof s);
    crclink_json_str_add(&j, "foo", "fum");
    crclink_json_int_add(&j, "fum", 12);
    crclink_json_int_list_add(&j, "fst", xs, 5);
    crclink_json_dict_add(&j, "sub", "{\"k\":1}");
    crclink_json_end(&j);

    TEST_ASSERT_EQUAL_STRING(
        "{\"foo\":\"fum\",\"fum\":12,\"fst\":[1,2,3,4,5],\"sub\":{\"k\":1},\"crc\":\"f74d\"}", s);
}

/* A user putc-style sink, to prove sink-independence against the buffer sink. */
static char g_collect[256];
static size_t g_collect_n;
static int collect_sink(void *ctx, uint8_t byte) {
    (void)ctx;
    g_collect[g_collect_n++] = (char)byte;
    g_collect[g_collect_n] = '\0';
    return 0;
}

void test_buffer_and_putc_sinks_produce_identical_bytes(void) {
    char s[100];
    crclink_json_t jb;
    crclink_json_buf_t b;
    crclink_json_start_buf(&jb, &b, s, sizeof s);
    crclink_json_str_add(&jb, "msg", "hi");
    crclink_json_int_add(&jb, "v", 12);
    crclink_json_end(&jb);

    g_collect_n = 0;
    g_collect[0] = '\0';
    crclink_json_t jc;
    crclink_json_start(&jc, collect_sink, NULL);
    crclink_json_str_add(&jc, "msg", "hi");
    crclink_json_int_add(&jc, "v", 12);
    crclink_json_end(&jc);

    TEST_ASSERT_EQUAL_STRING(s, g_collect);
}

void test_overflow_returns_negative_and_stays_in_bounds(void) {
    char s[10]; /* far too small for the frame below */
    crclink_json_t j;
    crclink_json_buf_t b;
    crclink_json_start_buf(&j, &b, s, sizeof s);
    crclink_json_str_add(&j, "foo", "fum");
    crclink_json_int_add(&j, "fum", 12);
    int r = crclink_json_end(&j);

    TEST_ASSERT_LESS_THAN_INT(0, r);        /* overflow signalled */
    TEST_ASSERT_TRUE(b.len < b.cap);        /* never wrote past capacity */
    TEST_ASSERT_EQUAL_CHAR('\0', s[b.len]); /* still NUL-terminated in bounds */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_crc_self_test_passes);
    RUN_TEST(test_simple_frame);
    RUN_TEST(test_int_list);
    RUN_TEST(test_empty_object);
    RUN_TEST(test_bool);
    RUN_TEST(test_str_list);
    RUN_TEST(test_bool_list);
    RUN_TEST(test_polymorphic_list);
    RUN_TEST(test_string_escaping_quote_and_backslash);
    RUN_TEST(test_backspace_uses_short_escape);
    RUN_TEST(test_nested_dict_verbatim);
    RUN_TEST(test_buffer_and_putc_sinks_produce_identical_bytes);
    RUN_TEST(test_overflow_returns_negative_and_stays_in_bounds);
    return UNITY_END();
}
