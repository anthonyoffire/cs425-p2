/* ======================================================================
 * LAYER 1: PACKETS
 * Pure functions: bytes in, bytes or a struct out. No sockets, clocks or files.
 * ====================================================================== */

#include "packet.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

uint16_t checksum_compute(const uint8_t *buf, size_t len) {
  uint64_t sum = 0;
  size_t i = 0;
  for (; i + 1 < len; i += 2) {
    sum += ((uint64_t)buf[i] << 8) | buf[i + 1];
  }
  if (i < len) {
    sum += (uint64_t)buf[i] << 8;
  }
  while (sum >> 16) {
    sum = (sum & 0xffff) + (sum >> 16);
  }
  return (uint16_t)(~sum & 0xffff);
}

enum pkt_status pkt_encode(const struct packet *p, uint8_t *buf, size_t cap,
                       size_t *out_len) {
  if (p == NULL || buf == NULL || out_len == NULL) {
    return PKT_ERR_ARG;
  }
  if (p->type > PKT_FIN || p->length > PKT_MAX_PAYLOAD) {
    return PKT_ERR_ARG;
  }
  size_t total = PKT_HEADER_LEN + (size_t)p->length;
  if (cap < total) {
    return PKT_ERR_ARG;
  }

  uint32_t seq = htonl(p->seq);
  uint16_t len = htons(p->length);
  buf[0] = p->type;
  buf[1] = 0;
  buf[2] = 0;
  buf[3] = 0;
  memcpy(buf + 4, &seq, sizeof seq);
  memcpy(buf + 8, &len, sizeof len);
  if (p->length > 0) {
    memcpy(buf + PKT_HEADER_LEN, p->payload, p->length);
  }

  uint16_t sum = htons(checksum_compute(buf, total));
  memcpy(buf + 2, &sum, sizeof sum);
  *out_len = total;
  return PKT_OK;
}

enum pkt_status pkt_decode(const uint8_t *buf, size_t n, struct packet *out) {
  if (buf == NULL || out == NULL || n < PKT_HEADER_LEN) {
    return PKT_DROP;
  }
  uint16_t len_net;
  memcpy(&len_net, buf + 8, sizeof len_net);
  uint16_t length = ntohs(len_net);
  if (length > PKT_MAX_PAYLOAD || PKT_HEADER_LEN + (size_t)length != n) {
    return PKT_DROP;
  }
  if (buf[0] > PKT_FIN || buf[1] != 0) {
    return PKT_DROP;
  }
  if (checksum_compute(buf, n) != 0) {
    return PKT_DROP;
  }

  uint32_t seq_net;
  memcpy(&seq_net, buf + 4, sizeof seq_net);
  out->type = buf[0];
  out->seq = ntohl(seq_net);
  out->length = length;
  if (length > 0) {
    memcpy(out->payload, buf + PKT_HEADER_LEN, length);
  }
  return PKT_OK;
}

/* ======================================================================
 * LAYER 3: I/O (relay hello)
 * The hello text is built and parsed here without touching the socket; sys_ops.c
 * sends and receives it.
 * ====================================================================== */

enum pkt_status hello_format(char *buf, size_t cap, const char *session,
                             int is_sender, double loss, double corrupt,
                             double dup, size_t *out_len) {
  if (buf == NULL || session == NULL || out_len == NULL) {
    return PKT_ERR_ARG;
  }
  int n;
  if (is_sender) {
    n = snprintf(buf, cap, "HELLO %s send %.6f %.6f %.6f", session, loss, corrupt, dup);
  } else {
    n = snprintf(buf, cap, "HELLO %s recv", session);
  }
  if (n < 0 || (size_t)n >= cap) {
    return PKT_ERR_ARG;
  }
  *out_len = (size_t)n;
  return PKT_OK;
}

enum hello_reply hello_parse_reply(const uint8_t *buf, size_t n, char *reason,
                                   size_t reason_cap) {
  if (reason != NULL && reason_cap > 0) {
    reason[0] = '\0';
  }
  if (buf == NULL) {
    return HELLO_OTHER;
  }
  if (n == 2 && memcmp(buf, "OK", 2) == 0) {
    return HELLO_OK;
  }
  if (n >= 4 && memcmp(buf, "ERR ", 4) == 0) {
    if (reason != NULL && reason_cap > 0) {
      size_t i = 0;
      for (; i < n - 4 && i + 1 < reason_cap; i++) {
        uint8_t ch = buf[4 + i];
        reason[i] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '?';
      }
      reason[i] = '\0';
    }
    return HELLO_REFUSED;
  }
  return HELLO_OTHER;
}
