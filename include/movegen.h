#ifndef MOVEGEN_H
#define MOVEGEN_H

#include "position.h"

int square_attacked(const Position *pos, int sq, int by_color);
int in_check(const Position *pos, int color);
void gen_moves(const Position *pos, MoveList *list, int captures_only);

#endif
