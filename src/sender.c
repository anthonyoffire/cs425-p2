/* ======================================================================
 * LAYER 2: GO-BACK-N STATE MACHINES (sender)
 * No I/O: time is passed in, and each call returns what to do next.
 * ====================================================================== */

#include "sender.h"

#include <string.h>

#ifndef TEST
#include "sys_ops.h"
#endif

enum win_status win_init(struct window *w, uint32_t size) {
  if (w == NULL || size < 1 || size > SENDER_MAX_WINDOW) {
    return WIN_ERR_ARG;
  }
  w->base = 0;
  w->next = 0;
  w->size = size;
  return WIN_OK;
}

int win_full(const struct window *w) { return w->next - w->base >= w->size; }

int win_empty(const struct window *w) { return w->base == w->next; }

enum win_status win_push(struct window *w, const uint8_t *dg, size_t len) {
  if (w == NULL || dg == NULL || len > PKT_MAX_LEN || win_full(w)) {
    return WIN_ERR_ARG;
  }
  struct slot *s = &w->ring[w->next % SENDER_MAX_WINDOW];
  memcpy(s->dg, dg, len);
  s->len = len;
  w->next++;
  return WIN_OK;
}

enum win_status win_ack(struct window *w, uint32_t seq) {
  if (w == NULL || seq <= w->base || seq > w->next) {
    return WIN_IGNORED;
  }
  w->base = seq;
  return WIN_OK;
}

const struct slot *win_get(const struct window *w, uint32_t seq) {
  if (w == NULL || seq < w->base || seq >= w->next) {
    return NULL;
  }
  return &w->ring[seq % SENDER_MAX_WINDOW];
}

enum sender_state sender_init(struct sender *s, uint32_t window, uint32_t timeout_ms) {
  if (s == NULL) {
    return SENDER_ERROR;
  }
  memset(s, 0, sizeof *s);
  if (timeout_ms == 0 || win_init(&s->win, window) != WIN_OK) {
    s->state = SENDER_ERROR;
    return s->state;
  }
  s->timeout_ms = timeout_ms;
  s->state = SENDER_RUNNING;
  return s->state;
}

int sender_has_data(const struct sender *s) {
  return s != NULL && s->state == SENDER_RUNNING && !s->eof && !win_full(&s->win);
}

static int out_begin(struct sender_out *out) {
  out->nsend = 0;
  out->timer_on = 0;
  out->timer_due = 0;
  return 0;
}

static enum sender_state finish(struct sender *s, struct sender_out *out) {
  out->timer_on = s->timer_on;
  out->timer_due = s->deadline;
  return s->state;
}

static int queue_send(struct sender *s, struct sender_out *out, uint32_t seq, int resend) {
  const struct slot *slot = win_get(&s->win, seq);
  if (slot == NULL) {
    s->state = SENDER_ERROR;
    return -1;
  }
  out->send[out->nsend++] = slot;
  s->stats.sent++;
  if (resend) {
    s->stats.retransmitted++;
  }
  return 0;
}

static int push_packet(struct sender *s, const struct packet *p, uint64_t now,
                       struct sender_out *out) {
  uint8_t dg[PKT_MAX_LEN];
  size_t len = 0;
  if (pkt_encode(p, dg, sizeof dg, &len) != PKT_OK ||
      win_push(&s->win, dg, len) != WIN_OK) {
    s->state = SENDER_ERROR;
    return -1;
  }
  if (queue_send(s, out, p->seq, 0) != 0) {
    return -1;
  }
  if (!s->timer_on) {
    s->timer_on = 1;
    s->deadline = now + s->timeout_ms;
  }
  return 0;
}

/* The FIN goes out once every DATA packet has been acknowledged. */
static int maybe_send_fin(struct sender *s, uint64_t now, struct sender_out *out) {
  if (!s->eof || s->win.next != s->total || s->win.base != s->total) {
    return 0;
  }
  struct packet fin;
  memset(&fin, 0, sizeof fin);
  fin.type = PKT_FIN;
  fin.seq = s->total;
  return push_packet(s, &fin, now, out);
}

