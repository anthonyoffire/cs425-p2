#ifndef SYS_OPS_H
#define SYS_OPS_H

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include "receiver.h"
#include "sender.h"

/* Layer 3: the socket, relay hello, poll loop, clock and file. Reads an event, hands it to
 * the layer 2 state machine, carries out what comes back. */

enum sys_status {
  SYS_OK,
  SYS_TIMEOUT, /* nothing arrived in time (internal to the loops) */
  SYS_REFUSED, /* relay answered ERR */
  SYS_GAVE_UP, /* peer or relay stopped answering */
  SYS_ERR      /* network, file or internal error */
};

struct sys_ctx {
  int fd;   /* connected UDP socket, -1 when closed */
  FILE *fp; /* file being sent or written */
};

/**
 * @brief Marks a context as closed (fd -1, no file).
 * @param c Context to initialise.
 * @return SYS_OK, or SYS_ERR if @p c is NULL.
 */
enum sys_status sys_ctx_init(struct sys_ctx *c);

#define EXIT_OK 0
#define EXIT_USAGE 1
#define EXIT_FAIL 2

/**
 * @brief Milliseconds from now until due, for use as a poll timeout.
 * @param due Absolute deadline in ms.
 * @param now Current time in ms.
 * @return Time remaining, clamped to 0 and INT_MAX.
 */
int sys_timeout_until(uint64_t due, uint64_t now);

/**
 * @brief Message for an I/O status.
 * @param st Status to describe.
 * @return Static message string; never NULL.
 */
const char *sys_describe(enum sys_status st);

/**
 * @brief Maps a status to a process exit code.
 * @param st Final status.
 * @return EXIT_OK on success, EXIT_FAIL otherwise.
 */
int sys_exit_code(enum sys_status st);

/**
 * @brief Resolves host (IPv4, as the relay is) and connects one UDP socket to it.
 * @param c    Context; its fd is set on success.
 * @param host Relay host name or address.
 * @param port Relay UDP port.
 * @return SYS_OK, or SYS_ERR on invalid arguments, resolution or connect failure.
 */
enum sys_status net_connect(struct sys_ctx *c, const char *host, uint16_t port);

/**
 * @brief Opens the transfer file.
 * @param c         Context; its fp is set on success.
 * @param path      File path.
 * @param for_write Non-zero to create/truncate for writing, zero to open for reading.
 * @return SYS_OK, or SYS_ERR on invalid arguments or open failure.
 */
enum sys_status open_file(struct sys_ctx *c, const char *path, int for_write);

/**
 * @brief Closes socket and file.
 * @param c Context; fd and fp are reset.
 * @return SYS_OK, or SYS_ERR if @p c is NULL or flushing the file failed.
 */
enum sys_status net_close(struct sys_ctx *c);

/**
 * @brief Sends the hello up to 5 times, 1 s apart, and waits for the relay's reply.
 * @param c          Connected context.
 * @param session    Session name.
 * @param is_sender  Non-zero to register as sender, zero as receiver.
 * @param loss       Loss probability (sender only).
 * @param corrupt    Corruption probability (sender only).
 * @param dup        Duplication probability (sender only).
 * @param reason     Filled with the relay's reason on SYS_REFUSED.
 * @param reason_cap Capacity of @p reason in bytes.
 * @return SYS_OK, SYS_REFUSED if the relay answered ERR, SYS_GAVE_UP if it never answered,
 *         SYS_ERR on a network or internal error.
 */
enum sys_status net_register(struct sys_ctx *c, const char *session, int is_sender,
                             double loss, double corrupt, double dup, char *reason,
                             size_t reason_cap);

/**
 * @brief Reads the monotonic clock.
 * @param out_ms Receives the time in ms.
 * @return SYS_OK, or SYS_ERR if the clock cannot be read.
 */
enum sys_status get_current_time_ms(uint64_t *out_ms);

/**
 * @brief Sends one datagram on the connected socket, retrying on EINTR.
 * @param c   Connected context.
 * @param buf Datagram bytes.
 * @param len Number of bytes in @p buf.
 * @return SYS_OK if all bytes were sent, SYS_ERR otherwise.
 */
enum sys_status send_datagram(const struct sys_ctx *c, const uint8_t *buf, size_t len);

/**
 * @brief Waits up to a timeout for one datagram.
 * @param c          Connected context.
 * @param buf        Receive buffer.
 * @param cap        Capacity of @p buf in bytes.
 * @param timeout_ms Maximum wait in ms.
 * @param out_len    Receives the datagram length on SYS_OK.
 * @return SYS_OK, SYS_TIMEOUT if nothing usable arrived (including an oversized datagram),
 *         SYS_ERR on a network error.
 */
enum sys_status poll_for_datagram(const struct sys_ctx *c, uint8_t *buf, size_t cap,
                                  int timeout_ms, size_t *out_len);

/**
 * @brief Layer 3 send loop (defined in sender.c): reads the file, drives the sender state
 *        machine and carries out its output until it finishes.
 * @param c Context with an open file and connected socket.
 * @param s Initialised sender.
 * @return SYS_OK when the loop ended (check @p s->state for the outcome), otherwise the
 *         failing I/O status.
 */
enum sys_status run_send_loop(const struct sys_ctx *c, struct sender *s);

/**
 * @brief Layer 3 receive loop (defined in receiver.c): feeds datagrams to the receiver state
 *        machine, writes the file and sends ACKs until it finishes.
 * @param c Context with an open file and connected socket.
 * @param r Receiver; initialised by this function.
 * @return SYS_OK when the loop ended (check @p r->state for the outcome), otherwise the
 *         failing I/O status.
 */
enum sys_status wait_for_datagram(struct sys_ctx *c, struct receiver *r);

/**
 * @brief Sends the open file to the relay with Go-Back-N.
 * @param c          Registered context with an open file.
 * @param window     Window size, 1..SENDER_MAX_WINDOW.
 * @param timeout_ms Retransmission timeout in ms, greater than 0.
 * @param stats      Receives transfer statistics; may be NULL.
 * @return SYS_OK when everything was acknowledged, SYS_GAVE_UP if the receiver stopped
 *         answering, SYS_ERR otherwise.
 */
enum sys_status sys_run_sender(struct sys_ctx *c, uint32_t window, uint32_t timeout_ms,
                               struct sender_stats *stats);

/**
 * @brief Receives a file from the relay with Go-Back-N and writes it to the open file.
 * @param c Registered context with a file open for writing.
 * @return SYS_OK when the transfer completed, SYS_GAVE_UP if nothing valid arrived in time,
 *         SYS_ERR otherwise.
 */
enum sys_status sys_run_receiver(struct sys_ctx *c);
#endif // SYS_OPS_H
