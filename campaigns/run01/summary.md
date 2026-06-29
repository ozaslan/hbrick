# Benchmark campaign summary

| Field | Value |
|-------|-------|
| Campaign | `run01` |
| Schema | `2` |
| Started | 2026-06-29T22:55:57Z |
| Git | `d2290b56684303a6a73e1d18c6e9560746c207dd` |
| Build | Release |
| Compiler | GNU 13.3.0 |
| CPU | Intel(R) Core(TM) i7-8650U CPU @ 1.90GHz |
| Threads | 8 |
| Timer | `std::chrono::steady_clock` |

## Results

| Map | Class | Method | Config | Status | QPS | Total vs BFS | Mismatches |
|-----|-------|--------|--------|--------|-----|-------------|------------|
| proc_w5h5_c1_o1_open2_random_asymmetric_i0 | cyclic_maze | CsrBfs | t4x4_k0_mem512m_hg2x2_dfull | Completed | 1.30666e+07 | 1.00x | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i0 | cyclic_maze | BrickSearch | t4x4_k0_mem512m_hg2x2_dfull | Completed | 4.23925e+06 | 0.05x | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i0 | cyclic_maze | BrickClosure | t4x4_k0_mem512m_hg2x2_dfull | Completed | 1.67847e+07 | 0.05x | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i0 | cyclic_maze | HBrick | t4x4_k0_mem512m_hg2x2_dfull | Completed | 6.29426e+06 | 0.03x | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i1 | cyclic_maze | CsrBfs | t4x4_k0_mem512m_hg2x2_dfull | Completed | 1.26258e+07 | 1.00x | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i1 | cyclic_maze | BrickSearch | t4x4_k0_mem512m_hg2x2_dfull | Completed | 1.57055e+07 | 0.06x | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i1 | cyclic_maze | BrickClosure | t4x4_k0_mem512m_hg2x2_dfull | Completed | 1.28669e+07 | 0.05x | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i1 | cyclic_maze | HBrick | t4x4_k0_mem512m_hg2x2_dfull | Completed | 3.50762e+06 | 0.02x | 0 |

## Best completed QPS by map class

| Class | Map | Method | Config | QPS |
|-------|-----|--------|--------|-----|
| cyclic_maze | proc_w5h5_c1_o1_open2_random_asymmetric_i0 | BrickClosure | t4x4_k0_mem512m_hg2x2_dfull | 1.67847e+07 |
