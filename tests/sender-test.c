#include <string.h>
#include "harness/unity.h"
#include "../src/sender.h"
#include "tests.h"

#define RTT_MS 100
#define MAXQ 256
#define SRC_MAX 150000
#define T0 1000

static struct sender S;
static struct sender_out O;
static struct window W;

/* ---- direct, single-step tests of the state machine ---- */

static const uint8_t full_chunk[PKT_MAX_PAYLOAD] = {1};

static void start(uint32_t window) {
  memset(&O, 0, sizeof O);
  TEST_ASSERT_EQUAL(SENDER_RUNNING, sender_init(&S, window, 250));
}

static void ack(uint32_t seq, uint64_t now) {
  struct packet p;
  memset(&p, 0, sizeof p);
  p.type = PKT_ACK;
  p.seq = seq;
  uint8_t dg[PKT_HEADER_LEN];
  size_t len = 0;
  TEST_ASSERT_EQUAL(PKT_OK, pkt_encode(&p, dg, sizeof dg, &len));
  (void)sender_rdt_rcv(&S, dg, len, now, &O);
}

static struct packet sent_packet(size_t i) {
  struct packet p;
  TEST_ASSERT_TRUE(i < O.nsend);
  TEST_ASSERT_EQUAL(PKT_OK, pkt_decode(O.send[i]->dg, O.send[i]->len, &p));
  return p;
}

static void send_full_chunks(size_t count, uint64_t now) {
  for (size_t i = 0; i < count; i++) {
    (void)rdt_send(&S, full_chunk, sizeof full_chunk, now, &O);
  }
}

void test_sender_window_basics(void) {
  uint8_t dg[10] = {0};
  TEST_ASSERT_EQUAL(WIN_ERR_ARG, win_init(&W, 0));
  TEST_ASSERT_EQUAL(WIN_ERR_ARG, win_init(&W, SENDER_MAX_WINDOW + 1));
  TEST_ASSERT_EQUAL(WIN_OK, win_init(&W, 4));
  TEST_ASSERT_TRUE(win_empty(&W));
  for (uint8_t i = 0; i < 4; i++) {
    dg[0] = i;
    TEST_ASSERT_EQUAL(WIN_OK, win_push(&W, dg, sizeof dg));
  }
  TEST_ASSERT_TRUE(win_full(&W));
  TEST_ASSERT_EQUAL(WIN_ERR_ARG, win_push(&W, dg, sizeof dg));

  TEST_ASSERT_EQUAL(WIN_IGNORED, win_ack(&W, 0));
  TEST_ASSERT_EQUAL(WIN_IGNORED, win_ack(&W, 5));
  TEST_ASSERT_EQUAL(WIN_OK, win_ack(&W, 2));
  TEST_ASSERT_EQUAL_UINT32(2, W.base);
  TEST_ASSERT_EQUAL(WIN_IGNORED, win_ack(&W, 2));
  TEST_ASSERT_EQUAL(WIN_IGNORED, win_ack(&W, 1));
  TEST_ASSERT_FALSE(win_full(&W));
  TEST_ASSERT_NULL(win_get(&W, 1));
  TEST_ASSERT_NOT_NULL(win_get(&W, 2));
  TEST_ASSERT_EQUAL_UINT8(3, win_get(&W, 3)->dg[0]);
  TEST_ASSERT_NULL(win_get(&W, 4));

  TEST_ASSERT_EQUAL(WIN_OK, win_ack(&W, 4));
  TEST_ASSERT_TRUE(win_empty(&W));
}

void test_sender_init_rejects_bad_config(void) {
  TEST_ASSERT_EQUAL(SENDER_ERROR, sender_init(&S, 0, 250));
  TEST_ASSERT_EQUAL(SENDER_ERROR, sender_init(&S, SENDER_MAX_WINDOW + 1, 250));
  TEST_ASSERT_EQUAL(SENDER_ERROR, sender_init(&S, 8, 0));
  TEST_ASSERT_EQUAL(SENDER_ERROR, sender_init(NULL, 8, 250));
}

