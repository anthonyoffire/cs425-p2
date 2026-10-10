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

/**
 * @brief Usage text, exactly as the assignment specifies.
 * @return Static, NUL-terminated usage string; never NULL.
 */
const char *args_usage(void);

/**
 * @brief Parses argv (mode in argv[1], then getopt options).
 * @param argc Argument count, as passed to main.
 * @param argv Argument vector, as passed to main. The relay and file pointers in @p out
 *             point into it.
 * @param out  Filled only when ARGS_OK is returned.
 * @return ARGS_OK if parsed, ARGS_USAGE if there were no arguments, ARGS_BAD if malformed.
 */
enum args_status args_parse(int argc, char **argv, struct args *out);

#endif // ARGS_H
