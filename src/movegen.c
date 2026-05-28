#include "movegen.h"

static void movelist_clear(MoveList *list) {
    list->size = 0;
}

static void movelist_push(MoveList *list, Move move) {
    int index = list->size++;
    list->moves[index] = move;
    list->score[index] = 0;
}

static U64 ray_attacks(int from, int df, int dr, U64 occ_all) {
    U64 attacks = 0;
    int file = sq_file(from);
    int rank = sq_rank(from);

    while (1) {
        int sq;
        file += df;
        rank += dr;
        if (!on_board(file, rank)) {
            break;
        }
        sq = rank * 8 + file;
        attacks |= 1ULL << sq;
        if (occ_all & (1ULL << sq)) {
            break;
        }
    }

    return attacks;
}

static U64 bishop_attacks(int from, U64 occ_all) {
    return ray_attacks(from, +1, +1, occ_all)
         | ray_attacks(from, -1, +1, occ_all)
         | ray_attacks(from, +1, -1, occ_all)
         | ray_attacks(from, -1, -1, occ_all);
}

static U64 rook_attacks(int from, U64 occ_all) {
    return ray_attacks(from, +1, 0, occ_all)
         | ray_attacks(from, -1, 0, occ_all)
         | ray_attacks(from, 0, +1, occ_all)
         | ray_attacks(from, 0, -1, occ_all);
}

static U64 queen_attacks(int from, U64 occ_all) {
    return bishop_attacks(from, occ_all) | rook_attacks(from, occ_all);
}

int square_attacked(const Position *pos, int sq, int by_color) {
    U64 sq_bb = 1ULL << sq;
    U64 occ_all = pos->occ[2];
    U64 bishops;
    U64 rooks;
    U64 queens;

    if (by_color == WHITE) {
        U64 pawn_attacks = ((pos->bb[WP] << 7) & ~FILE_H) | ((pos->bb[WP] << 9) & ~FILE_A);
        if (pawn_attacks & sq_bb) {
            return 1;
        }
    } else {
        U64 pawn_attacks = ((pos->bb[BP] >> 7) & ~FILE_A) | ((pos->bb[BP] >> 9) & ~FILE_H);
        if (pawn_attacks & sq_bb) {
            return 1;
        }
    }

    if (knight_attacks(sq) & ((by_color == WHITE) ? pos->bb[WN] : pos->bb[BN])) {
        return 1;
    }
    if (king_attacks(sq) & ((by_color == WHITE) ? pos->bb[WK] : pos->bb[BK])) {
        return 1;
    }

    bishops = (by_color == WHITE) ? pos->bb[WB] : pos->bb[BB];
    rooks = (by_color == WHITE) ? pos->bb[WR] : pos->bb[BR];
    queens = (by_color == WHITE) ? pos->bb[WQ] : pos->bb[BQ];

    if (bishop_attacks(sq, occ_all) & (bishops | queens)) {
        return 1;
    }
    if (rook_attacks(sq, occ_all) & (rooks | queens)) {
        return 1;
    }

    return 0;
}

int in_check(const Position *pos, int color) {
    int sq = king_sq(pos, color);
    return sq >= 0 ? square_attacked(pos, sq, color ^ 1) : 0;
}

