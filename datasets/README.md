# Datasets

The evaluation grids are the eight Moving AI maps under [`movingai/`](movingai/README.md). That note cites the Moving AI benchmark paper and lists which file is which map in the H-BRICK manuscript.

Those files record occupancy only. The directed edge orientation used in the experiments is not stored in the map. It is stored as a recipe. The recipe format, the six regimes, and the calibrated probabilities are described in [`../recipes/README.md`](../recipes/README.md).

The manuscript benchmark runs on these maps and those recipes together. What each CSV row measures is described in the repository [README](../README.md) under Manuscript matrix.
