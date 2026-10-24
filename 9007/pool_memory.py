"""#9007's two Resident Evil Village (PPSA01556) menu sessions, A B B A each, on one memory measure set:
private memory at the menu + 1 and + 10 minutes (the sample nearest that time), the peak private memory, and the
largest page file allocation, from each run's mem10.txt samples. The menu time is menu-seen.txt, when the run's
menu screenshot was written. usage: pool_memory.py <folder holding the <session>-<run> folders>"""
import os
import re
import sys

LINE = re.compile(r"^(\d\d):(\d\d):(\d\d) pid \d+: private ([\d,]+) MB.*?page file ([\d,]+) MB allocated")
SESSIONS = ("q6f", "q6ft")
RUNS = ("a1", "b1", "b2", "a2")


def seconds(text):
    hour, minute, second = (int(part) for part in text.split(":"))
    return hour * 3600 + minute * 60 + second


root = sys.argv[1] if len(sys.argv) > 1 else "."
for session in SESSIONS:
    figures = {}
    for run in RUNS:
        folder = os.path.join(root, f"{session}-{run}")
        menu = seconds(open(os.path.join(folder, "menu-seen.txt"), encoding="utf-8").read().strip())
        samples = []
        for text in open(os.path.join(folder, "mem10.txt"), encoding="utf-8", errors="replace"):
            match = LINE.match(text)
            if match:
                at = int(match.group(1)) * 3600 + int(match.group(2)) * 60 + int(match.group(3))
                samples.append((at, int(match.group(4).replace(",", "")), int(match.group(5).replace(",", ""))))
        first = samples[0][0]
        samples = [(at + 86400 if at < first else at, private, pagefile) for at, private, pagefile in samples]
        if menu < first - 43200:
            menu += 86400
        nearest = lambda offset: min(samples, key=lambda sample: abs(sample[0] - menu - offset))[1]
        figures[run] = (nearest(60), nearest(600), max(sample[1] for sample in samples), max(sample[2] for sample in samples))
        print(f"{session}-{run}: menu + 1 {figures[run][0]:,} MB, menu + 10 {figures[run][1]:,} MB, peak {figures[run][2]:,} MB, page file {figures[run][3]:,} MB")
    for b, a in (("b1", "a1"), ("b2", "a2")):
        deltas = [figures[b][index] - figures[a][index] for index in range(4)]
        print(f"{session}: {b} - {a}: menu + 1 {deltas[0]:+,}, menu + 10 {deltas[1]:+,}, peak {deltas[2]:+,}, page file {deltas[3]:+,} MB")
