#include "position.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "eval.h"

const U64 FILE_A = 0x0101010101010101ULL;
const U64 FILE_H = 0x8080808080808080ULL;

static U64 knight_attack_table[64];
static U64 king_attack_table[64];
static U64 zobrist_piece_table[PIECE_N][64];
static U64 zobrist_castling_table[16];
static U64 zobrist_ep_table[64];
static U64 zobrist_side_value;
static int engine_ready = 0;

static char piece_to_char(int piece) {
    switch (piece) {
        case WP: return 'P';
        case WN: return 'N';
        case WB: return 'B';
        case WR: return 'R';
        case WQ: return 'Q';
        case WK: return 'K';
        case BP: return 'p';
        case BN: return 'n';
        case BB: return 'b';
        case BR: return 'r';
        case BQ: return 'q';
        case BK: return 'k';
        default: return '.';
    }
}

static int char_to_piece(char c) {
    switch (c) {
        case 'P': return WP;
        case 'N': return WN;
        case 'B': return WB;
        case 'R': return WR;
        case 'Q': return WQ;
        case 'K': return WK;
        case 'p': return BP;
        case 'n': return BN;
        case 'b': return BB;
        case 'r': return BR;
        case 'q': return BQ;
        case 'k': return BK;
        default: return NO_PIECE;
    }
}

