# Benchmark campaign summary

| Field | Value |
|-------|-------|
| Campaign | `run02` |
| Schema | `2` |
| Started | 2026-06-30T21:28:46Z |
| Git | `6b862090eca836b343211e2bd41c6cf72d1d6a00` |
| Build | Release |
| Compiler | GNU 13.3.0 |
| CPU | Intel(R) Core(TM) i7-8650U CPU @ 1.90GHz |
| Threads | 8 |
| Timer | `std::chrono::steady_clock` |

## Results

| Map | Class | Method | Config | Status | QPS | Total vs BFS | Mismatches |
|-----|-------|--------|--------|--------|-----|-------------|------------|
| proc_w31h31_c42_o0_open43_random_asymmetric_i0 | perfect_maze | CsrBfs | t4x4_k0_mem512m_hg2x2_dfull | Completed | 9.46046e+06 | 1.00x | 0 |
| proc_w31h31_c42_o0_open43_random_asymmetric_i0 | perfect_maze | BrickSearch | t4x4_k0_mem512m_hg2x2_dfull | Completed | 7.61361e+06 | 0.0016x | 0 |
| proc_w31h31_c42_o0_open43_random_asymmetric_i0 | perfect_maze | BrickClosure | t4x4_k0_mem512m_hg2x2_dfull | Completed | 4.49438e+06 | 0.0008x | 0 |
| proc_w31h31_c42_o0_open43_random_asymmetric_i0 | perfect_maze | HBrick | t4x4_k0_mem512m_hg2x2_dfull | Completed | 257136 | <0.0001x | 0 |
| proc_w31h31_c42_o0_open43_random_asymmetric_i1 | perfect_maze | CsrBfs | t4x4_k0_mem512m_hg2x2_dfull | Completed | 9.35126e+06 | 1.00x | 0 |
| proc_w31h31_c42_o0_open43_random_asymmetric_i1 | perfect_maze | BrickSearch | t4x4_k0_mem512m_hg2x2_dfull | Completed | 2.88874e+06 | 0.0023x | 0 |
| proc_w31h31_c42_o0_open43_random_asymmetric_i1 | perfect_maze | BrickClosure | t4x4_k0_mem512m_hg2x2_dfull | Completed | 1.60389e+06 | 0.0009x | 0 |
| proc_w31h31_c42_o0_open43_random_asymmetric_i1 | perfect_maze | HBrick | t4x4_k0_mem512m_hg2x2_dfull | Completed | 292796 | <0.0001x | 0 |
| proc_w31h31_c42_o0_open43_random_asymmetric_i2 | perfect_maze | CsrBfs | t4x4_k0_mem512m_hg2x2_dfull | Completed | 8.25913e+06 | 1.00x | 0 |
| proc_w31h31_c42_o0_open43_random_asymmetric_i2 | perfect_maze | BrickSearch | t4x4_k0_mem512m_hg2x2_dfull | Completed | 6.25061e+06 | 0.0031x | 0 |
| proc_w31h31_c42_o0_open43_random_asymmetric_i2 | perfect_maze | BrickClosure | t4x4_k0_mem512m_hg2x2_dfull | Completed | 4.14857e+06 | 0.0009x | 0 |
| proc_w31h31_c42_o0_open43_random_asymmetric_i2 | perfect_maze | HBrick | t4x4_k0_mem512m_hg2x2_dfull | Completed | 355090 | 0.0001x | 0 |
| proc_w31h31_c42_o0_open43_random_asymmetric_i3 | perfect_maze | CsrBfs | t4x4_k0_mem512m_hg2x2_dfull | Completed | 8.70748e+06 | 1.00x | 0 |
| proc_w31h31_c42_o0_open43_random_asymmetric_i3 | perfect_maze | BrickSearch | t4x4_k0_mem512m_hg2x2_dfull | Completed | 4.41958e+06 | 0.0019x | 0 |
| proc_w31h31_c42_o0_open43_random_asymmetric_i3 | perfect_maze | BrickClosure | t4x4_k0_mem512m_hg2x2_dfull | Completed | 2.892e+06 | 0.0008x | 0 |
| proc_w31h31_c42_o0_open43_random_asymmetric_i3 | perfect_maze | HBrick | t4x4_k0_mem512m_hg2x2_dfull | Completed | 340975 | <0.0001x | 0 |

## Best completed QPS by map class

| Class | Map | Method | Config | QPS |
|-------|-----|--------|--------|-----|
| perfect_maze | proc_w31h31_c42_o0_open43_random_asymmetric_i0 | CsrBfs | t4x4_k0_mem512m_hg2x2_dfull | 9.46046e+06 |
