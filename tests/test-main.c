#include "harness/unity.h"
#include "tests.h"

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();
  run_args_tests();
  run_packet_tests();
  run_receiver_tests();
  run_sender_tests();
  run_transfer_tests();
  run_sys_ops_tests();
  return UNITY_END();
}
