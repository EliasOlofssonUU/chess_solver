#include "eval.h"

static const int piece_values[PIECE_N] = {
    100, 320, 330, 500, 900, 0,
    100, 320, 330, 500, 900, 0
};

int piece_value(int piece) {
    return (piece >= 0 && piece < PIECE_N) ? piece_values[piece] : 0;
}

int eval_material(const Position *pos) {
    return (pos->side == WHITE) ? pos->material : -pos->material;
}

static int mvv_lva_score(int captured, int attacker) {
    return piece_value(captured) * 16 - piece_value(attacker);
}

int move_score(Move move) {
    int flags = m_flags(move);
    int score = 0;

    if (flags & PROMOTION) {
        score += 8000 + piece_value(m_promo(move));
    }
    if (flags & CAPTURE) {
        score += 4000 + mvv_lva_score(m_captured(move), m_piece(move));
    }
    if (flags & CASTLING) {
        score += 100;
    }

    return score;
}

void order_moves(MoveList *list, Move hash_move) {
    for (int i = 0; i < list->size; ++i) {
        list->score[i] = move_score(list->moves[i]);
        if (hash_move && list->moves[i] == hash_move) {
            list->score[i] += 20000;
        }
    }

    for (int i = 1; i < list->size; ++i) {
        Move key_move = list->moves[i];
        int key_score = list->score[i];
        int j = i - 1;

        while (j >= 0 && list->score[j] < key_score) {
            list->moves[j + 1] = list->moves[j];
            list->score[j + 1] = list->score[j];
            --j;
        }

        list->moves[j + 1] = key_move;
        list->score[j + 1] = key_score;
    }
}
