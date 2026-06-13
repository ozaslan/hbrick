# hbrick Dataset Browser

The **hbrick dataset browser** (`hbrick_dataset_browser`) is an interactive desktop GUI for exploring the [MovingAI 2D pathfinding benchmarks](https://movingai.com/benchmarks/grids.html) (Sturtevant 2012, ODC-BY). It loads maps through the hbrick I/O layer, converts them to `MazeLayout`, and lets you inspect terrain, passability, and directed-graph structure without modifying the underlying dataset files.

The tool is built with **GLFW**, **Dear ImGui**, and **OpenGL 3.0**. Map data is read-only; the only persistent writes are optional **orientation recipes** (JSON parameter files) saved under the repository `recipes/` directory.

---

## Building and running

Build the executable with the standard CMake presets. The GUI is gated behind `HBRICK_BUILD_TOOLS` (default **OFF**):

```bash
cmake --preset dev -DHBRICK_BUILD_TOOLS=ON
cmake --build --preset dev --target hbrick_dataset_browser
```

Run with an optional dataset root argument:

```bash
./build/dev/tools/dataset_browser/hbrick_dataset_browser
./build/dev/tools/dataset_browser/hbrick_dataset_browser /path/to/movingai/extracted
```

When no path is given, the binary defaults to `datasets/movingai/extracted` (configured at compile time via `HBRICK_DATASETS_DIR`). If that directory is empty or missing, the status bar reports the problem and suggests running:

```bash
datasets/movingai/fetch_movingai.sh --extract-only
```

Recipes are stored under `recipes/` at the repository root (`HBRICK_RECIPES_DIR`).

---

## What the tool is for

The browser serves three overlapping purposes:

1. **Dataset exploration** — Browse hundreds of benchmark maps with thumbnails, search/filter, and side-by-side comparison.
2. **Library validation** — Every map is loaded via `loadMovingAiMap`, converted to `MazeLayout`, and rendered through the same types the library uses in tests and benchmarks.
3. **Directed-graph experimentation** — Generate seeded directed graphs from grid adjacencies, run SCC decomposition, estimate reachability density, and probe forward reachability interactively.

It is **not** a pathfinding solver, map editor, or benchmark runner. It is a visual inspection and analysis front-end for hbrick’s grid-to-graph pipeline.

---

## User interface overview

The window uses ImGui docking with three built-in layout presets (View menu or `Ctrl+Shift+1/2/3`):

| Preset | Layout |
|--------|--------|
| **Browse** | Datasets gallery (left), map panels (center), Inspector (right) |
| **Focus** | Map panels only; gallery and inspector hidden |
| **Compare** | Two map slots side by side; gallery and inspector tabbed on the left |

Additional chrome:

- **Menu bar** — File, View, and Help menus
- **Status bar** — Active map metadata, hover cell details, or global status messages
- **Keyboard shortcuts window** — Help → Keyboard shortcuts (`F1`)

Panels can be rearranged manually by dragging dock tabs; applying a layout preset re-docks map panels automatically.

---

## Datasets gallery

The **Datasets** panel (`Ctrl+G`) lists every benchmark set found under `<root>/<set>/maps/*.map`, sorted by name. Sets appear as collapsible headers showing `visible/total` map counts after filtering.

### Thumbnails

Each map row shows a downsampled terrain thumbnail (longest edge ≤ 64 px). Thumbnails are generated lazily on first display and cached in GPU memory.

### Opening maps

| Action | Behavior |
|--------|----------|
| **Single click** | Opens the map in the reusable **preview** panel (VS Code–style: one preview slot shared by successive clicks) |
| **Double click** | Opens the map in a **pinned** panel with its own tab (`~` prefix in the tab title marks preview panels) |

If a map is already open, clicking focuses that panel; double-clicking promotes a preview to pinned.

### Filtering

- **Text filter** — Case-insensitive substring search by default. Use `*` (any run) and `?` (single character) for glob patterns, e.g. `maze512-*-0` or `AR0?03SR`.
- **Only maps with recipes** (`Ctrl+Shift+R`) — Hides maps that have no saved orientation recipes.

### Recipe badges

Maps with one or more saved recipes show an orange dot on the thumbnail corner. Hover for the count.

---

## Map panels

Each open map appears as a docked window with a toolbar and scrollable/zoomable canvas.

### Toolbar

| Control | Action |
|---------|--------|
| **Fit** (`F`) | Scale and center the map to fill the panel |
| **1:1** (`1`) | Set zoom to one screen pixel per map cell |
| **Pin** (`P`) | Promote a preview panel to pinned (hidden when already pinned) |
| **Orient** (`O`) | Toggle the orientation editor window for this map |

### Canvas navigation

| Input | Action |
|-------|--------|
| **Mouse wheel** | Zoom centered on cursor (range 0.05×–64×) |
| **Left or middle drag** | Pan |
| **Hover** | Highlights the cell under the cursor (yellow outline at zoom ≥ 3×); updates status bar and Inspector |
| **Right click** | Probe the cell (requires a generated directed graph; see Probing) |

### View modes

Switch with the Inspector radio buttons or `Alt+1/2/3`:

| Mode | Shortcut | Description |
|------|----------|-------------|
| **Terrain** | `Alt+1` | Raw MovingAI terrain colors (`.` ground, `@` obstacle, etc.) |
| **Passability** | `Alt+2` | Binary pass/fail under the active passability policy |
| **SCC components** | `Alt+3` | Passable cells colored by strongly connected component id (requires prior SCC computation) |

### Overlays and aids

| Feature | Toggle | When active |
|---------|--------|-------------|
| **Grid lines** | `Ctrl+Shift+G` | Visible at zoom ≥ 8× |
| **Directed edge arrows** | (automatic) | Visible at zoom ≥ 10× when a graph is generated; one-way arcs in orange, bidirectional corridors in gray |
| **Loupe** | `Ctrl+L` | Picture-in-picture magnifier (12×) in a corner when the main zoom is below 12× and the canvas is large enough; shows edge arrows when a graph exists |
| **Probe overlay** | (via right click) | Semi-transparent reachability or SCC highlight on top of the base view |

The loupe uses sticky corner placement with hysteresis so it does not flip every time the cursor crosses the canvas midpoint.

---

## Inspector panel

The **Inspector** (`Ctrl+I`) shows context for the **active** map panel (the one with keyboard focus or last hovered canvas).

### View section

- Radio buttons for Terrain / MazeLayout passability / SCC view modes
- **Passability policy** combo — Global setting that affects all open maps (see below)

### Active map section

Set name, map name, preview vs pinned status, MovingAI type string, dimensions, passable cell count, and number of open panels.

### Cell under cursor section

When hovering a map canvas:

- Terrain color swatch and coordinates
- Raw character, terrain class name, passability under current policy
- `MazeLayout` vertex id
- SCC id and size (when SCC labels are valid)
- Whether the cell is in the current probe highlight

### About section

Brief attribution and usage reminder (preview vs pin).

---

## Passability policies

The global **policy** determines which terrain cells become passable vertices in `MazeLayout`. Changing it rebuilds layouts and textures for all open panels.

| Policy | Passable terrain |
|--------|------------------|
| **Ground only** (default) | `.` and `G` |
| **Ground + swamp** | `.`, `G`, and `S` |
| **All traversable** | `.`, `G`, `S`, and `W` |
| **Any terrain letter** | Every non-obstacle character, weighted by MovingAI rules |

This mirrors the policies exposed by `MovingAiPassabilityPolicy` in the I/O module. Recipes store the policy alongside orientation parameters so a saved configuration can be reproduced exactly.

---

## Orientation editor

Press **Orient** (`O`) on a map panel to open the **Orientation** window. The editor is tied to that panel: it hides when the map tab is not visible, so you never edit a graph you cannot see.

The editor drives hbrick’s `DirectedGridGraphBuilder` and graph analysis routines on the panel’s current `MazeLayout`.

### Edge orientation

Choose a **conversion mode** and parameters, then generate a directed graph.

| Mode | Parameters | Result |
|------|------------|--------|
| **Random asymmetric** | Seed, `p_one-way`, `p bidirectional` | Each corridor independently becomes two-way, one-way (random direction), or removed. The two probabilities partition outcomes and are constrained to sum ≤ 1. |
| **Gradient flow** | Seed, flow angle (°), `p bidirectional`, `p against gradient` | Corridors prefer arcs along a global flow direction (0° = east, 90° = south). Backflow noise creates cycles. |
| **Bidirectional (baseline)** | — | Every passable adjacency becomes two opposing arcs (undirected maze as a directed graph). |
| **Acyclic east/south (DAG)** | — | Each corridor oriented east or south only; every passable cell is its own singleton SCC. |

**Seed** — Deterministic: same map + mode + parameters + seed always yields the same graph. Use **Roll dice** (`D`) to pick a random seed.

**Regenerate on change** — When checked, parameter edits trigger immediate regeneration. Otherwise use **Generate** (`Ctrl+Shift+Enter`).

After generation, the status area reports passable cell count and directed edge count. Edge arrows appear on the canvas at sufficient zoom.

### SCC analysis

**Compute SCCs** (`C`) runs strongly connected component decomposition on the generated graph (restricted to passable cells). Results include:

- Number of components
- Largest component size (cells and % of passable)
- Condensation graph size (vertices and inter-component arcs)
- Histogram of the top 64 component sizes (click a bin to highlight that SCC on the map)

Switching to **SCC view mode** (`Alt+3` or automatic after compute) recolors passable cells by component id.

### Reachable-pair density

**Estimate reachable-pair density** runs forward BFS from passable sources and reports the average fraction of passable vertices reachable from each source (self included). On small maps the estimate is **exact** over every passable cell. On larger maps it samples **distinct** random sources (partial Fisher–Yates shuffle, no repeats).

| Control | Behavior |
|---------|----------|
| **Parallel sampling** (default on) | Runs multiple distinct BFS samples per batch via `stepParallel` (`num_threads` = hardware concurrency) |
| **Choose samples automatically** (default on) | Stops once density and its uncertainty (σ) stabilize. The sample-count combo becomes a **maximum cap** (default 512). |
| **Sample count** combo | 128, 256, 512, 1024, 2048, 4096, 8192, or 16384 sources. With auto mode off, this is the fixed sample count. |

Clicking **Estimate reachable-pair density** opens a **progress modal** with a bar (`completed / total sources`), a running estimate (`Density so far: …`), parallel worker count when enabled, and a **Stop** button that keeps the result from sources completed so far. Estimation runs incrementally in parallel batches (one batch per UI slice, ~12 ms budget per frame) so the window stays responsive. Orientation controls are disabled while a job is active.

The editor shows the final result as `Density: …` with ±2σ when sampling, or `(exact, all N sources)` when exhaustive.

This measures how “connected” the directed graph is in an ordered-pair sense: 1.0 means fully reachable, near 0 means highly fragmented.

### Right-click probe

Two probe modes, selectable via radio buttons in the editor or `Ctrl+Shift+P`:

| Mode | Behavior |
|------|----------|
| **Reachable set (BFS)** | Forward reachable set from the clicked cell, colored by hop distance (yellow near source, violet at frontier). Reports count, percentage of passable cells, and max depth. |
| **Strongly connected component** | All cells in the same SCC as the clicked cell. Computes SCC labels automatically if needed. Reports SCC size and percentage. |

Switching modes while a probe is active re-runs the highlight under the new mode. **Clear probe** button, `Esc`, or `Space` removes the overlay.

### Recipes

Recipes capture **parameters only** — never the maze file itself. They are flat JSON files (`schema: hbrick_orientation_recipe_v1`) written to `recipes/`.

**Save current as recipe** (`Ctrl+S`) — Writes set name, map name, policy, mode, seed, and probability fields. Filename derives from map name, mode, seed, and a content hash; identical parameters overwrite the same file.

**Saved recipes list** — Per-map recipes appear in the editor (newest first). Each entry shows the filename, saved timestamp, mode, seed, and probability fields, with **Load** and **Delete** buttons. Delete opens a confirmation modal.

**File → Load recipe** — Opens any recipe from disk: locates the referenced map in the dataset index, opens it pinned, applies parameters, regenerates the graph, and computes SCCs.

Gallery recipe badges and the “only maps with recipes” filter stay in sync when recipes are saved or deleted.

Example recipe fields:

```json
{
  "schema": "hbrick_orientation_recipe_v1",
  "set": "maze",
  "map": "maze512-1-0.map",
  "policy": "ground_only",
  "mode": "random_asymmetric",
  "seed": "0x0000000000000001",
  "p_one_way": 0.3,
  "p_bidirectional": 0.1,
  "gradient_angle_degrees": 45.0,
  "p_against_gradient": 0.02
}
```

---

## Keyboard shortcuts

Shortcuts are disabled while a text input field has focus. Full list: **Help → Keyboard shortcuts** (`F1`).

### Application

| Key | Action |
|-----|--------|
| `F1` | Toggle shortcuts window |
| `Ctrl+Q` | Quit |
| `Ctrl+=` / `Ctrl+-` | Increase / decrease UI font size (12–28 px) |
| `Ctrl+0` | Reset font to 16 px |
| `Ctrl+Shift+1/2/3` | Browse / Focus / Compare layout |
| `Ctrl+G` | Toggle Datasets panel |
| `Ctrl+I` | Toggle Inspector |
| `Ctrl+L` | Toggle loupe |
| `Ctrl+Shift+G` | Toggle grid lines |
| `Ctrl+Shift+R` | Toggle “only maps with recipes” filter |

### Active map panel

| Key | Action |
|-----|--------|
| `Ctrl+W` | Close active panel |
| `F` | Fit map to panel |
| `1` | Zoom 1:1 |
| `[` / `]` | Zoom out / in |
| `O` | Toggle orientation editor |
| `P` | Pin preview panel |
| `Alt+1/2/3` | Terrain / passability / SCC view |
| `Esc` / `Space` | Clear probe highlight |
| Right-click | Probe cell (with generated graph) |

### Orientation editor (when open)

| Key | Action |
|-----|--------|
| `Ctrl+Shift+Enter` | Generate directed graph |
| `D` | Randomize seed |
| `C` | Compute SCCs |
| `Ctrl+S` | Save recipe |
| `Ctrl+Shift+P` | Switch probe mode (BFS / SCC) |

---

## Library integration

The browser exercises these hbrick components end to end:

| Component | Use in the GUI |
|-----------|----------------|
| `loadMovingAiMap` / `MovingAiMap` | Load `.map` files from disk |
| `MazeLayout` | Passability grid and vertex indexing |
| `DirectedGridGraphBuilder` | Grid-to-directed-graph conversion |
| `DirectedGridGraph` | CSR storage; neighbor iteration for arrows and probes |
| `SccDecomposition` | Component labeling and condensation stats |
| `Bfs` | Right-click reachability probes |
| `ReachabilityDensityEstimator` | Reachable-pair density (`FixedSamples` or `AutoStopWhenStable`) |

Rendering paths deliberately use `MazeLayout` (not raw map characters) for passability and SCC views so the GUI validates the same conversion pipeline as tests.

Hot-path rules from the library (no allocations in traversal) apply to library code; the GUI itself may allocate for textures, UI, and analysis buffers outside query hot paths.

---

## Limitations and design notes

- **Read-only maps** — Dataset files are never modified. ImGui layout is also not persisted (`IniFilename = nullptr`).
- **Single preview slot** — Only one unpinned preview panel exists; new single-clicks replace its contents.
- **Global passability policy** — One policy applies to all panels; recipes can change it when loaded.
- **Arrow draw budget** — Edge arrows skip rendering when the visible cell count exceeds 60,000 (safety valve for huge windows at high zoom).
- **SCC view invalidation** — Regenerating a graph clears SCC labels until recomputed; view mode falls back to passability.
- **Incremental density jobs** — Reachable-pair density estimation runs one BFS sample per UI frame slice; only one job can be active per open map panel.
- **No batch export** — SVG visualization exists in `hbrick_viz` but is not wired into this tool.

---

## Source layout

```
tools/dataset_browser/
├── main.cpp           GLFW + ImGui entry point
├── browser_app.cpp/hpp  Application state and all UI panels
├── dataset_index.cpp/hpp  Scan `<root>/<set>/maps/*.map`
├── map_render.cpp/hpp     Terrain/passability/SCC → OpenGL textures
├── orientation.cpp/hpp    Graph generation, SCC, probes, overlays
└── recipe.cpp/hpp         JSON recipe save/load/list/delete
```

CMake target: `hbrick_dataset_browser`, linked against `hbrick_io`, `hbrick_graph`, and `hbrick_imgui`.
