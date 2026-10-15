#!/usr/bin/env bash
set -u
logs=$PWD/hang-logs
mkdir -p "$logs"
iterations=${ITERATIONS:-60}
build=$(cygpath -w "$PWD/build")
echo 60 > "$logs/limit"

processes() {
    powershell -NoProfile -Command "Get-CimInstance Win32_Process | Where-Object { \$_.ExecutablePath -like '$build\\*' } | ForEach-Object { '{0} {1} {2} {3}' -f \$_.ProcessId, \$_.ParentProcessId, [int]((Get-Date) - \$_.CreationDate).TotalSeconds, \$_.Name }" | tr -d '\r'
}

dumpProcess() {
    local pid=$1 name=$2 age=$3
    echo "HANG $name pid $pid age ${age}s phase $(cat "$logs/phase") at $(date +%T)" | tee -a "$logs/summary.txt"
    powershell -NoProfile -Command "(Get-Process -Id $pid).Threads | Format-Table -AutoSize Id,ThreadState,WaitReason,StartAddress,TotalProcessorTime | Out-String -Width 220" > "$logs/$name-$pid.threads.txt" 2>&1
    timeout 180 gdb -p "$pid" -batch -ex "set pagination off" -ex "info sharedlibrary" -ex "info threads" -ex "thread apply all bt" -ex "thread apply all x/12i \$pc" -ex "thread apply all info registers" -ex "thread 1" -ex "x/16gx \$rsp" -ex "x/4gx \$rbx" -ex "x/4gx \$rcx" -ex "x/4gx \$rdx" -ex "x/4gx \$rsi" -ex "x/4gx \$rdi" -ex "info symbol \$rbx" -ex "info symbol \$rcx" -ex "info symbol \$rsi" -ex "info symbol \$rdi" -ex "detach" > "$logs/$name-$pid.stacks.txt" 2>&1
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

for iteration in $(seq 1 "$iterations"); do
    echo "iteration-$iteration" > "$logs/phase"
    started=$(date +%s)
    ctest --test-dir build -R '^(agc_interpolant_mapping|agc_driver_readonly_resummarize)(_dup[0-9])?$' --parallel 4 --timeout 600 --output-on-failure > "$logs/iteration-$iteration.log" 2>&1
    status=$?
    echo "PHASE iteration-$iteration status=$status $(( $(date +%s) - started ))s $(grep -E 'tests passed|tests failed' "$logs/iteration-$iteration.log" | tail -1)" | tee -a "$logs/summary.txt"
    if [ "$status" -eq 0 ]; then rm -f "$logs/iteration-$iteration.log"; fi
done

touch "$logs/done"
wait "$monitorPid"
objdump -d -p -h -t "$PWD/build/tests/libwinpthread-1.dll" > "$logs/libwinpthread-1.objdump.txt" 2>&1
objdump -p "$PWD/build/tests/libstdc++-6.dll" | sed -n '/Export Address Table/,/^$/p;/Ordinal\/Name Pointer/,/^$/p' > "$logs/libstdc++-6.exports.txt" 2>&1
hangs=$(grep -c '^HANG' "$logs/summary.txt" 2>/dev/null || true)
echo "RESULT iterations=$iterations hangs=${hangs:-0}" | tee -a "$logs/summary.txt"
