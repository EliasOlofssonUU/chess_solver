#ifndef POSITION_H
#define POSITION_H

#include <stdint.h>

typedef uint64_t U64;
typedef uint32_t Move;

#define MOVE_NONE ((Move)0)

enum { WHITE = 0, BLACK = 1 };

enum Piece {
    WP = 0, WN, WB, WR, WQ, WK,
    BP, BN, BB, BR, BQ, BK,
    NO_PIECE = 15
};

enum {
    PIECE_N = 12,
    MAX_MOVES = 256,
    CR_WK = 1,
    CR_WQ = 2,
    CR_BK = 4,
    CR_BQ = 8,
    INF = 1000000,
    CHECKMATE_SCORE = 100000
};

enum MoveFlags {
    QUIET      = 0,
    CAPTURE    = 1 << 0,
    PROMOTION  = 1 << 1,
    ENPASSANT  = 1 << 2,
    CASTLING   = 1 << 3,
    DOUBLEPUSH = 1 << 4
};

typedef struct {
    int castling;
    int ep_sq;
    int captured;
    int material;
    U64 zobrist;
} State;

typedef struct {
    U64 bb[PIECE_N];
    U64 occ[3];
    int board[64];
    int side;
    int castling;
    int ep_sq;
    int material;
    U64 zobrist;
} Position;

typedef struct {
    Move moves[MAX_MOVES];
    int score[MAX_MOVES];
    int size;
} MoveList;

extern const U64 FILE_A;
extern const U64 FILE_H;

static inline int popcount_u64(U64 x) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(x);
#else
    int count = 0;
    while (x) {
        x &= x - 1;
        ++count;
    }
    return count;
#endif
}

static inline int lsb_index(U64 x) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(x);
#else
    int index = 0;
    while (((x >> index) & 1ULL) == 0ULL) {
        ++index;
    }
    return index;
#endif
}

static inline U64 pop_lsb(U64 *bb) {
    U64 lsb = *bb & (~*bb + 1ULL);
    *bb ^= lsb;
    return lsb;
}

static inline int sq_file(int sq) { return sq & 7; }
static inline int sq_rank(int sq) { return sq >> 3; }
static inline int on_board(int file, int rank) { return file >= 0 && file < 8 && rank >= 0 && rank < 8; }
static inline int is_white_piece(int piece) { return piece >= WP && piece <= WK; }
static inline int is_black_piece(int piece) { return piece >= BP && piece <= BK; }
static inline int piece_on(const Position *pos, int sq) { return pos->board[sq]; }

static inline Move make_move_u32(int from, int to, int piece, int captured, int promo, int flags) {
    return (Move)from
        | ((Move)to << 6)
        | ((Move)piece << 12)
        | ((Move)captured << 16)
        | ((Move)promo << 20)
        | ((Move)flags << 24);
}

static inline int m_from(Move move) { return (int)(move & 63u); }
static inline int m_to(Move move) { return (int)((move >> 6) & 63u); }
static inline int m_piece(Move move) { return (int)((move >> 12) & 15u); }
static inline int m_captured(Move move) { return (int)((move >> 16) & 15u); }
static inline int m_promo(Move move) { return (int)((move >> 20) & 15u); }
static inline int m_flags(Move move) { return (int)((move >> 24) & 255u); }

void engine_init(void);
void engine_cleanup(void);

void sq_to_str(int sq, char out[3]);
void move_to_uci(Move move, char out[6]);
int str_to_sq(const char *text);
int set_fen(Position *pos, const char *fen);
void print_board(const Position *pos);
double now_ms(void);

void update_occ(Position *pos);
int king_sq(const Position *pos, int color);
void set_piece(Position *pos, int piece, int sq);
void clear_piece(Position *pos, int piece, int sq);

U64 knight_attacks(int sq);
U64 king_attacks(int sq);
U64 zobrist_piece_key(int piece, int sq);
U64 zobrist_castling_key(int castling);
U64 zobrist_ep_key(int sq);
U64 zobrist_side_key(void);

#endif
