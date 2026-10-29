#!/usr/bin/env python3
"""Generate gen/ (recompiled C) for SWAT 4 from the user's own game copy.

Runs tools/discover.py then tools/lift.py for every module in
tools/swat4_modules.py, with the rebased load addresses. Prints a per-module
report (wall time, peak RSS, functions, rejected) and writes
analysis/swat4_regen_report.json.

Usage: tools/swat4_regen.py GAME_SYSTEM_DIR GEN_DIR ANALYSIS_DIR
Needs: pip install capstone pefile
"""
import json, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from swat4_modules import MODULES, EXE


def run(cmd, env=None):
    """Run a child and return (rc, stdout, stderr, wall_s, rss_kb).

    rss_kb is getrusage(RUSAGE_CHILDREN).ru_maxrss, i.e. the peak among all
    children spawned so far (Linux reports the high-water mark). It is a safe
    upper bound for sizing LB_GEN_JOBS, not a per-module measurement.
    """
    import resource
    t = time.time()
    p = subprocess.Popen(cmd, cwd=HERE, env=env,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    out, err = p.communicate()
    rss = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    return p.returncode, out, err, time.time() - t, rss


def main():
    # Absolute paths: discover.py/lift.py run with cwd=tools, so a relative
    # "analysis/..." would resolve inside tools/ and fail.
    game, gen, analysis = [os.path.abspath(p) for p in sys.argv[1:4]]
    os.makedirs(gen, exist_ok=True)
    os.makedirs(analysis, exist_ok=True)
    allmods = MODULES + [EXE]
    report = []
    for mid, fname, base, kind in allmods:
        src = os.path.join(game, fname)
        if not os.path.exists(src):
            print(f"SKIP {mid}: {src} not found", flush=True)
            continue
        fj = os.path.join(analysis, f"functions_{mid}.json")
        print(f"== {mid} ({fname}, base 0x{base:08X}) ==", flush=True)
        rc, out, err, dt, rss = run([sys.executable, "discover.py", src, fj, hex(base)])
        if rc:
            print(f"  discovery FAILED rc={rc}\n{err[-800:]}", flush=True)
            report.append({"id": mid, "stage": "discover", "rc": rc})
            continue
        tail = [l for l in out.splitlines() if l.startswith(("functions", "gaps", "text bytes"))]
        print("  " + " | ".join(tail), flush=True)
        # More buckets for bigger modules => better compile parallelism.
        nf = int(next((l.split()[1] for l in out.splitlines() if l.startswith("functions")), "0") or 0)
        lift_files = max(8, min(96, nf // 250 + 1))
        env = dict(os.environ, LIFT_GAME="swat4", LIFT_FILES=str(lift_files))
        rc2, out2, err2, dt2, rss2 = run(
            [sys.executable, "lift.py", src, fj, gen, mid, hex(base)], env=env)
        lifted = [l for l in out2.splitlines() if l.startswith("lifted")]
        print(f"  {lifted[0] if lifted else 'lift: no summary'}  ({dt2:.1f}s)", flush=True)
        if rc2:
            print(f"  lift FAILED rc={rc2}\n{err2[-800:]}", flush=True)
        report.append({"id": mid, "file": fname, "base": hex(base), "kind": kind,
                       "discover_s": round(dt, 1), "lift_s": round(dt2, 1),
                       "maxrss_kb": rss2, "functions": nf, "lift_files": lift_files,
                       "rc": rc2, "discover_tail": tail,
                       "lift": lifted[0] if lifted else ""})
        json.dump(report, open(os.path.join(analysis, "swat4_regen_report.json"), "w"), indent=1)
    print("done:", len(report), "modules")


if __name__ == "__main__":
    main()