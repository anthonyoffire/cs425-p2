#ifndef PACKET_H
#define PACKET_H

#include <stddef.h>
#include <stdint.h>

#define PKT_HEADER_LEN 10
#define PKT_MAX_PAYLOAD 1024
#define PKT_MAX_LEN (PKT_HEADER_LEN + PKT_MAX_PAYLOAD)

enum pkt_status {
  PKT_OK = 0,
  PKT_DROP,
  PKT_ERR_ARG
};

enum pkt_type { PKT_DATA = 0, PKT_ACK = 1, PKT_FIN = 2 };

struct packet {
  uint8_t type;
  uint32_t seq;
  uint16_t length;
  uint8_t payload[PKT_MAX_PAYLOAD];
};

/**
 * @brief RFC 1071 Internet checksum of a buffer.
 * @param buf Bytes to sum.
 * @param len Number of bytes in @p buf.
 * @return The checksum; an intact packet (checksum field included) yields 0.
 */
uint16_t checksum_compute(const uint8_t *buf, size_t len);

/**
 * @brief Serialises a packet (network byte order, checksum filled in).
 * @param p       Packet to encode.
 * @param buf     Output buffer.
 * @param cap     Capacity of @p buf in bytes.
 * @param out_len Receives the number of bytes written.
 * @return PKT_OK on success, PKT_ERR_ARG on invalid arguments or insufficient capacity.
 */
enum pkt_status pkt_encode(const struct packet *p, uint8_t *buf, size_t cap,
                       size_t *out_len);

/**
 * @brief Validates and parses a received datagram.
 * @param buf Datagram bytes.
 * @param n   Number of bytes in @p buf.
 * @param out Receives the parsed packet on PKT_OK.
 * @return PKT_OK on success, PKT_DROP if the datagram must be discarded,
 *         PKT_ERR_ARG on invalid arguments.
 */
enum pkt_status pkt_decode(const uint8_t *buf, size_t n, struct packet *out);

#define HELLO_MAX 96
#define HELLO_REASON_MAX 64

enum hello_reply {
  HELLO_OK,
  HELLO_REFUSED, /* "ERR <reason>" */
  HELLO_OTHER    /* anything else: not a hello reply */
};

/**
 * @brief Builds "HELLO <session> recv" or "HELLO <session> send <loss> <corrupt> <dup>".
 * @param buf       Output buffer for the message.
 * @param cap       Capacity of @p buf in bytes.
 * @param session   Session name.
 * @param is_sender Non-zero for the send form, zero for the recv form.
 * @param loss      Loss probability (send form only).
 * @param corrupt   Corruption probability (send form only).
 * @param dup       Duplication probability (send form only).
 * @param out_len   Receives the message length.
 * @return PKT_OK on success, PKT_ERR_ARG on invalid arguments or insufficient capacity.
 */
enum pkt_status hello_format(char *buf, size_t cap, const char *session,
                             int is_sender, double loss, double corrupt,
                             double dup, size_t *out_len);

/**
 * @brief Classifies the relay's reply to a hello.
 * @param buf        Reply bytes.
 * @param n          Number of bytes in @p buf.
 * @param reason     Filled with the refusal reason (printable ASCII, truncated) on
 *                   HELLO_REFUSED.
 * @param reason_cap Capacity of @p reason in bytes.
 * @return HELLO_OK, HELLO_REFUSED, or HELLO_OTHER if it is not a hello reply.
 */
enum hello_reply hello_parse_reply(const uint8_t *buf, size_t n, char *reason,
                                   size_t reason_cap);

#endif // PACKET_H
