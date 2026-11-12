import argparse
import re
import sys

NAMES = [
    "marker", "lower", "record.low", "record.high", "record.code", "root.code",
    "child.fault", "child.mapStart", "child.keys", "child.baseLow", "child.record", "child.mapped",
    "image.flags", "image.element", "driver.fault", "code", "offset", "pointer.low", "pointer.high", "address.low",
    "table", "column.addend", "column.stride", "column.offset",
]
STEPS = [
    "lower", "record.low", "record.high", "record.code", "root.code",
    "child.fault", "child.mapStart", "child.keys", "child.baseLow", "child.record", "child.mapped", "code",
    "image.flags", "image.element", "driver.fault", "address.low",
]
DEFAULTS = [1, 0, 32, 0]
POISON = 0x80000000
MASK = 0xffffffff
MAX_PARENT_KEYS = 524288
REASONS = {"outside the snapshot": 0, "outside the key domain": 1}
DETAIL_RUNS = 2
LINE_START = re.compile(r"(\s*$|\[|trace |  map\[|  trace g|AGC |pointer |At line|\s*\+ |\S+\.exe : )")


def ascii_text(text):
    return text.encode("ascii", "replace").decode("ascii")


def hex_word(value):
    return "-" if value is None else "0x%08x" % value


class Run:
    def __init__(self, number, shader_words, recompile_words):
        self.number = number
        self.shader_words = shader_words
        self.recompile_words = recompile_words
        self.map = []
        self.rows = []
        self.faults = []


def unwrap(lines):
    joined = []
    open_line = False
    for line in lines:
        if open_line and not LINE_START.match(line):
            joined[-1] += line
            continue
        joined.append(line)
        open_line = "[gpu]" in line or "image table:" in line
    return joined


def parse(lines):
    bases = None
    subgroup = None
    runs = []
    pending = []
    for line in unwrap(lines):
        text = line.rstrip()
        if text.startswith("trace bases:"):
            bases = text
        found = re.search(r"subgroup=(\d+)", text)
        if found and subgroup is None:
            subgroup = int(found.group(1))
        fault = re.search(r"image table: .*selected the descriptor at 0x([0-9a-fA-F]+)(.*)$", text)
        if fault:
            pending.append((int(fault.group(1), 16), fault.group(2).strip()))
        header = re.match(r"trace map: (\d+) words read by the shader, (\d+) words at recompile", text)
        if header:
            run = Run(len(runs), int(header.group(1)), int(header.group(2)))
            run.faults = pending
            pending = []
            runs.append(run)
        elif runs and text.startswith("  map["):
            try:
                runs[-1].map.extend(int(word.rstrip("*"), 16) for word in text.split()[1:])
            except ValueError:
                pass
        elif runs and text.startswith("  trace g"):
            head, _, words = text.partition(":")
            position = re.findall(r"\d+", head)
            try:
                row = [int(word, 16) for word in words.split()]
            except ValueError:
                continue
            if len(position) != 2 or len(row) not in (20, 24) or row[0] == 0xdeadbeef:
                continue
            if len(row) == 20:
                row.extend(DEFAULTS)
            runs[-1].rows.append((int(position[0]), int(position[1]), dict(zip(NAMES, row))))
    if pending and runs and not runs[-1].faults:
        runs[-1].faults = pending
    return bases, subgroup, runs