void test_sender_first_chunk_is_sent_and_starts_timer(void) {
  start(4);
  TEST_ASSERT_TRUE(sender_has_data(&S));
  TEST_ASSERT_EQUAL(SENDER_RUNNING, rdt_send(&S, full_chunk, sizeof full_chunk, T0, &O));
  TEST_ASSERT_EQUAL_UINT(1, O.nsend);
  struct packet p = sent_packet(0);
  TEST_ASSERT_EQUAL_UINT8(PKT_DATA, p.type);
  TEST_ASSERT_EQUAL_UINT32(0, p.seq);
  TEST_ASSERT_EQUAL_UINT16(PKT_MAX_PAYLOAD, p.length);
  TEST_ASSERT_TRUE(O.timer_on);
  TEST_ASSERT_EQUAL_UINT64(T0 + 250, O.timer_due);
}

void test_sender_timer_is_not_restarted_by_later_sends(void) {
  start(4);
  send_full_chunks(1, T0);
  send_full_chunks(1, T0 + 100);
  TEST_ASSERT_EQUAL_UINT32(1, sent_packet(0).seq);
  TEST_ASSERT_EQUAL_UINT64(T0 + 250, O.timer_due);
}

void test_sender_full_window_stops_wanting_data(void) {
  start(2);
  send_full_chunks(2, T0);
  TEST_ASSERT_FALSE(sender_has_data(&S));
  TEST_ASSERT_EQUAL(SENDER_ERROR, rdt_send(&S, full_chunk, sizeof full_chunk, T0, &O));
}

void test_sender_ack_slides_window_and_restarts_timer(void) {
  start(4);
  send_full_chunks(4, T0);
  ack(3, T0 + 300);
  TEST_ASSERT_EQUAL_UINT32(3, S.win.base);
  TEST_ASSERT_EQUAL_UINT(0, O.nsend);
  TEST_ASSERT_TRUE(O.timer_on);
  TEST_ASSERT_EQUAL_UINT64(T0 + 300 + 250, O.timer_due);
  TEST_ASSERT_TRUE(sender_has_data(&S));

  ack(4, T0 + 310);
  TEST_ASSERT_FALSE(O.timer_on);
}

void test_sender_ignores_duplicate_out_of_range_and_bad_datagrams(void) {
  start(4);
  send_full_chunks(2, T0);
  ack(1, T0 + 100);
  uint64_t due = O.timer_due;

  ack(1, T0 + 120);
  ack(0, T0 + 130);
  ack(9, T0 + 140);
  TEST_ASSERT_EQUAL_UINT32(1, S.win.base);
  TEST_ASSERT_EQUAL_UINT64(due, O.timer_due);

  const uint8_t junk[3] = {1, 2, 3};
  (void)sender_rdt_rcv(&S, junk, sizeof junk, T0 + 150, &O);
  TEST_ASSERT_EQUAL_UINT32(1, S.win.base);

  struct packet p;
  memset(&p, 0, sizeof p);
  p.type = PKT_DATA;
  p.seq = 2;
  uint8_t dg[PKT_MAX_LEN];
  size_t len = 0;
  TEST_ASSERT_EQUAL(PKT_OK, pkt_encode(&p, dg, sizeof dg, &len));
  (void)sender_rdt_rcv(&S, dg, len, T0 + 160, &O);
  TEST_ASSERT_EQUAL_UINT32(1, S.win.base);
  TEST_ASSERT_EQUAL_UINT64(due, O.timer_due);
}

void test_sender_timer_before_due_does_nothing(void) {
  start(3);
  send_full_chunks(3, T0);
  (void)sender_timeout(&S, T0 + 249, &O);
  TEST_ASSERT_EQUAL_UINT(0, O.nsend);
  TEST_ASSERT_EQUAL_UINT32(0, S.stats.timeouts);
}

