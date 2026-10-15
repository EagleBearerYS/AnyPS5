import collections
import glob
import os
import re
import sys

COND_STATIC_INIT = (0x2682, 0x26F6)
COND_INIT = (0x281C, 0x2A17)
COND_DESTROY = (0x2A17, 0x2CAF)
SPIN_LOCK = (0x6BAC, 0x6BFB)
SYSTEM = ("ntdll", "KERNELBASE", "KERNEL32", "kernel32", "libwinpthread", "libstdc++", "ucrtbase", "msvcrt", "libgcc", "win32u", "ws2_32", "combase", "RPCRT4")

logs = sorted(glob.glob(os.path.join(sys.argv[1], "*.gdb.log")))
histogram = collections.Counter()
held = collections.Counter()
lines = []
for path in logs:
    text = open(path, encoding="utf-8", errors="replace").read()
    status = "hang" if "STATUS hang" in text else ("exited" if "STATUS exited" in text else "unknown")
    lock = re.search(r"^COND_LOCKED (\S+) .*?:\s+(0x[0-9a-f]+)\s*$", text, re.M)
    base = re.search(r"WINPTHREAD_TEXT (0x[0-9a-f]+)", text)
    base = int(base.group(1), 16) - 0x1000 if base else None
    threads = re.split(r"\n(?=Thread \d+ \()", text.split("thread apply all bt", 1)[-1] if "thread apply all bt" in text else text)
    summary = []
    for block in threads:
        header = re.match(r"Thread (\d+) \(", block)
        if not header:
            continue
        frames = re.findall(r"^#(\d+)\s+(?:(0x[0-9a-f]+) in )?(.*)$", block, re.M)
        marks = []
        first = None
        for _, address, rest in frames:
            if base is not None and address and "libwinpthread" in rest:
                rva = int(address, 16) - base
                for label, (low, high) in (("cond_static_init", COND_STATIC_INIT), ("pthread_cond_init", COND_INIT), ("pthread_cond_destroy", COND_DESTROY), ("pthread_spin_lock", SPIN_LOCK)):
                    if low <= rva < high:
                        marks.append(label)
            if first is None and not any(name in rest for name in SYSTEM) and not rest.startswith("?? ()"):
                first = re.sub(r"\(.*?\) at ", "at ", rest)[:160]
        where = first or (frames[0][2][:120] if frames else "no frames")
        tag = (" [" + ",".join(dict.fromkeys(marks)) + "]") if marks else ""
        summary.append(f"  thread {header.group(1)}: {where}{tag}")
        histogram[where + tag] += 1
        if "cond_static_init" in marks or "pthread_cond_init" in marks:
            held[where] += 1
    lock_text = f"{lock.group(1)}={lock.group(2)}" if lock else "unread"
    lines.append(f"{os.path.basename(path)} status={status} cond_locked={lock_text} threads={len(summary)}")
    lines.extend(summary)
print("\n".join(lines))
print("\nTHREADS AT EXIT (all logs):")
for where, count in histogram.most_common(60):
    print(f"{count:5d}  {where}")
print("\nTHREADS INSIDE A FIRST WAIT AT EXIT:")
for where, count in held.most_common():
    print(f"{count:5d}  {where}")
hangs = sum(1 for line in lines if " status=hang " in line)
print(f"\nRESULT logs={len(logs)} hangs={hangs}")
