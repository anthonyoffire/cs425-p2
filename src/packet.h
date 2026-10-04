#ifndef PACKET_H
#define PACKET_H

#include <stddef.h>
#include <stdint.h>

#define PKT_HEADER_LEN 10
#define PKT_MAX_PAYLOAD 1024
#define PKT_MAX_LEN (PKT_HEADER_LEN + PKT_MAX_PAYLOAD)

enum pkt_status {
  PKT_OK = 0,
  PKT_DROP, /* failed validation: discard as if lost */
  PKT_ERR_ARG
};

enum pkt_type { PKT_DATA = 0, PKT_ACK = 1, PKT_FIN = 2 };

struct packet {
  uint8_t type;
  uint32_t seq;
  uint16_t length;
  uint8_t payload[PKT_MAX_PAYLOAD];
};

/** RFC 1071 Internet checksum of buf; an intact packet (checksum included) yields 0. */
uint16_t checksum_compute(const uint8_t *buf, size_t len);

/** Serialises p into buf (network byte order, checksum filled in). */
enum pkt_status pkt_encode(const struct packet *p, uint8_t *buf, size_t cap,
                       size_t *out_len);

/** Validates and parses a datagram of n bytes; PKT_DROP if it must be discarded. */
enum pkt_status pkt_decode(const uint8_t *buf, size_t n, struct packet *out);

#define HELLO_MAX 96
#define HELLO_REASON_MAX 64

enum hello_reply {
  HELLO_OK,
  HELLO_REFUSED, /* "ERR <reason>" */
  HELLO_OTHER    /* anything else: not a hello reply */
};

/** Builds "HELLO <session> recv" or "HELLO <session> send <loss> <corrupt> <dup>". */
enum pkt_status hello_format(char *buf, size_t cap, const char *session,
                             int is_sender, double loss, double corrupt,
                             double dup, size_t *out_len);

/** Classifies the relay's reply; fills reason (printable ASCII, truncated) on refusal. */
enum hello_reply hello_parse_reply(const uint8_t *buf, size_t n, char *reason,
                                   size_t reason_cap);

#endif // PACKET_H
