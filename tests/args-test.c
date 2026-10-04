#include <stdio.h>
#include <string.h>
#include "harness/unity.h"
#include "../src/args.h"
#include "tests.h"

void test_no_args_is_usage(void) {
  char *argv[] = {"myapp", NULL};
  struct args a;
  TEST_ASSERT_EQUAL(ARGS_USAGE, args_parse(1, argv, &a));
}

void test_send_defaults(void) {
  char *argv[] = {"myapp", "send", "-s", "jdoe-1", "127.0.0.1", "in.bin", NULL};
  struct args a;
  TEST_ASSERT_EQUAL(ARGS_OK, args_parse(6, argv, &a));
  TEST_ASSERT_EQUAL(MODE_SEND, a.mode);
  TEST_ASSERT_EQUAL_STRING("jdoe-1", a.session);
  TEST_ASSERT_EQUAL_UINT(8, a.window);
  TEST_ASSERT_EQUAL_UINT(250, a.timeout_ms);
  TEST_ASSERT_EQUAL_UINT16(4250, a.port);
  TEST_ASSERT_EQUAL_STRING("127.0.0.1", a.relay);
  TEST_ASSERT_EQUAL_STRING("in.bin", a.file);
}

void test_send_all_options(void) {
  char *argv[] = {"myapp", "send", "-s", "x", "-w", "16", "-T", "100", "-l", "0.1",
                  "-c", "0.05", "-d", "0.2", "-p", "30000", "host", "f", NULL};
  struct args a;
  TEST_ASSERT_EQUAL(ARGS_OK, args_parse(18, argv, &a));
  TEST_ASSERT_EQUAL_UINT(16, a.window);
  TEST_ASSERT_EQUAL_UINT(100, a.timeout_ms);
  TEST_ASSERT_TRUE(a.loss == 0.1);
  TEST_ASSERT_TRUE(a.corrupt == 0.05);
  TEST_ASSERT_TRUE(a.dup == 0.2);
  TEST_ASSERT_EQUAL_UINT16(30000, a.port);
}

void test_recv_ok(void) {
  char *argv[] = {"myapp", "recv", "-s", "abc", "-p", "20000", "h", "out", NULL};
  struct args a;
  TEST_ASSERT_EQUAL(ARGS_OK, args_parse(8, argv, &a));
  TEST_ASSERT_EQUAL(MODE_RECV, a.mode);
  TEST_ASSERT_EQUAL_UINT16(20000, a.port);
}

void test_recv_rejects_send_options(void) {
  char *argv[] = {"myapp", "recv", "-s", "abc", "-w", "4", "h", "out", NULL};
  struct args a;
  TEST_ASSERT_EQUAL(ARGS_BAD, args_parse(8, argv, &a));
}

void test_bad_mode_and_missing_pieces(void) {
  struct args a;
  char *m1[] = {"myapp", "bogus", "-s", "a", "h", "f", NULL};
  char *m2[] = {"myapp", "send", "h", "f", NULL};
  char *m3[] = {"myapp", "send", "-s", "a", "h", NULL};
  char *m4[] = {"myapp", "send", "-s", "a", "h", "f", "extra", NULL};
  char *m5[] = {"myapp", "send", "-s", "a", "-z", "h", "f", NULL};
  TEST_ASSERT_EQUAL(ARGS_BAD, args_parse(6, m1, &a));
  TEST_ASSERT_EQUAL(ARGS_BAD, args_parse(4, m2, &a));
  TEST_ASSERT_EQUAL(ARGS_BAD, args_parse(5, m3, &a));
  TEST_ASSERT_EQUAL(ARGS_BAD, args_parse(7, m4, &a));
  TEST_ASSERT_EQUAL(ARGS_BAD, args_parse(7, m5, &a));
}

void test_timeout_limits(void) {
  struct args a;
  char *ok[] = {"myapp", "send", "-s", "a", "-T", "60000", "h", "f", NULL};
  char *over[] = {"myapp", "send", "-s", "a", "-T", "60001", "h", "f", NULL};
  char *zero[] = {"myapp", "send", "-s", "a", "-T", "0", "h", "f", NULL};
  TEST_ASSERT_EQUAL(ARGS_OK, args_parse(8, ok, &a));
  TEST_ASSERT_EQUAL_UINT(60000, a.timeout_ms);
  TEST_ASSERT_EQUAL(ARGS_BAD, args_parse(8, over, &a));
  TEST_ASSERT_EQUAL(ARGS_BAD, args_parse(8, zero, &a));
}

void test_bad_values(void) {
  struct args a;
  char *w0[] = {"myapp", "send", "-s", "a", "-w", "0", "h", "f", NULL};
  char *w65[] = {"myapp", "send", "-s", "a", "-w", "65", "h", "f", NULL};
  char *wx[] = {"myapp", "send", "-s", "a", "-w", "4x", "h", "f", NULL};
  char *l2[] = {"myapp", "send", "-s", "a", "-l", "-0.1", "h", "f", NULL};
  char *p0[] = {"myapp", "send", "-s", "a", "-p", "0", "h", "f", NULL};
  char *sU[] = {"myapp", "send", "-s", "Bad_Name", "h", "f", NULL};
  TEST_ASSERT_EQUAL(ARGS_BAD, args_parse(8, w0, &a));
  TEST_ASSERT_EQUAL(ARGS_BAD, args_parse(8, w65, &a));
  TEST_ASSERT_EQUAL(ARGS_BAD, args_parse(8, wx, &a));
  TEST_ASSERT_EQUAL(ARGS_BAD, args_parse(8, l2, &a));
  TEST_ASSERT_EQUAL(ARGS_BAD, args_parse(8, p0, &a));
  TEST_ASSERT_EQUAL(ARGS_BAD, args_parse(6, sU, &a));
}

void test_usage_text_starts_correctly(void) {
  TEST_ASSERT_EQUAL_INT(0, strncmp(args_usage(), "Usage: myapp send -s <session>", 30));
}

void run_args_tests(void) {
  RUN_TEST(test_no_args_is_usage);
  RUN_TEST(test_send_defaults);
  RUN_TEST(test_send_all_options);
  RUN_TEST(test_recv_ok);
  RUN_TEST(test_recv_rejects_send_options);
  RUN_TEST(test_bad_mode_and_missing_pieces);
  RUN_TEST(test_bad_values);
  RUN_TEST(test_timeout_limits);
  RUN_TEST(test_usage_text_starts_correctly);
}