static void gen_pawn_moves(const Position *pos, MoveList *list, int captures_only) {
    int us = pos->side;
    U64 occ_all = pos->occ[2];
    U64 temp;

    if (us == WHITE) {
        U64 pawns = pos->bb[WP];
        U64 their_occ = pos->occ[BLACK];

        if (!captures_only) {
            U64 single = (pawns << 8) & ~occ_all;
            U64 promotions = single & 0xFF00000000000000ULL;
            U64 quiet = single & ~0xFF00000000000000ULL;
            temp = quiet;
            while (temp) {
                int to = lsb_index(pop_lsb(&temp));
                movelist_push(list, make_move_u32(to - 8, to, WP, NO_PIECE, NO_PIECE, QUIET));
            }
            temp = promotions;
            while (temp) {
                int to = lsb_index(pop_lsb(&temp));
                int from = to - 8;
                movelist_push(list, make_move_u32(from, to, WP, NO_PIECE, WQ, PROMOTION));
                movelist_push(list, make_move_u32(from, to, WP, NO_PIECE, WR, PROMOTION));
                movelist_push(list, make_move_u32(from, to, WP, NO_PIECE, WB, PROMOTION));
                movelist_push(list, make_move_u32(from, to, WP, NO_PIECE, WN, PROMOTION));
            }

            temp = (((pawns & 0x000000000000FF00ULL) << 8) & ~occ_all);
            temp = (temp << 8) & ~occ_all;
            while (temp) {
                int to = lsb_index(pop_lsb(&temp));
                movelist_push(list, make_move_u32(to - 16, to, WP, NO_PIECE, NO_PIECE, DOUBLEPUSH));
            }
        }

        {
            U64 left = (pawns << 7) & their_occ & ~FILE_H;
            U64 right = (pawns << 9) & their_occ & ~FILE_A;
            U64 promo_left = left & 0xFF00000000000000ULL;
            U64 promo_right = right & 0xFF00000000000000ULL;
            U64 normal_left = left & ~0xFF00000000000000ULL;
            U64 normal_right = right & ~0xFF00000000000000ULL;

            temp = normal_left;
            while (temp) {
                int to = lsb_index(pop_lsb(&temp));
                movelist_push(list, make_move_u32(to - 7, to, WP, piece_on(pos, to), NO_PIECE, CAPTURE));
            }
            temp = normal_right;
            while (temp) {
                int to = lsb_index(pop_lsb(&temp));
                movelist_push(list, make_move_u32(to - 9, to, WP, piece_on(pos, to), NO_PIECE, CAPTURE));
            }
            temp = promo_left;
            while (temp) {
                int to = lsb_index(pop_lsb(&temp));
                int from = to - 7;
                int cap = piece_on(pos, to);
                movelist_push(list, make_move_u32(from, to, WP, cap, WQ, CAPTURE | PROMOTION));
                movelist_push(list, make_move_u32(from, to, WP, cap, WR, CAPTURE | PROMOTION));
                movelist_push(list, make_move_u32(from, to, WP, cap, WB, CAPTURE | PROMOTION));
                movelist_push(list, make_move_u32(from, to, WP, cap, WN, CAPTURE | PROMOTION));
            }
            temp = promo_right;
            while (temp) {
                int to = lsb_index(pop_lsb(&temp));
                int from = to - 9;
                int cap = piece_on(pos, to);
                movelist_push(list, make_move_u32(from, to, WP, cap, WQ, CAPTURE | PROMOTION));
                movelist_push(list, make_move_u32(from, to, WP, cap, WR, CAPTURE | PROMOTION));
                movelist_push(list, make_move_u32(from, to, WP, cap, WB, CAPTURE | PROMOTION));
                movelist_push(list, make_move_u32(from, to, WP, cap, WN, CAPTURE | PROMOTION));
            }
        }

        if (pos->ep_sq >= 0) {
            U64 ep_mask = 1ULL << pos->ep_sq;
            U64 from_left = (ep_mask >> 7) & pawns & ~FILE_A;
            U64 from_right = (ep_mask >> 9) & pawns & ~FILE_H;
            if (from_left) {
                movelist_push(list, make_move_u32(lsb_index(from_left), pos->ep_sq, WP, BP, NO_PIECE, CAPTURE | ENPASSANT));
            }
            if (from_right) {
                movelist_push(list, make_move_u32(lsb_index(from_right), pos->ep_sq, WP, BP, NO_PIECE, CAPTURE | ENPASSANT));
            }
        }
    } else {
        U64 pawns = pos->bb[BP];
        U64 their_occ = pos->occ[WHITE];

        if (!captures_only) {
            U64 single = (pawns >> 8) & ~occ_all;
            U64 promotions = single & 0x00000000000000FFULL;
            U64 quiet = single & ~0x00000000000000FFULL;
            temp = quiet;
            while (temp) {
                int to = lsb_index(pop_lsb(&temp));
                movelist_push(list, make_move_u32(to + 8, to, BP, NO_PIECE, NO_PIECE, QUIET));
            }
            temp = promotions;
            while (temp) {
                int to = lsb_index(pop_lsb(&temp));
                int from = to + 8;
                movelist_push(list, make_move_u32(from, to, BP, NO_PIECE, BQ, PROMOTION));
                movelist_push(list, make_move_u32(from, to, BP, NO_PIECE, BR, PROMOTION));
                movelist_push(list, make_move_u32(from, to, BP, NO_PIECE, BB, PROMOTION));
                movelist_push(list, make_move_u32(from, to, BP, NO_PIECE, BN, PROMOTION));
            }

            temp = (((pawns & 0x00FF000000000000ULL) >> 8) & ~occ_all);
            temp = (temp >> 8) & ~occ_all;
            while (temp) {
                int to = lsb_index(pop_lsb(&temp));
                movelist_push(list, make_move_u32(to + 16, to, BP, NO_PIECE, NO_PIECE, DOUBLEPUSH));
            }
        }

        {
            U64 left = (pawns >> 9) & their_occ & ~FILE_H;
            U64 right = (pawns >> 7) & their_occ & ~FILE_A;
            U64 promo_left = left & 0x00000000000000FFULL;
            U64 promo_right = right & 0x00000000000000FFULL;
            U64 normal_left = left & ~0x00000000000000FFULL;
            U64 normal_right = right & ~0x00000000000000FFULL;

            temp = normal_left;
            while (temp) {
                int to = lsb_index(pop_lsb(&temp));
                movelist_push(list, make_move_u32(to + 9, to, BP, piece_on(pos, to), NO_PIECE, CAPTURE));
            }
            temp = normal_right;
            while (temp) {
                int to = lsb_index(pop_lsb(&temp));
                movelist_push(list, make_move_u32(to + 7, to, BP, piece_on(pos, to), NO_PIECE, CAPTURE));
            }
            temp = promo_left;
            while (temp) {
                int to = lsb_index(pop_lsb(&temp));
                int from = to + 9;
                int cap = piece_on(pos, to);
                movelist_push(list, make_move_u32(from, to, BP, cap, BQ, CAPTURE | PROMOTION));
                movelist_push(list, make_move_u32(from, to, BP, cap, BR, CAPTURE | PROMOTION));
                movelist_push(list, make_move_u32(from, to, BP, cap, BB, CAPTURE | PROMOTION));
                movelist_push(list, make_move_u32(from, to, BP, cap, BN, CAPTURE | PROMOTION));
            }
            temp = promo_right;
            while (temp) {
                int to = lsb_index(pop_lsb(&temp));
                int from = to + 7;
                int cap = piece_on(pos, to);
                movelist_push(list, make_move_u32(from, to, BP, cap, BQ, CAPTURE | PROMOTION));
                movelist_push(list, make_move_u32(from, to, BP, cap, BR, CAPTURE | PROMOTION));
                movelist_push(list, make_move_u32(from, to, BP, cap, BB, CAPTURE | PROMOTION));
                movelist_push(list, make_move_u32(from, to, BP, cap, BN, CAPTURE | PROMOTION));
            }
        }

        if (pos->ep_sq >= 0) {
            U64 ep_mask = 1ULL << pos->ep_sq;
            U64 from_left = (ep_mask << 9) & pawns & ~FILE_A;
            U64 from_right = (ep_mask << 7) & pawns & ~FILE_H;
            if (from_left) {
                movelist_push(list, make_move_u32(lsb_index(from_left), pos->ep_sq, BP, WP, NO_PIECE, CAPTURE | ENPASSANT));
            }
            if (from_right) {
                movelist_push(list, make_move_u32(lsb_index(from_right), pos->ep_sq, BP, WP, NO_PIECE, CAPTURE | ENPASSANT));
            }
        }
    }
}

