# Orientation recipes

A Moving AI map records which cells are open. A recipe records how those open cells are connected by directed edges. The same map with a different recipe is a different reachability instance. The graph is not stored in the JSON file. The benchmark tools load the map named by the recipe and rebuild the directed graph from the fields below.

The 48 recipes used in the H-BRICK manuscript are the JSON files in this directory. There are eight maps and six regimes on each map. The map files themselves are under [`../datasets/movingai/`](../datasets/movingai/README.md).

## Where the tools look

A recipe contains `set` and `map`. The loader joins them as

```
datasets/movingai/<set>/maps/<map>
```

`hbrick_quick_bench` and `hbrick_batch_pilot` take that directory as `--datasets-root datasets/movingai`.

## Fields

Every shipped file uses schema `hbrick_orientation_recipe_v1`, policy `ground_only`, and seed `0x0000000000000001`. Policy `ground_only` keeps ordinary ground cells and treats the other terrain symbols as blocked.

| Field | Role |
|-------|------|
| `set`, `map` | Moving AI benchmark set and file name |
| `label` | Short id, such as `M01_V01_ra_target95` |
| `mode` | `random_asymmetric` or `gradient_flow` |
| `seed` | Seed of the orientation draw. The same seed rebuilds the same arcs |
| `p_bidirectional` | Probability that a passable adjacency is kept in both directions |
| `p_one_way` | Probability that a passable adjacency becomes a single arc |
| `gradient_angle_degrees` | Flow direction for gradient mode. 0 is east, 90 is south |
| `p_against_gradient` | Probability of pointing a one-way arc against the flow |

In the random-asymmetric recipes the two probabilities sum to 1, so every passable adjacency receives an arc: both ways with probability `p_bidirectional`, and one way with probability `p_one_way`. The manuscript writes the bidirectional probability as p_bi. In the files, `p_bidirectional` is that value and `p_one_way` is 1 minus p_bi.

Gradient-flow recipes set both of those probabilities to 0. Each adjacency then follows the flow angle, except that a fraction `p_against_gradient` of the one-way arcs point backwards. The manuscript uses 45 degrees and a backflow probability of 0.05. On map M1 the gradient recipe has no backflow, so the orientation is a directed acyclic graph.

## File names

```
<recipe id>_<regime>_<profile>__<set>__<file>__<mode>__<seed>__<hash>.json
```

The recipe id (`M01`, `M03`, …) is the id used when the files were calibrated. The manuscript renumbers the same eight maps as M1 through M8. The two labels are not the same number:

| Paper id | Recipe id | Map file |
|----------|-----------|----------|
| M1 | M03 | maze512-1-0.map |
| M2 | M04 | maze512-32-0.map |
| M3 | M05 | 8room_000.map |
| M4 | M06 | random512-20-0.map |
| M5 | M02 | arena2.map |
| M6 | M01 | Boston_0_256.map |
| M7 | M07 | Boston_0_512.map |
| M8 | M09 | Boston_0_1024.map |

`V01` through `V06` in the file name are the six regimes. Leave these files unchanged if you want the instances reported in the manuscript.

## What the six regimes are

Moving AI grids are undirected 4-neighbor lattices. The experiments direct them in two ways.

Regimes V01 through V05 are random asymmetric. Adjacent passable cells are connected in both directions with probability p_bi and in one direction with probability 1 minus p_bi. Lowering p_bi removes return paths and shrinks the reachable region. Each map has its own p_bi, found by binary search, because a doorway and an open street do not percolate at the same probability.

The target is the reachability density: among ordered pairs of distinct passable cells, the fraction for which a directed path exists. The ladder runs from about 95 percent in V01 down to about 3 percent in V05. The measured density is not exactly the target on every map. The table below is the value stored with each recipe. The number in parentheses is p_bi, which is `p_bidirectional` in the JSON file.

Map M1 is the exception. `maze512-1-0` is a tree: every corridor is a bridge, so a random orientation does not grow one giant strongly connected piece. Its V01–V05 recipes are calibrated by component count instead of by reachability density. With p_bi = 1 the map is one strongly connected piece. As p_bi falls, the number of strongly connected components is about (1 minus p_bi) times the number of passable cells. V06 on M1 has no backflow.

| Paper id | V01 | V02 | V03 | V04 | V05 | V06 |
|----------|-----|-----|-----|-----|-----|-----|
| M1 | one SCC (p_bi 1.00) | SCC count about 20 percent of N (0.80) | about 40 percent (0.60) | about 60 percent (0.40) | about 80 percent (0.20) | DAG, no backflow |
| M2 | 95.6 percent (0.31) | 62.6 percent (0.18) | 27.8 percent (0.16) | 6.8 percent (0.09) | 3.1 percent (0.06) | 1.0 percent |
| M3 | 97.0 percent (0.85) | 60.4 percent (0.69) | 45.7 percent (0.66) | 32.1 percent (0.63) | 2.5 percent (0.57) | 0.1 percent |
| M4 | 94.7 percent (0.61) | 68.2 percent (0.39) | 46.9 percent (0.36) | 35.6 percent (0.35) | 8.7 percent (0.32) | 9.2 percent |
| M5 | 96.8 percent (0.53) | 81.7 percent (0.21) | 50.1 percent (0.15) | 36.7 percent (0.11) | 4.9 percent (0.04) | 9.1 percent |
| M6 | 93.5 percent (0.43) | 61.7 percent (0.21) | 43.5 percent (0.18) | 21.4 percent (0.14) | 2.0 percent (0.05) | 7.3 percent |
| M7 | 92.5 percent (0.33) | 60.2 percent (0.13) | 52.1 percent (0.11) | 15.4 percent (0.07) | 2.7 percent (0.04) | 7.8 percent |
| M8 | 94.6 percent (0.35) | 71.0 percent (0.11) | 37.0 percent (0.06) | 15.3 percent (0.04) | 3.0 percent (0.02) | 7.6 percent |

For M2 through M8, V01–V05 entries are the measured reachability density, and V06 is the measured density of the 45-degree gradient with backflow 0.05. Parentheses are p_bi.

## What the manuscript benchmark runs on these files

`tools/run_manuscript_benchmarks.sh` takes every `*.json` in this directory as one instance. The map is loaded from `datasets/movingai/<set>/maps/<map>`, using the `set` and `map` fields. Nothing in the JSON is rewritten.

For each file the single-pair campaign writes rows to `campaigns/manuscript/results.csv`:

- `HBrickSkipLift` at all 24 combinations of tile side 4, 8, 16, 24, 32, 48, 64, 96 and group size 2, 4, 8. The published operating point is the one cell tile 24, group 4.
- `BrickSearch` at each of those eight tile sides, with the hierarchy group left unused. That is the flat BRICK ablation.
- On the operating-point cell only, also `CsrBfs`, `SccDagSearch`, `Grail`, `Oreach`, and `TwoHop`.

Each of those rows times 8192 queries after 128 warmup queries. The batch stage then appends `campaigns/manuscript/batch.csv` for the same file: batch sizes 4, 16, and 64, two warmup batches and eleven timed repetitions, at tile 24 and group 4. That stage records batched H-BRICK, scalar H-BRICK on the same pairs, O'Reach, one BFS per source, SCC-DAG search, and SCC-DAG closure.

The full description of the clock protocol, the 4 GiB skip rule, and the break-even formula is in the repository [README](../README.md) under Manuscript matrix.
