"""Driver.Packet.dispatch per frame over each q9098 run's load: the summary lines whose first_present lies in
[present at Yes, present at the first lit frame). The figure is the presents-weighted mean of each line's
top_phases_ms_per_frame entry, as in q3b_report.py. Each line lists only its top 8 phases, so the figure is given
twice: a line missing dispatch counted as 0 (a lower bound), and set to that line's 8th phase (an upper bound).
The median per line is printed too. usage: q9098_check.py <folder holding the q9098-<run> folders>"""
import json
import os
import statistics
import sys

PHASE = "Driver.Packet.dispatch"
WINDOWS = {
    "b1": (4673, 5717),
    "a1": (4260, 5359),
    "a2": (4777, 5535),
    "b2": (4264, 5332),
}

root = sys.argv[1] if len(sys.argv) > 1 else "."
for run, (first, past) in WINDOWS.items():
    lines = []
    with open(os.path.join(root, f"q9098-{run}", "aps5-summary.jsonl"), encoding="utf-8") as file:
        for text in file:
            text = text.strip()
            if not text:
                continue
            try:
                line = json.loads(text)
            except ValueError:
                continue
            if first <= line["window"]["first_present"] < past and line.get("presents"):
                lines.append(line)
    lower, upper, missing, missingPresents = [], [], 0, 0
    for line in lines:
        top = line.get("top_phases_ms_per_frame", {})
        if PHASE in top:
            lower.append(top[PHASE])
            upper.append(top[PHASE])
        else:
            missing += 1
            missingPresents += line["presents"]
            lower.append(0.0)
            upper.append(min(top.values()) if top else 0.0)
    presents = [line["presents"] for line in lines]
    total = sum(presents)
    weighted = lambda values: sum(value * count for value, count in zip(values, presents)) / max(total, 1)
    print(f"{run}: presents [{first}, {past}), {len(lines)} lines over {total} presents, {PHASE} missing in {missing} lines ({missingPresents} presents); weighted {weighted(lower):,.0f} to {weighted(upper):,.0f} ms per frame; median per line {statistics.median(lower):.1f} to {statistics.median(upper):.1f} ms (missing as 0, then as the 8th phase)")