def recompute(table_map, row):
    def at(index):
        if index is None:
            return None
        index &= MASK
        return table_map[index] if index < len(table_map) else None

    out = {}
    reads = {}
    header = 8 + 8 * row["table"]
    start, stored, outside, fault = at(header), at(header + 1), at(header + 6), at(header + 7)
    reads["root header"] = (header, 8)
    if None in (start, stored, fault):
        return out, reads
    low, high, offset = row["pointer.low"], row["pointer.high"], row["offset"]
    count = stored if stored <= MAX_PARENT_KEYS and fault == 0 else 0
    lower, upper = 0, count
    while lower < upper:
        middle = lower + ((upper - lower) >> 1)
        record = start + 3 * middle
        record_low, record_high = at(record), at(record + 1)
        if record_low is None or record_high is None:
            return out, reads
        less = record_high < high or (record_high == high and record_low < low)
        lower, upper = (middle + 1, upper) if less else (lower, middle)
    in_bounds = lower < count
    selected = (start + 3 * lower) & MASK if in_bounds else header
    words = [at(selected), at(selected + 1), at(selected + 2)]
    reads["selected record"] = (selected, 3)
    out.update({"lower": lower, "record.low": words[0], "record.high": words[1], "record.code": words[2]})
    if None in words:
        return out, reads
    same = words[0] == low and words[1] == high
    missing = outside if low == 0 and high == 0 else POISON
    code = words[2] if in_bounds and same else missing
    root = fault if fault != 0 else code
    out["root.code"] = root
    if root is None:
        return out, reads
    root_poisoned = (root & POISON) != 0
    child = header if root_poisoned else root
    child_fault, child_start, keys, base_low = at(child + 7), at(child), at(child + 1), at(child + 4)
    reads["child header"] = (child, 8)
    out.update({"child.fault": child_fault, "child.mapStart": child_start, "child.keys": keys, "child.baseLow": base_low})
    if None in (child_fault, child_start, keys, base_low):
        return out, reads
    stride = row["column.stride"]
    relative = (offset - row["column.addend"]) & MASK
    record = relative // stride if stride else 0
    in_map = ((relative - record * stride) & MASK) == 0 and record < keys
    slot = (child_start + record) & MASK if in_map else 0
    mapped = at(slot)
    reads["mapped"] = (slot, 1)
    child_code = child_fault if child_fault != 0 else (mapped if in_map else POISON)
    address = (base_low + (offset & ~3) + (row["column.offset"] & ~3)) & MASK
    final = root if root_poisoned else child_code
    out.update({"child.record": record, "child.mapped": mapped, "code": final, "address.low": low if root_poisoned else address})
    if final is None:
        return out, reads
    image_selected = final != 0 and (final & POISON) == 0
    image = final if image_selected else 0
    flags, element, element_count, faults = at(image), at(image + 2), at(2), at(5)
    reads["image record"] = (image, 3)
    out.update({"image.flags": flags, "image.element": element})
    if None in (flags, element, element_count, faults):
        return out, reads
    rejected = image_selected and (flags & POISON) != 0
    checked = image_selected and not rejected and element < element_count
    driver_slot = (faults + element) & MASK if checked else 0
    reads["driver fault"] = (driver_slot, 1)
    out["driver.fault"] = at(driver_slot)
    return out, reads


def departure(row, expected):
    for name in STEPS:
        value = expected.get(name)
        if value is not None and row[name] != value:
            return name, row[name], value
    return None


def faults_of(row, table_map):
    found = []
    code = row["code"]
    if code & POISON:
        found.append(("code", code))
        if row["root.code"] & POISON:
            found.append(("root.code", row["root.code"]))
        return found
    if code == 0:
        return found
    if row["image.flags"] & POISON:
        found.append(("image.flags", row["image.flags"]))
    elif len(table_map) > 2 and row["image.element"] >= table_map[2]:
        found.append(("image.element", POISON))
    elif row["driver.fault"] & POISON:
        found.append(("driver.fault", row["driver.fault"]))
    return found


def reported_word(reason):
    entry = re.search(r"poison entry (\d+)", reason)
    if entry:
        return POISON | int(entry.group(1))
    for name, value in REASONS.items():
        if re.search(r":\s*" + re.escape(name) + r"(:|$)", reason):
            return POISON | value
    return None


def lanes_text(lanes):
    groups = {}
    for group, lane in lanes:
        groups.setdefault(group, []).append(lane)
    parts = []
    for group in sorted(groups):
        values = sorted(groups[group])
        spans = []
        first = previous = values[0]
        for lane in values[1:] + [None]:
            if lane is not None and lane == previous + 1:
                previous = lane
                continue
            spans.append("l%d" % first if first == previous else "l%d-%d" % (first, previous))
            if lane is not None:
                first = previous = lane
        parts.append("g%d %s" % (group, ",".join(spans)))
    return "; ".join(parts)


