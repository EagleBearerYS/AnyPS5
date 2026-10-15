import json
import os
import subprocess
import sys

tests_json, name, gdb_script, log_path, timeout = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5])
tests = json.load(open(tests_json, encoding="utf-8"))["tests"]
test = next(t for t in tests if t["name"] == name)
env = dict(os.environ)
cwd = None
for prop in test.get("properties", []):
    if prop["name"] == "WORKING_DIRECTORY":
        cwd = prop["value"]
    elif prop["name"] == "ENVIRONMENT":
        for entry in prop["value"]:
            key, _, value = entry.partition("=")
            env[key] = value
    elif prop["name"] == "ENVIRONMENT_MODIFICATION":
        for entry in prop["value"]:
            key, _, rest = entry.partition("=")
            op, _, value = rest.partition(":")
            current = env.get(key, "")
            if op == "path_list_prepend":
                env[key] = value + (os.pathsep + current if current else "")
            elif op == "path_list_append":
                env[key] = (current + os.pathsep if current else "") + value
            elif op == "set":
                env[key] = value
            elif op == "unset":
                env.pop(key, None)
command = ["gdb", "-nx", "-batch", "-x", gdb_script, "--args"] + test["command"]
with open(log_path, "w", encoding="utf-8", errors="replace") as log:
    log.write("COMMAND " + " ".join(test["command"]) + "\n")
    log.flush()
    try:
        result = subprocess.run(command, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
        log.write(f"\nSTATUS exited gdb={result.returncode}\n")
    except subprocess.TimeoutExpired:
        log.write("\nSTATUS hang\n")
