#!/usr/bin/env python3
import argparse
import bisect
import os
import re
import shutil
import struct
import subprocess
import sys

STACK = re.compile(r"^\[bigalloc\] #\d+ live .* frames (?P<frames>.*)$")
FRAME = re.compile(r"^(?P<module>\S+)\+0x(?P<offset>[0-9a-fA-F]+)$")


def image_base(path):
    with open(path, "rb") as image:
        head = image.read(4096)
    if head[:2] == b"MZ":
        pe = struct.unpack_from("<I", head, 0x3C)[0]
        with open(path, "rb") as image:
            image.seek(pe)
            header = image.read(64)
        if header[:4] != b"PE\0\0":
            return 0
        if struct.unpack_from("<H", header, 24)[0] == 0x20B:
            return struct.unpack_from("<Q", header, 48)[0]
        return struct.unpack_from("<I", header, 52)[0]
    if head[:4] != b"\x7fELF":
        return 0
    wide = head[4] == 2
    order = "<" if head[5] == 1 else ">"
    if struct.unpack_from(order + "H", head, 16)[0] != 2:
        return 0
    if wide:
        table = struct.unpack_from(order + "Q", head, 32)[0]
        size, count = struct.unpack_from(order + "HH", head, 54)
    else:
        table = struct.unpack_from(order + "I", head, 28)[0]
        size, count = struct.unpack_from(order + "HH", head, 42)
    with open(path, "rb") as image:
        image.seek(table)
        entries = image.read(size * count)
    loads = []
    for index in range(count):
        entry = entries[index * size:(index + 1) * size]
        if struct.unpack_from(order + "I", entry, 0)[0] != 1:
            continue
        address = struct.unpack_from(order + "Q", entry, 16)[0] if wide else struct.unpack_from(order + "I", entry, 8)[0]
        loads.append(address & ~0xFFF)
    return min(loads) if loads else 0


def portable_executable(path):
    with open(path, "rb") as image:
        return image.read(2) == b"MZ"


def find_module(name, searches, explicit):
    if name in explicit:
        return explicit[name]
    for search in searches:
        direct = os.path.join(search, name)
        if os.path.isfile(direct):
            return direct
    for search in searches:
        for root, _, files in os.walk(search):
            if name in files:
                return os.path.join(root, name)
    return None


def tool(preferred, fallback):
    for name in preferred:
        if name and shutil.which(name):
            return name
    return fallback


def nearest_symbols(path, nm):
    try:
        output = subprocess.run([nm, "-C", "--defined-only", path], capture_output=True, text=True, check=False).stdout
    except OSError:
        return [], []
    symbols = []
    for line in output.splitlines():
        parts = line.split(" ", 2)
        if len(parts) == 3 and parts[1] in "tTwW" and parts[0]:
            try:
                symbols.append((int(parts[0], 16), parts[2]))
            except ValueError:
                continue
    symbols.sort()
    return [address for address, _ in symbols], [name for _, name in symbols]


def resolve_module(path, offsets, addr2line, nm):
    base = image_base(path)
    addresses = [base + offset - 1 for offset in offsets]
    results = {}
    try:
        output = subprocess.run([addr2line, "-f", "-C", "-e", path] + [hex(address) for address in addresses], capture_output=True, text=True, check=False).stdout.splitlines()
    except OSError:
        output = []
    starts, names = None, None
    for index, offset in enumerate(offsets):
        function = output[2 * index] if 2 * index < len(output) else "??"
        location = output[2 * index + 1] if 2 * index + 1 < len(output) else "??:0"
        if function.startswith("??"):
            if starts is None:
                starts, names = nearest_symbols(path, nm)
            slot = bisect.bisect_right(starts, addresses[index]) - 1
            if slot >= 0:
                function = f"{names[slot]}+{hex(addresses[index] + 1 - starts[slot])}"
        results[offset] = (function, location)
    return results


def main():
    parser = argparse.ArgumentParser(description="Resolve the module+0xoffset frames of [bigalloc] lines to function names")
    parser.add_argument("log", nargs="?", help="log file with [bigalloc] lines (standard input when absent)")
    parser.add_argument("--search", action="append", default=[], help="directory to look for the modules in (repeatable)")
    parser.add_argument("--module", action="append", default=[], help="NAME=PATH for a module found elsewhere")
    parser.add_argument("--addr2line", help="addr2line to use")
    parser.add_argument("--nm", help="nm to use")
    arguments = parser.parse_args()
    text = open(arguments.log, encoding="utf-8", errors="replace").read() if arguments.log else sys.stdin.read()
    searches = arguments.search or ["."]
    explicit = dict(entry.split("=", 1) for entry in arguments.module)
    lines = text.splitlines()
    wanted = {}
    for line in lines:
        match = STACK.match(line)
        if not match:
            continue
        for frame in match.group("frames").split():
            parsed = FRAME.match(frame)
            if parsed:
                wanted.setdefault(parsed.group("module"), set()).add(int(parsed.group("offset"), 16))
    resolved = {}
    for module, offsets in wanted.items():
        path = find_module(module, searches, explicit)
        if path is None:
            continue
        mingw = portable_executable(path)
        addr2line = arguments.addr2line or tool(["x86_64-w64-mingw32-addr2line"] if mingw else [], "addr2line")
        nm = arguments.nm or tool(["x86_64-w64-mingw32-nm"] if mingw else [], "nm")
        ordered = sorted(offsets)
        for offset, result in resolve_module(path, ordered, addr2line, nm).items():
            resolved[(module, offset)] = result
    for line in lines:
        print(line)
        match = STACK.match(line)
        if not match:
            continue
        for frame in match.group("frames").split():
            parsed = FRAME.match(frame)
            key = (parsed.group("module"), int(parsed.group("offset"), 16)) if parsed else None
            function, location = resolved.get(key, ("(module not found)", "")) if key else ("(no module)", "")
            print(f"    {frame:<40} {function}  {location}".rstrip())
    return 0


if __name__ == "__main__":
    sys.exit(main())
