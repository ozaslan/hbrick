#!/usr/bin/env bash
# Reproduce the complete empirical evaluation reported in the H-BRICK manuscript.
#
# Stages (--stage all, or comma-separated subset):
#   graph_stats        -> graph_and_calibration.csv (Tables 4 & 5)
#   param_sweep        -> param_sweep_results.csv (Tables 6, 7, 8, 9)
#   threeway           -> threeway_b24_g4_results.csv, threeway_b24_g4_results_raw.csv (Table 10)
#   external           -> lifecycle_results.csv (Tables 14, 15, 16; Fig. 6a, 7; TwoHop)
#   batch_multisample  -> batch_multisample.csv (Tables 11, 12, 13; Fig. 6b)
#   polarity_and_v06   -> polarity_split.csv, v06_sensitivity.csv (Tables 17 & 18)
#   closure_kernels    -> closure_kernel_matrix.csv, closure_tile_ablation.csv (Sections 6.1 & 7.1)
#   batch_equal_pairs  -> batch_equal_pairs.csv via hbrick_batch_pilot (Table 19)
#   summary            -> MANUSCRIPT_REPORT.md + figure_data/*.csv
#
# Usage:
#   ./tools/run_manuscript_benchmarks.sh
#   ./tools/run_manuscript_benchmarks.sh --quick
#   ./tools/run_manuscript_benchmarks.sh --stage external,threeway,summary
#   ./tools/run_manuscript_benchmarks.sh --shard-index 0 --shard-count 4

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${HBRICK_BUILD_DIR:-$ROOT/build/dev}"
CAMPAIGN_DIR="${HBRICK_CAMPAIGN_DIR:-$ROOT/campaigns/manuscript}"
BENCH_BIN="${HBRICK_MANUSCRIPT_BIN:-$BUILD_DIR/tools/hbrick_manuscript_bench}"
BATCH_BIN="${HBRICK_BATCH_BIN:-$BUILD_DIR/tools/hbrick_batch_pilot}"
DATASETS="${HBRICK_DATASETS:-$ROOT/datasets/movingai}"
RECIPES="${HBRICK_RECIPES:-$ROOT/recipes}"

STAGE="all"
SHARD_INDEX=0
SHARD_COUNT=1
QUICK=0
RESUME=1
EXTRA_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --stage)
            STAGE="$2"
            shift 2
            ;;
        --shard-index)
            SHARD_INDEX="$2"
            shift 2
            ;;
        --shard-count)
            SHARD_COUNT="$2"
            shift 2
            ;;
        --out-dir)
            CAMPAIGN_DIR="$2"
            shift 2
            ;;
        --quick)
            QUICK=1
            shift
            ;;
        --no-resume)
            RESUME=0
            shift
            ;;
        --resume)
            RESUME=1
            shift
            ;;
        --help|-h)
            sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            EXTRA_ARGS+=("$1")
            shift
            ;;
    esac
done

has_stage() {
    local target="$1"
    if [[ "$STAGE" == "all" ]]; then
        return 0
    fi
    [[ ",$STAGE," == *",$target,"* ]]
}

mkdir -p "$CAMPAIGN_DIR"

# Record host environment fingerprint for reproducibility auditing.
HOST_FILE="$CAMPAIGN_DIR/host_info.txt"
if [[ "$SHARD_COUNT" -gt 1 ]]; then
    HOST_FILE="$CAMPAIGN_DIR/host_info_shard_${SHARD_INDEX}.txt"
fi
{
    echo "=== DATE (UTC) ==="
    date -u +"%Y-%m-%dT%H:%M:%SZ"
    echo "=== GIT REVISION ==="
    git rev-parse HEAD 2>/dev/null || echo "unknown"
    echo "=== OS ==="
    cat /etc/os-release 2>/dev/null || uname -a
    echo "=== COMPILER ==="
    g++ --version 2>/dev/null | head -n 1 || c++ --version 2>/dev/null | head -n 1 || echo "unknown"
    echo "=== CPU ==="
    lscpu 2>/dev/null || true
    echo "=== RAM ==="
    free -h 2>/dev/null || true
} > "$HOST_FILE"

PIN_PREFIX=()
if [[ "${HBRICK_PIN_CPU:-1}" != "0" ]] && command -v taskset >/dev/null 2>&1; then
    PIN_PREFIX=(taskset -c "${HBRICK_CPU_CORE:-0}")
fi

