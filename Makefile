CC := gcc
CFLAGS := -O3 -std=c11 -Wall -Wextra -Wpedantic -Iinclude -pthread -fopenmp
LDFLAGS := -pthread -fopenmp
BUILD_DIR := build

SRC := \
	src/main.c \
	src/position.c \
	src/movegen.c \
	src/makemove.c \
	src/eval.c \
	src/perft.c \
	src/solver_baseline.c \
	src/solver_Pthreads.c \
	src/solver_OpenMP.c

OBJ := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC))

.PHONY: all baseline pthreads openmp clean

all: chess

baseline: chess

pthreads: chess

openmp: chess

chess: $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) chess
