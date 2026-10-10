#include <stdio.h>
#include <string.h>
#include "harness/unity.h"
#include "../src/receiver.h"
#include "../src/sender.h"
#include "tests.h"

/* Real sender and real receiver state machines joined by an in-memory channel. */

#define ONE_WAY_MS 50
#define MAXQ 1024
#define SRC_MAX 130000
#define T0 1000
#define TIMEOUT_MS 250

struct flight {
  uint64_t at;
  int to_receiver;
  size_t len;
  uint8_t dg[PKT_MAX_LEN];
};

static struct {
  uint64_t now;
  uint64_t rng;
  unsigned pct; /* loss, corruption and duplication probability, each way */
  struct flight q[MAXQ];
  size_t head, count;
  struct sender s;
  struct sender_out so;
  struct receiver r;
  struct receiver_out ro;
  uint8_t file[SRC_MAX];
  size_t filelen;
  int closed;
} n;

static uint8_t src[SRC_MAX];

static unsigned rnd(unsigned mod) {
  n.rng ^= n.rng << 13;
  n.rng ^= n.rng >> 7;
  n.rng ^= n.rng << 17;
  return (unsigned)(n.rng % mod);
}

/* Relay rules: drop, else flip one bit, else duplicate. Returns copies to deliver. */
static int damage(uint8_t *dg, size_t len) {
  if (rnd(100) < n.pct) {
    return 0;
  }
  if (rnd(100) < n.pct) {
    unsigned bit = rnd((unsigned)(len * 8));
    dg[bit / 8] ^= (uint8_t)(1u << (bit % 8));
    return 1;
  }
  if (rnd(100) < n.pct) {
    return 2;
  }
  return 1;
}

static void transmit(const uint8_t *dg, size_t len, int to_receiver) {
  uint8_t tmp[PKT_MAX_LEN];
  memcpy(tmp, dg, len);
  int copies = damage(tmp, len);
  for (int i = 0; i < copies && n.count < MAXQ; i++) {
    struct flight *f = &n.q[(n.head + n.count) % MAXQ];
    f->at = n.now + ONE_WAY_MS;
    f->to_receiver = to_receiver;
    f->len = len;
    memcpy(f->dg, tmp, len);
    n.count++;
  }
}

static void carry_out_sender(void) {
  for (size_t i = 0; i < n.so.nsend; i++) {
    transmit(n.so.send[i]->dg, n.so.send[i]->len, 1);
  }
}

static void carry_out_receiver(void) {
  if (n.ro.payload_len > 0 && n.filelen + n.ro.payload_len <= sizeof n.file) {
    memcpy(n.file + n.filelen, n.ro.payload, n.ro.payload_len);
    n.filelen += n.ro.payload_len;
  }
  if (n.ro.close_file) {
    n.closed = 1;
  }
  if (n.ro.ack_len > 0) {
    transmit(n.ro.ack, n.ro.ack_len, 0);
  }
}

