#include "makemove.h"

#include "eval.h"
#include "movegen.h"

int make_move(Position *pos, Move move, State *state) {
    int from = m_from(move);
    int to = m_to(move);
    int piece = m_piece(move);
    int captured = m_captured(move);
    int promo = m_promo(move);
    int flags = m_flags(move);

    state->castling = pos->castling;
    state->ep_sq = pos->ep_sq;
    state->captured = captured;
    state->material = pos->material;
    state->zobrist = pos->zobrist;

    if (pos->ep_sq >= 0) {
        pos->zobrist ^= zobrist_ep_key(pos->ep_sq);
    }
    pos->zobrist ^= zobrist_castling_key(pos->castling);
    pos->ep_sq = -1;

    clear_piece(pos, piece, from);

    if (flags & ENPASSANT) {
        int cap_sq = (pos->side == WHITE) ? (to - 8) : (to + 8);
        int cap_piece = (pos->side == WHITE) ? BP : WP;
        clear_piece(pos, cap_piece, cap_sq);
        pos->material += (pos->side == WHITE) ? piece_value(cap_piece) : -piece_value(cap_piece);
    } else if (captured != NO_PIECE) {
        clear_piece(pos, captured, to);
        pos->material += is_white_piece(captured) ? -piece_value(captured) : piece_value(captured);
    }

    if (flags & PROMOTION) {
        set_piece(pos, promo, to);
        if (pos->side == WHITE) {
            pos->material += piece_value(promo) - piece_value(WP);
        } else {
            pos->material -= piece_value(promo) - piece_value(BP);
        }
    } else {
        set_piece(pos, piece, to);
    }

    if (flags & DOUBLEPUSH) {
        pos->ep_sq = (pos->side == WHITE) ? (to - 8) : (to + 8);
    }

    if (flags & CASTLING) {
        if (piece == WK) {
            if (to == 6) {
                clear_piece(pos, WR, 7);
                set_piece(pos, WR, 5);
            } else {
                clear_piece(pos, WR, 0);
                set_piece(pos, WR, 3);
            }
        } else if (piece == BK) {
            if (to == 62) {
                clear_piece(pos, BR, 63);
                set_piece(pos, BR, 61);
            } else {
                clear_piece(pos, BR, 56);
                set_piece(pos, BR, 59);
            }
        }
    }

    if (piece == WK) pos->castling &= ~(CR_WK | CR_WQ);
    if (piece == BK) pos->castling &= ~(CR_BK | CR_BQ);
    if (piece == WR) {
        if (from == 7) pos->castling &= ~CR_WK;
        else if (from == 0) pos->castling &= ~CR_WQ;
    }
    if (piece == BR) {
        if (from == 63) pos->castling &= ~CR_BK;
        else if (from == 56) pos->castling &= ~CR_BQ;
    }
    if (captured == WR) {
        if (to == 7) pos->castling &= ~CR_WK;
        else if (to == 0) pos->castling &= ~CR_WQ;
    }
    if (captured == BR) {
        if (to == 63) pos->castling &= ~CR_BK;
        else if (to == 56) pos->castling &= ~CR_BQ;
    }

    pos->zobrist ^= zobrist_castling_key(pos->castling);
    if (pos->ep_sq >= 0) {
        pos->zobrist ^= zobrist_ep_key(pos->ep_sq);
    }
    pos->side ^= 1;
    pos->zobrist ^= zobrist_side_key();
    update_occ(pos);

    if (in_check(pos, pos->side ^ 1)) {
        unmake_move(pos, move, state);
        return 0;
    }

    return 1;
}

void unmake_move(Position *pos, Move move, const State *state) {
    int from = m_from(move);
    int to = m_to(move);
    int piece = m_piece(move);
    int captured = state->captured;
    int promo = m_promo(move);
    int flags = m_flags(move);

    pos->side ^= 1;
    pos->castling = state->castling;
    pos->ep_sq = state->ep_sq;
    pos->material = state->material;
    pos->zobrist = state->zobrist;

    if (flags & PROMOTION) {
        clear_piece(pos, promo, to);
        set_piece(pos, piece, from);
    } else {
        clear_piece(pos, piece, to);
        set_piece(pos, piece, from);
    }

    if (flags & CASTLING) {
        if (piece == WK) {
            if (to == 6) {
                clear_piece(pos, WR, 5);
                set_piece(pos, WR, 7);
            } else {
                clear_piece(pos, WR, 3);
                set_piece(pos, WR, 0);
            }
        } else if (piece == BK) {
            if (to == 62) {
                clear_piece(pos, BR, 61);
                set_piece(pos, BR, 63);
            } else {
                clear_piece(pos, BR, 59);
                set_piece(pos, BR, 56);
            }
        }
    }

    if (flags & ENPASSANT) {
        int cap_sq = (pos->side == WHITE) ? (to - 8) : (to + 8);
        set_piece(pos, pos->side == WHITE ? BP : WP, cap_sq);
    } else if (captured != NO_PIECE) {
        set_piece(pos, captured, to);
    }

    update_occ(pos);
    pos->material = state->material;
    pos->zobrist = state->zobrist;
}
