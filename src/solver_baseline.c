#include "solver.h"

#include <string.h>

#include "eval.h"
#include "makemove.h"
#include "movegen.h"

typedef struct {
    uint64_t nodes;
} BaselineContext;

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

static int baseline_negamax(Position *pos, int depth, int alpha, int beta, BaselineContext *ctx) {
    MoveList list;
    int best = -INF;
    int any_legal = 0;

    ++ctx->nodes;
    if (depth == 0) {
        return eval_material(pos);
    }

    gen_moves(pos, &list, 0);
    order_moves(&list, 0);

    for (int i = 0; i < list.size; ++i) {
        State state;
        Move move = list.moves[i];
        int score;

        if (!make_move(pos, move, &state)) {
            continue;
        }
        any_legal = 1;
        score = -baseline_negamax(pos, depth - 1, -beta, -alpha, ctx);
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

static void baseline_search(Position *pos, int depth, SearchResult *result) {
    BaselineContext ctx = {0};
    MoveList list;
    Move best_move = MOVE_NONE;
    int best_score = -INF;
    int alpha = -INF;
    int legal_count = 0;
    double start_ms = now_ms();

    gen_moves(pos, &list, 0);
    order_moves(&list, 0);

    for (int i = 0; i < list.size; ++i) {
        State state;
        Move move = list.moves[i];
        int score;

        if (!make_move(pos, move, &state)) {
            continue;
        }
        ++legal_count;
        score = -baseline_negamax(pos, depth - 1, -INF, -alpha, &ctx);
        unmake_move(pos, move, &state);

        if (score > best_score) {
            best_score = score;
            best_move = move;
        }
        if (score > alpha) {
            alpha = score;
        }
    }

    if (!legal_count) {
        finish_result(result, MOVE_NONE, terminal_score(pos), ctx.nodes, start_ms);
        return;
    }

    finish_result(result, best_move, best_score, ctx.nodes, start_ms);
}

static void baseline_init(void) {
}

static void baseline_cleanup(void) {
}

const Solver *solver_baseline(void) {
    static const Solver solver = {
        "baseline",
        baseline_init,
        baseline_cleanup,
        baseline_search
    };

    return &solver;
}
