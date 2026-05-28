#!/usr/bin/env bash
set -e

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
REPEATS="${REPEATS:-3}"
THREADS="${CHESS_THREADS:-4}"
DEPTH="${DEPTH:-8}"

SOLVERS="baseline pthreads openmp"

POSITIONS=(
    "Start position|rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
    "Kiwipete|r3k2r/p1ppqpb1/bn2pnp1/2P5/1p2P3/2N2N2/PPQPBPPP/R3K2R w KQkq - 0 1"
    "CPW position 3|rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"
    "CPW endgame|8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"
    "CPW middlegame|r4rk1/1pp1qppp/p1np1n2/4p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10"
    "Castling stress|r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"
)

value_of() {
    local key="$1"
    shift

    for word in "$@"; do
        case "$word" in
            "$key="*)
                printf "%s\n" "${word#*=}"
                return
                ;;
        esac
    done
}

add_ms() {
    awk -v a="$1" -v b="$2" 'BEGIN { printf "%.3f", a + b }'
}

avg_ms() {
    awk -v total="$1" -v count="$2" 'BEGIN { printf "%.3f", total / count }'
}

make -C "$ROOT_DIR" >/dev/null

printf "CHESS_THREADS=%s  REPEATS=%s  DEPTH=%s\n\n" "$THREADS" "$REPEATS" "$DEPTH"
printf "%-18s %-9s %5s %10s %8s %8s %12s\n" "position" "solver" "depth" "avg_ms" "best" "score" "nodes"
printf "%-18s %-9s %5s %10s %8s %8s %12s\n" "--------" "------" "-----" "------" "----" "-----" "-----"

for position in "${POSITIONS[@]}"; do
    IFS="|" read -r name fen <<< "$position"

    for solver in $SOLVERS; do
        total="0.000"

        for ((run = 1; run <= REPEATS; ++run)); do
            output="$(CHESS_THREADS="$THREADS" "$ROOT_DIR/chess" --solver "$solver" --fen "$fen" --depth "$DEPTH")"
            total="$(add_ms "$total" "$(value_of time_ms $output)")"
        done

        bestmove="$(value_of bestmove $output)"
        score="$(value_of score $output)"
        nodes="$(value_of nodes $output)"

        printf "%-18s %-9s %5s %10s %8s %8s %12s\n" "$name" "$solver" "$DEPTH" "$(avg_ms "$total" "$REPEATS")" "$bestmove" "$score" "$nodes"
    done
done
