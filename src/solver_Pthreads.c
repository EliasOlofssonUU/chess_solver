#include "solver.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "eval.h"
#include "makemove.h"
#include "movegen.h"

#define MAX_SEARCH_THREADS 256
#define PARALLEL_SPLIT_MIN_DEPTH 6
#define THREAD_COUNT_ENV "CHESS_THREADS"

typedef struct {
    uint64_t nodes;
} SearchStats;

typedef struct {
    const MoveList *list;
    Position root;
    int depth;
    int next_index;
    int alpha;
    uint64_t nodes;
    pthread_mutex_t lock;
    Move best_move;
    int best_score;
    int best_index;
} ParallelRoot;

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

static int available_threads(void) {
    int requested = parse_thread_env();
    long online;

    if (requested > 0) {
        return requested;
    }

    online = sysconf(_SC_NPROCESSORS_ONLN);
    if (online < 1) {
        return 1;
    }
    if (online > MAX_SEARCH_THREADS) {
        return MAX_SEARCH_THREADS;
    }

    return (int)online;
}

static int choose_thread_count(int work_items) {
    int threads = available_threads();

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

static int pthreads_negamax(Position *pos, int depth, int alpha, int beta, SearchStats *stats) {
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
        score = -pthreads_negamax(pos, depth - 1, -beta, -alpha, stats);
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

    *score = -pthreads_negamax(&child, depth - 1, -INF, -alpha, &stats);
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

static void update_parallel_best(ParallelRoot *shared, Move move, int score, int index) {
    pthread_mutex_lock(&shared->lock);

    if (better_root_score(score, index, shared->best_score, shared->best_index)) {
        shared->best_score = score;
        shared->best_move = move;
        shared->best_index = index;
        shared->alpha = score;
    }

    pthread_mutex_unlock(&shared->lock);
}

static int next_parallel_index(ParallelRoot *shared) {
    int index;

    pthread_mutex_lock(&shared->lock);
    index = shared->next_index++;
    pthread_mutex_unlock(&shared->lock);

    return index;
}

static int current_parallel_alpha(ParallelRoot *shared) {
    int alpha;

    pthread_mutex_lock(&shared->lock);
    alpha = shared->alpha;
    pthread_mutex_unlock(&shared->lock);

    return alpha;
}

static void add_parallel_nodes(ParallelRoot *shared, uint64_t nodes) {
    pthread_mutex_lock(&shared->lock);
    shared->nodes += nodes;
    pthread_mutex_unlock(&shared->lock);
}

static void *pthreads_worker(void *arg) {
    ParallelRoot *shared = (ParallelRoot *)arg;
    uint64_t local_nodes = 0;

    for (;;) {
        int index = next_parallel_index(shared);
        Move move;
        uint64_t searched_nodes = 0;
        int alpha;
        int score;

        if (index >= shared->list->size) {
            break;
        }

        move = shared->list->moves[index];
        alpha = current_parallel_alpha(shared);
        if (!search_root_move(&shared->root, move, shared->depth, alpha, &score, &searched_nodes)) {
            continue;
        }

        local_nodes += searched_nodes;
        update_parallel_best(shared, move, score, index);
    }

    add_parallel_nodes(shared, local_nodes);
    return NULL;
}

static uint64_t search_remaining_parallel(const Position *root, const MoveList *list, int start_index, int depth, Move *best_move, int *best_score, int *best_index, int thread_count) {
    ParallelRoot shared;
    pthread_t *threads;
    int created = 0;
    uint64_t nodes;

    memset(&shared, 0, sizeof(shared));
    shared.list = list;
    shared.root = *root;
    shared.depth = depth;
    shared.best_move = *best_move;
    shared.best_score = *best_score;
    shared.best_index = *best_index;
    shared.next_index = start_index;
    shared.alpha = *best_score;
    shared.nodes = 0;

    if (pthread_mutex_init(&shared.lock, NULL) != 0) {
        return search_remaining_sequential(root, list, start_index, depth, best_move, best_score, best_index);
    }

    threads = (pthread_t *)malloc((size_t)(thread_count - 1) * sizeof(*threads));
    if (!threads) {
        pthread_mutex_destroy(&shared.lock);
        return search_remaining_sequential(root, list, start_index, depth, best_move, best_score, best_index);
    }

    for (int i = 0; i < thread_count - 1; ++i) {
        if (pthread_create(&threads[created], NULL, pthreads_worker, &shared) == 0) {
            ++created;
        }
    }

    pthreads_worker(&shared);

    for (int i = 0; i < created; ++i) {
        pthread_join(threads[i], NULL);
    }

    *best_move = shared.best_move;
    *best_score = shared.best_score;
    *best_index = shared.best_index;
    nodes = shared.nodes;

    free(threads);
    pthread_mutex_destroy(&shared.lock);
    return nodes;
}

static void pthreads_search(Position *pos, int depth, SearchResult *result) {
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

static void pthreads_init(void) {
}

static void pthreads_cleanup(void) {
}

const Solver *solver_pthreads(void) {
    static const Solver solver = {
        "pthreads",
        pthreads_init,
        pthreads_cleanup,
        pthreads_search
    };

    return &solver;
}
