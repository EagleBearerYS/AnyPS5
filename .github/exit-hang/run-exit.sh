#!/usr/bin/env bash
set -u
logs=$PWD/hang-logs
mkdir -p "$logs"
iterations=${ITERATIONS:-40}
here=$PWD/.github/exit-hang
ctest --test-dir build --show-only=json-v1 -R '^(agc_interpolant_mapping|agc_driver_readonly_resummarize)$' > "$logs/tests.json"
json=$(cygpath -w "$logs/tests.json")
script=$(cygpath -w "$here/exit.gdb")
launcher=$(cygpath -w "$here/launch.py")
for iteration in $(seq 1 "$iterations"); do
    pids=()
    for name in agc_interpolant_mapping agc_driver_readonly_resummarize; do
        for copy in 1 2; do
            python "$launcher" "$json" "$name" "$script" "$(cygpath -w "$logs/$name-$iteration-$copy.gdb.log")" 300 &
            pids+=($!)
        done
    done
    wait "${pids[@]}"
    taskkill //F //IM agc_driver_tests.exe > /dev/null 2>&1
    taskkill //F //IM agc_interpolant_mapping_tests.exe > /dev/null 2>&1
    echo "PHASE iteration-$iteration hangs=$(grep -l 'STATUS hang' "$logs"/*-"$iteration"-*.gdb.log 2>/dev/null | wc -l)" | tee -a "$logs/phases.txt"
done
python "$(cygpath -w "$here/summarize.py")" "$(cygpath -w "$logs")" > "$logs/summary.txt" 2>&1
tail -80 "$logs/summary.txt"
