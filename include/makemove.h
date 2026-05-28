#ifndef MAKEMOVE_H
#define MAKEMOVE_H

#include "position.h"

int make_move(Position *pos, Move move, State *state);
void unmake_move(Position *pos, Move move, const State *state);

#endif
