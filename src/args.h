#ifndef ARGS_H
#define ARGS_H

#include <stdint.h>

#define ARGS_SESSION_MAX 32

enum args_mode { MODE_SEND, MODE_RECV };

enum args_status {
  ARGS_OK,    /* parsed */
  ARGS_USAGE, /* no arguments at all: print usage, exit 0 */
  ARGS_BAD    /* malformed command line: exit 1 */
};

struct args {
  enum args_mode mode;
  char session[ARGS_SESSION_MAX + 1];
  uint16_t window;
  uint16_t timeout_ms;
  double loss;
  double corrupt;
  double dup;
  uint16_t port;
  const char *relay; /* points into argv */
  const char *file;  /* points into argv */
};

/** Usage text, exactly as the assignment specifies. */
const char *args_usage(void);

/** Parses argv (mode in argv[1], then getopt options). Fills *out only on ARGS_OK. */
enum args_status args_parse(int argc, char **argv, struct args *out);

#endif // ARGS_H
