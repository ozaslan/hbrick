#!/usr/bin/env python3
"""Audit and summarize H-BRICK manuscript benchmark CSVs into MANUSCRIPT_REPORT.md
and plotting datasets under <campaign_dir>/figure_data/.

Uses Python 3 standard library only. Verifies zero oracle mismatches across
every ingested CSV row and prints a concise summary to stdout.
"""

from __future__ import annotations

import argparse
import csv
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path

MAP_SPECS = [
    ("M03", "M1", "maze512-1-0"),
    ("M04", "M2", "maze512-32-0"),
    ("M05", "M3", "8room_000"),
    ("M06", "M4", "random512-20-0"),
    ("M02", "M5", "arena2"),
    ("M01", "M6", "Boston_0_256"),
    ("M07", "M7", "Boston_0_512"),
    ("M09", "M8", "Boston_0_1024"),
]
PREFIX_TO_PID = {p: pid for p, pid, _ in MAP_SPECS}
PID_TO_NUM = {pid: i + 1 for i, (_, pid, _) in enumerate(MAP_SPECS)}
PID_TO_NAME = {pid: name for _, pid, name in MAP_SPECS}
ACTIVE_PREFIXES = set(PREFIX_TO_PID.keys())
REGIMES = ["V01", "V02", "V03", "V04", "V05", "V06"]
SWEEP_B = [4, 8, 16, 24, 32, 48, 64, 96]
SWEEP_G = [2, 4, 8]


def quantile_sorted(vals: list[float], p: float) -> float:
    if not vals:
        return 0.0
    s = sorted(vals)
    if len(s) == 1:
        return s[0]
    pos = p * (len(s) - 1)
    lo = int(math.floor(pos))
    hi = int(math.ceil(pos))
    frac = pos - lo
    return s[lo] * (1.0 - frac) + s[hi] * frac


def med_q1_q3(vals: list[float]) -> tuple[float, float, float]:
    return (
        quantile_sorted(vals, 0.50),
        quantile_sorted(vals, 0.25),
        quantile_sorted(vals, 0.75),
    )


def gmean(vals: list[float]) -> float:
    pos = [v for v in vals if v > 0.0]
    if not pos:
        return 0.0
    return math.exp(sum(math.log(v) for v in pos) / len(pos))


def pid_from_label(label: str) -> str:
    prefix = label.split("_", 1)[0]
    return PREFIX_TO_PID.get(prefix, "")


def regime_from_label(label: str) -> str:
    parts = label.split("_")
    if len(parts) >= 2 and parts[1].startswith("V"):
        return parts[1]
    return ""


def load_csv(path: Path | None) -> list[dict[str, str]]:
    if path is None or not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def resolve_input(explicit: Path | None, campaign_dir: Path, candidates: list[str]) -> Path | None:
    if explicit is not None:
        return explicit if explicit.is_file() else None
    for name in candidates:
        cand = campaign_dir / name
        if cand.is_file():
            return cand
    return None


def audit_mismatches(datasets: dict[str, list[dict[str, str]]]) -> list[str]:
    errors: list[str] = []
    for label, rows in datasets.items():
        for idx, row in enumerate(rows):
            if "mismatches" in row and row["mismatches"] != "":
                mm = int(float(row["mismatches"]))
                if mm != 0:
                    errors.append(f"{label} row {idx}: mismatches={mm}")
            if "equal_verified" in row and row["equal_verified"] != "":
                if int(row["equal_verified"]) != 1:
                    errors.append(f"{label} row {idx}: equal_verified={row['equal_verified']}")
            if "byte_identical" in row and row["byte_identical"] != "":
                if int(row["byte_identical"]) != 1:
                    errors.append(f"{label} row {idx}: byte_identical={row['byte_identical']}")
    return errors


def build_md_table(headers: list[str], rows: list[list[str]], right_from: int = 1) -> str:
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join("---:" if idx >= right_from else "---" for idx in range(len(headers))) + " |",
    ]
    for row in rows:
        lines.append("| " + " | ".join(row) + " |")
    return "\n".join(lines)


