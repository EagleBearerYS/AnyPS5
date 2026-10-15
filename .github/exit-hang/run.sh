#!/usr/bin/env bash
set -u
logs=$PWD/hang-logs
mkdir -p "$logs"
repeats=${REPEATS:-100}
budget=${BUDGET_SECONDS:-5400}
started=$(date +%s)
build=$(cygpath -w "$PWD/build")
echo 60 > "$logs/limit"

processes() {
    powershell -NoProfile -Command "Get-CimInstance Win32_Process | Where-Object { \$_.ExecutablePath -like '$build\\*' } | ForEach-Object { '{0} {1} {2} {3}' -f \$_.ProcessId, \$_.ParentProcessId, [int]((Get-Date) - \$_.CreationDate).TotalSeconds, \$_.Name }" | tr -d '\r'
}

dumpProcess() {
    local pid=$1 name=$2 age=$3
    echo "HANG $name pid $pid age ${age}s phase $(cat "$logs/phase") at $(date +%T)" | tee -a "$logs/summary.txt"
    {
        echo "== processes under the build directory (pid parent age name)"
        processes
        echo "== all processes"
        tasklist //V //FO CSV
    } > "$logs/$name-$pid.processes.txt" 2>&1
    powershell -NoProfile -Command "(Get-Process -Id $pid).Threads | Format-Table -AutoSize Id,ThreadState,WaitReason,StartAddress,TotalProcessorTime | Out-String -Width 220" > "$logs/$name-$pid.threads.txt" 2>&1
    timeout 180 gdb -p "$pid" -batch -ex "set pagination off" -ex "info threads" -ex "thread apply all bt" -ex "detach" > "$logs/$name-$pid.stacks.txt" 2>&1
    taskkill //F //T //PID "$pid" > /dev/null 2>&1
}

monitor() {
    while [ ! -f "$logs/done" ]; do
        local limit
        limit=$(cat "$logs/limit")
        while read -r pid parent age name; do
            [ -n "${pid:-}" ] || continue
            [ -f "$logs/seen-$pid" ] && continue
            if [ "$age" -ge "$limit" ]; then
                touch "$logs/seen-$pid"
                dumpProcess "$pid" "$name" "$age"
            fi
        done < <(processes)
        sleep 1
    done
}

monitor &
monitorPid=$!

echo targeted > "$logs/phase"
phaseStart=$(date +%s)
ctest --test-dir build -R '^(agc_interpolant_mapping|agc_driver_readonly_resummarize)(_dup[0-9])?$' --parallel 4 --repeat "until-fail:$repeats" --timeout 600 --output-on-failure > "$logs/targeted.log" 2>&1
echo "PHASE targeted status=$? $(( $(date +%s) - phaseStart ))s" | tee -a "$logs/summary.txt"

echo 90 > "$logs/limit"
sweep=0
while [ $(( $(date +%s) - started )) -lt "$budget" ]; do
    sweep=$((sweep + 1))
    echo "sweep-$sweep" > "$logs/phase"
    phaseStart=$(date +%s)
    ctest --test-dir build -R '^agc' --parallel 4 --timeout 600 --output-on-failure > "$logs/sweep-$sweep.log" 2>&1
    status=$?
    took=$(( $(date +%s) - phaseStart ))
    summaryLine=$(grep -E 'tests passed|tests failed' "$logs/sweep-$sweep.log" | tail -1)
    echo "PHASE sweep-$sweep status=$status ${took}s $summaryLine" | tee -a "$logs/summary.txt"
    if [ "$status" -eq 0 ]; then rm -f "$logs/sweep-$sweep.log"; fi
done

touch "$logs/done"
wait "$monitorPid"
hangs=$(grep -c '^HANG' "$logs/summary.txt" 2>/dev/null || true)
echo "RESULT sweeps=$sweep hangs=${hangs:-0}" | tee -a "$logs/summary.txt"
