#include "solver.h"

#include <omp.h>
#include <stdlib.h>
#include <string.h>

#include "eval.h"
#include "makemove.h"
#include "movegen.h"

#define MAX_SEARCH_THREADS 256
#define PARALLEL_SPLIT_MIN_DEPTH 6
#define THREAD_COUNT_ENV "CHESS_THREADS"

typedef struct {
    uint64_t nodes;
} SearchStats;

static int terminal_score(const Position *pos) {
    return in_check(pos, pos->side) ? -CHECKMATE_SCORE : 0;
}

static void finish_result(SearchResult *result, Move best_move, int score, uint64_t nodes, double start_ms) {
    memset(result, 0, sizeof(*result));
    result->best_move = best_move;
    result->score = score;
    result->nodes = nodes;
    result->time_ms = now_ms() - start_ms;
    result->nps = (result->time_ms > 0.0) ? ((double)nodes / (result->time_ms / 1000.0)) : 0.0;
}

static int parse_thread_env(void) {
    const char *value = getenv(THREAD_COUNT_ENV);
    char *end = NULL;
    long parsed;

    if (!value || !*value) {
        return 0;
    }

    parsed = strtol(value, &end, 10);
    if (!end || *end != '\0' || parsed < 1) {
        return 0;
    }
    if (parsed > MAX_SEARCH_THREADS) {
        return MAX_SEARCH_THREADS;
    }

    return (int)parsed;
}

static int choose_thread_count(int work_items) {
    int requested = parse_thread_env();
    int threads = requested > 0 ? requested : omp_get_max_threads();

    if (threads > MAX_SEARCH_THREADS) {
        threads = MAX_SEARCH_THREADS;
    }
    if (work_items < 1) {
        return 1;
    }
    if (threads > work_items) {
        threads = work_items;
    }
    if (threads < 1) {
        threads = 1;
    }

    return threads;
}

static int better_root_score(int score, int move_index, int best_score, int best_index) {
    return score > best_score || (score == best_score && (best_index < 0 || move_index < best_index));
}

static int openmp_negamax(Position *pos, int depth, int alpha, int beta, SearchStats *stats) {
    MoveList list;
    int best = -INF;
    int any_legal = 0;

    ++stats->nodes;
    if (depth == 0) {
        return eval_material(pos);
    }

    gen_moves(pos, &list, 0);
    order_moves(&list, MOVE_NONE);

    for (int i = 0; i < list.size; ++i) {
        State state;
        Move move = list.moves[i];
        int score;

        if (!make_move(pos, move, &state)) {
            continue;
        }

        any_legal = 1;
        score = -openmp_negamax(pos, depth - 1, -beta, -alpha, stats);
        unmake_move(pos, move, &state);

        if (score > best) {
            best = score;
        }
        if (score > alpha) {
            alpha = score;
        }
        if (alpha >= beta) {
            break;
        }
    }

    return any_legal ? best : terminal_score(pos);
}

static int search_root_move(const Position *root, Move move, int depth, int alpha, int *score, uint64_t *nodes) {
    Position child = *root;
    SearchStats stats = {0};
    State state;

    if (!make_move(&child, move, &state)) {
        return 0;
    }

    *score = -openmp_negamax(&child, depth - 1, -INF, -alpha, &stats);
    *nodes = stats.nodes;
    return 1;
}

static uint64_t search_remaining_sequential(const Position *root, const MoveList *list, int start_index, int depth, Move *best_move, int *best_score, int *best_index) {
    uint64_t nodes = 0;

    for (int i = start_index; i < list->size; ++i) {
        Move move = list->moves[i];
        uint64_t searched_nodes = 0;
        int score;

        if (!search_root_move(root, move, depth, *best_score, &score, &searched_nodes)) {
            continue;
        }

        nodes += searched_nodes;

        if (better_root_score(score, i, *best_score, *best_index)) {
            *best_score = score;
            *best_move = move;
            *best_index = i;
        }
    }

    return nodes;
}

