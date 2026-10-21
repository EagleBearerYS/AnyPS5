#!/usr/bin/env python3
import os
import subprocess
import sys


def resolved_stacks(lines):
    stacks = []
    for line in lines:
        if line.startswith("[bigalloc] #") and " frames " in line:
            stacks.append((line, []))
        elif line.startswith("    ") and stacks:
            stacks[-1][1].append(line)
    return stacks


def main():
    binary, resolver = sys.argv[1], sys.argv[2]
    functions = sys.argv[3:] or ["AllocateBigVectors"]
    environment = dict(os.environ, APS5_PROFILE_BIG_ALLOCS="1")
    run = subprocess.run([binary], env=environment, capture_output=True, text=True, timeout=60, check=False)
    sys.stdout.write(run.stdout)
    if run.returncode != 0:
        sys.stderr.write(run.stderr)
        print(f"big allocation sites: {os.path.basename(binary)} exited with {run.returncode}")
        return 1
    resolved = subprocess.run([sys.executable, resolver, "--search", os.path.dirname(os.path.abspath(binary))], input=run.stdout, capture_output=True, text=True, timeout=60, check=False)
    print(resolved.stdout)
    if resolved.returncode != 0:
        sys.stderr.write(resolved.stderr)
        print("big allocation sites: the resolver failed")
        return 1
    stacks = resolved_stacks(resolved.stdout.splitlines())
    for function in functions:
        found = next((header for header, frames in stacks if any(function in frame for frame in frames)), None)
        if found is None:
            print(f"big allocation sites: no frame of any stack resolves to {function}")
            return 1
        print(f"big allocation sites: a stack resolves to {function} ({'scanned' if ' scanned frames ' in found else 'unwound'})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
