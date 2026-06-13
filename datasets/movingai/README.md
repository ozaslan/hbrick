# MovingAI 2D Pathfinding Benchmarks (local archive)

Archival copy of the 2D grid benchmark sets from Nathan Sturtevant's Moving AI Lab:
<https://www.movingai.com/benchmarks/grids.html>

The zips under `zips/` are committed to this repository on purpose: the upstream
website may not be available indefinitely, and these benchmarks are needed for
reproducible experiments. `SHA256SUMS` records the checksums of the archives as
downloaded. Extract with `./fetch_movingai.sh --extract-only` (or unzip manually);
extracted data lands in `extracted/<set>/` and is not tracked by git.

## Included benchmark sets

### Commercial Game Benchmarks

| Set | Slug | Source game |
|-----|------|-------------|
| Dragon Age: Origins | `dao` | BioWare, 2009 |
| Dragon Age 2 | `da2` | BioWare, 2011 |
| Warcraft III (scaled to 512x512) | `wc3maps512` | Blizzard, 2002 |
| Baldur's Gate II (scaled to 512x512) | `bg512` | BioWare, 2000 |
| Baldur's Gate II (original maps) | `bgmaps` | BioWare, 2000 |
| Starcraft | `sc1` | Blizzard, 1998 |

### Real-World Benchmarks

| Set | Slug | Notes |
|-----|------|-------|
| City/street maps | `street` | Discretized building/road maps, provided by Konstantin Yakovlev and Anton Andreychuk |

### Artificial Benchmarks

| Set | Slug | Notes |
|-----|------|-------|
| Mazes (variable corridor width) | `maze` | Nathan Sturtevant / HOG2 |
| Random maps (10%-40% obstacles) | `random` | Nathan Sturtevant / HOG2 |
| Room maps | `room` | Square rooms with random openings, Nathan Sturtevant / HOG2 |
| Terrain/weighted maps | `weighted` | Nathan Sturtevant / HOG2. **Different encoding:** cells are terrain letters `A`-`Z`, all passable with varying movement cost. Not directly usable as a passable/blocked bitmap. |

Each set ships as two archives: `<slug>-map.zip` (the `.map` grid files) and
`<slug>-scen.zip` (the `.scen` benchmark problem files).

## File formats

Documented at <https://www.movingai.com/benchmarks/formats.html>.

**`.map`** — ASCII grid preceded by a four-line header (`type octile`,
`height <y>`, `width <x>`, `map`). Cell characters: `.` and `G` are passable;
`@`, `O`, `T` are impassable; `S` (swamp) and `W` (water) are conditionally
passable. The `weighted` set instead uses letters `A`-`Z` as terrain cost classes.

**`.scen`** — one problem per line with 9 tab-separated fields: bucket, map path,
map width, map height, start x, start y, goal x, goal y, optimal path length.
Optimal lengths assume 8-connected octile movement with sqrt(2) diagonal cost and
no corner cutting. Note that hbrick's grid conversion is 4-connected, so these
lengths are reference data, not an oracle for hbrick graph modes.

## License

All data is made available by the Moving AI Lab under the
[Open Data Commons Attribution License (ODC-BY)](https://opendatacommons.org/licenses/by/1-0/).
Attribution: Nathan Sturtevant, Moving AI Lab.

## Usage in hbrick

- **Extracted maps** under `extracted/<set>/maps/*.map` feed integration tests (`test_movingai_reachability`) and the optional dataset browser GUI. Tests skip gracefully when `extracted/` is absent.
- **Orientation recipes** saved by the dataset browser live in the repository-root `recipes/` directory (parameters only — never the map files themselves). `test_recipe_reachability` validates graphs built from committed recipe fixtures.

## Citation

Publications using these benchmarks should cite:

```bibtex
@article{sturtevant2012benchmarks,
  title={Benchmarks for Grid-Based Pathfinding},
  author={Sturtevant, N.},
  journal={Transactions on Computational Intelligence and AI in Games},
  volume={4},
  number={2},
  pages={144 -- 148},
  year={2012},
  url = {http://web.cs.du.edu/~sturtevant/papers/benchmarks.pdf},
}
```
