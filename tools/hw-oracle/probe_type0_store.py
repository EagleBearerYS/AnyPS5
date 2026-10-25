import argparse
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import hw_oracle

HERE = Path(__file__).resolve().parent
LANES = 32
ROW_BYTES = LANES * 16
INPUT_BYTES = 65536
PATTERN = 0xA5A50000
SENTINEL = 0x7E57AB1E
FORMAT_32_UINT = 20
CONTROL_WIDTH = 64
MODES = dict(ieee=0, denorm32=0, denorm16=3, dx10_clamp=1, round32=0, round16=0, fp16_overflow=0)
TAMALERO = (0x04EFD000, 0x00040011, 0x00017A00, 0x00005204, 0x0AF00000, 0x00000000, 0x00000612, 0x00FFF000)
STORE = "  image_store v[30:33], v[34:35], s[8:15] dmask:0xf dim:SQ_RSRC_IMG_2D unorm"
ATOMIC = "  v_mov_b32 v36, v7\n  image_atomic_add v36, v[34:35], s[8:15] dmask:0x1 dim:SQ_RSRC_IMG_2D unorm glc"
CASES = ("control-store", "type0-store", "type1-store", "control-atomic", "type0-atomic", "type1-atomic")
USAGE = """Measure what an RDNA GPU does with image_store and image_atomic_add through a T# whose TYPE is 0 or 1.

WARNING: a TYPE 0 or TYPE 1 T# is not a valid image. A store or atomic through one may fault, hang the GPU
or leave the driver unusable until you restart the computer (#5828 saw a TYPE 2 T# do that). Close other
GPU work and save your files first. The probe stops at the first fault or hang unless --keep-going is given.

Each case runs one wave of 32 lanes on the local GPU. The T# base points at a buffer the probe owns, filled
with a known pattern 3.5 KiB below and 60 KiB above the base. After the access, the kernel reports every
changed dword of that buffer and what image_load through the same T# returns. The cases run in this order:
  control-store   a valid 2D linear 64x1 32_UINT image (TYPE 9), image_store at x = lane
  type0-store     the 8 words from PPSA01474 (#9749), TYPE 0, base replaced, image_store at x = lane
  type1-store     the same words with TYPE 1
  control-atomic  the TYPE 9 image, image_atomic_add at x = lane
  type0-atomic    TYPE 0, image_atomic_add
  type1-atomic    TYPE 1, image_atomic_add
Leave out the atomics with --no-atomics, or name cases with --case.

Each case prints one line:
  <case>: ran; no dword changed in base-0xe00..base+0xf000; image_load returned R G B A
  <case>: ran; N dwords changed, first at base+0xF = V, last at base+0xL = V; image_load returned R G B A
  <case>: fault: the GPU or runtime reported an error (exit status S): <message>
  <case>: hang: the dispatch did not finish within T s
The controls must change 32 dwords, base+0x0 to base+0x7c, to 5e5e00LL (store) or the pattern plus 010000LL
(atomic) in lane LL, and image_load must read that value back in its first word; otherwise the probe prints
"control failed" and stops, since the TYPE 0 and TYPE 1 lines could not be trusted. Every case is assembled
before the first dispatch, so a missing tool stops the probe before it touches the GPU. Post the whole output
with --json, and name the GPU, driver and OS."""


def store_rows():
    return [(lane, 0, 0x5E5E0000 | lane, 0) for lane in range(LANES)]


def atomic_rows():
    return [(lane, 0, 0, 0x01000000 | lane) for lane in range(LANES)]


def control_words(target):
    resource_level = 1 if target.startswith("gfx10") else 0
    width = CONTROL_WIDTH - 1
    return (0, (FORMAT_32_UINT << 20) | ((width & 3) << 30), ((width >> 2) & 0x3FFF) | (resource_level << 31),
            0xFAC | (9 << 28), 0, 0, 0, 0)


def typed_words(image_type):
    words = list(TAMALERO)
    words[3] = (words[3] & 0x0FFFFFFF) | (image_type << 28)
    return tuple(words)


def case_words(case, target):
    kind = case.split("-")[0]
    if kind == "control":
        return control_words(target)
    return typed_words(0 if kind == "type0" else 1)


def body(case, target):
    words = case_words(case, target)
    text = (HERE / "type0_store.s").read_text()
    text = text.replace("@WORD1@", f"0x{words[1] & 0xFFFFFF00:08x}")
    for index in range(2, 8):
        text = text.replace(f"@WORD{index}@", f"0x{words[index]:08x}")
    return text.replace("@ACCESS@", ATOMIC if case.endswith("atomic") else STORE)