enum sender_state rdt_send(struct sender *s, const uint8_t *chunk, size_t n,
                                 uint64_t now, struct sender_out *out) {
  if (s == NULL || out == NULL) {
    return SENDER_ERROR;
  }
  out_begin(out);
  if (s->state != SENDER_RUNNING) {
    return finish(s, out);
  }
  if (s->eof || win_full(&s->win) || n > PKT_MAX_PAYLOAD || (n > 0 && chunk == NULL)) {
    s->state = SENDER_ERROR;
    return finish(s, out);
  }

  if (n == 0) {
    s->eof = 1;
    s->total = s->win.next;
    (void)maybe_send_fin(s, now, out);
    return finish(s, out);
  }

  struct packet p;
  memset(&p, 0, sizeof p);
  p.type = PKT_DATA;
  p.seq = s->win.next;
  p.length = (uint16_t)n;
  memcpy(p.payload, chunk, n);
  if (push_packet(s, &p, now, out) == 0 && n < PKT_MAX_PAYLOAD) {
    s->eof = 1;
    s->total = s->win.next;
  }
  return finish(s, out);
}

enum sender_state sender_rdt_rcv(struct sender *s, const uint8_t *buf, size_t len,
                                     uint64_t now, struct sender_out *out) {
  if (s == NULL || out == NULL) {
    return SENDER_ERROR;
  }
  out_begin(out);
  if (s->state != SENDER_RUNNING) {
    return finish(s, out);
  }

  struct packet p;
  if (pkt_decode(buf, len, &p) != PKT_OK || p.type != PKT_ACK ||
      win_ack(&s->win, p.seq) != WIN_OK) {
    return finish(s, out);
  }

  s->timeouts = 0;
  if (win_empty(&s->win)) {
    s->timer_on = 0;
  } else {
    s->deadline = now + s->timeout_ms;
  }
  (void)maybe_send_fin(s, now, out);
  if (s->state == SENDER_RUNNING && s->eof && s->win.base == s->total + 1) {
    s->state = SENDER_DONE;
    s->timer_on = 0;
  }
  return finish(s, out);
}

enum sender_state sender_timeout(struct sender *s, uint64_t now, struct sender_out *out) {
  if (s == NULL || out == NULL) {
    return SENDER_ERROR;
  }
  out_begin(out);
  if (s->state != SENDER_RUNNING || !s->timer_on || now < s->deadline) {
    return finish(s, out);
  }

  s->stats.timeouts++;
  s->timeouts++;
  if (s->timeouts >= SENDER_MAX_TIMEOUTS) {
    s->state = SENDER_GAVE_UP;
    s->timer_on = 0;
    return finish(s, out);
  }
  for (uint32_t seq = s->win.base; seq < s->win.next; seq++) {
    if (queue_send(s, out, seq, 1) != 0) {
      break;
    }
  }
  s->deadline = now + s->timeout_ms;
  return finish(s, out);
}

#ifndef TEST

/* ======================================================================
 * LAYER 3: SENDER I/O
 * ====================================================================== */

static enum sys_status send_out(const struct sys_ctx *c, const struct sender_out *out) {
  for (size_t i = 0; i < out->nsend; i++) {
    enum sys_status st = send_datagram(c, out->send[i]->dg, out->send[i]->len);
    if (st != SYS_OK) {
      return st;
    }
  }
  return SYS_OK;
}

enum sys_status run_send_loop(const struct sys_ctx *c, struct sender *s) {
  struct sender_out out;
  uint8_t buf[PKT_MAX_LEN];
  uint8_t chunk[PKT_MAX_PAYLOAD];
  memset(&out, 0, sizeof out);

  for (;;) {
    uint64_t now = 0;
    enum sys_status st = get_current_time_ms(&now);
    if (st != SYS_OK) {
      return st;
    }

    while (sender_has_data(s)) {
      size_t n = fread(chunk, 1, sizeof chunk, c->fp);
      if (n < sizeof chunk && ferror(c->fp)) {
        return SYS_ERR;
      }
      (void)rdt_send(s, chunk, n, now, &out);
      st = send_out(c, &out);
      if (st != SYS_OK) {
        return st;
      }
    }
    if (s->state != SENDER_RUNNING) {
      return SYS_OK;
    }
    if (!out.timer_on) {
      return SYS_ERR;
    }

    st = get_current_time_ms(&now);
    if (st != SYS_OK) {
      return st;
    }
    if (now >= out.timer_due) {
      (void)sender_timeout(s, now, &out);
    } else {
      size_t n = 0;
      st = poll_for_datagram(c, buf, sizeof buf, sys_timeout_until(out.timer_due, now), &n);
      if (st == SYS_TIMEOUT) {
        continue;
      }
      if (st != SYS_OK) {
        return st;
      }
      st = get_current_time_ms(&now);
      if (st != SYS_OK) {
        return st;
      }
      (void)sender_rdt_rcv(s, buf, n, now, &out);
    }
    st = send_out(c, &out);
    if (st != SYS_OK) {
      return st;
    }
  }
}

#endif
