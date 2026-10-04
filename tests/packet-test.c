#include <string.h>
#include "harness/unity.h"
#include "../src/packet.h"
#include "tests.h"

static void fix_checksum(uint8_t *buf, size_t n) {
  buf[2] = 0;
  buf[3] = 0;
  uint16_t c = checksum_compute(buf, n);
  buf[2] = (uint8_t)(c >> 8);
  buf[3] = (uint8_t)(c & 0xff);
}

void test_rfc1071_example(void) {
  const uint8_t d[] = {0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6, 0xf7};
  TEST_ASSERT_EQUAL_HEX16(0x220d, checksum_compute(d, sizeof d));
}

void test_encode_hi_packet(void) {
  struct packet p;
  memset(&p, 0, sizeof p);
  p.type = PKT_DATA;
  p.seq = 2;
  p.length = 3;
  memcpy(p.payload, "Hi!", 3);
  uint8_t buf[PKT_MAX_LEN];
  size_t n = 0;
  TEST_ASSERT_EQUAL(PKT_OK, pkt_encode(&p, buf, sizeof buf, &n));
  const uint8_t want[] = {0x00, 0x00, 0x96, 0x91, 0x00, 0x00, 0x00,
                          0x02, 0x00, 0x03, 0x48, 0x69, 0x21};
  TEST_ASSERT_EQUAL_UINT(sizeof want, n);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, buf, sizeof want);
}

void test_encode_ack3(void) {
  struct packet p;
  memset(&p, 0, sizeof p);
  p.type = PKT_ACK;
  p.seq = 3;
  uint8_t buf[PKT_MAX_LEN];
  size_t n = 0;
  TEST_ASSERT_EQUAL(PKT_OK, pkt_encode(&p, buf, sizeof buf, &n));
  const uint8_t want[] = {0x01, 0x00, 0xfe, 0xfc, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00};
  TEST_ASSERT_EQUAL_UINT(sizeof want, n);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, buf, sizeof want);
}

