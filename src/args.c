/* ======================================================================
 * LAYER 3: I/O (command line)
 * Pure parsing of argv into a struct; no I/O of its own. Only main uses it.
 * ====================================================================== */

#include "args.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_WINDOW 8
#define DEFAULT_TIMEOUT_MS 250
#define DEFAULT_PORT 4250
#define MAX_WINDOW 64
#define MAX_TIMEOUT_MS 60000

static const char USAGE[] =
    "Usage: myapp send -s <session> [-w window] [-T timeout-ms] [-l loss]\n"
    "                  [-c corrupt] [-d dup] [-p port] <relay> <file>\n"
    "       myapp recv -s <session> [-p port] <relay> <file>\n"
    "\n"
    "  -s <session>     session name shared by the sender and the receiver\n"
    "  -w <window>      Go-Back-N window size in packets, 1 to 64 (default: 8)\n"
    "  -T <timeout-ms>  retransmission timeout in milliseconds (default: 250)\n"
    "  -l <loss>        probability the relay drops a packet (default: 0)\n"
    "  -c <corrupt>     probability the relay flips a bit (default: 0)\n"
    "  -d <dup>         probability the relay duplicates a packet (default: 0)\n"
    "  -p <port>        relay port (default: 4250)\n"
    "  <relay>          host name or address of the relay\n"
    "  <file>           file to send, or file to write what is received\n";

const char *args_usage(void) { return USAGE; }

static int parse_long(const char *s, long min, long max, long *out) {
  char *end = NULL;
  errno = 0;
  long v = strtol(s, &end, 10);
  if (s[0] == '\0' || *end != '\0' || errno != 0 || v < min || v > max) {
    return -1;
  }
  *out = v;
  return 0;
}

static int parse_prob(const char *s, double *out) {
  char *end = NULL;
  errno = 0;
  double v = strtod(s, &end);
  if (s[0] == '\0' || *end != '\0' || errno != 0 || !(v >= 0.0 && v <= 1.0)) {
    return -1;
  }
  *out = v;
  return 0;
}

static int valid_session(const char *s) {
  size_t n = strlen(s);
  if (n < 1 || n > ARGS_SESSION_MAX) {
    return 0;
  }
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
      return 0;
    }
  }
  return 1;
}

static void reset_getopt(void) {
  opterr = 0;
#if defined(__GLIBC__)
  optind = 0;
#else
  optind = 1;
#if defined(__APPLE__) || defined(__FreeBSD__)
  optreset = 1;
#endif
#endif
}

enum args_status args_parse(int argc, char **argv, struct args *out) {
  if (out == NULL || argv == NULL || argc < 1) {
    return ARGS_BAD;
  }
  if (argc == 1) {
    return ARGS_USAGE;
  }

  struct args a;
  memset(&a, 0, sizeof a);
  a.window = DEFAULT_WINDOW;
  a.timeout_ms = DEFAULT_TIMEOUT_MS;
  a.port = DEFAULT_PORT;

  if (strcmp(argv[1], "send") == 0) {
    a.mode = MODE_SEND;
  } else if (strcmp(argv[1], "recv") == 0) {
    a.mode = MODE_RECV;
  } else {
    return ARGS_BAD;
  }

  int have_session = 0;
  int opt;
  long n = 0;
  reset_getopt();
  while ((opt = getopt(argc - 1, argv + 1, "s:w:T:l:c:d:p:")) != -1) {
    if (a.mode == MODE_RECV && opt != 's' && opt != 'p') {
      return ARGS_BAD;
    }
    switch (opt) {
    case 's':
      if (!valid_session(optarg)) {
        return ARGS_BAD;
      }
      memcpy(a.session, optarg, strlen(optarg) + 1);
      have_session = 1;
      break;
    case 'w':
      if (parse_long(optarg, 1, MAX_WINDOW, &n) != 0) {
        return ARGS_BAD;
      }
      a.window = (uint16_t)n;
      break;
    case 'T':
      if (parse_long(optarg, 1, MAX_TIMEOUT_MS, &n) != 0) {
        return ARGS_BAD;
      }
      a.timeout_ms = (uint16_t)n;
      break;
    case 'l':
      if (parse_prob(optarg, &a.loss) != 0) {
        return ARGS_BAD;
      }
      break;
    case 'c':
      if (parse_prob(optarg, &a.corrupt) != 0) {
        return ARGS_BAD;
      }
      break;
    case 'd':
      if (parse_prob(optarg, &a.dup) != 0) {
        return ARGS_BAD;
      }
      break;
    case 'p':
      if (parse_long(optarg, 1, 65535, &n) != 0) {
        return ARGS_BAD;
      }
      a.port = (uint16_t)n;
      break;
    default:
      return ARGS_BAD;
    }
  }

  int rest = (argc - 1) - optind;
  if (!have_session || rest != 2) {
    return ARGS_BAD;
  }
  a.relay = argv[1 + optind];
  a.file = argv[1 + optind + 1];

  *out = a;
  return ARGS_OK;
}
