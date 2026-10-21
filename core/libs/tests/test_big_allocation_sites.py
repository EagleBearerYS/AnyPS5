#!/usr/bin/env python3
import os
import subprocess
import sys


def main():
    binary, resolver = sys.argv[1], sys.argv[2]
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
    lines = resolved.stdout.splitlines()
    start = next((index for index, line in enumerate(lines) if " live 2.0 MiB in 2 (+3 -1) sizes 1MiB 2 2MiB 0 other 0 frames " in line), None)
    if start is None:
        print("big allocation sites: the resolved report has no stack with 2 live 1 MiB blocks")
        return 1
    frames = []
    for line in lines[start + 1:]:
        if not line.startswith("    "):
            break
        frames.append(line)
    if not any("AllocateBigVectors" in frame for frame in frames):
        print("big allocation sites: no frame of the stack resolves to AllocateBigVectors")
        return 1
    print("big allocation sites: the stack resolves to AllocateBigVectors")
    return 0


if __name__ == "__main__":
    sys.exit(main())