void test_round_trip_max_payload(void) {
  struct packet p, q;
  memset(&p, 0, sizeof p);
  p.type = PKT_DATA;
  p.seq = 0x01020304;
  p.length = PKT_MAX_PAYLOAD;
  for (size_t i = 0; i < PKT_MAX_PAYLOAD; i++) {
    p.payload[i] = (uint8_t)(i * 7);
  }
  uint8_t buf[PKT_MAX_LEN];
  size_t n = 0;
  TEST_ASSERT_EQUAL(PKT_OK, pkt_encode(&p, buf, sizeof buf, &n));
  TEST_ASSERT_EQUAL_UINT(PKT_MAX_LEN, n);
  TEST_ASSERT_EQUAL(PKT_OK, pkt_decode(buf, n, &q));
  TEST_ASSERT_EQUAL_UINT8(PKT_DATA, q.type);
  TEST_ASSERT_EQUAL_UINT32(0x01020304, q.seq);
  TEST_ASSERT_EQUAL_UINT16(PKT_MAX_PAYLOAD, q.length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(p.payload, q.payload, PKT_MAX_PAYLOAD);
}

void test_encode_rejects_bad_input(void) {
  struct packet p;
  memset(&p, 0, sizeof p);
  uint8_t buf[PKT_MAX_LEN];
  size_t n = 0;
  p.type = 3;
  TEST_ASSERT_EQUAL(PKT_ERR_ARG, pkt_encode(&p, buf, sizeof buf, &n));
  p.type = PKT_DATA;
  p.length = PKT_MAX_PAYLOAD + 1;
  TEST_ASSERT_EQUAL(PKT_ERR_ARG, pkt_encode(&p, buf, sizeof buf, &n));
  p.length = 4;
  TEST_ASSERT_EQUAL(PKT_ERR_ARG, pkt_encode(&p, buf, 12, &n));
}

void test_decode_rejects_invalid(void) {
  struct packet p, q;
  memset(&p, 0, sizeof p);
  p.type = PKT_DATA;
  p.seq = 1;
  p.length = 2;
  p.payload[0] = 'a';
  p.payload[1] = 'b';
  uint8_t good[PKT_MAX_LEN];
  uint8_t bad[PKT_MAX_LEN + 8];
  size_t n = 0;
  TEST_ASSERT_EQUAL(PKT_OK, pkt_encode(&p, good, sizeof good, &n));
  TEST_ASSERT_EQUAL(PKT_OK, pkt_decode(good, n, &q));

  TEST_ASSERT_EQUAL(PKT_DROP, pkt_decode(good, PKT_HEADER_LEN - 1, &q));
  TEST_ASSERT_EQUAL(PKT_DROP, pkt_decode(good, n - 1, &q));
  TEST_ASSERT_EQUAL(PKT_DROP, pkt_decode(good, n + 1, &q));

  memcpy(bad, good, n);
  bad[0] = 3;
  fix_checksum(bad, n);
  TEST_ASSERT_EQUAL(PKT_DROP, pkt_decode(bad, n, &q));

  memcpy(bad, good, n);
  bad[1] = 1;
  fix_checksum(bad, n);
  TEST_ASSERT_EQUAL(PKT_DROP, pkt_decode(bad, n, &q));

  memset(bad, 0, sizeof bad);
  bad[8] = 0x04;
  bad[9] = 0x01; /* length 1025 */
  fix_checksum(bad, PKT_HEADER_LEN + 1025);
  TEST_ASSERT_EQUAL(PKT_DROP, pkt_decode(bad, PKT_HEADER_LEN + 1025, &q));

  for (size_t bit = 0; bit < n * 8; bit++) {
    memcpy(bad, good, n);
    bad[bit / 8] ^= (uint8_t)(1u << (bit % 8));
    TEST_ASSERT_EQUAL(PKT_DROP, pkt_decode(bad, n, &q));
  }
}

void test_hello_format(void) {
  char buf[HELLO_MAX];
  size_t n = 0;
  TEST_ASSERT_EQUAL(PKT_OK, hello_format(buf, sizeof buf, "jdoe-1", 0, 0, 0, 0, &n));
  TEST_ASSERT_EQUAL_STRING("HELLO jdoe-1 recv", buf);
  TEST_ASSERT_EQUAL_UINT(strlen(buf), n);
  TEST_ASSERT_EQUAL(PKT_OK, hello_format(buf, sizeof buf, "jdoe-1", 1, 0.1, 0.05, 0, &n));
  TEST_ASSERT_EQUAL_STRING("HELLO jdoe-1 send 0.100000 0.050000 0.000000", buf);
}

void test_hello_format_rejects_bad_input(void) {
  char small[8];
  char buf[HELLO_MAX];
  size_t n = 0;
  TEST_ASSERT_EQUAL(PKT_ERR_ARG, hello_format(small, sizeof small, "jdoe-1", 0, 0, 0, 0, &n));
  TEST_ASSERT_EQUAL(PKT_ERR_ARG, hello_format(NULL, 8, "a", 0, 0, 0, 0, &n));
  TEST_ASSERT_EQUAL(PKT_ERR_ARG, hello_format(buf, sizeof buf, NULL, 0, 0, 0, 0, &n));
}

void test_hello_parse_reply(void) {
  char reason[HELLO_REASON_MAX];
  TEST_ASSERT_EQUAL(HELLO_OK, hello_parse_reply((const uint8_t *)"OK", 2, reason, sizeof reason));
  TEST_ASSERT_EQUAL(HELLO_REFUSED,
                    hello_parse_reply((const uint8_t *)"ERR no receiver", 15, reason, sizeof reason));
  TEST_ASSERT_EQUAL_STRING("no receiver", reason);
  TEST_ASSERT_EQUAL(HELLO_OTHER, hello_parse_reply((const uint8_t *)"OKAY", 4, reason, sizeof reason));
  TEST_ASSERT_EQUAL(HELLO_OTHER, hello_parse_reply(NULL, 0, reason, sizeof reason));
}

void test_hello_reason_truncated_and_sanitised(void) {
  char reason[8];
  TEST_ASSERT_EQUAL(HELLO_REFUSED,
                    hello_parse_reply((const uint8_t *)"ERR a very long reason", 22, reason, sizeof reason));
  TEST_ASSERT_EQUAL_STRING("a very ", reason);
  const uint8_t odd[] = {'E', 'R', 'R', ' ', 'a', 0x01, 'b'};
  TEST_ASSERT_EQUAL(HELLO_REFUSED, hello_parse_reply(odd, sizeof odd, reason, sizeof reason));
  TEST_ASSERT_EQUAL_STRING("a?b", reason);
}

void test_decode_odd_length_packet(void) {
  const uint8_t wire[] = {0x00, 0x00, 0x96, 0x91, 0x00, 0x00, 0x00,
                          0x02, 0x00, 0x03, 0x48, 0x69, 0x21};
  struct packet p;
  TEST_ASSERT_EQUAL(PKT_OK, pkt_decode(wire, sizeof wire, &p));
  TEST_ASSERT_EQUAL_UINT8(PKT_DATA, p.type);
  TEST_ASSERT_EQUAL_UINT32(2, p.seq);
  TEST_ASSERT_EQUAL_UINT16(3, p.length);
  TEST_ASSERT_EQUAL_MEMORY("Hi!", p.payload, 3);
}

void run_packet_tests(void) {
  RUN_TEST(test_decode_odd_length_packet);
  RUN_TEST(test_hello_format);
  RUN_TEST(test_hello_format_rejects_bad_input);
  RUN_TEST(test_hello_parse_reply);
  RUN_TEST(test_hello_reason_truncated_and_sanitised);
  RUN_TEST(test_rfc1071_example);
  RUN_TEST(test_encode_hi_packet);
  RUN_TEST(test_encode_ack3);
  RUN_TEST(test_round_trip_max_payload);
  RUN_TEST(test_encode_rejects_bad_input);
  RUN_TEST(test_decode_rejects_invalid);
}
