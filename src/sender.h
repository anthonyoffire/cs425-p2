#ifndef SENDER_H
#define SENDER_H

#include <stddef.h>
#include <stdint.h>
#include "packet.h"

/* Layer 2: Go-Back-N sender. No I/O; time is passed in, results come back in sender_out. */

#define SENDER_MAX_WINDOW 64
#define SENDER_MAX_TIMEOUTS 10

enum sender_state {
  SENDER_RUNNING,
  SENDER_DONE,    /* every DATA packet and the FIN are acknowledged */
  SENDER_GAVE_UP, /* 10 timeouts in a row without progress */
  SENDER_ERROR    /* bad arguments or misuse */
};

struct sender_stats {
  uint32_t sent;          /* every transmission, including resends */
  uint32_t retransmitted; /* resends only */
  uint32_t timeouts;
};

/* Sent-but-unacknowledged datagrams: base..next-1 are in flight. */
struct slot {
  uint8_t dg[PKT_MAX_LEN];
  size_t len;
};

struct window {
  uint32_t base;
  uint32_t next;
  uint32_t size;
  struct slot ring[SENDER_MAX_WINDOW];
};

enum win_status { WIN_OK = 0, WIN_ERR_ARG, WIN_IGNORED };

/**
 * @brief Resets a window to empty with the given size.
 * @param w    Window to initialise.
 * @param size Window size, 1..SENDER_MAX_WINDOW.
 * @return WIN_OK, or WIN_ERR_ARG on a NULL window or out-of-range size.
 */
enum win_status win_init(struct window *w, uint32_t size);

/**
 * @brief Tells whether the window has no room for another packet.
 * @param w Window.
 * @return Non-zero if full, zero otherwise.
 */
int win_full(const struct window *w);

/**
 * @brief Tells whether nothing is in flight.
 * @param w Window.
 * @return Non-zero if empty, zero otherwise.
 */
int win_empty(const struct window *w);

/**
 * @brief Stores a copy of a datagram as packet `next` and advances next.
 * @param w   Window.
 * @param dg  Datagram bytes to copy.
 * @param len Number of bytes in @p dg, at most PKT_MAX_LEN.
 * @return WIN_OK, or WIN_ERR_ARG on invalid arguments or a full window.
 */
enum win_status win_push(struct window *w, const uint8_t *dg, size_t len);

/**
 * @brief Applies a cumulative ACK.
 * @param w   Window.
 * @param seq Next sequence number the peer expects.
 * @return WIN_OK if base moved to @p seq, WIN_IGNORED for a duplicate or out-of-range seq.
 */
enum win_status win_ack(struct window *w, uint32_t seq);

/**
 * @brief Looks up the datagram for an in-flight sequence number.
 * @param w   Window.
 * @param seq Sequence number.
 * @return The slot, or NULL if @p seq is not in flight.
 */
const struct slot *win_get(const struct window *w, uint32_t seq);

/** What the caller must do after an event. Pointers stay valid until the next call. */
struct sender_out {
  const struct slot *send[SENDER_MAX_WINDOW]; /* transmit in order */
  size_t nsend;
  int timer_on;       /* whether a retransmission timer is running */
  uint64_t timer_due; /* absolute ms; meaningful when timer_on */
};

struct sender {
  uint32_t timeout_ms;
  struct window win;
  uint32_t total; /* DATA packet count, which is the FIN's seq; valid once eof */
  int eof;
  int timer_on;
  uint64_t deadline;
  uint32_t timeouts; /* consecutive, reset by progress */
  enum sender_state state;
  struct sender_stats stats;
};

/**
 * @brief Initialises a sender.
 * @param s          Sender to initialise.
 * @param window     Window size, 1..SENDER_MAX_WINDOW.
 * @param timeout_ms Retransmission timeout in ms, greater than 0.
 * @return SENDER_RUNNING, or SENDER_ERROR on invalid arguments.
 */
enum sender_state sender_init(struct sender *s, uint32_t window, uint32_t timeout_ms);

/**
 * @brief Tells whether the caller should read the next file chunk and pass it to rdt_send.
 * @param s Sender.
 * @return Non-zero if the sender is running, has not reached end of file and has window room.
 */
int sender_has_data(const struct sender *s);

/**
 * @brief Accepts the next file chunk: 1024 bytes except the last; n == 0 means end of file.
 * @param s     Sender.
 * @param chunk File bytes.
 * @param n     Number of bytes in @p chunk.
 * @param now   Current time in ms.
 * @param out   Receives what the caller must do next.
 * @return The sender's state after the event.
 */
enum sender_state rdt_send(struct sender *s, const uint8_t *chunk, size_t n,
                                 uint64_t now, struct sender_out *out);

/**
 * @brief A datagram arrived from the network; anything but a valid, useful ACK is ignored.
 * @param s   Sender.
 * @param buf Datagram bytes.
 * @param len Number of bytes in @p buf.
 * @param now Current time in ms.
 * @param out Receives what the caller must do next.
 * @return The sender's state after the event.
 */
enum sender_state sender_rdt_rcv(struct sender *s, const uint8_t *buf, size_t len,
                                     uint64_t now, struct sender_out *out);

/**
 * @brief The caller's wait ended; resends the window if the timer is actually due.
 * @param s   Sender.
 * @param now Current time in ms.
 * @param out Receives what the caller must do next.
 * @return The sender's state after the event; SENDER_GAVE_UP after too many timeouts.
 */
enum sender_state sender_timeout(struct sender *s, uint64_t now, struct sender_out *out);


#endif // SENDER_H
