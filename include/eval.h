#ifndef EVAL_H
#define EVAL_H

#include "position.h"

int piece_value(int piece);
int eval_material(const Position *pos);
int move_score(Move move);
void order_moves(MoveList *list, Move hash_move);

#endif
