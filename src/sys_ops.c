/* ======================================================================
 * LAYER 3: I/O
 * The socket, relay hello, poll loop, clock and file. Reads an event, hands it to
 * the layer 2 state machine, carries out what comes back. Only the helpers without
 * system calls are unit tested.
 * ====================================================================== */

#define _POSIX_C_SOURCE 200809L

#include "sys_ops.h"

#include <limits.h>

/* Helpers without system calls; these are unit tested. */

enum sys_status sys_ctx_init(struct sys_ctx *c) {
  if (c == NULL) {
    return SYS_ERR;
  }
  c->fd = -1;
  c->fp = NULL;
  return SYS_OK;
}

int sys_timeout_until(uint64_t due, uint64_t now) {
  if (due <= now) {
    return 0;
  }
  uint64_t d = due - now;
  return d > (uint64_t)INT_MAX ? INT_MAX : (int)d;
}

const char *sys_describe(enum sys_status st) {
  switch (st) {
  case SYS_OK:
    return "ok";
  case SYS_TIMEOUT:
    return "timed out";
  case SYS_REFUSED:
    return "the relay refused the session";
  case SYS_GAVE_UP:
    return "giving up: the other side stopped answering";
  case SYS_ERR:
    return "network or file error";
  default:
    return "internal error";
  }
}

int sys_exit_code(enum sys_status st) { return st == SYS_OK ? EXIT_OK : EXIT_FAIL; }

/* Everything below needs a real kernel; unit tests drive the state machines directly instead. */
#ifndef TEST

#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "packet.h"

#define HELLO_ATTEMPTS 5
#define HELLO_REPLY_TIMEOUT_MS 1000
#define HELLO_MAX_STRAYS 16

#ifdef MSG_TRUNC
#define RECV_FLAGS MSG_TRUNC
#else
#define RECV_FLAGS 0
#endif

enum sys_status net_connect(struct sys_ctx *c, const char *host, uint16_t port) {
  if (c == NULL || host == NULL) {
    return SYS_ERR;
  }
  char service[8];
  snprintf(service, sizeof service, "%u", (unsigned)port);

  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET; /* the relay only binds IPv4 */
  hints.ai_socktype = SOCK_DGRAM;
  struct addrinfo *res = NULL;
  if (getaddrinfo(host, service, &hints, &res) != 0) {
    return SYS_ERR;
  }

  enum sys_status st = SYS_ERR;
  for (const struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next) {
    int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) {
      continue;
    }
    if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
      c->fd = fd;
      st = SYS_OK;
      break;
    }
    close(fd);
  }
  freeaddrinfo(res);
  return st;
}

enum sys_status open_file(struct sys_ctx *c, const char *path, int for_write) {
  if (c == NULL || path == NULL) {
    return SYS_ERR;
  }
  c->fp = fopen(path, for_write ? "wb" : "rb");
  return c->fp != NULL ? SYS_OK : SYS_ERR;
}

enum sys_status net_close(struct sys_ctx *c) {
  if (c == NULL) {
    return SYS_ERR;
  }
  enum sys_status st = SYS_OK;
  if (c->fd >= 0) {
    close(c->fd);
    c->fd = -1;
  }
  if (c->fp != NULL) {
    if (fclose(c->fp) != 0) {
      st = SYS_ERR;
    }
    c->fp = NULL;
  }
  return st;
}

enum sys_status get_current_time_ms(uint64_t *out_ms) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return SYS_ERR;
  }
  *out_ms = (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
  return SYS_OK;
}

enum sys_status send_datagram(const struct sys_ctx *c, const uint8_t *buf, size_t len) {
  for (;;) {
    ssize_t n = send(c->fd, buf, len, 0);
    if (n >= 0) {
      return (size_t)n == len ? SYS_OK : SYS_ERR;
    }
    if (errno != EINTR) {
      return SYS_ERR;
    }
  }
}

enum sys_status poll_for_datagram(const struct sys_ctx *c, uint8_t *buf, size_t cap,
                                  int timeout_ms, size_t *out_len) {
  struct pollfd pfd;
  pfd.fd = c->fd;
  pfd.events = POLLIN;
  pfd.revents = 0;

  int r = poll(&pfd, 1, timeout_ms);
  if (r < 0) {
    return errno == EINTR ? SYS_TIMEOUT : SYS_ERR;
  }
  if (r == 0) {
    return SYS_TIMEOUT;
  }
  ssize_t n = recv(c->fd, buf, cap, RECV_FLAGS);
  if (n < 0) {
    return (errno == EINTR || errno == EAGAIN) ? SYS_TIMEOUT : SYS_ERR;
  }
  if ((size_t)n > cap) {
    return SYS_TIMEOUT; /* oversized datagram: discard like any bad packet */
  }
  *out_len = (size_t)n;
  return SYS_OK;
}

enum sys_status net_register(struct sys_ctx *c, const char *session, int is_sender,
                             double loss, double corrupt, double dup, char *reason,
                             size_t reason_cap) {
  char hello[HELLO_MAX];
  size_t len = 0;
  if (c == NULL ||
      hello_format(hello, sizeof hello, session, is_sender, loss, corrupt, dup, &len) != PKT_OK) {
    return SYS_ERR;
  }

  uint8_t buf[128];
  for (int attempt = 0; attempt < HELLO_ATTEMPTS; attempt++) {
    enum sys_status st = send_datagram(c, (const uint8_t *)hello, len);
    if (st != SYS_OK) {
      return st;
    }
    for (int strays = 0; strays < HELLO_MAX_STRAYS; strays++) {
      size_t n = 0;
      st = poll_for_datagram(c, buf, sizeof buf, HELLO_REPLY_TIMEOUT_MS, &n);
      if (st == SYS_TIMEOUT) {
        break;
      }
      if (st != SYS_OK) {
        return st;
      }
      enum hello_reply reply = hello_parse_reply(buf, n, reason, reason_cap);
      if (reply == HELLO_OK) {
        return SYS_OK;
      }
      if (reply == HELLO_REFUSED) {
        return SYS_REFUSED;
      }
    }
  }
  return SYS_GAVE_UP;
}

#endif // TEST
