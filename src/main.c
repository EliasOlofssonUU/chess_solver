#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "perft.h"
#include "position.h"
#include "solver.h"

typedef struct {
    const char *solver_name;
    const char *fen;
    int depth;
    int perft_depth;
    int show_help;
} Options;

static const char *default_fen(void) {
    return "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
}

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage:\n"
            "  %s --solver baseline --fen \"<FEN>\" --depth 8\n"
            "  %s --solver pthreads --fen \"<FEN>\" --depth 8\n"
            "  %s --solver openmp --fen \"<FEN>\" --depth 8\n"
            "  %s --solver baseline --fen \"<FEN>\" --perft 8\n"
            "  %s --solver pthreads --fen \"<FEN>\" --perft 8\n"
            "  %s --solver openmp --fen \"<FEN>\" --perft 8\n",
            prog, prog, prog, prog, prog, prog);
}

static void cleanup_solver(const Solver *solver) {
    if (solver && solver->cleanup) {
        solver->cleanup();
    }
    engine_cleanup();
}

static int parse_int_arg(const char *text, int *out) {
    char *end = NULL;
    long value;

    if (!text || !out) {
        return 0;
    }

    value = strtol(text, &end, 10);
    if (!end || *end != '\0') {
        return 0;
    }

    *out = (int)value;
    return 1;
}

static int parse_args(int argc, char **argv, Options *opts) {
    int saw_depth = 0;
    int saw_perft = 0;

    opts->solver_name = "baseline";
    opts->fen = default_fen();
    opts->depth = 8;
    opts->perft_depth = -1;
    opts->show_help = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--solver") == 0 && i + 1 < argc) {
            opts->solver_name = argv[++i];
        } else if (strcmp(argv[i], "--fen") == 0 && i + 1 < argc) {
            opts->fen = argv[++i];
        } else if (strcmp(argv[i], "--depth") == 0 && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], &opts->depth)) {
                return 0;
            }
            saw_depth = 1;
        } else if (strcmp(argv[i], "--perft") == 0 && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], &opts->perft_depth)) {
                return 0;
            }
            saw_perft = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            opts->show_help = 1;
        } else {
            return 0;
        }
    }

    if (saw_depth && saw_perft) {
        return 0;
    }
    if (opts->depth < 1) {
        opts->depth = 1;
    }
    if (opts->perft_depth < -1) {
        return 0;
    }

    return 1;
}

static const Solver *find_solver(const char *name) {
    const Solver *solvers[] = {
        solver_baseline(),
        solver_pthreads(),
        solver_openmp()
    };

    for (size_t i = 0; i < sizeof(solvers) / sizeof(solvers[0]); ++i) {
        if (strcmp(solvers[i]->name, name) == 0) {
            return solvers[i];
        }
    }

    return NULL;
}

int main(int argc, char **argv) {
    Options opts;
    Position pos;
    const Solver *solver;

    if (!parse_args(argc, argv, &opts) || opts.show_help) {
        print_usage(argv[0]);
        return opts.show_help ? 0 : 1;
    }

    solver = find_solver(opts.solver_name);
    if (!solver) {
        fprintf(stderr, "Unknown solver: %s\n", opts.solver_name);
        print_usage(argv[0]);
        return 1;
    }

    engine_init();
    if (solver->init) {
        solver->init();
    }

    if (!set_fen(&pos, opts.fen)) {
        fprintf(stderr, "Invalid FEN\n");
        cleanup_solver(solver);
        return 1;
    }

    if (opts.perft_depth >= 0) {
        double start_ms = now_ms();
        uint64_t nodes = perft(&pos, opts.perft_depth);
        double time_ms = now_ms() - start_ms;
        double nps = (time_ms > 0.0) ? ((double)nodes / (time_ms / 1000.0)) : 0.0;

        printf("mode=perft solver=%s depth=%d nodes=%llu time_ms=%.3f nps=%llu\n",
               solver->name,
               opts.perft_depth,
               (unsigned long long)nodes,
               time_ms,
               (unsigned long long)nps);
    } else {
        SearchResult result;
        char move_text[6] = "none";

        solver->search_best_move(&pos, opts.depth, &result);
        if (result.best_move != MOVE_NONE) {
            move_to_uci(result.best_move, move_text);
        }

        printf("mode=search solver=%s depth=%d bestmove=%s score=%d nodes=%llu time_ms=%.3f nps=%llu\n",
               solver->name,
               opts.depth,
               move_text,
               result.score,
               (unsigned long long)result.nodes,
               result.time_ms,
               (unsigned long long)result.nps);
    }

    cleanup_solver(solver);
    return 0;
}
