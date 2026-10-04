#include <string.h>
#include "harness/unity.h"
#include "../src/receiver.h"
#include "tests.h"

static const uint64_t T0 = 1000;

static struct receiver R;
static struct receiver_out O;

/* Encodes a packet and hands it to the receiver as if it arrived at `now`. */
static enum receiver_state feed(uint8_t type, uint32_t seq, const char *payload, uint64_t now) {
  struct packet p;
  memset(&p, 0, sizeof p);
  p.type = type;
  p.seq = seq;
  p.length = (uint16_t)(payload ? strlen(payload) : 0);
  if (p.length > 0) {
    memcpy(p.payload, payload, p.length);
  }
  uint8_t dg[PKT_MAX_LEN];
  size_t len = 0;
  TEST_ASSERT_EQUAL(PKT_OK, pkt_encode(&p, dg, sizeof dg, &len));
  return rdt_rcv(&R, dg, len, now, &O);
}

static uint32_t ack_seq(void) {
  struct packet p;
  TEST_ASSERT_EQUAL_UINT(PKT_HEADER_LEN, O.ack_len);
  TEST_ASSERT_EQUAL(PKT_OK, pkt_decode(O.ack, O.ack_len, &p));
  TEST_ASSERT_EQUAL_UINT8(PKT_ACK, p.type);
  return p.seq;
}

static void start(void) {
  memset(&R, 0, sizeof R);
  memset(&O, 0, sizeof O);
  TEST_ASSERT_EQUAL(RECEIVER_RUNNING, receiver_init(&R, T0, &O));
}

void test_receiver_init_starts_idle_timer(void) {
  start();
  TEST_ASSERT_TRUE(O.timer_on);
  TEST_ASSERT_EQUAL_UINT64(T0 + RECEIVER_IDLE_MS, O.timer_due);
  TEST_ASSERT_EQUAL_UINT(0, O.ack_len);
}

void test_receiver_in_order_data_is_delivered_and_acked(void) {
  start();
  TEST_ASSERT_EQUAL(RECEIVER_RUNNING, feed(PKT_DATA, 0, "abc", T0 + 10));
  TEST_ASSERT_EQUAL_UINT(3, O.payload_len);
  TEST_ASSERT_EQUAL_MEMORY("abc", O.payload, 3);
  TEST_ASSERT_EQUAL_UINT32(1, ack_seq());
  TEST_ASSERT_FALSE(O.close_file);
  TEST_ASSERT_EQUAL_UINT64(T0 + 10 + RECEIVER_IDLE_MS, O.timer_due);

  feed(PKT_DATA, 1, "de", T0 + 20);
  TEST_ASSERT_EQUAL_UINT(2, O.payload_len);
  TEST_ASSERT_EQUAL_UINT32(2, ack_seq());
}

void test_receiver_gap_is_discarded_and_ack_repeated(void) {
  start();
  feed(PKT_DATA, 0, "a", T0 + 1);
  feed(PKT_DATA, 2, "c", T0 + 2);
  TEST_ASSERT_EQUAL_UINT(0, O.payload_len);
  TEST_ASSERT_EQUAL_UINT32(1, ack_seq());
}

void test_receiver_duplicate_is_discarded_and_ack_repeated(void) {
  start();
  feed(PKT_DATA, 0, "a", T0 + 1);
  feed(PKT_DATA, 1, "b", T0 + 2);
  feed(PKT_DATA, 0, "a", T0 + 3);
  TEST_ASSERT_EQUAL_UINT(0, O.payload_len);
  TEST_ASSERT_EQUAL_UINT32(2, ack_seq());
}

void test_receiver_fin_closes_and_starts_linger(void) {
  start();
  feed(PKT_DATA, 0, "a", T0 + 1);
  TEST_ASSERT_EQUAL(RECEIVER_LINGERING, feed(PKT_FIN, 1, NULL, T0 + 5));
  TEST_ASSERT_TRUE(O.close_file);
  TEST_ASSERT_EQUAL_UINT32(2, ack_seq());
  TEST_ASSERT_TRUE(O.timer_on);
  TEST_ASSERT_EQUAL_UINT64(T0 + 5 + RECEIVER_LINGER_MS, O.timer_due);
}

void test_receiver_empty_file(void) {
  start();
  TEST_ASSERT_EQUAL(RECEIVER_LINGERING, feed(PKT_FIN, 0, NULL, T0));
  TEST_ASSERT_TRUE(O.close_file);
  TEST_ASSERT_EQUAL_UINT32(1, ack_seq());
}

void test_receiver_fin_ahead_of_gap_is_not_accepted(void) {
  start();
  TEST_ASSERT_EQUAL(RECEIVER_RUNNING, feed(PKT_FIN, 3, NULL, T0 + 1));
  TEST_ASSERT_FALSE(O.close_file);
  TEST_ASSERT_EQUAL_UINT32(0, ack_seq());
}