def pattern():
    return b"".join(struct.pack("<I", PATTERN ^ index) for index in range(ROW_BYTES // 4, INPUT_BYTES // 4))


def rows(case):
    return atomic_rows() if case.endswith("atomic") else store_rows()


def run_case(case):
    lanes = hw_oracle.run(body(case, hw_oracle.target()), rows(case), pattern(), False, False, **MODES)
    return {"case": case, "target": hw_oracle.target(), "words": [f"{word:08x}" for word in case_words(case, hw_oracle.target())],
            "lanes": [list(lane) for lane in lanes]}


def signed(value):
    return value - (1 << 32) if value & 0x80000000 else value


def offset(value):
    return f"base{'-' if value < 0 else '+'}0x{abs(value):x}"


def summary(case, result):
    lanes = result["lanes"]
    if len(lanes) != LANES or any(lane[11] != SENTINEL for lane in lanes):
        return f"{case}: error: the kernel did not finish every lane"
    changed = [(signed(lane[1]), lane[2], signed(lane[3]), lane[4]) for lane in lanes if lane[0]]
    total = sum(lane[0] for lane in lanes)
    loaded = " ".join(f"{word:08x}" for word in lanes[0][5:9])
    text = f"{case}: ran; "
    if not total:
        text += f"no dword changed in base-0x{lanes[0][10] - ROW_BYTES:x}..base+0x{INPUT_BYTES - lanes[0][10]:x}"
    else:
        first = min(changed)
        last = max(changed, key=lambda entry: entry[2])
        text += f"{total} dwords changed, first at {offset(first[0])} = {first[1]:08x}, last at {offset(last[2])} = {last[3]:08x}"
    text += f"; image_load returned {loaded}"
    if case.endswith("atomic"):
        text += f"; image_atomic_add returned {lanes[0][9]:08x}"
    if case.startswith("control") and not control_ok(case, lanes):
        text += "; control failed"
    return text


def control_ok(case, lanes):
    base = lanes[0][10]
    expected = []
    for lane in range(LANES):
        before = PATTERN ^ ((base + lane * 4) // 4)
        expected.append(((before + (0x01000000 | lane)) & 0xFFFFFFFF) if case.endswith("atomic") else (0x5E5E0000 | lane))
    if sum(lane[0] for lane in lanes) != LANES:
        return False
    starts = [signed(lane[1]) for lane in lanes if lane[0]]
    ends = [signed(lane[3]) for lane in lanes if lane[0]]
    if min(starts) != 0 or max(ends) != (LANES - 1) * 4:
        return False
    if case.endswith("atomic") and any(lanes[lane][9] != PATTERN ^ ((base + lane * 4) // 4) for lane in range(LANES)):
        return False
    return all(lanes[lane][5] == expected[lane] for lane in range(LANES))


def outcome(case, completed, timeout):
    if completed is None:
        return {"case": case, "outcome": "hang", "line": f"{case}: hang: the dispatch did not finish within {timeout} s"}
    message = " ".join(completed.stderr.strip().splitlines()[-3:])
    if "kernel timed out" in completed.stderr:
        return {"case": case, "outcome": "hang", "line": f"{case}: hang: the dispatch did not finish within 5 s", "stderr": completed.stderr}
    if completed.returncode != 0:
        return {"case": case, "outcome": "fault", "stderr": completed.stderr,
                "line": f"{case}: fault: the GPU or runtime reported an error (exit status {completed.returncode}): {message}"}
    result = json.loads(completed.stdout)
    line = summary(case, result)
    return {"case": case, "outcome": "control failed" if line.endswith("control failed") else "ran", "line": line, **result}


def measure(cases, timeout, keep_going):
    assemble_only(cases, quiet=True)
    reports = []
    for case in cases:
        try:
            completed = subprocess.run([sys.executable, str(Path(__file__).resolve()), "--run-case", case],
                                       capture_output=True, text=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            completed = None
        report = outcome(case, completed, timeout)
        reports.append(report)
        print(report["line"], flush=True)
        if report["outcome"] in ("fault", "hang", "control failed") and not keep_going:
            for remaining in cases[len(reports):]:
                reports.append({"case": remaining, "outcome": "not run", "line": f"{remaining}: not run"})
                print(f"{remaining}: not run", flush=True)
            break
    return reports


def assemble_only(cases, quiet=False):
    for case in cases:
        with tempfile.TemporaryDirectory() as tmp:
            hw_oracle.assemble(body(case, hw_oracle.target()), Path(tmp), False, **MODES)
        if not quiet:
            print(f"{case}: assembled for {hw_oracle.target()}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=USAGE, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--case", action="append", choices=CASES, help="run only this case (repeatable)")
    parser.add_argument("--no-atomics", action="store_true", help="leave out the image_atomic_add cases")
    parser.add_argument("--keep-going", action="store_true", help="run the remaining cases after a fault or hang")
    parser.add_argument("--timeout", type=int, default=60, metavar="SECONDS", help="time limit for each case (default 60)")
    parser.add_argument("--json", action="store_true", help="also print every case's lanes and descriptor words as JSON")
    parser.add_argument("--assemble-only", action="store_true", help="assemble every case for the target and run nothing")
    parser.add_argument("--run-case", choices=CASES, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if args.run_case:
        print(json.dumps(run_case(args.run_case)))
        return 0
    cases = [case for case in CASES if (not args.case or case in args.case) and not (args.no_atomics and case.endswith("atomic"))]
    if not cases:
        parser.error("no case selected")
    if args.assemble_only:
        assemble_only(cases)
        return 0
    reports = measure(cases, args.timeout, args.keep_going)
    if args.json:
        print(json.dumps(reports, indent=2))
    return 0 if all(report["outcome"] == "ran" for report in reports) else 1


if __name__ == "__main__":
    sys.exit(main())