static void run_transfer(size_t srclen, uint32_t window, unsigned pct, uint64_t seed) {
  for (size_t i = 0; i < SRC_MAX; i++) {
    src[i] = (uint8_t)(i * 31 + 7);
  }
  memset(&n, 0, sizeof n);
  n.now = T0;
  n.rng = (seed * 0x9E3779B97F4A7C15ull) | 1;
  n.pct = pct;
  TEST_ASSERT_EQUAL(SENDER_RUNNING, sender_init(&n.s, window, TIMEOUT_MS));
  TEST_ASSERT_EQUAL(RECEIVER_RUNNING, receiver_init(&n.r, n.now, &n.ro));

  size_t pos = 0;
  for (long guard = 0; guard < 2000000; guard++) {
    while (sender_has_data(&n.s)) {
      size_t left = srclen - pos;
      size_t len = left < PKT_MAX_PAYLOAD ? left : PKT_MAX_PAYLOAD;
      (void)rdt_send(&n.s, src + pos, len, n.now, &n.so);
      pos += len;
      carry_out_sender();
    }

    uint64_t next = UINT64_MAX;
    if (n.count > 0) {
      next = n.q[n.head].at;
    }
    if (n.so.timer_on && n.so.timer_due < next) {
      next = n.so.timer_due;
    }
    if (n.ro.timer_on && n.ro.timer_due < next) {
      next = n.ro.timer_due;
    }
    if (next == UINT64_MAX) {
      return;
    }
    if (next > n.now) {
      n.now = next;
    }

    if (n.count > 0 && n.q[n.head].at <= n.now) {
      struct flight f = n.q[n.head];
      n.head = (n.head + 1) % MAXQ;
      n.count--;
      if (f.to_receiver) {
        (void)rdt_rcv(&n.r, f.dg, f.len, n.now, &n.ro);
        carry_out_receiver();
      } else {
        (void)sender_rdt_rcv(&n.s, f.dg, f.len, n.now, &n.so);
        carry_out_sender();
      }
    } else {
      (void)sender_timeout(&n.s, n.now, &n.so);
      carry_out_sender();
      (void)receiver_timeout(&n.r, n.now, &n.ro);
      carry_out_receiver();
    }
  }
  TEST_FAIL_MESSAGE("transfer did not finish");
}

static void check_transfer(size_t srclen, uint32_t window, unsigned pct, uint64_t seed) {
  char msg[128];
  run_transfer(srclen, window, pct, seed);
  int ok = n.s.state == SENDER_DONE && n.r.state == RECEIVER_DONE && n.closed &&
           n.filelen == srclen && (srclen == 0 || memcmp(src, n.file, srclen) == 0);
  snprintf(msg, sizeof msg, "size %zu window %u seed %u: sender %d receiver %d delivered %zu",
           srclen, (unsigned)window, (unsigned)seed, (int)n.s.state, (int)n.r.state,
           n.filelen);
  TEST_ASSERT_TRUE_MESSAGE(ok, msg);
}

void test_transfer_clean_channel_needs_no_retransmission(void) {
  check_transfer(10000, 8, 0, 1);
  TEST_ASSERT_EQUAL_UINT32(0, n.s.stats.retransmitted);
  TEST_ASSERT_EQUAL_UINT32(0, n.s.stats.timeouts);
  TEST_ASSERT_EQUAL_UINT32(11, n.s.stats.sent); /* 10 DATA + FIN */
}

void test_transfer_20_percent_each_way_window_1(void) {
  for (uint64_t seed = 1; seed <= 8; seed++) {
    check_transfer(8000, 1, 20, seed);
  }
}

void test_transfer_20_percent_each_way_window_8(void) {
  for (uint64_t seed = 1; seed <= 8; seed++) {
    check_transfer(60000, 8, 20, seed);
  }
}

void test_transfer_20_percent_each_way_window_64(void) {
  for (uint64_t seed = 1; seed <= 8; seed++) {
    check_transfer(120000, 64, 20, seed);
  }
}

void test_transfer_20_percent_empty_file(void) {
  for (uint64_t seed = 1; seed <= 8; seed++) {
    check_transfer(0, 4, 20, seed);
  }
}

void test_transfer_20_percent_exact_multiple_of_packet_size(void) {
  for (uint64_t seed = 1; seed <= 8; seed++) {
    check_transfer(3 * PKT_MAX_PAYLOAD, 4, 20, seed);
  }
}

void test_transfer_20_percent_short_final_packet(void) {
  for (uint64_t seed = 1; seed <= 8; seed++) {
    check_transfer(2500, 16, 20, seed);
  }
}

void run_transfer_tests(void) {
  RUN_TEST(test_transfer_clean_channel_needs_no_retransmission);
  RUN_TEST(test_transfer_20_percent_each_way_window_1);
  RUN_TEST(test_transfer_20_percent_each_way_window_8);
  RUN_TEST(test_transfer_20_percent_each_way_window_64);
  RUN_TEST(test_transfer_20_percent_empty_file);
  RUN_TEST(test_transfer_20_percent_exact_multiple_of_packet_size);
  RUN_TEST(test_transfer_20_percent_short_final_packet);
}
