#include <limits.h>
#include <string.h>
#include "harness/unity.h"
#include "../src/sys_ops.h"
#include "tests.h"

/* Only the layer 3 helpers that make no system calls; the rest needs a kernel. */

void test_sys_ctx_init_marks_everything_closed(void) {
  struct sys_ctx c;
  c.fd = 7;
  c.fp = (FILE *)&c;
  TEST_ASSERT_EQUAL(SYS_OK, sys_ctx_init(&c));
  TEST_ASSERT_EQUAL_INT(-1, c.fd);
  TEST_ASSERT_NULL(c.fp);
}

void test_sys_ctx_init_rejects_null(void) {
  TEST_ASSERT_EQUAL(SYS_ERR, sys_ctx_init(NULL));
}

void test_sys_timeout_until_counts_down(void) {
  TEST_ASSERT_EQUAL_INT(250, sys_timeout_until(1250, 1000));
  TEST_ASSERT_EQUAL_INT(1, sys_timeout_until(1001, 1000));
}

void test_sys_timeout_until_is_zero_when_due_or_past(void) {
  TEST_ASSERT_EQUAL_INT(0, sys_timeout_until(1000, 1000));
  TEST_ASSERT_EQUAL_INT(0, sys_timeout_until(999, 1000));
  TEST_ASSERT_EQUAL_INT(0, sys_timeout_until(0, UINT64_MAX));
}

void test_sys_timeout_until_clamps_to_int_max(void) {
  TEST_ASSERT_EQUAL_INT(INT_MAX, sys_timeout_until((uint64_t)INT_MAX + 100, 0));
  TEST_ASSERT_EQUAL_INT(INT_MAX, sys_timeout_until(UINT64_MAX, 0));
  TEST_ASSERT_EQUAL_INT(INT_MAX, sys_timeout_until((uint64_t)INT_MAX, 0));
}

void test_sys_describe_has_a_message_for_every_status(void) {
  const enum sys_status all[] = {SYS_OK, SYS_TIMEOUT, SYS_REFUSED, SYS_GAVE_UP, SYS_ERR};
  for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) {
    const char *m = sys_describe(all[i]);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_TRUE(strlen(m) > 0);
    for (size_t j = 0; j < i; j++) {
      TEST_ASSERT_TRUE(strcmp(m, sys_describe(all[j])) != 0);
    }
  }
}

void test_sys_describe_unknown_status_is_not_null(void) {
  TEST_ASSERT_NOT_NULL(sys_describe((enum sys_status)99));
}

void test_sys_exit_code_maps_success_and_failures(void) {
  TEST_ASSERT_EQUAL_INT(EXIT_OK, sys_exit_code(SYS_OK));
  TEST_ASSERT_EQUAL_INT(EXIT_FAIL, sys_exit_code(SYS_TIMEOUT));
  TEST_ASSERT_EQUAL_INT(EXIT_FAIL, sys_exit_code(SYS_REFUSED));
  TEST_ASSERT_EQUAL_INT(EXIT_FAIL, sys_exit_code(SYS_GAVE_UP));
  TEST_ASSERT_EQUAL_INT(EXIT_FAIL, sys_exit_code(SYS_ERR));
}

void test_exit_codes_match_the_assignment(void) {
  TEST_ASSERT_EQUAL_INT(0, EXIT_OK);
  TEST_ASSERT_EQUAL_INT(1, EXIT_USAGE);
  TEST_ASSERT_EQUAL_INT(2, EXIT_FAIL);
}

void run_sys_ops_tests(void) {
  RUN_TEST(test_sys_ctx_init_marks_everything_closed);
  RUN_TEST(test_sys_ctx_init_rejects_null);
  RUN_TEST(test_sys_timeout_until_counts_down);
  RUN_TEST(test_sys_timeout_until_is_zero_when_due_or_past);
  RUN_TEST(test_sys_timeout_until_clamps_to_int_max);
  RUN_TEST(test_sys_describe_has_a_message_for_every_status);
  RUN_TEST(test_sys_describe_unknown_status_is_not_null);
  RUN_TEST(test_sys_exit_code_maps_success_and_failures);
  RUN_TEST(test_exit_codes_match_the_assignment);
}
