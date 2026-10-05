# Moving AI maps

These eight grids are the maps used in the H-BRICK evaluation. They are already unpacked. A recipe names a set and a file, and the tools load

```
datasets/movingai/<set>/maps/<file>.map
```

| Paper id | Set | File | Grid |
|----------|-----|------|------|
| M1 | maze | maze512-1-0.map | 512 by 512 maze, corridors one cell wide |
| M2 | maze | maze512-32-0.map | 512 by 512 maze, corridors 32 cells wide |
| M3 | room | 8room_000.map | 512 by 512 building with eight rooms |
| M4 | random | random512-20-0.map | 512 by 512 field, about 20 percent obstacles |
| M5 | dao | arena2.map | Dragon Age arena, 281 by 209 |
| M6 | street | Boston_0_256.map | Boston streets, 256 by 256 |
| M7 | street | Boston_0_512.map | Boston streets, 512 by 512 |
| M8 | street | Boston_0_1024.map | Boston streets, 1024 by 1024 |

Each file is an occupancy grid from the Moving AI Lab 2D pathfinding benchmarks. Passable cells and blocked cells are recorded; the file does not store a directed graph. H-BRICK turns a grid into a directed graph by applying an orientation recipe. The manuscript uses six recipes on each of these eight maps, 48 instances in total. The recipe files, the meaning of each field, and the calibrated probabilities are in [`../../recipes/README.md`](../../recipes/README.md).

The manuscript benchmark loads these eight files and no others. For every recipe that names one of them, `tools/run_manuscript_benchmarks.sh` times `HBrickSkipLift` on the full tile and group sweep, flat `BrickSearch` on the eight tile sides, and at tile 24 and group 4 also BFS, SCC-DAG search, GRAIL, O'Reach, and 2-hop. It then times batched queries at batch sizes 4, 16, and 64. The rows land in `campaigns/manuscript/results.csv` and `campaigns/manuscript/batch.csv`. The protocol is written out in the repository [README](../../README.md) under Manuscript matrix and in the [recipe README](../../recipes/README.md).

## Citation

If you use these grids, cite the benchmark paper that published the collection:

Sturtevant, N. R. Benchmarks for Grid-Based Pathfinding. IEEE Transactions on Computational Intelligence and AI in Games, 4(2), 144–148, 2012. https://doi.org/10.1109/TCIAIG.2012.2197681

```bibtex
@article{sturtevant2012benchmarks,
  author  = {Sturtevant, Nathan R.},
  title   = {Benchmarks for Grid-Based Pathfinding},
  journal = {IEEE Transactions on Computational Intelligence and AI in Games},
  volume  = {4},
  number  = {2},
  pages   = {144--148},
  year    = {2012},
  doi     = {10.1109/TCIAIG.2012.2197681}
}
```

The files are redistributed from that collection: https://movingai.com/benchmarks/grids.html

The street maps in the collection were contributed by Konstantin Yakovlev and Anton Andreychuk. The grids remain subject to the terms of the Moving AI Lab collection.