void test_receiver_repeated_fin_during_linger_is_reacked(void) {
  start();
  feed(PKT_FIN, 0, NULL, T0);
  TEST_ASSERT_EQUAL(RECEIVER_LINGERING, feed(PKT_FIN, 0, NULL, T0 + 1500));
  TEST_ASSERT_FALSE(O.close_file);
  TEST_ASSERT_EQUAL_UINT32(1, ack_seq());
  TEST_ASSERT_EQUAL_UINT64(T0 + 1500 + RECEIVER_LINGER_MS, O.timer_due);
}

void test_receiver_data_during_linger_is_not_written(void) {
  start();
  feed(PKT_FIN, 0, NULL, T0);
  feed(PKT_DATA, 1, "x", T0 + 10);
  TEST_ASSERT_EQUAL_UINT(0, O.payload_len);
  TEST_ASSERT_EQUAL_UINT32(1, ack_seq());
}

void test_receiver_ignores_corrupt_datagram(void) {
  start();
  struct packet p;
  memset(&p, 0, sizeof p);
  p.type = PKT_DATA;
  p.length = 1;
  p.payload[0] = 'a';
  uint8_t dg[PKT_MAX_LEN];
  size_t len = 0;
  TEST_ASSERT_EQUAL(PKT_OK, pkt_encode(&p, dg, sizeof dg, &len));
  dg[PKT_HEADER_LEN] ^= 0x01;
  TEST_ASSERT_EQUAL(RECEIVER_RUNNING, rdt_rcv(&R, dg, len, T0 + 500, &O));
  TEST_ASSERT_EQUAL_UINT(0, O.ack_len);
  TEST_ASSERT_EQUAL_UINT(0, O.payload_len);
  TEST_ASSERT_EQUAL_UINT64(T0 + RECEIVER_IDLE_MS, O.timer_due);
}

void test_receiver_ignores_ack_packets(void) {
  start();
  feed(PKT_ACK, 0, NULL, T0 + 5);
  TEST_ASSERT_EQUAL_UINT(0, O.ack_len);
  TEST_ASSERT_EQUAL_UINT(0, O.payload_len);
}

void test_receiver_timer_before_due_does_nothing(void) {
  start();
  TEST_ASSERT_EQUAL(RECEIVER_RUNNING, receiver_timeout(&R, T0 + RECEIVER_IDLE_MS - 1, &O));
  TEST_ASSERT_TRUE(O.timer_on);
}

void test_receiver_gives_up_after_idle(void) {
  start();
  TEST_ASSERT_EQUAL(RECEIVER_GAVE_UP, receiver_timeout(&R, T0 + RECEIVER_IDLE_MS, &O));
  TEST_ASSERT_FALSE(O.timer_on);
}

void test_receiver_idle_timer_restarts_on_valid_packet(void) {
  start();
  feed(PKT_DATA, 0, "a", T0 + 20000);
  TEST_ASSERT_EQUAL(RECEIVER_RUNNING, receiver_timeout(&R, T0 + RECEIVER_IDLE_MS, &O));
  TEST_ASSERT_EQUAL(RECEIVER_GAVE_UP, receiver_timeout(&R, T0 + 20000 + RECEIVER_IDLE_MS, &O));
}

void test_receiver_done_after_linger(void) {
  start();
  feed(PKT_FIN, 0, NULL, T0);
  TEST_ASSERT_EQUAL(RECEIVER_LINGERING, receiver_timeout(&R, T0 + RECEIVER_LINGER_MS - 1, &O));
  TEST_ASSERT_EQUAL(RECEIVER_DONE, receiver_timeout(&R, T0 + RECEIVER_LINGER_MS, &O));
  TEST_ASSERT_FALSE(O.timer_on);
}

void test_receiver_null_arguments(void) {
  TEST_ASSERT_EQUAL(RECEIVER_ERROR, receiver_init(NULL, T0, &O));
  TEST_ASSERT_EQUAL(RECEIVER_ERROR, rdt_rcv(NULL, NULL, 0, T0, &O));
  TEST_ASSERT_EQUAL(RECEIVER_ERROR, receiver_timeout(&R, T0, NULL));
}

void run_receiver_tests(void) {
  RUN_TEST(test_receiver_init_starts_idle_timer);
  RUN_TEST(test_receiver_in_order_data_is_delivered_and_acked);
  RUN_TEST(test_receiver_gap_is_discarded_and_ack_repeated);
  RUN_TEST(test_receiver_duplicate_is_discarded_and_ack_repeated);
  RUN_TEST(test_receiver_fin_closes_and_starts_linger);
  RUN_TEST(test_receiver_empty_file);
  RUN_TEST(test_receiver_fin_ahead_of_gap_is_not_accepted);
  RUN_TEST(test_receiver_repeated_fin_during_linger_is_reacked);
  RUN_TEST(test_receiver_data_during_linger_is_not_written);
  RUN_TEST(test_receiver_ignores_corrupt_datagram);
  RUN_TEST(test_receiver_ignores_ack_packets);
  RUN_TEST(test_receiver_timer_before_due_does_nothing);
  RUN_TEST(test_receiver_gives_up_after_idle);
  RUN_TEST(test_receiver_idle_timer_restarts_on_valid_packet);
  RUN_TEST(test_receiver_done_after_linger);
  RUN_TEST(test_receiver_null_arguments);
}
