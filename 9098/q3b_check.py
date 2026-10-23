import json, re, sys
from datetime import datetime
from pathlib import Path

S = Path("<path>")
G = Path("<path>")
Q = (S / "vm-queue.out").read_text(encoding="utf-8", errors="replace").splitlines()
K = "Driver.Packet.dispatch"


def stamp(label, pattern):
    hits = [l for l in Q if l[9:].startswith(f"{label}: ") and re.search(pattern, l)]
    return hits[-1] if hits else None


def epoch(line, day):
    return datetime.combine(day, datetime.strptime(line[:8], "%H:%M:%S").time()).timestamp()


for label in sys.argv[1:]:
    launch = int((S / f"{label}-launch.txt").read_text().strip())
    day = datetime.fromtimestamp(launch).date()
    yes = epoch(stamp(label, r"Yes pressed"), day)
    dl, cl = stamp(label, r"loading screen \(dark\)"), stamp(label, r"CONTROL: first lit frame")
    dark = epoch(dl, day) if dl else None
    ctl = epoch(cl, day) if cl else None
    rows = [json.loads(l) for l in (G / f"trace-{label}" / "aps5-summary.jsonl").read_text().splitlines() if l.strip()]
    st = [l.split() for l in (S / f"{label}-summary-stamps.txt").read_text().splitlines() if l.strip()]
    off = int(st[-1][0]) - launch - rows[min(int(st[-1][1]), len(rows)) - 1]["interval_end_s"]
    lo, hi = (dark or yes) - launch - off, (ctl or yes) - launch - off
    sel = [r for r in rows if r.get("presents") and r["interval_start_s"] >= lo and r["interval_end_s"] <= hi]
    pres = sum(r["presents"] for r in sel)
    have = [r for r in sel if K in r.get("top_phases_ms_per_frame", {})]
    miss = len(sel) - len(have)
    miss_p = sum(r["presents"] for r in sel if K not in r.get("top_phases_ms_per_frame", {}))
    val = sum(r.get("top_phases_ms_per_frame", {}).get(K, 0) * r["presents"] for r in sel) / max(pres, 1)
    pres_h = sum(r["presents"] for r in have)
    val_h = sum(r["top_phases_ms_per_frame"][K] * r["presents"] for r in have) / max(pres_h, 1)
    smallest_top = []
    for r in sel:
        t = r.get("top_phases_ms_per_frame", {})
        if K not in t and t:
            smallest_top.append(min(t.values()))
    cap = max(smallest_top) if smallest_top else 0
    up = sum((r["top_phases_ms_per_frame"][K] if K in r.get("top_phases_ms_per_frame", {}) else (min(r["top_phases_ms_per_frame"].values()) if r.get("top_phases_ms_per_frame") else 0)) * r["presents"] for r in sel) / max(pres, 1)
    print(f"{label}: upper bound with each missing interval at its 8th phase: {up:,.0f}")
    print(f"{label}: intervals {len(sel)}, presents {pres}, dispatch {val:,.0f} (missing->0); intervals without it {miss} ({miss_p} presents); "
          f"over intervals with it {val_h:,.0f}; the 8th phase where it's missing is at most {cap:,.0f} ms/frame")
