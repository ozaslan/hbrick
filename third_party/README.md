# Third-party code

H-BRICK is compared with two published reachability indexes. Their sources are compiled into the baselines `Grail` and `Oreach`. They are not part of the H-BRICK index. Each query still returns an exact yes or no; when a label does not settle the pair, the implementation falls back to search.

## GRAIL

`grail/` is the reachability index of Yildirim, Chaoji, and Zaki. After the graph is condensed into its strongly connected components, GRAIL assigns each component a small set of numeric intervals from random depth-first searches. A query that hits an interval can be answered from the labels. A query that misses falls back to a search on the condensation.

In this study GRAIL is the interval-labeling baseline on general directed graphs. The wrapper is `GrailBaseline`. The original notice is `grail/COPYRIGHT`: the code is provided for research, as is.

Yildirim, H.; Chaoji, V.; Zaki, M. J. GRAIL: Scalable Reachability Index for Large Graphs. Proceedings of the VLDB Endowment, 3(1–2), 276–284, 2010. https://doi.org/10.14778/1920841.1920879

```bibtex
@article{yildirim2010grail,
  author  = {Yildirim, Hilmi and Chaoji, Vineet and Zaki, Mohammed J.},
  title   = {{GRAIL}: Scalable Reachability Index for Large Graphs},
  journal = {Proceedings of the VLDB Endowment},
  volume  = {3},
  number  = {1--2},
  pages   = {276--284},
  year    = {2010},
  doi     = {10.14778/1920841.1920879}
}
```

## O'Reach

`oreach/` is the reachability index of Hanauer, Schulz, and Trummer. It also works on the condensation DAG. It keeps several topological orders, forward and backward levels, and bitsets of supporting vertices. Many pairs are then decided by comparing those labels. Pairs that remain go to a pruned bidirectional breadth-first search.

In this study O'Reach is the fast general-graph index. On the random-asymmetric regimes a large share of pairs lie inside one strong component, so comparing component identifiers already answers the query. The wrapper is `OreachBaseline`. The sources are under the MIT license in `oreach/LICENSE`.

The O'Reach tree includes a small excerpt of KaHIP (Karlsruhe High Quality Partitioning, Christian Schulz) for its graph container and strongly connected components. Those files keep their original headers under `oreach/extern/KaHIP/`. KaHIP is not a baseline in the experiments.

Hanauer, K.; Schulz, C.; Trummer, J. O'Reach: Even Faster Reachability in Large Graphs. In 19th International Symposium on Experimental Algorithms (SEA 2021), Leibniz International Proceedings in Informatics, vol. 190, pp. 13:1–13:24. https://doi.org/10.4230/LIPIcs.SEA.2021.13

```bibtex
@inproceedings{hanauer2021oreach,
  author    = {Hanauer, Kathrin and Schulz, Christian and Trummer, Jonathan},
  title     = {{O'Reach}: Even Faster Reachability in Large Graphs},
  booktitle = {19th International Symposium on Experimental Algorithms (SEA 2021)},
  series    = {Leibniz International Proceedings in Informatics},
  volume    = {190},
  pages     = {13:1--13:24},
  year      = {2021},
  doi       = {10.4230/LIPIcs.SEA.2021.13}
}
```
