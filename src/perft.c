#include "perft.h"

#include "makemove.h"
#include "movegen.h"

uint64_t perft(Position *pos, int depth) {
    MoveList list;
    uint64_t nodes = 0;

    if (depth == 0) {
        return 1;
    }

    gen_moves(pos, &list, 0);
    for (int i = 0; i < list.size; ++i) {
        State state;
        Move move = list.moves[i];
        if (!make_move(pos, move, &state)) {
            continue;
        }
        nodes += perft(pos, depth - 1);
        unmake_move(pos, move, &state);
    }

    return nodes;
}