static void gen_knight_moves(const Position *pos, MoveList *list, int captures_only) {
    int us = pos->side;
    U64 knights = (us == WHITE) ? pos->bb[WN] : pos->bb[BN];
    U64 us_occ = pos->occ[us];
    U64 them_occ = pos->occ[us ^ 1];
    U64 temp = knights;

    while (temp) {
        int from = lsb_index(pop_lsb(&temp));
        U64 attacks = knight_attacks(from) & ~us_occ;
        if (captures_only) {
            attacks &= them_occ;
        }

        while (attacks) {
            int to = lsb_index(pop_lsb(&attacks));
            int cap = (them_occ & (1ULL << to)) ? piece_on(pos, to) : NO_PIECE;
            int piece = (us == WHITE) ? WN : BN;
            int flags = (cap != NO_PIECE) ? CAPTURE : QUIET;
            movelist_push(list, make_move_u32(from, to, piece, cap, NO_PIECE, flags));
        }
    }
}

static void gen_slider_piece_set(const Position *pos, MoveList *list, int piece, U64 pieces, U64 (*attacks_fn)(int, U64), int captures_only) {
    int us = pos->side;
    U64 us_occ = pos->occ[us];
    U64 them_occ = pos->occ[us ^ 1];
    U64 occ_all = pos->occ[2];
    U64 temp = pieces;

    while (temp) {
        int from = lsb_index(pop_lsb(&temp));
        U64 attacks = attacks_fn(from, occ_all) & ~us_occ;
        if (captures_only) {
            attacks &= them_occ;
        }

        while (attacks) {
            int to = lsb_index(pop_lsb(&attacks));
            int cap = (them_occ & (1ULL << to)) ? piece_on(pos, to) : NO_PIECE;
            int flags = (cap != NO_PIECE) ? CAPTURE : QUIET;
            movelist_push(list, make_move_u32(from, to, piece, cap, NO_PIECE, flags));
        }
    }
}

