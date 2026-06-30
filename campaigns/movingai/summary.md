# Benchmark campaign summary

| Field | Value |
|-------|-------|
| Campaign | `movingai` |
| Schema | `2` |
| Started | 2026-06-30T21:30:08Z |
| Git | `6b862090eca836b343211e2bd41c6cf72d1d6a00` |
| Build | Release |
| Compiler | GNU 13.3.0 |
| CPU | Intel(R) Core(TM) i7-8650U CPU @ 1.90GHz |
| Threads | 8 |
| Timer | `std::chrono::steady_clock` |

## Results

| Map | Class | Method | Config | Status | QPS | Total vs BFS | Mismatches |
|-----|-------|--------|--------|--------|-----|-------------|------------|
| movingai_bgmaps_AR0408SR.map | movingai | CsrBfs | t4x4_k0_mem512m_hg2x2_dfull | Completed | 9.53658e+06 | 1.00x | 0 |
| movingai_bgmaps_AR0408SR.map | movingai | BrickSearch | t4x4_k0_mem512m_hg2x2_dfull | Completed | 2.95067e+07 | 0.01x | 0 |
| movingai_bgmaps_AR0408SR.map | movingai | BrickClosure | t4x4_k0_mem512m_hg2x2_dfull | Completed | 3.0146e+07 | 0.0082x | 0 |
| movingai_bgmaps_AR0408SR.map | movingai | HBrick | t4x4_k0_mem512m_hg2x2_dfull | Completed | 1.66493e+07 | 0.0014x | 0 |
| movingai_da2_ht_store.map | movingai | CsrBfs | t4x4_k0_mem512m_hg2x2_dfull | Completed | 9.65309e+06 | 1.00x | 0 |
| movingai_da2_ht_store.map | movingai | BrickSearch | t4x4_k0_mem512m_hg2x2_dfull | Completed | 1.03947e+07 | 0.0079x | 0 |
| movingai_da2_ht_store.map | movingai | BrickClosure | t4x4_k0_mem512m_hg2x2_dfull | Completed | 7.09141e+06 | 0.0048x | 0 |
| movingai_da2_ht_store.map | movingai | HBrick | t4x4_k0_mem512m_hg2x2_dfull | Completed | 1.1976e+06 | 0.0009x | 0 |
| movingai_dao_ost102d.map | movingai | CsrBfs | t4x4_k0_mem512m_hg2x2_dfull | Completed | 1.2096e+07 | 1.00x | 0 |
| movingai_dao_ost102d.map | movingai | BrickSearch | t4x4_k0_mem512m_hg2x2_dfull | Completed | 1.34652e+07 | - | 0 |
| movingai_dao_ost102d.map | movingai | BrickClosure | t4x4_k0_mem512m_hg2x2_dfull | Completed | 6.89953e+06 | - | 0 |
| movingai_dao_ost102d.map | movingai | HBrick | t4x4_k0_mem512m_hg2x2_dfull | Completed | 1.85669e+06 | - | 0 |

## Best completed QPS by map class

| Class | Map | Method | Config | QPS |
|-------|-----|--------|--------|-----|
| movingai | movingai_bgmaps_AR0408SR.map | BrickClosure | t4x4_k0_mem512m_hg2x2_dfull | 3.0146e+07 |
