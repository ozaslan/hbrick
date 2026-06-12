#!/usr/bin/env bash
# Downloads the MovingAI 2D grid benchmark archives into zips/ and extracts
# them into extracted/<set>/{maps,scenarios}/.
#
# Usage:
#   ./fetch_movingai.sh                # download (skip existing) + verify + extract
#   ./fetch_movingai.sh --extract-only # extract the already-committed zips
#
# Data: https://www.movingai.com/benchmarks/grids.html (ODC-BY license).
set -euo pipefail

cd "$(dirname "$0")"

BASE_URL="https://movingai.com/benchmarks"
SETS=(dao da2 wc3maps512 bg512 bgmaps sc1 street maze random room weighted)

EXTRACT_ONLY=0
if [[ "${1:-}" == "--extract-only" ]]; then
    EXTRACT_ONLY=1
fi

mkdir -p zips

if [[ "$EXTRACT_ONLY" -eq 0 ]]; then
    for set in "${SETS[@]}"; do
        for kind in map scen; do
            zip="zips/${set}-${kind}.zip"
            if [[ -s "$zip" ]]; then
                echo "exists  $zip"
                continue
            fi
            url="${BASE_URL}/${set}/${set}-${kind}.zip"
            echo "fetch   $url"
            curl -fSL --retry 3 -o "$zip" "$url"
        done
    done

    ( cd zips && sha256sum -- *.zip ) > SHA256SUMS
    echo "wrote SHA256SUMS"
fi

if [[ -f SHA256SUMS ]]; then
    ( cd zips && sha256sum --check --quiet ../SHA256SUMS )
    echo "checksums OK"
fi

for set in "${SETS[@]}"; do
    mkdir -p "extracted/${set}/maps" "extracted/${set}/scenarios"
    unzip -oq "zips/${set}-map.zip" -d "extracted/${set}/maps"
    unzip -oq "zips/${set}-scen.zip" -d "extracted/${set}/scenarios"
done
echo "extracted all sets under extracted/"