void test_sender_timeout_goes_back_to_base(void) {
  start(3);
  send_full_chunks(3, T0);
  ack(1, T0 + 100);
  (void)sender_timeout(&S, T0 + 100 + 250, &O);
  TEST_ASSERT_EQUAL_UINT(2, O.nsend);
  TEST_ASSERT_EQUAL_UINT32(1, sent_packet(0).seq);
  TEST_ASSERT_EQUAL_UINT32(2, sent_packet(1).seq);
  TEST_ASSERT_EQUAL_UINT64(T0 + 100 + 250 + 250, O.timer_due);
  TEST_ASSERT_EQUAL_UINT32(1, S.stats.timeouts);
  TEST_ASSERT_EQUAL_UINT32(2, S.stats.retransmitted);
  TEST_ASSERT_EQUAL_UINT32(5, S.stats.sent);
}

void test_sender_timeout_resends_the_whole_window(void) {
  start(4);
  send_full_chunks(4, T0);
  (void)sender_timeout(&S, T0 + 250, &O);
  TEST_ASSERT_EQUAL_UINT(4, O.nsend);
  for (uint32_t i = 0; i < 4; i++) {
    TEST_ASSERT_EQUAL_UINT32(i, sent_packet(i).seq);
  }
  TEST_ASSERT_EQUAL_UINT32(4, S.stats.retransmitted);
  TEST_ASSERT_EQUAL_UINT32(0, S.win.base);
  TEST_ASSERT_EQUAL_UINT32(4, S.win.next);
}

void test_sender_gives_up_after_ten_timeouts(void) {
  start(2);
  send_full_chunks(2, T0);
  uint64_t now = T0;
  for (int i = 1; i < SENDER_MAX_TIMEOUTS; i++) {
    now = O.timer_due;
    TEST_ASSERT_EQUAL(SENDER_RUNNING, sender_timeout(&S, now, &O));
  }
  TEST_ASSERT_EQUAL(SENDER_GAVE_UP, sender_timeout(&S, O.timer_due, &O));
  TEST_ASSERT_EQUAL_UINT32(SENDER_MAX_TIMEOUTS, S.stats.timeouts);
  TEST_ASSERT_FALSE(O.timer_on);
}

void test_sender_progress_resets_timeout_count(void) {
  start(2);
  send_full_chunks(2, T0);
  for (int i = 0; i < 9; i++) {
    (void)sender_timeout(&S, O.timer_due, &O);
  }
  ack(1, O.timer_due);
  for (int i = 0; i < 9; i++) {
    TEST_ASSERT_EQUAL(SENDER_RUNNING, sender_timeout(&S, O.timer_due, &O));
  }
}

void test_sender_fin_follows_last_ack(void) {
  start(4);
  const uint8_t tail[100] = {7};
  (void)rdt_send(&S, tail, sizeof tail, T0, &O);
  TEST_ASSERT_EQUAL_UINT16(100, sent_packet(0).length);
  TEST_ASSERT_FALSE(sender_has_data(&S));

  ack(1, T0 + 100);
  TEST_ASSERT_EQUAL_UINT(1, O.nsend);
  struct packet fin = sent_packet(0);
  TEST_ASSERT_EQUAL_UINT8(PKT_FIN, fin.type);
  TEST_ASSERT_EQUAL_UINT32(1, fin.seq);
  TEST_ASSERT_TRUE(O.timer_on);

  TEST_ASSERT_EQUAL(SENDER_RUNNING, sender_timeout(&S, O.timer_due, &O));
  TEST_ASSERT_EQUAL_UINT8(PKT_FIN, sent_packet(0).type);

  ack(2, T0 + 700);
  TEST_ASSERT_EQUAL(SENDER_DONE, S.state);
  TEST_ASSERT_FALSE(O.timer_on);
}

void test_sender_empty_file_sends_only_fin(void) {
  start(4);
  TEST_ASSERT_EQUAL(SENDER_RUNNING, rdt_send(&S, NULL, 0, T0, &O));
  TEST_ASSERT_EQUAL_UINT(1, O.nsend);
  struct packet fin = sent_packet(0);
  TEST_ASSERT_EQUAL_UINT8(PKT_FIN, fin.type);
  TEST_ASSERT_EQUAL_UINT32(0, fin.seq);
  ack(1, T0 + 100);
  TEST_ASSERT_EQUAL(SENDER_DONE, S.state);
}

