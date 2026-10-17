#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Paired-timing verdict for one autoresearch eval.

usage: analyze.py TIMING_DIR BAR ALLOW_CONTENDED

TIMING_DIR/runs.txt has one line per compare_rocm run:
    IDX LABEL RC T0 T1 LOG FOREIGN
Consecutive runs (1,2), (3,4), ... form one pair of `cand` and `best`. Each
pair gives speedup = best advance ms / cand advance ms, so drift between
pairs cancels. A pair is flagged when either run failed, saw another user's
GPU process (unless ALLOW_CONTENDED), or ran at a busy GFX clock below 97% of
the median run. The candidate meets the bar when the median clean speedup is
at least BAR and every clean pair is faster than 1.0, except that one pair
may disagree once there are four or more. Loom's advance time is bimodal on
the MI210 (runs land near 443 or 471 ms on the seed even on an idle GPU), so
a single run in the slow mode must not veto a real speedup.
"""
import csv
import re
import statistics
import sys
from pathlib import Path

ADVANCE = re.compile(r"^advance kernel GPU median: original ([\d.]+) ms, Loom ([\d.]+) ms")


def telemetry(path):
    """[(epoch_s, gfx_clk_mhz, gfx_util_pct)] from `amd-smi monitor --csv`."""
    samples, header = [], None
    try:
        lines = path.read_text().splitlines()
    except OSError:
        return samples
    for row in csv.reader(lines):
        if row and row[0] == "timestamp":
            header = row
            continue
        if not header or len(row) != len(header):
            continue
        try:
            fields = dict(zip(header, row))
            samples.append((float(fields["timestamp"]), float(fields["gfx_clk"]), float(fields["gfx"])))
        except (KeyError, ValueError):
            continue
    return samples


def main():
    out, bar, allow_contended = Path(sys.argv[1]), float(sys.argv[2]), sys.argv[3] == "1"
    samples = telemetry(out / "telemetry.csv")
    runs = []
    for line in (out / "runs.txt").read_text().splitlines():
        idx, label, rc, t0, t1, log, foreign = line.split()
        text = Path(log).read_text(errors="replace") if Path(log).exists() else ""
        match = next((ADVANCE.match(l) for l in text.splitlines() if ADVANCE.match(l)), None)
        busy = [clk for t, clk, util in samples if float(t0) <= t <= float(t1) and util >= 50]
        runs.append({
            "idx": int(idx), "label": label, "rc": int(rc), "foreign": int(foreign),
            "pass": any(l.startswith("PASS") for l in text.splitlines()),
            "hip": float(match[1]) if match else None, "loom": float(match[2]) if match else None,
            "clk": statistics.mean(busy) if busy else None,
        })
    clocks = [r["clk"] for r in runs if r["clk"]]
    median_clk = statistics.median(clocks) if clocks else None
    for r in runs:
        flags = []
        if r["rc"] != 0 or r["loom"] is None:
            flags.append("failed")
        elif not r["pass"]:
            flags.append("incorrect")
        if r["foreign"]:
            flags.append("contended")
        if median_clk and r["clk"] and r["clk"] < 0.97 * median_clk:
            flags.append(f"clock={r['clk']:.0f}")
        r["flags"] = flags

    pairs = []
    for a, b in zip(runs[0::2], runs[1::2]):
        cand, best = (a, b) if a["label"] == "cand" else (b, a)
        flags = sorted(set(cand["flags"] + best["flags"]))
        blocking = [f for f in flags if not (f == "contended" and allow_contended)]
        speedup = hipnorm = None
        if cand["loom"] and best["loom"]:
            speedup = best["loom"] / cand["loom"]
            hipnorm = (best["loom"] / best["hip"]) / (cand["loom"] / cand["hip"])
        pairs.append({"cand": cand, "best": best, "flags": flags, "clean": not blocking and speedup,
                      "speedup": speedup, "hipnorm": hipnorm})

    for n, p in enumerate(pairs, 1):
        fmt = lambda v: f"{v:.4f}" if v else "-"
        print(f"PAIR {n} speedup={fmt(p['speedup'])} hipnorm={fmt(p['hipnorm'])} "
              f"cand_ms={p['cand']['loom']} best_ms={p['best']['loom']} "
              f"flags={','.join(p['flags']) or 'none'}{'' if p['clean'] else ' (excluded)'}")
    clean = [p for p in pairs if p["clean"]]
    print(f"CLEAN_PAIRS {len(clean)}")
    # Ask for re-runs only for transient flags; contention and incorrect
    # results do not improve with another pair.
    retry = [p for p in pairs if not p["clean"] and not any(
        f in ("incorrect", "contended") for f in p["flags"])]
    print(f"FLAGGED_PAIRS {len(retry)}")

    incorrect = sum("incorrect" in r["flags"] for r in runs if r["label"] == "cand")
    failed = sum("failed" in r["flags"] for r in runs if r["label"] == "cand")
    result = {"clean_pairs": len(clean), "incorrect_runs": incorrect, "failed_runs": failed}
    if clean:
        s = [p["speedup"] for p in clean]
        h = [p["hipnorm"] for p in clean]
        cand_ms = statistics.median(p["cand"]["loom"] for p in clean)
        best_ms = statistics.median(p["best"]["loom"] for p in clean)
        hip_ms = statistics.median(p["cand"]["hip"] for p in clean)
        agree = sum(x > 1.0 for x in s)
        result.update({
            "median_speedup": f"{statistics.median(s):.4f}", "min_speedup": f"{min(s):.4f}",
            "max_speedup": f"{max(s):.4f}", "hipnorm_speedup": f"{statistics.median(h):.4f}",
            "agree": f"{agree}/{len(s)}", "cand_ms": f"{cand_ms:.2f}", "best_ms": f"{best_ms:.2f}",
            "hip_ms": f"{hip_ms:.2f}", "loom_hip": f"{cand_ms / hip_ms:.3f}",
            "aa_spread": f"{max(abs(x - 1.0) for x in s):.4f}",
            "aa_median_dev": f"{statistics.median(abs(x - 1.0) for x in s):.4f}",
            "contended_pairs": sum("contended" in p["flags"] for p in clean),
            "meets_bar": int(len(clean) >= 2 and statistics.median(s) >= bar
                             and agree >= len(s) - (1 if len(s) >= 4 else 0)),
        })
    else:
        result["meets_bar"] = 0
    # clean_pairs comes first and is followed by a space; remote.sh matches it.
    print("RESULT " + " ".join(f"{k}={v}" for k, v in result.items()))


if __name__ == "__main__":
    main()