static void gen_slider_moves(const Position *pos, MoveList *list, int captures_only) {
    if (pos->side == WHITE) {
        gen_slider_piece_set(pos, list, WB, pos->bb[WB], bishop_attacks, captures_only);
        gen_slider_piece_set(pos, list, WR, pos->bb[WR], rook_attacks, captures_only);
        gen_slider_piece_set(pos, list, WQ, pos->bb[WQ], queen_attacks, captures_only);
    } else {
        gen_slider_piece_set(pos, list, BB, pos->bb[BB], bishop_attacks, captures_only);
        gen_slider_piece_set(pos, list, BR, pos->bb[BR], rook_attacks, captures_only);
        gen_slider_piece_set(pos, list, BQ, pos->bb[BQ], queen_attacks, captures_only);
    }
}

static void gen_king_moves(const Position *pos, MoveList *list, int captures_only) {
    int us = pos->side;
    int from = king_sq(pos, us);
    U64 us_occ = pos->occ[us];
    U64 them_occ = pos->occ[us ^ 1];
    U64 occ_all = pos->occ[2];
    U64 attacks;

    if (from < 0) {
        return;
    }

    attacks = king_attacks(from) & ~us_occ;
    if (captures_only) {
        attacks &= them_occ;
    }

    while (attacks) {
        int to = lsb_index(pop_lsb(&attacks));
        int cap = (them_occ & (1ULL << to)) ? piece_on(pos, to) : NO_PIECE;
        int piece = (us == WHITE) ? WK : BK;
        int flags = (cap != NO_PIECE) ? CAPTURE : QUIET;
        movelist_push(list, make_move_u32(from, to, piece, cap, NO_PIECE, flags));
    }

    if (captures_only) {
        return;
    }

    if (us == WHITE) {
        if ((pos->castling & CR_WK)
            && (pos->bb[WR] & (1ULL << 7))
            && ((occ_all & ((1ULL << 5) | (1ULL << 6))) == 0)
            && !square_attacked(pos, 4, BLACK)
            && !square_attacked(pos, 5, BLACK)
            && !square_attacked(pos, 6, BLACK)) {
            movelist_push(list, make_move_u32(4, 6, WK, NO_PIECE, NO_PIECE, CASTLING));
        }
        if ((pos->castling & CR_WQ)
            && (pos->bb[WR] & (1ULL << 0))
            && ((occ_all & ((1ULL << 1) | (1ULL << 2) | (1ULL << 3))) == 0)
            && !square_attacked(pos, 4, BLACK)
            && !square_attacked(pos, 3, BLACK)
            && !square_attacked(pos, 2, BLACK)) {
            movelist_push(list, make_move_u32(4, 2, WK, NO_PIECE, NO_PIECE, CASTLING));
        }
    } else {
        if ((pos->castling & CR_BK)
            && (pos->bb[BR] & (1ULL << 63))
            && ((occ_all & ((1ULL << 61) | (1ULL << 62))) == 0)
            && !square_attacked(pos, 60, WHITE)
            && !square_attacked(pos, 61, WHITE)
            && !square_attacked(pos, 62, WHITE)) {
            movelist_push(list, make_move_u32(60, 62, BK, NO_PIECE, NO_PIECE, CASTLING));
        }
        if ((pos->castling & CR_BQ)
            && (pos->bb[BR] & (1ULL << 56))
            && ((occ_all & ((1ULL << 57) | (1ULL << 58) | (1ULL << 59))) == 0)
            && !square_attacked(pos, 60, WHITE)
            && !square_attacked(pos, 59, WHITE)
            && !square_attacked(pos, 58, WHITE)) {
            movelist_push(list, make_move_u32(60, 58, BK, NO_PIECE, NO_PIECE, CASTLING));
        }
    }
}

void gen_moves(const Position *pos, MoveList *list, int captures_only) {
    movelist_clear(list);
    gen_pawn_moves(pos, list, captures_only);
    gen_knight_moves(pos, list, captures_only);
    gen_slider_moves(pos, list, captures_only);
    gen_king_moves(pos, list, captures_only);
}
