#ifndef SYS_OPS_H
#define SYS_OPS_H

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
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

enum sys_status sys_ctx_init(struct sys_ctx *c);

#define EXIT_OK 0
#define EXIT_USAGE 1
#define EXIT_FAIL 2

/** Milliseconds from now until due, clamped to 0 and INT_MAX, for use as a poll timeout. */
int sys_timeout_until(uint64_t due, uint64_t now);
/** Message for an I/O status. Never NULL. */
const char *sys_describe(enum sys_status st);
/** Process exit code: EXIT_OK on success, EXIT_FAIL otherwise. */
int sys_exit_code(enum sys_status st);
/** Resolves host (IPv4, as the relay is) and connects one UDP socket to it. */
enum sys_status net_connect(struct sys_ctx *c, const char *host, uint16_t port);
enum sys_status open_file(struct sys_ctx *c, const char *path, int for_write);
/** Closes socket and file; SYS_ERR if flushing the file failed. */
enum sys_status net_close(struct sys_ctx *c);

/** Sends the hello up to 5 times, 1 s apart; reason is filled on SYS_REFUSED. */
enum sys_status net_register(struct sys_ctx *c, const char *session, int is_sender,
                             double loss, double corrupt, double dup, char *reason,
                             size_t reason_cap);
enum sys_status sys_run_sender(struct sys_ctx *c, uint32_t window, uint32_t timeout_ms,
                               struct sender_stats *stats);
enum sys_status run_receiver(struct sys_ctx *c);

enum sys_status get_current_time_ms(uint64_t *out_ms);
enum sys_status send_datagram(const struct sys_ctx *c, const uint8_t *buf, size_t len);
enum sys_status wait_for_datagram(const struct sys_ctx *c, uint8_t *buf, size_t cap,
                                  int timeout_ms, size_t *out_len);

#endif // SYS_OPS_H