void test_sender_exact_multiple_waits_for_acks_before_fin(void) {
  start(4);
  send_full_chunks(1, T0);
  (void)rdt_send(&S, NULL, 0, T0 + 1, &O);
  TEST_ASSERT_EQUAL_UINT(0, O.nsend);
  ack(1, T0 + 100);
  TEST_ASSERT_EQUAL_UINT(1, O.nsend);
  struct packet fin = sent_packet(0);
  TEST_ASSERT_EQUAL_UINT8(PKT_FIN, fin.type);
  TEST_ASSERT_EQUAL_UINT32(1, fin.seq);
  ack(2, T0 + 200);
  TEST_ASSERT_EQUAL(SENDER_DONE, S.state);
}

void test_sender_rejects_misuse(void) {
  start(4);
  TEST_ASSERT_EQUAL(SENDER_ERROR, rdt_send(&S, full_chunk, PKT_MAX_PAYLOAD + 1, T0, &O));
  start(4);
  TEST_ASSERT_EQUAL(SENDER_ERROR, rdt_send(&S, NULL, 5, T0, &O));
  TEST_ASSERT_EQUAL(SENDER_ERROR, rdt_send(NULL, full_chunk, 1, T0, &O));
  TEST_ASSERT_EQUAL(SENDER_ERROR, sender_timeout(&S, T0, NULL));
}

/* ---- whole transfers over a simulated relay, driven like the real loop ---- */

struct arrival {
  uint64_t at;
  size_t len;
  uint8_t dg[PKT_MAX_LEN];
};

/* Channel plus a minimal Go-Back-N receiver model standing in for the peer. */
static struct {
  uint64_t now;
  uint64_t rng;
  unsigned loss, corrupt, dup; /* percent */
  long drop_seq_once;          /* drop the first transmission of this DATA seq */
  struct arrival q[MAXQ];
  size_t nq;
  uint32_t expected;
  uint8_t file[SRC_MAX];
  size_t filelen;
  int closed;
} g;

static uint8_t src[SRC_MAX];

static unsigned rnd(unsigned mod) {
  g.rng ^= g.rng << 13;
  g.rng ^= g.rng >> 7;
  g.rng ^= g.rng << 17;
  return (unsigned)(g.rng % mod);
}

/* Relay rules: drop, else corrupt, else duplicate. Returns copies to deliver. */
static int damage(uint8_t *dg, size_t len) {
  if (rnd(100) < g.loss) {
    return 0;
  }
  if (rnd(100) < g.corrupt) {
    unsigned bit = rnd((unsigned)(len * 8));
    dg[bit / 8] ^= (uint8_t)(1u << (bit % 8));
    return 1;
  }
  if (rnd(100) < g.dup) {
    return 2;
  }
  return 1;
}

static void enqueue(const uint8_t *dg, size_t len) {
  if (g.nq < MAXQ) {
    g.q[g.nq].at = g.now + RTT_MS;
    g.q[g.nq].len = len;
    memcpy(g.q[g.nq].dg, dg, len);
    g.nq++;
  }
}

/* Receiver model: accept in order only, always answer ACK expected. */
static void model_receive(const struct packet *p) {
  if (p->type == PKT_DATA && p->seq == g.expected && !g.closed &&
      g.filelen + p->length <= sizeof g.file) {
    memcpy(g.file + g.filelen, p->payload, p->length);
    g.filelen += p->length;
    g.expected++;
  } else if (p->type == PKT_FIN && p->seq == g.expected && !g.closed) {
    g.closed = 1;
    g.expected++;
  }

  struct packet ack;
  memset(&ack, 0, sizeof ack);
  ack.type = PKT_ACK;
  ack.seq = g.expected;
  uint8_t tmp[PKT_HEADER_LEN];
  size_t len = 0;
  (void)pkt_encode(&ack, tmp, sizeof tmp, &len);
  int copies = damage(tmp, len);
  for (int i = 0; i < copies; i++) {
    enqueue(tmp, len);
  }
}