CPP_STAGES=()
for st in graph_stats param_sweep threeway external batch_multisample polarity_and_v06 closure_kernels; do
    if [[ "${HBRICK_SKIP_BATCH:-0}" == "1" && "$st" == "batch_multisample" ]]; then
        continue
    fi
    if has_stage "$st"; then
        CPP_STAGES+=("$st")
    fi
done

if [[ ${#CPP_STAGES[@]} -gt 0 ]]; then
    if [[ ! -x "$BENCH_BIN" ]]; then
        echo "Missing $BENCH_BIN. Build first: cmake --preset dev && cmake --build --preset dev" >&2
        exit 1
    fi
    STAGE_CSV="$(IFS=,; echo "${CPP_STAGES[*]}")"
    CMD=(
        "${PIN_PREFIX[@]}" "$BENCH_BIN"
        --stage "$STAGE_CSV"
        --recipes-dir "$RECIPES"
        --datasets-root "$DATASETS"
        --out-dir "$CAMPAIGN_DIR"
        --shard-index "$SHARD_INDEX"
        --shard-count "$SHARD_COUNT"
    )
    if [[ "$RESUME" == "1" ]]; then
        CMD+=(--resume)
    fi
    if [[ "$QUICK" == "1" ]]; then
        CMD+=(--quick)
    fi
    if [[ ${#EXTRA_ARGS[@]} -gt 0 ]]; then
        CMD+=("${EXTRA_ARGS[@]}")
    fi
    "${CMD[@]}"
fi

if has_stage "batch_equal_pairs" && [[ "${HBRICK_SKIP_BATCH:-0}" != "1" ]]; then
    if [[ ! -x "$BATCH_BIN" ]]; then
        echo "Missing $BATCH_BIN. Build first: cmake --preset dev && cmake --build --preset dev" >&2
        exit 1
    fi
    shopt -s nullglob
    recipes=("$RECIPES"/*.json)
    if [[ ${#recipes[@]} -eq 0 ]]; then
        echo "No recipes in $RECIPES" >&2
        exit 1
    fi
    # Sort deterministically by filename and apply strided sharding across active M1..M8 recipes.
    IFS=$'\n' sorted_recipes=($(printf '%s\n' "${recipes[@]}" | sort))
    unset IFS

    JSON_DIR="$CAMPAIGN_DIR/batch_equal_pairs_json"
    mkdir -p "$JSON_DIR"
    EQ_CSV="$CAMPAIGN_DIR/batch_equal_pairs.csv"

    K_LIST="4,16,32,64"
    TOTAL_PAIRS=65536
    WARMUP_BATCHES=5
    MAX_RECIPES=48
    if [[ "$QUICK" == "1" ]]; then
        K_LIST="4,16"
        TOTAL_PAIRS=1024
        WARMUP_BATCHES=1
        MAX_RECIPES=2
    fi

    echo "[batch_equal_pairs] Running equal-pair (${TOTAL_PAIRS} pairs/k) batch pilot..."
    active_idx=0
    for recipe in "${sorted_recipes[@]}"; do
        base_name="$(basename "$recipe" .json)"
        if [[ "$base_name" == M08_* || "$base_name" == M10_* ]]; then
            continue
        fi
        if [[ "$QUICK" == "1" && $active_idx -ge $MAX_RECIPES ]]; then
            break
        fi
        if (( active_idx % SHARD_COUNT == SHARD_INDEX )); then
            json_out="$JSON_DIR/${base_name}.json"
            if [[ "$RESUME" == "1" && -f "$json_out" && -s "$json_out" ]]; then
                echo "  [batch_equal_pairs] Skipping already-completed $base_name"
            else
                "${PIN_PREFIX[@]}" "$BATCH_BIN" \
                    --recipe "$recipe" \
                    --datasets-root "$DATASETS" \
                    --b 24 \
                    --g 4 \
                    --k "$K_LIST" \
                    --warmup-batches "$WARMUP_BATCHES" \
                    --total-pairs "$TOTAL_PAIRS" \
                    --memory-gib 2.5 \
                    --core-only \
                    --no-scc-closure \
                    --csv-out "$EQ_CSV" \
                    --json-out "$json_out" \
                    >/dev/null
            fi
        fi
        active_idx=$((active_idx + 1))
    done
fi

if has_stage "summary"; then
    python3 "$ROOT/tools/summarize_manuscript_results.py" --campaign-dir "$CAMPAIGN_DIR"
fi

echo "Done. Outputs under $CAMPAIGN_DIR"
