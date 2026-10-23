"""Step 3b's per-run figures (#9098's four-arm session): the Yes press (the last 'Yes pressed' line, the retry's if any),
the dark load screen and the first lit frame from vm-queue.out; the host-import mode from '[gpu] host imports at
shutdown' (low: made < 30,000 MiB; high: >= 50,000); and over the post-Yes dark phase (dark .. first lit) the summary
intervals' presents and presents-weighted ms per frame of Driver.Packet.dispatch, Resources.Complete, Driver.Packet and
Driver.Packet.memory_wait (aps5-summary.jsonl in trace-<label>, the trace clock mapped through <label>-summary-stamps.txt;
a phase missing from an interval's top list counts as 0). usage: q3b_report.py <label> ..."""
import json
import re
import sys
from datetime import datetime
from pathlib import Path

S = Path(__file__).resolve().parent
G = Path("<path>")
Q = (S / "vm-queue.out").read_text(encoding="utf-8", errors="replace").splitlines()
KEYS = ("Driver.Packet.dispatch", "Resources.Complete", "Driver.Packet", "Driver.Packet.memory_wait")


def stamp(label, pattern, last=True):
    hits = [l for l in Q if l[9:].startswith(f"{label}: ") and re.search(pattern, l)]
    return (hits[-1] if last else hits[0]) if hits else None


def epoch(line, day):
    return datetime.combine(day, datetime.strptime(line[:8], "%H:%M:%S").time()).timestamp()


def mmss(seconds):
    return "%d:%02d" % (seconds // 60, seconds % 60)


print("run | Yes | dark after Yes | first lit after Yes | mode (made MiB) | presents | " + " | ".join(KEYS))
for label in sys.argv[1:]:
    launch = int((S / f"{label}-launch.txt").read_text().strip())
    day = datetime.fromtimestamp(launch).date()
    yes_line = stamp(label, r"Yes pressed")
    dark_line = stamp(label, r"loading screen \(dark\)")
    ctl_line = stamp(label, r"CONTROL: first lit frame")
    yes = epoch(yes_line, day)
    dark = epoch(dark_line, day) if dark_line else None
    ctl = epoch(ctl_line, day) if ctl_line else None
    log = (S / f"{label}-full.log").read_text(encoding="utf-8", errors="replace")
    m = re.search(r"host imports at shutdown: .*?made \d+ \(([\d.]+) MiB\)", log)
    made = float(m.group(1)) if m else None
    mode = "?" if made is None else ("low" if made < 30000 else ("high" if made >= 50000 else "between"))
    rows = [json.loads(l) for l in (G / f"trace-{label}" / "aps5-summary.jsonl").read_text().splitlines() if l.strip()]
    st = [l.split() for l in (S / f"{label}-summary-stamps.txt").read_text().splitlines() if l.strip()]
    off = int(st[-1][0]) - launch - rows[min(int(st[-1][1]), len(rows)) - 1]["interval_end_s"]
    lo, hi = (dark or yes) - launch - off, (ctl or yes) - launch - off
    sel = [r for r in rows if r.get("presents") and r["interval_start_s"] >= lo and r["interval_end_s"] <= hi]
    pres = sum(r["presents"] for r in sel)
    vals = [sum(r.get("top_phases_ms_per_frame", {}).get(k, 0) * r["presents"] for r in sel) / max(pres, 1) for k in KEYS]
    print(f"{label} | {yes_line[:8]} | {mmss(dark - yes) if dark else '-'} | {mmss(ctl - yes) if ctl else '-'} | "
          f"{mode} ({made:,.0f}) | {pres} | " + " | ".join(f"{v:,.0f}" for v in vals))