static uint64_t search_remaining_parallel(const Position *root, const MoveList *list, int start_index, int depth, Move *best_move, int *best_score, int *best_index, int thread_count) {
    int shared_alpha = *best_score;
    uint64_t nodes = 0;

#pragma omp parallel for schedule(dynamic, 1) num_threads(thread_count) reduction(+:nodes)
    for (int i = start_index; i < list->size; ++i) {
        Move move = list->moves[i];
        uint64_t searched_nodes = 0;
        int alpha_snapshot;
        int score;

#pragma omp critical(openmp_alpha_read)
        {
            alpha_snapshot = shared_alpha;
        }

        if (!search_root_move(root, move, depth, alpha_snapshot, &score, &searched_nodes)) {
            continue;
        }

        nodes += searched_nodes;

#pragma omp critical(openmp_root_best)
        {
            if (better_root_score(score, i, *best_score, *best_index)) {
                *best_score = score;
                *best_move = move;
                *best_index = i;
                shared_alpha = score;
            }
        }
    }

    return nodes;
}

static void openmp_search(Position *pos, int depth, SearchResult *result) {
    MoveList list;
    Move best_move = MOVE_NONE;
    int best_score = -INF;
    int best_index = -1;
    uint64_t nodes = 0;
    int first_legal = -1;
    int next_index;
    double start_ms = now_ms();

    if (depth < 1) {
        depth = 1;
    }

    gen_moves(pos, &list, 0);
    order_moves(&list, MOVE_NONE);

    for (int i = 0; i < list.size; ++i) {
        Move move = list.moves[i];
        uint64_t searched_nodes = 0;
        int score;

        if (!search_root_move(pos, move, depth, -INF, &score, &searched_nodes)) {
            continue;
        }

        first_legal = i;
        best_move = move;
        best_score = score;
        best_index = i;
        nodes += searched_nodes;
        break;
    }

    if (first_legal < 0) {
        finish_result(result, MOVE_NONE, terminal_score(pos), 0, start_ms);
        return;
    }

    next_index = first_legal + 1;
    if (next_index < list.size) {
        int work_items = list.size - next_index;
        int thread_count = choose_thread_count(work_items);
        int warmup_target = thread_count;
        int warmup_legal = 1;

        if (depth < PARALLEL_SPLIT_MIN_DEPTH) {
            thread_count = 1;
            warmup_target = 1;
        }

        /* Search a few moves serially first. A better alpha usually saves more
           time than immediately throwing weak bounds at every worker. */
        while (thread_count > 1 && warmup_legal < warmup_target && next_index < list.size) {
            Move move = list.moves[next_index];
            uint64_t searched_nodes = 0;
            int score;

            if (search_root_move(pos, move, depth, best_score, &score, &searched_nodes)) {
                nodes += searched_nodes;

                if (better_root_score(score, next_index, best_score, best_index)) {
                    best_score = score;
                    best_move = move;
                    best_index = next_index;
                }
                ++warmup_legal;
            }

            ++next_index;
        }

        work_items = list.size - next_index;
        thread_count = (depth >= PARALLEL_SPLIT_MIN_DEPTH) ? choose_thread_count(work_items) : 1;

        if (next_index < list.size && thread_count > 1) {
            nodes += search_remaining_parallel(pos, &list, next_index, depth, &best_move, &best_score, &best_index, thread_count);
        } else if (next_index < list.size) {
            nodes += search_remaining_sequential(pos, &list, next_index, depth, &best_move, &best_score, &best_index);
        }
    }

    finish_result(result, best_move, best_score, nodes, start_ms);
}

static void openmp_init(void) {
    int requested = parse_thread_env();

    omp_set_dynamic(0);
    if (requested > 0) {
        omp_set_num_threads(requested);
    }
}

static void openmp_cleanup(void) {
}

const Solver *solver_openmp(void) {
    static const Solver solver = {
        "openmp",
        openmp_init,
        openmp_cleanup,
        openmp_search
    };

    return &solver;
}
