# Benchmark campaign summary

| Field | Value |
|-------|-------|
| Campaign | `run01` |
| Schema | `2` |
| Started | 2026-06-30T21:24:11Z |
| Git | `1062cedbcfce482a401f036fa50c435d7fdb9f84` |
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
| proc_w5h5_c1_o1_open2_random_asymmetric_i0 | cyclic_maze | CsrBfs | t4x4_k0_mem512m_hg2x2_dfull | Completed | 3.38911e+06 | 1.00x | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i0 | cyclic_maze | CsrDfs | t4x4_k0_mem512m_hg2x2_dfull | Completed | 8.16483e+06 | 2.26x | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i0 | cyclic_maze | SccDagSearch | t4x4_k0_mem512m_hg2x2_dfull | Completed | 1.9542e+07 | 1.61x | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i0 | cyclic_maze | SccDagClosure | t4x4_k0_mem512m_hg2x2_dfull | Completed | 2.63077e+07 | 1.31x | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i0 | cyclic_maze | TwoHop | t4x4_k0_mem512m_hg2x2_dfull | Completed | 2.00329e+07 | 1.03x | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i0 | cyclic_maze | Grail | t4x4_k0_mem512m_hg2x2_dfull | Completed | 9.23121e+06 | 1.43x | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i0 | cyclic_maze | FullClosure | t4x4_k0_mem512m_hg2x2_dfull | Completed | 2.65946e+07 | 1.56x | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i1 | cyclic_maze | CsrBfs | t4x4_k0_mem512m_hg2x2_dfull | Completed | 8.53846e+06 | 1.00x | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i1 | cyclic_maze | CsrDfs | t4x4_k0_mem512m_hg2x2_dfull | Completed | 8.24689e+06 | 0.97x | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i1 | cyclic_maze | SccDagSearch | t4x4_k0_mem512m_hg2x2_dfull | Completed | 8.35591e+06 | 0.66x | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i1 | cyclic_maze | SccDagClosure | t4x4_k0_mem512m_hg2x2_dfull | Completed | 1.08374e+07 | 0.38x | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i1 | cyclic_maze | TwoHop | t4x4_k0_mem512m_hg2x2_dfull | Completed | 9.49062e+06 | 0.29x | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i1 | cyclic_maze | Grail | t4x4_k0_mem512m_hg2x2_dfull | Completed | 5.71824e+06 | 0.46x | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i1 | cyclic_maze | FullClosure | t4x4_k0_mem512m_hg2x2_dfull | Completed | 1.08131e+07 | 0.44x | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i0 | cyclic_maze | BrickSearch | t4x4_k1n0_mem512m_hg2x2_dfull | Completed | 2.15742e+07 | - | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i0 | cyclic_maze | BrickClosure | t4x4_k1n0_mem512m_hg2x2_dfull | Completed | 2.18972e+07 | - | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i0 | cyclic_maze | HBrick | t4x4_k1n0_mem512m_hg2x2_dfull | Completed | 9.2239e+06 | - | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i0 | cyclic_maze | BrickSearch | t8x8_k0_mem512m_hg2x2_dfull | Completed | 1.58337e+07 | - | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i0 | cyclic_maze | BrickClosure | t8x8_k0_mem512m_hg2x2_dfull | Completed | 1.65749e+07 | - | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i0 | cyclic_maze | HBrick | t8x8_k0_mem512m_hg2x2_dfull | Completed | 1.49097e+07 | - | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i0 | cyclic_maze | BrickSearch | t8x8_k1n0_mem512m_hg2x2_dfull | Completed | 1.88305e+07 | - | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i0 | cyclic_maze | BrickClosure | t8x8_k1n0_mem512m_hg2x2_dfull | Completed | 1.64029e+07 | - | 0 |
| proc_w5h5_c1_o1_open2_random_asymmetric_i0 | cyclic_maze | HBrick | t8x8_k1n0_mem512m_hg2x2_dfull | Completed | 1.34865e+07 | - | 0 |

## Best completed QPS by map class

| Class | Map | Method | Config | QPS |
|-------|-----|--------|--------|-----|
| cyclic_maze | proc_w5h5_c1_o1_open2_random_asymmetric_i0 | FullClosure | t4x4_k0_mem512m_hg2x2_dfull | 2.65946e+07 |