static U64 splitmix64(U64 *seed) {
    U64 z = (*seed += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static void init_leaper_attacks(void) {
    static const int knight_df[8] = {+1, +2, +2, +1, -1, -2, -2, -1};
    static const int knight_dr[8] = {+2, +1, -1, -2, -2, -1, +1, +2};

    for (int sq = 0; sq < 64; ++sq) {
        int file = sq_file(sq);
        int rank = sq_rank(sq);
        U64 knight = 0;
        U64 king = 0;

        for (int i = 0; i < 8; ++i) {
            int nf = file + knight_df[i];
            int nr = rank + knight_dr[i];
            if (on_board(nf, nr)) {
                knight |= 1ULL << (nr * 8 + nf);
            }
        }

        for (int df = -1; df <= 1; ++df) {
            for (int dr = -1; dr <= 1; ++dr) {
                if (df == 0 && dr == 0) {
                    continue;
                }
                int nf = file + df;
                int nr = rank + dr;
                if (on_board(nf, nr)) {
                    king |= 1ULL << (nr * 8 + nf);
                }
            }
        }

        knight_attack_table[sq] = knight;
        king_attack_table[sq] = king;
    }
}

static void init_zobrist(void) {
    U64 seed = 0x123456789abcdef0ULL;

    for (int piece = 0; piece < PIECE_N; ++piece) {
        for (int sq = 0; sq < 64; ++sq) {
            zobrist_piece_table[piece][sq] = splitmix64(&seed);
        }
    }

    for (int i = 0; i < 16; ++i) {
        zobrist_castling_table[i] = splitmix64(&seed);
    }

    for (int i = 0; i < 64; ++i) {
        zobrist_ep_table[i] = splitmix64(&seed);
    }

    zobrist_side_value = splitmix64(&seed);
}

static int compute_material(const Position *pos) {
    int score = 0;

    for (int sq = 0; sq < 64; ++sq) {
        int piece = pos->board[sq];
        if (piece == NO_PIECE) {
            continue;
        }
        if (is_white_piece(piece)) {
            score += piece_value(piece);
        } else {
            score -= piece_value(piece);
        }
    }

    return score;
}

static U64 compute_zobrist(const Position *pos) {
    U64 key = 0;

    for (int sq = 0; sq < 64; ++sq) {
        int piece = pos->board[sq];
        if (piece != NO_PIECE) {
            key ^= zobrist_piece_table[piece][sq];
        }
    }

    key ^= zobrist_castling_table[pos->castling & 15];
    if (pos->ep_sq >= 0) {
        key ^= zobrist_ep_table[pos->ep_sq];
    }
    if (pos->side == BLACK) {
        key ^= zobrist_side_value;
    }

    return key;
}

void engine_init(void) {
    if (engine_ready) {
        return;
    }

    init_leaper_attacks();
    init_zobrist();
    engine_ready = 1;
}

void engine_cleanup(void) {
}

void sq_to_str(int sq, char out[3]) {
    out[0] = (char)('a' + sq_file(sq));
    out[1] = (char)('1' + sq_rank(sq));
    out[2] = '\0';
}

void move_to_uci(Move move, char out[6]) {
    char from_str[3];
    char to_str[3];
    int promo = m_promo(move);

    sq_to_str(m_from(move), from_str);
    sq_to_str(m_to(move), to_str);

    out[0] = from_str[0];
    out[1] = from_str[1];
    out[2] = to_str[0];
    out[3] = to_str[1];

    if (promo != NO_PIECE) {
        out[4] = (promo == WN || promo == BN) ? 'n'
              : (promo == WB || promo == BB) ? 'b'
              : (promo == WR || promo == BR) ? 'r'
              : 'q';
        out[5] = '\0';
    } else {
        out[4] = '\0';
    }
}

int str_to_sq(const char *text) {
    if (!text || !text[0] || !text[1]) {
        return -1;
    }
    if (text[0] < 'a' || text[0] > 'h' || text[1] < '1' || text[1] > '8') {
        return -1;
    }
    return (text[1] - '1') * 8 + (text[0] - 'a');
}

int set_fen(Position *pos, const char *fen) {
    char buf[256];
    char *fields[6] = {0};
    int field_count = 0;
    int rank = 7;
    int file = 0;

    memset(pos, 0, sizeof(*pos));
    for (int i = 0; i < 64; ++i) {
        pos->board[i] = NO_PIECE;
    }
    pos->side = WHITE;
    pos->castling = 0;
    pos->ep_sq = -1;

    if (!fen) {
        return 0;
    }

    strncpy(buf, fen, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tok = strtok(buf, " ");
    while (tok && field_count < 6) {
        fields[field_count++] = tok;
        tok = strtok(NULL, " ");
    }
    if (field_count < 4) {
        return 0;
    }

    for (const char *p = fields[0]; *p; ++p) {
        char c = *p;
        if (c == '/') {
            if (file != 8 || rank == 0) {
                return 0;
            }
            --rank;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8) {
                return 0;
            }
        } else {
            int piece = char_to_piece(c);
            int sq;
            if (piece == NO_PIECE || file >= 8) {
                return 0;
            }
            sq = rank * 8 + file;
            pos->bb[piece] |= 1ULL << sq;
            pos->board[sq] = piece;
            ++file;
        }
    }

    if (rank != 0 || file != 8) {
        return 0;
    }

    if (fields[1][0] == 'w' && fields[1][1] == '\0') {
        pos->side = WHITE;
    } else if (fields[1][0] == 'b' && fields[1][1] == '\0') {
        pos->side = BLACK;
    } else {
        return 0;
    }

    if (!(fields[2][0] == '-' && fields[2][1] == '\0')) {
        for (const char *p = fields[2]; *p; ++p) {
            if (*p == 'K') pos->castling |= CR_WK;
            else if (*p == 'Q') pos->castling |= CR_WQ;
            else if (*p == 'k') pos->castling |= CR_BK;
            else if (*p == 'q') pos->castling |= CR_BQ;
            else return 0;
        }
    }

    if (!(fields[3][0] == '-' && fields[3][1] == '\0')) {
        pos->ep_sq = str_to_sq(fields[3]);
        if (pos->ep_sq < 0) {
            return 0;
        }
    }

    update_occ(pos);
    if (popcount_u64(pos->bb[WK]) != 1 || popcount_u64(pos->bb[BK]) != 1) {
        return 0;
    }

    pos->material = compute_material(pos);
    pos->zobrist = compute_zobrist(pos);
    return 1;
}

void print_board(const Position *pos) {
    for (int rank = 7; rank >= 0; --rank) {
        printf("%d  ", rank + 1);
        for (int file = 0; file < 8; ++file) {
            printf("%c ", piece_to_char(pos->board[rank * 8 + file]));
        }
        printf("\n");
    }
    printf("\n   a b c d e f g h\n");
    printf("Side: %c  Castling: ", pos->side == WHITE ? 'w' : 'b');
    if (pos->castling & CR_WK) printf("K");
    if (pos->castling & CR_WQ) printf("Q");
    if (pos->castling & CR_BK) printf("k");
    if (pos->castling & CR_BQ) printf("q");
    if (!pos->castling) printf("-");
    if (pos->ep_sq >= 0) {
        char ep[3];
        sq_to_str(pos->ep_sq, ep);
        printf("  EP: %s\n", ep);
    } else {
        printf("  EP: -\n");
    }
}

double now_ms(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

void update_occ(Position *pos) {
    pos->occ[WHITE] = pos->bb[WP] | pos->bb[WN] | pos->bb[WB] | pos->bb[WR] | pos->bb[WQ] | pos->bb[WK];
    pos->occ[BLACK] = pos->bb[BP] | pos->bb[BN] | pos->bb[BB] | pos->bb[BR] | pos->bb[BQ] | pos->bb[BK];
    pos->occ[2] = pos->occ[WHITE] | pos->occ[BLACK];
}

int king_sq(const Position *pos, int color) {
    U64 kings = (color == WHITE) ? pos->bb[WK] : pos->bb[BK];
    return kings ? lsb_index(kings) : -1;
}

void set_piece(Position *pos, int piece, int sq) {
    pos->bb[piece] |= 1ULL << sq;
    pos->board[sq] = piece;
    pos->zobrist ^= zobrist_piece_table[piece][sq];
}

void clear_piece(Position *pos, int piece, int sq) {
    pos->bb[piece] &= ~(1ULL << sq);
    pos->board[sq] = NO_PIECE;
    pos->zobrist ^= zobrist_piece_table[piece][sq];
}

U64 knight_attacks(int sq) {
    return knight_attack_table[sq];
}

U64 king_attacks(int sq) {
    return king_attack_table[sq];
}

U64 zobrist_piece_key(int piece, int sq) {
    return zobrist_piece_table[piece][sq];
}

U64 zobrist_castling_key(int castling) {
    return zobrist_castling_table[castling & 15];
}

U64 zobrist_ep_key(int sq) {
    return zobrist_ep_table[sq];
}

U64 zobrist_side_key(void) {
    return zobrist_side_value;
}
