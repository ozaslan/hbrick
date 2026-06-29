# Benchmark campaign summary

| Field | Value |
|-------|-------|
| Campaign | `smoke` |
| Schema | `2` |
| Started | 2026-06-29T22:55:19Z |
| Git | `d2290b56684303a6a73e1d18c6e9560746c207dd` |
| Build | Release |
| Compiler | GNU 13.3.0 |
| CPU | Intel(R) Core(TM) i7-8650U CPU @ 1.90GHz |
| Threads | 8 |
| Timer | `std::chrono::steady_clock` |

## Results

| Map | Class | Method | Config | Status | QPS | Total vs BFS | Mismatches |
|-----|-------|--------|--------|--------|-----|-------------|------------|
| smoke_8x8_random_asymmetric | smoke_grid | CsrBfs | t4x4_k0_mem512m_hg2x2_dfull | Completed | 9.7695e+06 | 1.00x | 0 |
| smoke_8x8_random_asymmetric | smoke_grid | BrickSearch | t4x4_k0_mem512m_hg2x2_dfull | Completed | 8.40667e+06 | 0.09x | 0 |
| smoke_8x8_random_asymmetric | smoke_grid | BrickClosure | t4x4_k0_mem512m_hg2x2_dfull | Completed | 7.37582e+06 | 0.07x | 0 |
| smoke_8x8_random_asymmetric | smoke_grid | HBrick | t4x4_k0_mem512m_hg2x2_dfull | Completed | 821071 | 0.03x | 0 |

## Best completed QPS by map class

| Class | Map | Method | Config | QPS |
|-------|-----|--------|--------|-----|
| smoke_grid | smoke_8x8_random_asymmetric | CsrBfs | t4x4_k0_mem512m_hg2x2_dfull | 9.7695e+06 |