static void react_to_datagram(const struct sender_out *o) {
  for (size_t i = 0; i < o->nsend; i++) {
    uint8_t tmp[PKT_MAX_LEN];
    size_t len = o->send[i]->len;
    struct packet p;
    memcpy(tmp, o->send[i]->dg, len);
    if (pkt_decode(tmp, len, &p) == PKT_OK && p.type == PKT_DATA &&
        (long)p.seq == g.drop_seq_once) {
      g.drop_seq_once = -1;
      continue;
    }
    int copies = damage(tmp, len);
    for (int c = 0; c < copies; c++) {
      if (pkt_decode(tmp, len, &p) == PKT_OK) {
        model_receive(&p);
      }
    }
  }
}

static enum sender_state transfer(size_t srclen, uint32_t window, unsigned loss,
                                  unsigned corrupt, unsigned dup, uint64_t seed,
                                  long drop_seq, struct sender_stats *stats) {
  for (size_t i = 0; i < SRC_MAX; i++) {
    src[i] = (uint8_t)(i * 31 + 7);
  }
  memset(&g, 0, sizeof g);
  g.now = T0;
  g.rng = seed | 1;
  g.loss = loss;
  g.corrupt = corrupt;
  g.dup = dup;
  g.drop_seq_once = drop_seq;

  start(window);
  size_t pos = 0;
  for (int guard = 0; guard < 5000000 && S.state == SENDER_RUNNING; guard++) {
    while (sender_has_data(&S)) {
      size_t left = srclen - pos;
      size_t n = left < PKT_MAX_PAYLOAD ? left : PKT_MAX_PAYLOAD;
      (void)rdt_send(&S, src + pos, n, g.now, &O);
      pos += n;
      react_to_datagram(&O);
    }
    if (S.state != SENDER_RUNNING) {
      break;
    }
    TEST_ASSERT_TRUE(O.timer_on);

    if (g.nq > 0 && g.q[0].at <= O.timer_due) {
      uint8_t dg[PKT_MAX_LEN];
      size_t len = g.q[0].len;
      if (g.q[0].at > g.now) {
        g.now = g.q[0].at;
      }
      memcpy(dg, g.q[0].dg, len);
      memmove(&g.q[0], &g.q[1], (g.nq - 1) * sizeof g.q[0]);
      g.nq--;
      (void)sender_rdt_rcv(&S, dg, len, g.now, &O);
    } else {
      if (O.timer_due > g.now) {
        g.now = O.timer_due;
      }
      (void)sender_timeout(&S, g.now, &O);
    }
    react_to_datagram(&O);
  }
  if (stats != NULL) {
    *stats = S.stats;
  }
  return S.state;
}

static void assert_file_intact(size_t srclen) {
  TEST_ASSERT_TRUE(g.closed);
  TEST_ASSERT_EQUAL_UINT(srclen, g.filelen);
  if (srclen > 0) {
    TEST_ASSERT_EQUAL_MEMORY(src, g.file, srclen);
  }
}

void test_sender_transfer_clean_stop_and_wait(void) {
  struct sender_stats st;
  TEST_ASSERT_EQUAL(SENDER_DONE, transfer(2500, 1, 0, 0, 0, 1, -1, &st));
  assert_file_intact(2500);
  TEST_ASSERT_EQUAL_UINT32(4, st.sent);
  TEST_ASSERT_EQUAL_UINT32(0, st.retransmitted);
  TEST_ASSERT_EQUAL_UINT32(0, st.timeouts);
}

void test_sender_transfer_clean_window_8(void) {
  struct sender_stats st;
  TEST_ASSERT_EQUAL(SENDER_DONE, transfer(2500, 8, 0, 0, 0, 1, -1, &st));
  assert_file_intact(2500);
  TEST_ASSERT_EQUAL_UINT32(4, st.sent);
  TEST_ASSERT_EQUAL_UINT32(0, st.retransmitted);
}

void test_sender_transfer_empty_file(void) {
  struct sender_stats st;
  TEST_ASSERT_EQUAL(SENDER_DONE, transfer(0, 4, 0, 0, 0, 1, -1, &st));
  assert_file_intact(0);
  TEST_ASSERT_EQUAL_UINT32(1, st.sent);
}