def origin(row, expected, table_map):
    header = 8 + 8 * row["table"]
    count = table_map[header + 1] if header + 1 < len(table_map) else None
    table_fault = table_map[header + 7] if header + 7 < len(table_map) else None
    root = expected.get("root.code", row["root.code"])
    if root is not None and root & POISON:
        if table_fault:
            return "table %d's fault word 0x%08x" % (row["table"], table_fault)
        return "root search: pointer 0x%08x:%08x is not among the %s root records of table %d" % (row["pointer.high"], row["pointer.low"], count, row["table"])
    code = expected.get("code", row["code"])
    if code is not None and code & POISON:
        if row["child.fault"]:
            return "child table fault word 0x%08x" % row["child.fault"]
        return "child table: offset 0x%x (addend 0x%x, stride 0x%x) is outside its %d keys" % (row["offset"], row["column.addend"], row["column.stride"], row["child.keys"])
    if row["image.flags"] & POISON:
        return "image record %d flags 0x%08x" % (code, row["image.flags"])
    if len(table_map) > 2 and row["image.element"] >= table_map[2]:
        return "image element %d is outside the %d elements" % (row["image.element"], table_map[2])
    if row["driver.fault"] & POISON:
        return "driver fault word 0x%08x for element %d" % (row["driver.fault"], row["image.element"])
    return "no poisoned step"


def row_lines(group, lane, row, mark=None):
    fields = ["%s=%s%s" % (name, hex_word(row[name]), "!" if mark and name == mark else "") for name in NAMES]
    lines = []
    for first in range(0, len(fields), 6):
        prefix = "  g%d l%d: " % (group, lane) if first == 0 else "          "
        lines.append(prefix + " ".join(fields[first:first + 6]))
    return lines


def read_lines(table_map, reads):
    parts = []
    for name in ("root header", "selected record", "child header", "mapped", "image record", "driver fault"):
        if name not in reads:
            continue
        start, count = reads[name]
        words = " ".join(hex_word(table_map[index] if index < len(table_map) else None) for index in range(start, start + count))
        parts.append("%s map[%d%s] %s" % (name, start, "" if count == 1 else "..%d" % (start + count - 1), words))
    return ["  read " + part for part in parts]


