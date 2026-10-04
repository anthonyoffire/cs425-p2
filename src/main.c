/* ======================================================================
 * LAYER 3: I/O (entry point)
 * Parses arguments, wires up the I/O layer and maps results to exit codes.
 * Excluded from unit tests, which supply their own main.
 * ====================================================================== */
#ifndef TEST

#include <stdio.h>
#include "args.h"
#include "sys_ops.h"

int main(int argc, char **argv)
{
    struct args a;
    switch (args_parse(argc, argv, &a)) {
    case ARGS_OK:
        break;
    case ARGS_USAGE:
        fputs(args_usage(), stdout);
        return EXIT_OK;
    case ARGS_BAD:
    default:
        fputs(args_usage(), stderr);
        return EXIT_USAGE;
    }

    struct sys_ctx ctx;
    (void)sys_ctx_init(&ctx);
    if (open_file(&ctx, a.file, a.mode == MODE_RECV) != SYS_OK) {
        fprintf(stderr, "myapp: cannot open %s\n", a.file);
        return EXIT_FAIL;
    }
    if (net_connect(&ctx, a.relay, a.port) != SYS_OK) {
        fprintf(stderr, "myapp: cannot reach relay %s port %u\n", a.relay, (unsigned)a.port);
        (void)net_close(&ctx);
        return EXIT_FAIL;
    }

    char reason[HELLO_REASON_MAX];
    enum sys_status st = net_register(&ctx, a.session, a.mode == MODE_SEND, a.loss,
                                      a.corrupt, a.dup, reason, sizeof reason);
    if (st == SYS_REFUSED) {
        fprintf(stderr, "myapp: relay refused: %s\n", reason);
    } else if (st == SYS_GAVE_UP) {
        fprintf(stderr, "myapp: no reply from relay\n");
    } else if (st == SYS_OK && a.mode == MODE_SEND) {
        struct sender_stats stats;
        st = sys_run_sender(&ctx, a.window, a.timeout_ms, &stats);
        if (st == SYS_OK) {
            fprintf(stderr, "sent %u packets, %u retransmitted, %u timeouts\n",
                    (unsigned)stats.sent, (unsigned)stats.retransmitted,
                    (unsigned)stats.timeouts);
        } else {
            fprintf(stderr, "myapp: %s\n", sys_describe(st));
        }
    } else if (st == SYS_OK) {
        st = run_receiver(&ctx);
        if (st != SYS_OK) {
            fprintf(stderr, "myapp: %s\n", sys_describe(st));
        }
    }

    if (net_close(&ctx) != SYS_OK && st == SYS_OK) {
        fprintf(stderr, "myapp: %s\n", sys_describe(SYS_ERR));
        st = SYS_ERR;
    }
    return sys_exit_code(st);
}

#endif // TEST
