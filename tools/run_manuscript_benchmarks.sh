#!/usr/bin/env bash
# Run the measurement matrix reported in the H-BRICK manuscript.
#
# Single-pair jobs (results.csv):
#   HBrickSkipLift at every b in {4,8,16,24,32,48,64,96} and g in {2,4,8}
#   BrickSearch at each of those b values
#   At b=24, g=4 also CsrBfs, SccDagSearch, Grail, Oreach, and TwoHop
# Timed workload: 8192 queries, 128 warmup queries, 128-query timing chunks.
#
# Batch jobs (batch.csv): k in {4,16,64}, two warmup batches, eleven timed
# repetitions, b=24, g=4, on every recipe.
#
# Extra arguments are forwarded to the campaign run (for example --max-jobs 1).
# The campaign resumes completed rows. Set HBRICK_SKIP_BATCH=1 to skip batch.csv.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

CAMPAIGN_DIR="${HBRICK_CAMPAIGN_DIR:-$ROOT/campaigns/manuscript}"
CAMPAIGN_BIN="${HBRICK_CAMPAIGN_BIN:-$ROOT/build/dev/src/bench/hbrick_benchmark_campaign}"
BATCH_BIN="${HBRICK_BATCH_BIN:-$ROOT/build/dev/tools/hbrick_batch_pilot}"
DATASETS="${HBRICK_DATASETS:-$ROOT/datasets/movingai}"
RECIPES="${HBRICK_RECIPES:-$ROOT/recipes}"

if [[ ! -x "$CAMPAIGN_BIN" || ! -x "$BATCH_BIN" ]]; then
    echo "Build the project first: cmake --preset dev && cmake --build --preset dev" >&2
    exit 1
fi

"$CAMPAIGN_BIN" import-recipes "$CAMPAIGN_DIR" \
    --id manuscript \
    --recipes-dir "$RECIPES" \
    --datasets-root "$DATASETS" \
    --skip-existing

"$CAMPAIGN_BIN" run "$CAMPAIGN_DIR" \
    --id manuscript \
    --preset manuscript \
    --configs manuscript \
    --datasets-root "$DATASETS" \
    --resume \
    "$@"

if [[ "${HBRICK_SKIP_BATCH:-0}" == "1" ]]; then
    echo "Batch stage skipped. Single-pair results: $CAMPAIGN_DIR/results.csv"
    exit 0
fi

shopt -s nullglob
recipes=("$RECIPES"/*.json)
if [[ ${#recipes[@]} -eq 0 ]]; then
    echo "No recipes in $RECIPES" >&2
    exit 1
fi

for recipe in "${recipes[@]}"; do
    "$BATCH_BIN" \
        --recipe "$recipe" \
        --datasets-root "$DATASETS" \
        --b 24 \
        --g 4 \
        --k 4,16,64 \
        --warmup-batches 2 \
        --repeats 11 \
        --csv-out "$CAMPAIGN_DIR/batch.csv"
done

echo "Single-pair results: $CAMPAIGN_DIR/results.csv"
echo "Batch results: $CAMPAIGN_DIR/batch.csv"