def report(run, full):
    lines = []
    results = []
    for group, lane, row in run.rows:
        expected, reads = recompute(run.map, row)
        results.append((group, lane, row, expected, reads, departure(row, expected), faults_of(row, run.map)))
    misplaced = [(g, l) for g, l, row, *_ in results if row["marker"] >> 24 != 0xd1 or (row["marker"] >> 12) & 0xfff != g or row["marker"] & 63 != l]
    departing = [item for item in results if item[5]]
    faulting = [item for item in results if item[6]]
    summary = "run %d: map %d words read by the shader (%d at recompile), %d rows, %d match the lookup recomputed from the map, %d depart, %d would fault" % (
        run.number, run.shader_words, run.recompile_words, len(results), len(results) - len(departing), len(departing), len(faulting))
    if misplaced:
        summary += ", %d rows whose marker names another lane" % len(misplaced)
    lines.append(summary)
    focus = None
    for address, reason in run.faults[:1]:
        word = reported_word(reason)
        separator = "" if reason.startswith(":") else " "
        lines.append(ascii_text("  fault: descriptor 0x%x%s%s%s" % (address, separator if reason else "", reason, "" if word is None else ", word 0x%08x" % word)))
        if word is not None:
            hit = [item for item in results if any(value == word for _, value in item[6])]
        else:
            hit = [item for item in results if item[6] and item[2]["address.low"] == address & MASK]
        if not hit:
            hit = [item for item in results if item[2]["address.low"] == address & MASK]
        if not hit:
            lines.append("  no row holds the reported word or address")
            continue
        fields = sorted({name for item in hit for name, value in item[6] if word is None or value == word}) or ["address.low"]
        lines.append("  %d %s it (%s): %s" % (len(hit), "lane holds" if len(hit) == 1 else "lanes hold", ", ".join(fields), lanes_text([(g, l) for g, l, *_ in hit])))
        moved = [item for item in hit if item[5]]
        if moved:
            first = moved[0]
            lines.append("  %d of them depart from the recomputed lookup; first g%d l%d at %s: 0x%08x, the map gives %s" % (len(moved), first[0], first[1], first[5][0], first[5][1], hex_word(first[5][2])))
            focus = first
        else:
            lines.append("  none of them departs from the lookup recomputed from the map, so the lookup read what the map holds; the poison comes from its inputs or after the lookup")
            lines.append("  origin: " + origin(hit[0][2], hit[0][3], run.map))
            focus = hit[0]
    if focus is None and departing:
        first = departing[0]
        lines.append("  first departure g%d l%d at %s: 0x%08x, the map gives %s" % (first[0], first[1], first[5][0], first[5][1], hex_word(first[5][2])))
        focus = first
    if focus is None and faulting:
        focus = faulting[0]
        lines.append("  g%d l%d would fault: %s" % (focus[0], focus[1], origin(focus[2], focus[3], run.map)))
    if focus is not None and not full:
        lines.extend(row_lines(focus[0], focus[1], focus[2], focus[5][0] if focus[5] else None))
        lines.extend(read_lines(run.map, focus[4]))
    if full:
        for first in range(0, len(run.map), 8):
            lines.append("  map[%d] %s" % (first, " ".join(hex_word(value) for value in run.map[first:first + 8])))
        for group, lane, row, expected, reads, moved, faults in results:
            lines.extend(row_lines(group, lane, row, moved[0] if moved else None))
            if moved:
                lines.append("          departs at %s: 0x%08x, the map gives %s" % (moved[0], moved[1], hex_word(moved[2])))
    return lines, bool(focus)


def marker_line(subgroup, runs):
    rows = [(run.number, g, l, row) for run in runs for g, l, row in run.rows]
    differ = [(n, g, l) for n, g, l, row in rows if (row["marker"] >> 6) & 63 != row["marker"] & 63]
    if subgroup is not None and subgroup != 32:
        return "marker: skipped, subgroup %d does not run two lanes per invocation" % subgroup
    text = "marker: %d of %d rows have a subgroup lane different from their local lane" % (len(differ), len(rows))
    if differ:
        text += " (first run %d g%d l%d)" % differ[0]
    if subgroup is None:
        text += "; the log does not give the subgroup size"
    return text


def main():
    parser = argparse.ArgumentParser(description="decode the image table trace printed by agc_driver_image_table_tests --pointer-vsharp-child-key-arm-trace")
    parser.add_argument("log")
    parser.add_argument("--full", action="store_true", help="print every map word and row")
    arguments = parser.parse_args()
    with open(arguments.log, "rb") as file:
        data = file.read()
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        text = data.decode("utf-16", errors="replace")
    elif data[1:2] == b"\x00":
        text = data.decode("utf-16-le", errors="replace")
    else:
        text = data.decode("utf-8", errors="replace")
    bases, subgroup, runs = parse(text.splitlines())
    if not runs:
        print("no trace in %s" % ascii_text(arguments.log))
        return 1
    print(ascii_text(bases) if bases else "trace bases: not in the log")
    print("device subgroup: %s" % ("not in the log" if subgroup is None else subgroup))
    print(marker_line(subgroup, runs))
    detailed = 0
    trimmed = False
    for run in runs:
        lines, focused = report(run, arguments.full)
        if focused and not arguments.full:
            if detailed >= DETAIL_RUNS:
                lines = lines[:1] + [line for line in lines[1:] if line.startswith(("  fault", "  origin")) or " hold" in line]
                trimmed = True
            detailed += 1
        for line in lines:
            print(ascii_text(line))
    if trimmed:
        print("later runs show their summary only; --full prints every row")
    return 0


if __name__ == "__main__":
    sys.exit(main())
