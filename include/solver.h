#ifndef SOLVER_H
#define SOLVER_H

#include <stdint.h>

#include "position.h"

typedef struct {
    Move best_move;
    int score;
    uint64_t nodes;
    double time_ms;
    double nps;
} SearchResult;

typedef struct {
    const char *name;
    void (*init)(void);
    void (*cleanup)(void);
    void (*search_best_move)(Position *pos, int depth, SearchResult *result);
} Solver;

const Solver *solver_baseline(void);
const Solver *solver_pthreads(void);
const Solver *solver_openmp(void);

#endif
