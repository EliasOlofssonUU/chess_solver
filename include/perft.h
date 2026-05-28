#ifndef PERFT_H
#define PERFT_H

#include <stdint.h>

#include "position.h"

uint64_t perft(Position *pos, int depth);

#endif