def write_figure_csvs(
    fig_dir: Path,
    graph_rows: list[dict[str, str]],
    sweep_rows: list[dict[str, str]],
    life_rows: list[dict[str, str]],
    batch_ms_rows: list[dict[str, str]],
    pol_rows: list[dict[str, str]],
) -> list[str]:
    fig_dir.mkdir(parents=True, exist_ok=True)
    written: list[str] = []

    # Lookup num_sccs & c_over_n from graph_rows, pol_rows, or life_rows
    scc_lookup: dict[tuple[int, str], tuple[str, str]] = {}
    for r in graph_rows:
        pid = r.get("paper_id") or pid_from_label(r.get("recipe_label", ""))
        reg = r.get("regime", "")
        if pid in PID_TO_NUM and reg:
            scc_lookup[(PID_TO_NUM[pid], reg)] = (r.get("num_sccs", "0"), r.get("scc_ratio", "0"))
    for r in pol_rows:
        pid = r.get("paper_id") or pid_from_label(r.get("recipe_label", ""))
        reg = r.get("regime", "")
        if pid in PID_TO_NUM and reg and (PID_TO_NUM[pid], reg) not in scc_lookup:
            scc_lookup[(PID_TO_NUM[pid], reg)] = (r.get("num_sccs", "0"), r.get("scc_ratio", "0"))
    for r in life_rows:
        pid = pid_from_label(r.get("recipe_label", ""))
        reg = r.get("regime", "")
        if pid in PID_TO_NUM and reg and r.get("num_sccs") and (PID_TO_NUM[pid], reg) not in scc_lookup:
            scc_lookup[(PID_TO_NUM[pid], reg)] = (r["num_sccs"], r.get("c_over_n", "0"))

    # 1. memory_vs_n.csv (operating point b=24, g=4 in V01, fallback to lifecycle)
    n_by_pid: dict[str, int] = {}
    tc_by_pid: dict[str, float] = {}
    for r in graph_rows:
        pid = r.get("paper_id") or pid_from_label(r.get("recipe_label", ""))
        if pid:
            n_by_pid[pid] = int(r["vertices"])
            tc_by_pid[pid] = float(r["tc_gib"])

    v01_mem: dict[str, float] = {}
    for r in sweep_rows:
        if r.get("b") == "24" and r.get("g") == "4" and r.get("method") == "HBrickSkipLift":
            lbl = r.get("recipe_label", "")
            if regime_from_label(lbl) == "V01":
                pid = pid_from_label(lbl)
                if pid:
                    v01_mem[pid] = float(r["index_mb"])
    for r in life_rows:
        if r.get("regime") == "V01" and r.get("method") == "HBrickSkipLift":
            pid = pid_from_label(r.get("recipe_label", ""))
            if pid and pid not in v01_mem:
                v01_mem[pid] = float(r["index_mb"])

    if v01_mem:
        out_path = fig_dir / "memory_vs_n.csv"
        with out_path.open("w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=["map_id", "map_name", "N", "index_mib", "tc_gib"])
            w.writeheader()
            for _, pid, name in MAP_SPECS:
                if pid in v01_mem:
                    w.writerow({
                        "map_id": PID_TO_NUM[pid],
                        "map_name": name,
                        "N": n_by_pid.get(pid, ""),
                        "index_mib": f"{v01_mem[pid]:.3f}",
                        "tc_gib": f"{tc_by_pid[pid]:.6f}" if pid in tc_by_pid else "",
                    })
        written.append(out_path.name)

    # 2. query_vs_orientation.csv, stability.csv, scc_regime.csv
    if life_rows:
        q_rows = []
        scc_rows = []
        stab_buckets: dict[tuple[int, str], list[float]] = defaultdict(list)

        bfs_by_cell: dict[tuple[str, str], float] = {}
        for r in life_rows:
            if r.get("method") == "CsrBFS" and r.get("status") == "Completed":
                pid = pid_from_label(r.get("recipe_label", ""))
                if pid:
                    bfs_by_cell[(pid, r["regime"])] = float(r["mean_query_us"])

        # Prefer b=24, g=4 HBrickSkipLift from sweep if lifecycle came from another tile, else lifecycle
        skip_b24: dict[tuple[str, str], float] = {}
        for r in sweep_rows:
            if r.get("b") == "24" and r.get("g") == "4" and r.get("method") == "HBrickSkipLift" and r.get("status") == "Completed":
                lbl = r.get("recipe_label", "")
                pid = pid_from_label(lbl)
                reg = regime_from_label(lbl)
                if pid and reg:
                    skip_b24[(pid, reg)] = float(r["mean_query_us"])

        method_map = {
            "CsrBFS": "CsrBfs",
            "OReach": "Oreach",
            "HBrickSkipLift": "HBrickSkipLift",
            "SccDagSearch": "SccDagSearch",
        }
        for r in life_rows:
            if r.get("status") != "Completed":
                continue
            pid = pid_from_label(r.get("recipe_label", ""))
            reg = r.get("regime", "")
            m_raw = r.get("method", "")
            if not pid or not reg or m_raw not in method_map:
                continue
            mid = PID_TO_NUM[pid]
            m_norm = method_map[m_raw]
            q_us = float(r["mean_query_us"])
            if m_raw == "HBrickSkipLift" and (pid, reg) in skip_b24:
                q_us_b24 = skip_b24[(pid, reg)]
            else:
                q_us_b24 = q_us

            if m_norm in ("CsrBfs", "Oreach", "HBrickSkipLift"):
                q_rows.append({
                    "map_id": mid,
                    "map_name": PID_TO_NAME[pid],
                    "regime": reg,
                    "method": m_norm,
                    "query_us": f"{q_us:.6f}",
                    "status": "Completed",
                })
            if m_norm in ("CsrBfs", "HBrickSkipLift"):
                stab_buckets[(mid, m_norm)].append(q_us)

            bfs_us = bfs_by_cell.get((pid, reg), 0.0)
            sp_bfs = (bfs_us / q_us_b24) if (bfs_us > 0.0 and q_us_b24 > 0.0) else float(r.get("speedup_vs_bfs", "0") or 0.0)
            num_sccs, c_over_n = scc_lookup.get((mid, reg), ("0", "0.000000"))
            scc_rows.append({
                "map_id": mid,
                "map_name": r.get("map_name", PID_TO_NAME[pid]),
                "regime": reg,
                "method": m_norm,
                "query_us": f"{q_us_b24:.6f}",
                "speedup_vs_bfs": f"{sp_bfs:.2f}",
                "num_sccs": num_sccs,
                "c_over_n": c_over_n,
                "host": "manuscript_bench",
                "status": "Completed",
            })

        if q_rows:
            q_rows.sort(key=lambda x: (int(x["map_id"]), x["regime"], x["method"]))
            out_path = fig_dir / "query_vs_orientation.csv"
            with out_path.open("w", newline="", encoding="utf-8") as f:
                w = csv.DictWriter(f, fieldnames=["map_id", "map_name", "regime", "method", "query_us", "status"])
                w.writeheader()
                w.writerows(q_rows)
            written.append(out_path.name)

        if scc_rows:
            scc_rows.sort(key=lambda x: (int(x["map_id"]), x["regime"], x["method"]))
            out_path = fig_dir / "scc_regime.csv"
            with out_path.open("w", newline="", encoding="utf-8") as f:
                w = csv.DictWriter(
                    f,
                    fieldnames=["map_id", "map_name", "regime", "method", "query_us", "speedup_vs_bfs", "num_sccs", "c_over_n", "host", "status"],
                )
                w.writeheader()
                w.writerows(scc_rows)
            written.append(out_path.name)

        if stab_buckets:
            out_path = fig_dir / "stability.csv"
            with out_path.open("w", newline="", encoding="utf-8") as f:
                w = csv.DictWriter(f, fieldnames=["map_id", "method", "n", "min_us", "max_us", "ratio", "cv_pct"])
                w.writeheader()
                for (mid, meth), vals in sorted(stab_buckets.items()):
                    mn = min(vals)
                    mx = max(vals)
                    mean_v = statistics.mean(vals)
                    sd_v = statistics.stdev(vals) if len(vals) > 1 else 0.0
                    w.writerow({
                        "map_id": mid,
                        "method": meth,
                        "n": len(vals),
                        "min_us": f"{mn:.6f}",
                        "max_us": f"{mx:.6f}",
                        "ratio": f"{(mx / mn) if mn > 0 else 0.0:.6f}",
                        "cv_pct": f"{(sd_v / mean_v * 100.0) if mean_v > 0 else 0.0:.4f}",
                    })
            written.append(out_path.name)

    # 3. query_memory_vs_b.csv (g=4 HBrickSkipLift sweep)
    if sweep_rows:
        b_rows = []
        for r in sweep_rows:
            if r.get("g") != "4" or r.get("method") != "HBrickSkipLift":
                continue
            lbl = r.get("recipe_label", "")
            pid = pid_from_label(lbl)
            reg = regime_from_label(lbl)
            if not pid or not reg:
                continue
            b_rows.append({
                "map_id": PID_TO_NUM[pid],
                "regime": reg,
                "b": int(r["b"]),
                "query_us": f"{float(r['mean_query_us']):.6f}",
                "index_mb": f"{float(r['index_mb']):.4f}",
                "preprocess_s": f"{float(r['preprocess_ms']) / 1000.0:.6f}",
                "status": r.get("status", "Completed"),
            })
        if b_rows:
            b_rows.sort(key=lambda x: (int(x["map_id"]), x["regime"], int(x["b"])))
            out_path = fig_dir / "query_memory_vs_b.csv"
            with out_path.open("w", newline="", encoding="utf-8") as f:
                w = csv.DictWriter(f, fieldnames=["map_id", "regime", "b", "query_us", "index_mb", "preprocess_s", "status"])
                w.writeheader()
                w.writerows(b_rows)
            written.append(out_path.name)

    # 4. batch_speedup.csv
    if batch_ms_rows:
        by_cell: dict[tuple[str, str, int], dict[str, dict[str, str]]] = defaultdict(dict)
        for r in batch_ms_rows:
            pid = r.get("paper_id") or pid_from_label(r.get("recipe_label", ""))
            reg = r.get("regime", "")
            kv = int(r["k"])
            if pid in PID_TO_NUM and reg:
                by_cell[(pid, reg, kv)][r["method"]] = r
        bout = []
        for (pid, reg, kv), mmap in sorted(by_cell.items(), key=lambda x: (PID_TO_NUM[x[0][0]], x[0][1], x[0][2])):
            hb = mmap.get("batch-h-brick")
            sc = mmap.get("scalar-h-brick")
            ore = mmap.get("oreach")
            bfs = mmap.get("bfs-per-source")
            if hb is None:
                continue
            bout.append({
                "map_id": PID_TO_NUM[pid],
                "map_name": PID_TO_NAME[pid],
                "regime": reg,
                "k": kv,
                "method": "batch-h-brick",
                "median_us": hb.get("median_batch_us", ""),
                "ns_per_pair": hb.get("median_ns_per_pair", ""),
                "speedup_vs_scalar": sc.get("batch_speedup_over_method_median", "") if sc else "",
                "speedup_vs_oreach": ore.get("batch_speedup_over_method_median", "") if ore else "",
                "speedup_vs_bfs": bfs.get("batch_speedup_over_method_median", "") if bfs else "",
                "mismatches": hb.get("mismatches", "0"),
                "host": "manuscript_bench",
            })
        if bout:
            out_path = fig_dir / "batch_speedup.csv"
            with out_path.open("w", newline="", encoding="utf-8") as f:
                w = csv.DictWriter(
                    f,
                    fieldnames=[
                        "map_id", "map_name", "regime", "k", "method", "median_us", "ns_per_pair",
                        "speedup_vs_scalar", "speedup_vs_oreach", "speedup_vs_bfs", "mismatches", "host",
                    ],
                )
                w.writeheader()
                w.writerows(bout)
            written.append(out_path.name)

    return written


def generate_report(
    graph_rows: list[dict[str, str]],
    sweep_rows: list[dict[str, str]],
    threeway_rows: list[dict[str, str]],
    life_rows: list[dict[str, str]],
    batch_ms_rows: list[dict[str, str]],
    pol_rows: list[dict[str, str]],
    v06_rows: list[dict[str, str]],
    ck_mat_rows: list[dict[str, str]],
    ck_tile_rows: list[dict[str, str]],
    batch_eq_rows: list[dict[str, str]],
    fig_files: list[str],
) -> str:
    sections: list[str] = [
        "# H-BRICK Manuscript Benchmark Verification Report\n",
        "Generated automatically from campaign CSV outputs by `tools/summarize_manuscript_results.py`.\n",
        f"Audited datasets: `0` oracle mismatches across all present CSVs. Figure datasets emitted: {', '.join(f'`{n}`' for n in fig_files) if fig_files else 'none'}.\n",
    ]

    # --- Tables 4 & 5: Graph stats & calibration ladder ---
    if graph_rows:
        sections.append("## Tables 4 & 5: Benchmark Maps and Calibrated Orientation Ladder\n")
        by_map: dict[str, list[dict[str, str]]] = defaultdict(list)
        by_cell: dict[tuple[str, str], dict[str, str]] = {}
        for r in graph_rows:
            pid = r.get("paper_id") or pid_from_label(r.get("recipe_label", ""))
            reg = r.get("regime", "")
            if pid in PID_TO_NUM:
                by_map[pid].append(r)
                by_cell[(pid, reg)] = r
        t4_rows = []
        for _, pid, name in MAP_SPECS:
            mrows = by_map.get(pid, [])
            if not mrows:
                continue
            r0 = mrows[0]
            pbis = [float(x["p_bi"]) for x in mrows if x.get("regime") != "V06"]
            pbi_rng = f"[{min(pbis):.2f}, {max(pbis):.2f}]" if pbis else "---"
            t4_rows.append([
                pid, name, f"{r0['width']}x{r0['height']}",
                f"{int(r0['vertices']):,}", f"{int(r0['geom_edges']):,}",
                f"{float(r0['geom_edges_per_vertex']):.2f}",
                f"{float(r0['obstacle_pct']):.1f}%", pbi_rng, f"{float(r0['tc_gib']):.3f}",
            ])
        sections.append("### Table 4: Map Properties\n")
        sections.append(build_md_table(
            ["Map", "Name", "Dims", "N", "M_geom", "M_geom/N", "Obst.", "p_bi range", "TC (GiB)"],
            t4_rows, right_from=2,
        ) + "\n")

        t5_rows = []
        for _, pid, _ in MAP_SPECS:
            row_cells = [pid]
            for reg in REGIMES:
                c = by_cell.get((pid, reg))
                if not c:
                    row_cells.append("---")
                    continue
                rho = float(c["reach_density_pct"])
                pbi = float(c["p_bi"])
                if reg == "V06":
                    row_cells.append(f"{rho:.3f}%" if rho < 0.05 else f"{rho:.1f}%")
                elif pid == "M1" and reg != "V01":
                    scc_pct = float(c["scc_ratio"]) * 100.0
                    row_cells.append(f"{rho:.3f}% (p_bi={pbi:.2f}, SCC={scc_pct:.0f}%)")
                else:
                    row_cells.append(f"{rho:.1f}% ({pbi:.2f})")
            t5_rows.append(row_cells)
        sections.append("### Table 5: Calibrated Reachability Density Ladder\n")
        sections.append(build_md_table(["Map"] + REGIMES, t5_rows, right_from=1) + "\n")

    # --- Tables 6, 7, 8, 9: Parameter sweep & Flat BRICK ablation ---
    if sweep_rows:
        skip_rows = [r for r in sweep_rows if r.get("method") == "HBrickSkipLift" and r.get("status") == "Completed"]
        flat_rows = [r for r in sweep_rows if r.get("method") == "BrickSearch" and r.get("status") == "Completed"]

        if skip_rows:
            sections.append("## Tables 6 & 7: HBrickSkipLift Parameter Sweep (b x g)\n")
            cell_map_g: dict[tuple[str, str, int, int], dict[str, str]] = {}
            for r in skip_rows:
                lbl = r.get("recipe_label", "")
                pid = pid_from_label(lbl)
                reg = regime_from_label(lbl)
                if pid and reg:
                    cell_map_g[(pid, reg, int(r["b"]), int(r["g"]))] = r

            t6_rows = []
            for _, pid, _ in MAP_SPECS:
                for gv in SWEEP_G:
                    pre_cols, q_cols, mem_cols = [], [], []
                    for bv in SWEEP_B:
                        hits = [cell_map_g[(pid, reg, bv, gv)] for reg in REGIMES if (pid, reg, bv, gv) in cell_map_g]
                        if hits:
                            pre_cols.append(f"{statistics.median(float(h['preprocess_ms'])/1000.0 for h in hits):.2f}")
                            q_cols.append(f"{statistics.median(float(h['mean_query_us']) for h in hits):.2f}")
                            mem_cols.append(f"{statistics.median(float(h['index_mb']) for h in hits):.1f}")
                        else:
                            pre_cols.append("---")
                            q_cols.append("---")
                            mem_cols.append("---")
                    t6_rows.append([pid, str(gv)] + pre_cols + q_cols + mem_cols)
            if t6_rows:
                hdr = (
                    ["Map", "g"]
                    + [f"Pre(s) b={b}" for b in SWEEP_B]
                    + [f"Q(us) b={b}" for b in SWEEP_B]
                    + [f"Mem(MiB) b={b}" for b in SWEEP_B]
                )
                sections.append("### Table 6: Per-Map Medians over V01..V06\n")
                sections.append(build_md_table(hdr, t6_rows, right_from=1) + "\n")

            t7_rows = []
            for reg in REGIMES:
                for gv in SWEEP_G:
                    pre_cols, q_cols, mem_cols = [], [], []
                    for bv in SWEEP_B:
                        hits = [cell_map_g[(pid, reg, bv, gv)] for _, pid, _ in MAP_SPECS if (pid, reg, bv, gv) in cell_map_g]
                        if hits:
                            pre_cols.append(f"{statistics.median(float(h['preprocess_ms'])/1000.0 for h in hits):.2f}")
                            q_cols.append(f"{statistics.median(float(h['mean_query_us']) for h in hits):.2f}")
                            mem_cols.append(f"{statistics.median(float(h['index_mb']) for h in hits):.1f}")
                        else:
                            pre_cols.append("---")
                            q_cols.append("---")
                            mem_cols.append("---")
                    t7_rows.append([reg, str(gv)] + pre_cols + q_cols + mem_cols)
            if t7_rows:
                hdr = (
                    ["Regime", "g"]
                    + [f"Pre(s) b={b}" for b in SWEEP_B]
                    + [f"Q(us) b={b}" for b in SWEEP_B]
                    + [f"Mem(MiB) b={b}" for b in SWEEP_B]
                )
                sections.append("### Table 7: Per-Regime Medians over M1..M8\n")
                sections.append(build_md_table(hdr, t7_rows, right_from=1) + "\n")

        if flat_rows:
            sections.append("## Tables 8 & 9: Single-Level Flat BRICK (BrickSearch) vs. H-BRICK-Skip (g=4)\n")
            flat_cell: dict[tuple[str, str, int], dict[str, str]] = {}
            skip_g4_cell: dict[tuple[str, str, int], dict[str, str]] = {}
            for r in flat_rows:
                lbl = r.get("recipe_label", "")
                pid, reg = pid_from_label(lbl), regime_from_label(lbl)
                if pid and reg:
                    flat_cell[(pid, reg, int(r["b"]))] = r
            for r in skip_rows:
                if r.get("g") == "4":
                    lbl = r.get("recipe_label", "")
                    pid, reg = pid_from_label(lbl), regime_from_label(lbl)
                    if pid and reg:
                        skip_g4_cell[(pid, reg, int(r["b"]))] = r

            def build_flat_table(row_keys: list[str], by_map_mode: bool) -> str:
                rows_abs, rows_ratio = [], []
                for rk in row_keys:
                    pre_c, q_c, mem_c = [], [], []
                    rpre_c, rq_c, rmem_c = [], [], []
                    for bv in SWEEP_B:
                        other_keys = REGIMES if by_map_mode else [p for _, p, _ in MAP_SPECS]
                        fhits = [flat_cell[(rk, ok, bv) if by_map_mode else (ok, rk, bv)]
                                 for ok in other_keys if ((rk, ok, bv) if by_map_mode else (ok, rk, bv)) in flat_cell]
                        shits = [skip_g4_cell[(rk, ok, bv) if by_map_mode else (ok, rk, bv)]
                                 for ok in other_keys if ((rk, ok, bv) if by_map_mode else (ok, rk, bv)) in skip_g4_cell]
                        if fhits:
                            f_pre = statistics.mean(float(x["preprocess_ms"]) / 1000.0 for x in fhits)
                            f_q = statistics.mean(float(x["mean_query_us"]) for x in fhits)
                            f_mem = statistics.mean(float(x["index_mb"]) for x in fhits)
                            pre_c.append(f"{f_pre:.2f}")
                            q_c.append(f"{f_q:.0f}" if f_q >= 100.0 else f"{f_q:.2f}")
                            mem_c.append(f"{f_mem:.1f}")
                            if shits:
                                s_pre = statistics.mean(float(x["preprocess_ms"]) / 1000.0 for x in shits)
                                s_q = statistics.mean(float(x["mean_query_us"]) for x in shits)
                                s_mem = statistics.mean(float(x["index_mb"]) for x in shits)
                                rpre_c.append(f"{(f_pre / s_pre) if s_pre > 0 else 0.0:.2f}")
                                q_rat = (f_q / s_q) if s_q > 0 else 0.0
                                rq_c.append(f"{q_rat:.0f}" if q_rat >= 100.0 else f"{q_rat:.1f}")
                                rmem_c.append(f"{(f_mem / s_mem) if s_mem > 0 else 0.0:.2f}")
                            else:
                                rpre_c.append("---"); rq_c.append("---"); rmem_c.append("---")
                        else:
                            pre_c.append("---"); q_c.append("---"); mem_c.append("---")
                            rpre_c.append("---"); rq_c.append("---"); rmem_c.append("---")
                    rows_abs.append([rk] + pre_c + q_c + mem_c)
                    rows_ratio.append([f"{rk} (Flat/Skip ratio)"] + rpre_c + rq_c + rmem_c)
                hdr = (
                    ["Key"]
                    + [f"Pre(s) b={b}" for b in SWEEP_B]
                    + [f"Q(us) b={b}" for b in SWEEP_B]
                    + [f"Mem(MiB) b={b}" for b in SWEEP_B]
                )
                return build_md_table(hdr, rows_abs + rows_ratio, right_from=1)

            sections.append("### Table 8: Flat BRICK by Map (Means over Regimes)\n")
            sections.append(build_flat_table([p for _, p, _ in MAP_SPECS], True) + "\n")
            sections.append("### Table 9: Flat BRICK by Regime (Means over Maps)\n")
            sections.append(build_flat_table(REGIMES, False) + "\n")

    # --- Table 10: Three-way ablation at (b=24, g=4) ---
    if threeway_rows:
        sections.append("## Table 10: Matched Three-Way Ablation at (b=24, g=4)\n")
        by_m_3w: dict[str, list[dict[str, str]]] = defaultdict(list)
        flat_q_by_lbl: dict[str, float] = {}
        for r in threeway_rows:
            if r.get("status") == "Completed":
                by_m_3w[r["method"]].append(r)
                if r["method"] == "BrickSearch":
                    flat_q_by_lbl[r["recipe_label"]] = float(r["mean_query_us"])

        t10_rows = []
        for m in ("BrickSearch", "HBrick", "HBrickSkipLift"):
            mrows = by_m_3w.get(m, [])
            if not mrows:
                continue
            pre_m, pre_q1, pre_q3 = med_q1_q3([float(r["preprocess_ms"]) / 1000.0 for r in mrows])
            mem_m, mem_q1, mem_q3 = med_q1_q3([float(r["index_mb"]) for r in mrows])
            ql_m, ql_q1, ql_q3 = med_q1_q3([float(r["mean_query_us"]) for r in mrows])
            sp_bfs_vals = [float(r["speedup_vs_bfs"]) for r in mrows]
            sp_bfs_g = gmean(sp_bfs_vals)
            _, sb_q1, sb_q3 = med_q1_q3(sp_bfs_vals)
            if m == "BrickSearch":
                vs_flat_str = "1.00x [1.00, 1.00]"
            else:
                vf_vals = [
                    flat_q_by_lbl[r["recipe_label"]] / float(r["mean_query_us"])
                    for r in mrows
                    if r["recipe_label"] in flat_q_by_lbl and float(r["mean_query_us"]) > 0.0
                ]
                vf_g = gmean(vf_vals)
                _, vf_q1, vf_q3 = med_q1_q3(vf_vals)
                vs_flat_str = f"{vf_g:.2f}x [{vf_q1:.2f}, {vf_q3:.2f}]"
            t10_rows.append([
                m,
                f"{pre_m:.2f} [{pre_q1:.2f}, {pre_q3:.2f}]",
                f"{mem_m:.1f} [{mem_q1:.1f}, {mem_q3:.1f}]",
                f"{ql_m:.2f} [{ql_q1:.2f}, {ql_q3:.2f}]",
                vs_flat_str,
                f"{sp_bfs_g:.2f}x [{sb_q1:.2f}, {sb_q3:.2f}]",
            ])
        sections.append(build_md_table(
            ["Method", "Preprocess (s) med [Q1, Q3]", "Index (MiB) med [Q1, Q3]", "Query (us) med [Q1, Q3]", "Speedup vs Flat (gmean)", "Speedup vs BFS (gmean)"],
            t10_rows, right_from=1,
        ) + "\n")

    # --- Tables 11, 12, 13: Multi-sample batch ---
    if batch_ms_rows:
        sections.append("## Tables 11, 12 & 13: Multi-Sample Alternating-Order Batched Queries (b=24, g=4)\n")
        sc_cell: dict[tuple[str, str, int], float] = {}
        for r in batch_ms_rows:
            if r.get("method") == "scalar-h-brick":
                pid = r.get("paper_id") or pid_from_label(r.get("recipe_label", ""))
                sc_cell[(pid, r["regime"], int(r["k"]))] = float(r["batch_speedup_over_method_median"])
        t11_rows = []
        for _, pid, _ in MAP_SPECS:
            row_c = [pid]
            for reg in REGIMES:
                for kv in (4, 16, 64):
                    val = sc_cell.get((pid, reg, kv))
                    row_c.append(f"{val:.2f}" if val is not None else "---")
            t11_rows.append(row_c)
        sections.append("### Table 11: Batched Speedup over Scalar Loop (T_scalar / T_batch)\n")
        sections.append(build_md_table(
            ["Map"] + [f"{reg} k={kv}" for reg in REGIMES for kv in (4, 16, 64)],
            t11_rows, right_from=1,
        ) + "\n")

        # Table 12: k=64 amortized ns/pair
        methods_12 = [
            "batch-h-brick",
            "scalar-h-brick",
            "bfs-per-source",
            "scc-dag-per-source",
            "scc-dag-dedup",
            "oreach",
        ]
        k64_by_m: dict[str, list[dict[str, str]]] = defaultdict(list)
        for r in batch_ms_rows:
            if int(r["k"]) == 64:
                k64_by_m[r["method"]].append(r)
        t12_rows = []
        for m in methods_12:
            mrows = k64_by_m.get(m, [])
            if not mrows:
                continue
            reg_meds = []
            for reg in REGIMES:
                vals = [float(r["median_ns_per_pair"]) for r in mrows if r["regime"] == reg]
                reg_meds.append(f"{statistics.median(vals):.1f}" if vals else "---")
            all_med, all_q1, all_q3 = med_q1_q3([float(r["median_ns_per_pair"]) for r in mrows])
            t12_rows.append([m] + reg_meds + [f"{all_med:.1f}", f"[{all_q1:.1f}, {all_q3:.1f}]"])
        sections.append("### Table 12: Amortized Latency (ns/pair) at k=64\n")
        sections.append(build_md_table(
            ["Method"] + REGIMES + ["All 48 med", "[Q1, Q3]"],
            t12_rows, right_from=1,
        ) + "\n")

        # Table 13: Speedup T_base / T_batch
        t13_rows = []
        for m in methods_12[1:]:
            cols = [f"vs {m}"]
            for kv in (4, 16, 64):
                vals = [float(r["batch_speedup_over_method_median"]) for r in batch_ms_rows if r["method"] == m and int(r["k"]) == kv]
                if vals:
                    med, q1, q3 = med_q1_q3(vals)
                    cols.append(f"{med:.2f}x [{q1:.2f}, {q3:.2f}]")
                else:
                    cols.append("---")
            v06_k64 = [float(r["batch_speedup_over_method_median"]) for r in batch_ms_rows if r["method"] == m and int(r["k"]) == 64 and r["regime"] == "V06"]
            if v06_k64:
                wins = sum(1 for v in v06_k64 if v > 1.0)
                cols.append(f"{statistics.median(v06_k64):.2f}x ({wins}/{len(v06_k64)})")
            else:
                cols.append("---")
            t13_rows.append(cols)
        sections.append("### Table 13: Batched Speedup (T_base / T_batch)\n")
        sections.append(build_md_table(
            ["Baseline", "k=4 med [Q1, Q3]", "k=16 med [Q1, Q3]", "k=64 med [Q1, Q3]", "V06 k=64 med (wins)"],
            t13_rows, right_from=1,
        ) + "\n")

    # --- Tables 14, 15, 16 & TwoHop summary ---
    if life_rows:
        life_map: dict[tuple[str, str, str], dict[str, str]] = {}
        for r in life_rows:
            pid = pid_from_label(r.get("recipe_label", ""))
            reg = r.get("regime", "")
            if pid and reg:
                life_map[(pid, reg, r["method"])] = r

        # Operating point HBrickSkipLift (prefer lifecycle if at b=24, else param_sweep b=24,g=4, else lifecycle)
        skip_op: dict[tuple[str, str], dict[str, str]] = {}
        for r in sweep_rows:
            if r.get("b") == "24" and r.get("g") == "4" and r.get("method") == "HBrickSkipLift" and r.get("status") == "Completed":
                lbl = r.get("recipe_label", "")
                pid, reg = pid_from_label(lbl), regime_from_label(lbl)
                if pid and reg:
                    skip_op[(pid, reg)] = r
        for (pid, reg, meth), r in life_map.items():
            if meth == "HBrickSkipLift" and r.get("status") == "Completed" and (pid, reg) not in skip_op:
                skip_op[(pid, reg)] = r

        # Table 14: Break-even Q_BE
        if skip_op:
            sections.append("## Table 14: Break-Even Query Threshold Q_BE (HBrickSkipLift at b=24, g=4)\n")
            t14_rows = []
            for reg in REGIMES:
                row_c = [reg]
                for _, pid, _ in MAP_SPECS:
                    bfs_r = life_map.get((pid, reg, "CsrBFS"))
                    hb_r = skip_op.get((pid, reg))
                    if not bfs_r or not hb_r:
                        row_c.append("---")
                        continue
                    bfs_us = float(bfs_r["mean_query_us"])
                    hb_us = float(hb_r["mean_query_us"])
                    pre_us = float(hb_r["preprocess_ms"]) * 1000.0
                    sp = bfs_us / hb_us if hb_us > 0.0 else 0.0
                    if bfs_us > hb_us:
                        q_be = int(round(pre_us / (bfs_us - hb_us)))
                        row_c.append(f"{q_be:,} ({sp:.1f}x)")
                    else:
                        row_c.append(f"--- ({sp:.2f}x)")
                t14_rows.append(row_c)
            sections.append(build_md_table(["Regime"] + [p for _, p, _ in MAP_SPECS], t14_rows, right_from=1) + "\n")

        # Table 15: External regime summary
        sections.append("## Tables 15 & 16 and Section 7.6 Summary: External Baselines\n")
        tw_map: dict[tuple[str, str, str], dict[str, str]] = {}
        for r in threeway_rows:
            pid = pid_from_label(r.get("recipe_label", ""))
            reg = r.get("regime", "")
            if pid and reg and r.get("status") == "Completed":
                tw_map[(pid, reg, r["method"])] = r

        methods_15 = [
            ("CsrBFS", "BFS", "life"),
            ("CsrDFS", "DFS", "life"),
            ("SccDagSearch", "SCC-DAG", "life"),
            ("Grail", "GRAIL", "life"),
            ("OReach", "O'Reach", "life"),
            ("BrickSearch", "Flat BRICK (b=24)", "threeway_or_life"),
            ("HBrick", "H-BRICK (b=24,g=4)", "threeway_or_life"),
            ("HBrickSkipLift", "H-BRICK-Skip (b=24,g=4)", "skip_op"),
        ]
        t15_rows = []
        for m_key, disp, src_mode in methods_15:
            cols = [disp]
            all_ratios: list[float] = []
            for reg in REGIMES:
                lats = []
                for _, pid, _ in MAP_SPECS:
                    row = None
                    if src_mode == "life":
                        row = life_map.get((pid, reg, m_key))
                    elif src_mode == "threeway_or_life":
                        row = tw_map.get((pid, reg, m_key)) or life_map.get((pid, reg, m_key))
                    else:
                        row = skip_op.get((pid, reg))
                    if row and row.get("status") == "Completed":
                        q_us = float(row["mean_query_us"])
                        lats.append(q_us)
                        bfs_r = (tw_map.get((pid, reg, "CsrBFS")) if src_mode == "threeway_or_life" and (pid, reg, "CsrBFS") in tw_map else life_map.get((pid, reg, "CsrBFS")))
                        if bfs_r and q_us > 0.0:
                            all_ratios.append(float(bfs_r["mean_query_us"]) / q_us)
                if lats:
                    m_v, q1_v, q3_v = med_q1_q3(lats)
                    cols.append(f"{m_v:.3g} [{q1_v:.3g}, {q3_v:.3g}]")
                else:
                    cols.append("---")
            if all_ratios:
                gm = gmean(all_ratios)
                _, r_q1, r_q3 = med_q1_q3(all_ratios)
                cols.append(f"{gm:.3g}x [{r_q1:.3g}, {r_q3:.3g}]")
            else:
                cols.append("---")
            t15_rows.append(cols)
        sections.append("### Table 15: Per-Regime Query Latency Medians [Q1, Q3] (us) & Overall BFS Speedup\n")
        sections.append(build_md_table(["Method"] + REGIMES + ["Overall vs BFS (gmean [Q1, Q3])"], t15_rows, right_from=1) + "\n")

        # Table 16: V06 map-by-map speedups
        t16_rows = []
        for m_key, disp in (("SccDagSearch", "SCC-DAG (vs BFS)"), ("Grail", "GRAIL (vs BFS)"), ("OReach", "O'Reach (vs BFS)")):
            row_c = [disp]
            for _, pid, _ in MAP_SPECS:
                r = life_map.get((pid, "V06", m_key))
                row_c.append(f"{float(r['speedup_vs_bfs']):.2f}x" if r and r.get("status") == "Completed" else "---")
            t16_rows.append(row_c)
        row_hb_bfs = ["H-BRICK-Skip (vs BFS)"]
        row_hb_or = ["H-BRICK-Skip (vs O'Reach)"]
        for _, pid, _ in MAP_SPECS:
            bfs_r = life_map.get((pid, "V06", "CsrBFS"))
            or_r = life_map.get((pid, "V06", "OReach"))
            hb_r = skip_op.get((pid, "V06"))
            if bfs_r and hb_r and float(hb_r["mean_query_us"]) > 0.0:
                hb_q = float(hb_r["mean_query_us"])
                row_hb_bfs.append(f"{float(bfs_r['mean_query_us']) / hb_q:.2f}x")
                if or_r and float(or_r["mean_query_us"]) > 0.0:
                    row_hb_or.append(f"{float(or_r['mean_query_us']) / hb_q:.2f}x")
                else:
                    row_hb_or.append("---")
            else:
                row_hb_bfs.append("---")
                row_hb_or.append("---")
        t16_rows.extend([row_hb_bfs, row_hb_or])
        sections.append("### Table 16: Gradient-Flow V06 Map-by-Map Speedups\n")
        sections.append(build_md_table(["Method / Metric"] + [p for _, p, _ in MAP_SPECS], t16_rows, right_from=1) + "\n")

        # Section 7.6 TwoHop summary
        th_rows = [r for r in life_rows if r.get("method") == "TwoHop"]
        if th_rows:
            ok = [r for r in th_rows if r.get("status") == "Completed"]
            skipped = [r for r in th_rows if r.get("status") != "Completed"]
            sections.append(
                f"### Section 7.6 TwoHop Baseline Coverage\n\n"
                f"Completed: **{len(ok)}/{len(th_rows)}** instances; "
                f"Skipped/Timed Out (`SkippedByPolicy`/`OutOfMemory`/`Timeout`): **{len(skipped)}/{len(th_rows)}**.\n"
            )
            if ok:
                th_list = []
                for r in ok:
                    pid = pid_from_label(r["recipe_label"])
                    reg = r["regime"]
                    hb_r = skip_op.get((pid, reg))
                    hb_q = f"{float(hb_r['mean_query_us']):.3f}" if hb_r else "---"
                    th_list.append([
                        pid, reg, f"{float(r['index_mb']):.1f}", f"{float(r['preprocess_ms'])/1000.0:.3f}",
                        f"{float(r['mean_query_us']):.3f}", hb_q,
                    ])
                sections.append(build_md_table(
                    ["Map", "Regime", "TwoHop Mem (MiB)", "TwoHop Pre (s)", "TwoHop Q (us)", "HBrickSkip Q (us)"],
                    th_list, right_from=2,
                ) + "\n")

    # --- Table 17: Polarity split ---
    if pol_rows:
        sections.append("## Table 17: Reachable (R=1) vs. Unreachable (R=0) Query Latency (us)\n")
        rows_17 = []
        for m_key, disp in (("OReach", "O'Reach"), ("HBrickSkipLift", "H-BRICK-Skip")):
            for pol_key, pol_lbl in (("reach_mean_us", "R=1"), ("unreach_mean_us", "R=0")):
                cols = [f"{disp} ({pol_lbl})"]
                for reg in REGIMES:
                    vals = []
                    for r in pol_rows:
                        if r.get("method") != m_key or r.get("regime") != reg:
                            continue
                        if pol_lbl == "R=1" and int(r.get("n_reach", "0")) > 0:
                            vals.append(float(r[pol_key]))
                        elif pol_lbl == "R=0" and int(r.get("n_unreach", "0")) > 0:
                            vals.append(float(r[pol_key]))
                    if vals:
                        m_v, q1_v, q3_v = med_q1_q3(vals)
                        cols.append(f"{m_v:.3f} [{q1_v:.3f}, {q3_v:.3f}]")
                    else:
                        cols.append("---")
                rows_17.append(cols)
        sections.append(build_md_table(["Method (us)"] + REGIMES, rows_17, right_from=1) + "\n")

    # --- Table 18: V06 (theta, p_back) sensitivity ---
    if v06_rows:
        sections.append("## Table 18: V06 Gradient-Flow (theta, p_back) Sensitivity vs. O'Reach\n")
        thetas = sorted({float(r["theta_deg"]) for r in v06_rows})
        pbacks = sorted({float(r["p_back"]) for r in v06_rows})
        subsets = [
            ("Maps M2..M8, all pairs", lambda r: r["paper_id"] != "M1", "hbrick_vs_oreach_overall"),
            ("Maps M2..M8, reachable (R=1)", lambda r: r["paper_id"] != "M1", "hbrick_vs_oreach_reach"),
            ("All maps (M1..M8), all pairs", lambda r: True, "hbrick_vs_oreach_overall"),
        ]
        t18_rows = []
        for s_lbl, pred, col in subsets:
            for th in thetas:
                cols = [s_lbl, f"{th:.1f} deg"]
                all_th_vals = []
                for pb in pbacks:
                    vals = [
                        float(r[col])
                        for r in v06_rows
                        if pred(r) and abs(float(r["theta_deg"]) - th) < 1e-6 and abs(float(r["p_back"]) - pb) < 1e-6
                    ]
                    all_th_vals.extend(vals)
                    cols.append(f"{gmean(vals):.2f}x" if vals else "---")
                wins = sum(1 for v in all_th_vals if v > 1.0)
                cols.append(f"{wins} / {len(all_th_vals)}")
                t18_rows.append(cols)
        sections.append(build_md_table(
            ["Subset", "Flow angle"] + [f"p_back={pb:.2f}" for pb in pbacks] + ["Wins"],
            t18_rows, right_from=2,
        ) + "\n")

    # --- Table 19: Equal-pair batch comparison ---
    if batch_eq_rows:
        sections.append("## Table 19: Equal-Pair Direct Batch Comparison (65,536 pairs per k)\n")
        t19_rows = []
        # Detect active recipes only
        active_eq = []
        for r in batch_eq_rows:
            rec = r.get("recipe", "")
            prefix = rec.split("_", 1)[0]
            if prefix in ACTIVE_PREFIXES:
                active_eq.append(r)
        ref_k = 64 if any(int(r["k"]) == 64 for r in active_eq) else max((int(r["k"]) for r in active_eq), default=64)
        or_ref_by_reg: dict[str, list[float]] = defaultdict(list)
        hb_ref_by_reg: dict[str, list[float]] = defaultdict(list)
        or_all: dict[str, list[float]] = defaultdict(list)
        hb_by_k_reg: dict[tuple[int, str], list[float]] = defaultdict(list)
        for r in active_eq:
            reg = r.get("regime") or regime_from_label(r.get("recipe", ""))
            kv = int(r["k"])
            pairs = float(kv * kv)
            mean_ns = float(r["mean_us"]) * 1000.0 / pairs
            if r["method"] == "oreach":
                or_all[reg].append(mean_ns)
                if kv == ref_k:
                    or_ref_by_reg[reg].append(mean_ns)
            elif r["method"] == "batch-h-brick":
                hb_by_k_reg[(kv, reg)].append(mean_ns)
                if kv == ref_k:
                    hb_ref_by_reg[reg].append(mean_ns)
        or_row = [f"O'Reach (k={ref_k})"] + [
            f"{statistics.median(or_ref_by_reg[reg]):.1f}" if or_ref_by_reg[reg] else "---"
            for reg in REGIMES
        ]
        t19_rows.append(or_row)
        k_seen = sorted({int(r["k"]) for r in active_eq if r["method"] == "batch-h-brick"})
        for kv in k_seen:
            t19_rows.append([f"Batched H-BRICK-Skip (k={kv})"] + [
                f"{statistics.median(hb_by_k_reg[(kv, reg)]):.1f}" if hb_by_k_reg[(kv, reg)] else "---"
                for reg in REGIMES
            ])
        ratio_row = [f"k={ref_k} vs O'Reach"]
        for reg in REGIMES:
            if or_ref_by_reg[reg] and hb_ref_by_reg[reg]:
                ratio_row.append(f"{statistics.median(or_ref_by_reg[reg]) / statistics.median(hb_ref_by_reg[reg]):.2f}x")
            else:
                ratio_row.append("---")
        t19_rows.append(ratio_row)
        sections.append(build_md_table(["Method / k (ns/pair)"] + REGIMES, t19_rows, right_from=1) + "\n")

    # --- Sections 6.1 & 7.1 Closure Kernel Summary ---
    if ck_mat_rows or ck_tile_rows:
        sections.append("## Sections 6.1 & 7.1: Adaptive Closure Kernel Verification\n")
        if ck_mat_rows:
            dims = sorted({int(r["dimension"]) for r in ck_mat_rows})
            ck_out = []
            for d in dims:
                drows = [r for r in ck_mat_rows if int(r["dimension"]) == d]
                w_wins = sum(1 for r in drows if r["faster_kernel"] == "Warshall")
                ratios = [float(r["warshall_over_m4rm"]) for r in drows]
                ck_out.append([
                    str(d),
                    f"{min(float(r['warshall_median_us']) for r in drows):.1f}..{max(float(r['warshall_median_us']) for r in drows):.1f}",
                    f"{min(float(r['m4rm_median_us']) for r in drows):.1f}..{max(float(r['m4rm_median_us']) for r in drows):.1f}",
                    f"{statistics.median(ratios):.2f}x",
                    f"{w_wins}/{len(drows)} Warshall",
                ])
            sections.append("### Isolated Warshall vs. M4RM Kernel Sweep\n")
            sections.append(build_md_table(
                ["Dimension d", "Warshall (us)", "M4RM (us)", "Median W/M4RM", "Faster kernel"],
                ck_out, right_from=0,
            ) + "\n")
        if ck_tile_rows:
            byte_ok = sum(1 for r in ck_tile_rows if int(r["byte_identical"]) == 1)
            sections.append(
                f"End-to-end tile vs. kernel ablation rows: **{len(ck_tile_rows)}**; "
                f"byte-identical index storage across `warshall`, `m4rm`, and `hybrid`: **{byte_ok}/{len(ck_tile_rows)}**.\n"
            )

    return "\n".join(sections)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--campaign-dir", type=Path, default=Path("campaigns/manuscript"))
    p.add_argument("--graph-csv", type=Path, default=None)
    p.add_argument("--param-sweep-csv", type=Path, default=None)
    p.add_argument("--threeway-csv", type=Path, default=None)
    p.add_argument("--lifecycle-csv", type=Path, default=None)
    p.add_argument("--batch-multisample-csv", type=Path, default=None)
    p.add_argument("--polarity-csv", type=Path, default=None)
    p.add_argument("--v06-csv", type=Path, default=None)
    p.add_argument("--closure-matrix-csv", type=Path, default=None)
    p.add_argument("--closure-tile-csv", type=Path, default=None)
    p.add_argument("--batch-equal-csv", type=Path, default=None)
    p.add_argument("--out-report", type=Path, default=None)
    p.add_argument("--figure-data-dir", type=Path, default=None)
    return p.parse_args()


def main() -> int:
    args = parse_args()
    cdir = args.campaign_dir
    out_report = args.out_report or (cdir / "MANUSCRIPT_REPORT.md")
    fig_dir = args.figure_data_dir or (cdir / "figure_data")

    graph_rows = load_csv(resolve_input(args.graph_csv, cdir, ["graph_and_calibration.csv"]))
    sweep_rows = load_csv(resolve_input(args.param_sweep_csv, cdir, ["param_sweep_results.csv", "results.csv"]))
    threeway_rows = load_csv(resolve_input(args.threeway_csv, cdir, ["threeway_b24_g4_results.csv"]))
    life_rows = load_csv(resolve_input(args.lifecycle_csv, cdir, ["lifecycle_results.csv"]))
    batch_ms_rows = load_csv(resolve_input(args.batch_multisample_csv, cdir, ["batch_multisample.csv", "exp5_6_batch_abba_multisample.csv"]))
    pol_rows = load_csv(resolve_input(args.polarity_csv, cdir, ["polarity_split.csv", "exp3a_canonical_reach_unreach.csv"]))
    v06_rows = load_csv(resolve_input(args.v06_csv, cdir, ["v06_sensitivity.csv", "exp3b_v06_sensitivity.csv"]))
    ck_mat_rows = load_csv(resolve_input(args.closure_matrix_csv, cdir, ["closure_kernel_matrix.csv", "exp7a_kernel_size_density.csv"]))
    ck_tile_rows = load_csv(resolve_input(args.closure_tile_csv, cdir, ["closure_tile_ablation.csv", "exp7b_tile_vs_kernel_ablation.csv"]))
    batch_eq_rows = load_csv(resolve_input(args.batch_equal_csv, cdir, ["batch_equal_pairs.csv", "skiplift_oreach_results.csv"]))

    datasets = {
        "graph_and_calibration": graph_rows,
        "param_sweep": sweep_rows,
        "threeway": threeway_rows,
        "lifecycle": life_rows,
        "batch_multisample": batch_ms_rows,
        "polarity_split": pol_rows,
        "v06_sensitivity": v06_rows,
        "closure_kernel_matrix": ck_mat_rows,
        "closure_tile_ablation": ck_tile_rows,
        "batch_equal_pairs": batch_eq_rows,
    }
    errors = audit_mismatches(datasets)
    if errors:
        for err in errors[:20]:
            print(f"ERROR: {err}", file=sys.stderr)
        return 2

    fig_files = write_figure_csvs(fig_dir, graph_rows, sweep_rows, life_rows, batch_ms_rows, pol_rows)
    report = generate_report(
        graph_rows, sweep_rows, threeway_rows, life_rows, batch_ms_rows,
        pol_rows, v06_rows, ck_mat_rows, ck_tile_rows, batch_eq_rows, fig_files,
    )
    out_report.parent.mkdir(parents=True, exist_ok=True)
    out_report.write_text(report, encoding="utf-8")

    non_empty = {k: len(v) for k, v in datasets.items() if v}
    print(f"Verified 0 mismatches across {len(non_empty)} datasets: {non_empty}")
    print(f"Wrote manuscript summary report: {out_report}")
    if fig_files:
        print(f"Wrote figure datasets under {fig_dir}: {', '.join(fig_files)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
