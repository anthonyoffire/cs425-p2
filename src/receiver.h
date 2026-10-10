#ifndef RECEIVER_H
#define RECEIVER_H

#include <stddef.h>
#include <stdint.h>
#include "packet.h"

/* Layer 2: Go-Back-N receiver. No I/O; time is passed in, results come back in receiver_out. */

#define RECEIVER_IDLE_MS 30000
#define RECEIVER_LINGER_MS 2000

enum receiver_state {
  RECEIVER_RUNNING,   /* waiting for packets */
  RECEIVER_LINGERING, /* FIN accepted; still answering repeated FINs */
  RECEIVER_DONE,      /* linger over: success */
  RECEIVER_GAVE_UP,   /* nothing valid for 30 s */
  RECEIVER_ERROR      /* bad arguments */
};

struct receiver {
  uint32_t expected;
  enum receiver_state state;
  uint64_t deadline; /* idle limit while running, end of linger while lingering */
};

/** What the caller must do after an event, in this order: deliver, close, send. */
struct receiver_out {
  uint8_t payload[PKT_MAX_PAYLOAD]; /* in-order bytes to append to the file */
  size_t payload_len;
  int close_file;                   /* the FIN was accepted */
  uint8_t ack[PKT_HEADER_LEN];      /* datagram to send */
  size_t ack_len;                   /* 0 when there is nothing to send */
  int timer_on;
  uint64_t timer_due;               /* absolute ms; meaningful when timer_on */
};

/**
 * @brief Starts a receiver waiting for packet 0 and arms the idle timer.
 * @param r   Receiver to initialise.
 * @param now Current time in ms.
 * @param out Receives what the caller must do next.
 * @return RECEIVER_RUNNING, or RECEIVER_ERROR on invalid arguments.
 */
enum receiver_state receiver_init(struct receiver *r, uint64_t now, struct receiver_out *out);

/**
 * @brief A datagram arrived; invalid ones are ignored without a reply.
 * @param r   Receiver.
 * @param buf Datagram bytes.
 * @param len Number of bytes in @p buf.
 * @param now Current time in ms.
 * @param out Receives what the caller must do next (deliver, close, send).
 * @return The receiver's state after the event.
 */
enum receiver_state rdt_rcv(struct receiver *r, const uint8_t *buf, size_t len,
                                         uint64_t now, struct receiver_out *out);

/**
 * @brief The caller's wait ended; acts only if the deadline has really passed.
 * @param r   Receiver.
 * @param now Current time in ms.
 * @param out Receives what the caller must do next.
 * @return The receiver's state after the event.
 */
enum receiver_state receiver_timeout(struct receiver *r, uint64_t now, struct receiver_out *out);

#endif // RECEIVER_H