void test_sender_transfer_exact_multiple_of_packet_size(void) {
  struct sender_stats st;
  TEST_ASSERT_EQUAL(SENDER_DONE, transfer(2048, 4, 0, 0, 0, 1, -1, &st));
  assert_file_intact(2048);
  TEST_ASSERT_EQUAL_UINT32(3, st.sent); /* 2 DATA + FIN */
}

void test_sender_transfer_lost_data_goes_back_to_base(void) {
  struct sender_stats st;
  TEST_ASSERT_EQUAL(SENDER_DONE, transfer(6 * 1024, 4, 0, 0, 0, 1, 2, &st));
  assert_file_intact(6 * 1024);
  TEST_ASSERT_EQUAL_UINT32(1, st.timeouts);
  TEST_ASSERT_EQUAL_UINT32(4, st.retransmitted); /* 2, 3, 4, 5 */
}

void test_sender_transfer_gives_up_when_nothing_gets_through(void) {
  struct sender_stats st;
  TEST_ASSERT_EQUAL(SENDER_GAVE_UP, transfer(3000, 4, 100, 0, 0, 1, -1, &st));
  TEST_ASSERT_EQUAL_UINT32(SENDER_MAX_TIMEOUTS, st.timeouts);
  TEST_ASSERT_FALSE(g.closed);
}

void test_sender_transfer_lossy_window_16(void) {
  struct sender_stats st;
  TEST_ASSERT_EQUAL(SENDER_DONE, transfer(100000, 16, 10, 5, 5, 7, -1, &st));
  assert_file_intact(100000);
  TEST_ASSERT_TRUE(st.retransmitted > 0);
}

void test_sender_transfer_lossy_window_1(void) {
  TEST_ASSERT_EQUAL(SENDER_DONE, transfer(20000, 1, 10, 5, 5, 99, -1, NULL));
  assert_file_intact(20000);
}

void test_sender_transfer_lossy_window_64(void) {
  TEST_ASSERT_EQUAL(SENDER_DONE, transfer(150000, 64, 20, 10, 10, 12345, -1, NULL));
  assert_file_intact(150000);
}

void run_sender_tests(void) {
  RUN_TEST(test_sender_window_basics);
  RUN_TEST(test_sender_init_rejects_bad_config);
  RUN_TEST(test_sender_first_chunk_is_sent_and_starts_timer);
  RUN_TEST(test_sender_timer_is_not_restarted_by_later_sends);
  RUN_TEST(test_sender_full_window_stops_wanting_data);
  RUN_TEST(test_sender_ack_slides_window_and_restarts_timer);
  RUN_TEST(test_sender_ignores_duplicate_out_of_range_and_bad_datagrams);
  RUN_TEST(test_sender_timer_before_due_does_nothing);
  RUN_TEST(test_sender_timeout_goes_back_to_base);
  RUN_TEST(test_sender_timeout_resends_the_whole_window);
  RUN_TEST(test_sender_gives_up_after_ten_timeouts);
  RUN_TEST(test_sender_progress_resets_timeout_count);
  RUN_TEST(test_sender_fin_follows_last_ack);
  RUN_TEST(test_sender_empty_file_sends_only_fin);
  RUN_TEST(test_sender_exact_multiple_waits_for_acks_before_fin);
  RUN_TEST(test_sender_rejects_misuse);
  RUN_TEST(test_sender_transfer_clean_stop_and_wait);
  RUN_TEST(test_sender_transfer_clean_window_8);
  RUN_TEST(test_sender_transfer_empty_file);
  RUN_TEST(test_sender_transfer_exact_multiple_of_packet_size);
  RUN_TEST(test_sender_transfer_lost_data_goes_back_to_base);
  RUN_TEST(test_sender_transfer_gives_up_when_nothing_gets_through);
  RUN_TEST(test_sender_transfer_lossy_window_16);
  RUN_TEST(test_sender_transfer_lossy_window_1);
  RUN_TEST(test_sender_transfer_lossy_window_64);
}
