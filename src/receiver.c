/* ======================================================================
 * LAYER 2: GO-BACK-N STATE MACHINES (receiver)
 * No I/O: time is passed in, and each call returns what to do next.
 * ====================================================================== */

#include "receiver.h"

#include <string.h>

#ifndef TEST
#include "sys_ops.h"
#endif

static int is_active(const struct receiver *r) {
  return r->state == RECEIVER_RUNNING || r->state == RECEIVER_LINGERING;
}

static int out_begin(struct receiver_out *out) {
  out->payload_len = 0;
  out->close_file = 0;
  out->ack_len = 0;
  out->timer_on = 0;
  out->timer_due = 0;
  return 0;
}

static enum receiver_state finish(struct receiver *r, struct receiver_out *out) {
  out->timer_on = is_active(r);
  out->timer_due = r->deadline;
  return r->state;
}

static int build_ack(struct receiver *r, struct receiver_out *out) {
  struct packet ack;
  memset(&ack, 0, sizeof ack);
  ack.type = PKT_ACK;
  ack.seq = r->expected;
  if (pkt_encode(&ack, out->ack, sizeof out->ack, &out->ack_len) != PKT_OK) {
    out->ack_len = 0;
    r->state = RECEIVER_ERROR;
    return -1;
  }
  return 0;
}

enum receiver_state receiver_init(struct receiver *r, uint64_t now, struct receiver_out *out) {
  if (r == NULL || out == NULL) {
    return RECEIVER_ERROR;
  }
  r->expected = 0;
  r->state = RECEIVER_RUNNING;
  r->deadline = now + RECEIVER_IDLE_MS;
  out_begin(out);
  return finish(r, out);
}

enum receiver_state rdt_rcv(struct receiver *r, const uint8_t *buf, size_t len,
                                         uint64_t now, struct receiver_out *out) {
  if (r == NULL || out == NULL) {
    return RECEIVER_ERROR;
  }
  out_begin(out);
  struct packet p;
  if (!is_active(r) || pkt_decode(buf, len, &p) != PKT_OK) {
    return finish(r, out);
  }

  if (p.type != PKT_ACK) {
    if (r->state == RECEIVER_RUNNING && p.seq == r->expected) {
      if (p.type == PKT_DATA) {
        memcpy(out->payload, p.payload, p.length);
        out->payload_len = p.length;
      } else {
        out->close_file = 1;
        r->state = RECEIVER_LINGERING;
      }
      r->expected++;
    }
    if (build_ack(r, out) != 0) {
      return finish(r, out);
    }
  }

  uint32_t window_ms = r->state == RECEIVER_LINGERING ? RECEIVER_LINGER_MS : RECEIVER_IDLE_MS;
  r->deadline = now + window_ms;
  return finish(r, out);
}

enum receiver_state receiver_timeout(struct receiver *r, uint64_t now, struct receiver_out *out) {
  if (r == NULL || out == NULL) {
    return RECEIVER_ERROR;
  }
  out_begin(out);
  if (is_active(r) && now >= r->deadline) {
    r->state = r->state == RECEIVER_LINGERING ? RECEIVER_DONE : RECEIVER_GAVE_UP;
  }
  return finish(r, out);
}

#ifndef TEST

/* ======================================================================
 * LAYER 3: RECEIVER I/O
 * ====================================================================== */

static enum sys_status react_to_datagram(struct sys_ctx *c, const struct receiver_out *out) {
  if (out->payload_len > 0 &&
      fwrite(out->payload, 1, out->payload_len, c->fp) != out->payload_len) {
    return SYS_ERR;
  }
  if (out->close_file) {
    int rc = fclose(c->fp);
    c->fp = NULL;
    if (rc != 0) {
      return SYS_ERR;
    }
  }
  if (out->ack_len > 0) {
    return send_datagram(c, out->ack, out->ack_len);
  }
  return SYS_OK;
}

static enum sys_status wait_for_datagram(struct sys_ctx *c, struct receiver *r) {
  struct receiver_out out;
  uint8_t buf[PKT_MAX_LEN];
  uint64_t now = 0;
  enum sys_status st = get_current_time_ms(&now);
  if (st != SYS_OK) {
    return st;
  }
  (void)receiver_init(r, now, &out);

  while (r->state == RECEIVER_RUNNING || r->state == RECEIVER_LINGERING) {
    st = get_current_time_ms(&now);
    if (st != SYS_OK) {
      return st;
    }
    if (now >= out.timer_due) {
      (void)receiver_timeout(r, now, &out);
      continue;
    }
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
    (void)rdt_rcv(r, buf, n, now, &out);
    st = react_to_datagram(c, &out);
    if (st != SYS_OK) {
      return st;
    }
  }
  return SYS_OK;
}

enum sys_status run_receiver(struct sys_ctx *c) {
  struct receiver r;
  if (c == NULL) {
    return SYS_ERR;
  }
  enum sys_status st = wait_for_datagram(c, &r);
  if (st != SYS_OK) {
    return st;
  }
  switch (r.state) {
    case RECEIVER_DONE:
      return SYS_OK;
    case RECEIVER_GAVE_UP:
      return SYS_GAVE_UP;
    default:
      return SYS_ERR;
  }
}

#endif
