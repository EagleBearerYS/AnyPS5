#!/usr/bin/env bash
# Runs on a GitHub runner for one finding. HEAD is the verify commit and its parent is the finding.
# Builds the finding's tests and the libc and libkernel suites, then runs the tests with main's
# implementation (they should fail), with the fix REPEAT times (they should pass), and the suites.
# config.sh sets BASE (the main commit the finding is on), REPEAT and EXTRA_TESTS (|-separated).
# An EXTRA_TESTS item starting with ^ is a regex: every test it matches runs, with --parallel 4, and
# the whole tree is built. BEFORE_REPEAT repeats the run with main's implementation until a run
# passes, so "fail" means every run failed; NO_BEFORE skips that run. VALIDATION, on Linux, builds
# main's whole tree instead and runs the named tests it has, then the change's, both under
# VK_LAYER_KHRONOS_validation with ANYPS5_NO_SHADER_CACHE=1 and ctest -V, and compares their VUIDs.
# A names-only run builds the named targets, libc/all and libkernel/all, and the suite tests' outside
# inputs: the relinker and the patched libc, libkernel, libkernel_sys and libkernel_web .prx that
# exec_arguments and guest_timed_join_import_* copy (without them those three failed with FileNotFoundError).
# SYMBOLS (|-separated build targets) lists each one's imported sce* symbols after the fix build: nm -D
# --undefined-only on Linux, the objdump -p import tables on Windows (full lists in symbols-<target>.txt).
set -u
. .github/verify/config.sh
mkdir -p verify-logs
exec > >(tee verify-logs/verify.log) 2>&1
os=${RUNNER_OS:-$(uname -s)}
jobs=$(nproc 2>/dev/null || echo 4)
head=$(git rev-parse HEAD~1)
echo "== $os: $(git log -1 --format='%h %s' "$head") on main $(git rev-parse --short "$BASE")"
git cat-file -e "$BASE^{commit}" 2>/dev/null || { echo "RESULT os=$os base-missing"; exit 2; }
changed=$(git diff --name-only "$BASE" "$head")
revert=""
for file in $(echo "$changed" | grep -vE '/tests/|^docs/|^\.github/' | grep -E '\.(c|cc|cpp|h|hpp|inc)$'); do
    git cat-file -e "$BASE:$file" 2>/dev/null && revert="$revert $file"
done
targets=""
for source in $(echo "$changed" | grep '/tests/'); do
    file=$(basename "$source")
    for cmake in $(grep -rl --include=CMakeLists.txt "$file" core); do
        targets="$targets $(awk -v f="$file" '/add_test_executable\(/ { t = $0; sub(/.*add_test_executable\(/, "", t); sub(/[^A-Za-z0-9_].*/, "", t); open = 1 } open && index($0, f) { print t; open = 0 } open && /\)/ { open = 0 }' "$cmake")"
    done
done
available=$(ctest --test-dir build -N | sed -nE 's/^ *Test +#[0-9]+: ([A-Za-z0-9_]+)$/\1/p')
commands=$(find build -name CTestTestfile.cmake -exec grep -h '^add_test(\[=\[' {} + 2>/dev/null | sed -nE 's/^add_test\(\[=\[([A-Za-z0-9_]+)\]=\] "([^"]*\/)?([^"\/]+)".*/\1 \3/p' | sed -E 's/\.exe$//')
known=$(ninja -C build -t targets all 2>/dev/null | sed -E 's/: .*//' | sort -u)
names=""
for target in $(echo $targets | tr ' ' '\n' | sort -u); do
    for cmake in $(grep -rl --include=CMakeLists.txt "COMMAND $target" core); do
        names="$names $(grep -oE "add_test\(NAME [A-Za-z0-9_]+ COMMAND $target\b" "$cmake" | sed -E 's/add_test\(NAME ([A-Za-z0-9_]+) .*/\1/')"
    done
done
regexes=""
unknown=""
for name in $(echo "${EXTRA_TESTS:-}" | tr '|' ' '); do
    case $name in ^*) regexes="${regexes:+$regexes|}$name"; continue ;; esac
    target=$(echo "$commands" | awk -v n="$name" '$1 == n { print $2; exit }')
    [ -n "$target" ] && [ -n "$known" ] && ! echo "$known" | grep -qxF "$target" && target=""
    [ -z "$target" ] && target=$(grep -rhoE --include=CMakeLists.txt "add_test\(NAME $name COMMAND [A-Za-z0-9_]+" core | head -1 | awk '{print $4}')
    # Tests registered in a foreach (add_test(NAME ${agcTestName} COMMAND ${agcTest})) run <name>_tests.
    [ -z "$target" ] && echo "$available" | grep -qxF "$name" && target=${name}_tests
    [ -n "$target" ] && { targets="$targets $target"; names="$names $name"; }
    echo "$available" | grep -qxF "$name" || unknown="${unknown:+$unknown,}$name"
done
names=$(for name in $names; do echo "$available" | grep -qxF "$name" && echo "$name"; done | sort -u)
tests=$(echo "$names" | paste -sd'|' -)
pattern=${tests:+^($tests)\$}
[ -n "$regexes" ] && pattern="${pattern:+$pattern|}$regexes"
parallel=${regexes:+--parallel 4}
echo "changed: $(echo $changed)"
echo "reverted for the run with main's implementation: ${revert:- none}${NO_BEFORE:+ (not run)}"
echo "tests on $os: ${tests:-none}${regexes:+, and every test matching $regexes ($(echo "$available" | grep -cE "$regexes") in all)}"
[ -n "$pattern" ] || { echo "RESULT os=$os no-tests-on-this-host${unknown:+ unknown-tests=$unknown}"; exit 0; }
[ -z "$unknown" ] || echo "not on $os: $unknown (the push checked every name against the tree, so these are other-platform tests)"
if [ -n "$regexes" ] && [ "$(echo "$available" | grep -cE "$regexes")" = 0 ]; then echo "RESULT os=$os regex-matches-nothing regex=$regexes"; exit 1; fi
if [ -n "$known" ]; then
    dropped=$(for target in $targets; do echo "$known" | grep -qxF "$target" || echo "$target"; done | sort -u)
    [ -n "$dropped" ] && echo "not build targets here, skipped: $(echo $dropped)"
    targets=$(for target in $targets; do echo "$known" | grep -qxF "$target" && echo "$target"; done | sort -u)
fi
output=--output-on-failure
[ -n "${VERBOSE:-}" ] && output=-V
validation=""
if [ -n "${VALIDATION:-}" ]; then
    if [ "$os" = Linux ]; then
        export VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation ANYPS5_NO_SHADER_CACHE=1 VK_LOADER_DEBUG=layer
        output=-V
    else
        validation=not-on-this-host
    fi
fi
suites="core/libs/prx/libc core/libs/prx/libkernel"
suiteInputs=""
for want in relinker core/libs/libs/libc.prx core/libs/libs/libkernel.prx core/libs/libs/libkernel_sys.prx core/libs/libs/libkernel_web.prx; do
    pattern_=$(printf '%s' "$want" | sed -e 's/\./\\./g' -e 's#/#[/\\\\]#g')
    hit=$(echo "$known" | grep -xE "$pattern_" | head -1)
    [ -n "$hit" ] && suiteInputs="$suiteInputs $hit"
done
echo "suite inputs built with the named targets:${suiteInputs:- none found}"
build() {
    if [ -n "$regexes" ]; then cmake --build build -j"$jobs" --target all $(echo $targets | tr ' ' '\n' | sort -u) > "verify-logs/build-$1.log" 2>&1; return; fi
    cmake --build build -j"$jobs" --target $(echo $targets | tr ' ' '\n' | sort -u) $(for dir in $suites; do echo "$dir/all"; done) $suiteInputs > "verify-logs/build-$1.log" 2>&1
}
failed() { awk '/The following tests FAILED:/ { f = 1; next } f && /^[[:space:]]*[0-9]+ - / { sub(/^[[:space:]]*[0-9]+ - /, ""); sub(/[^A-Za-z0-9_].*/, ""); print; next } { f = 0 }' "$@" 2>/dev/null | sort -u; }
suite() {
    local result=0
    for dir in $suites; do
        APS5_SKIP_TIMING_TESTS=1 ctest --test-dir "build/$dir" --parallel 4 --timeout 120 --output-on-failure > "verify-logs/suite-$1-$(basename "$dir").log" 2>&1 || result=1
    done
    return $result
}
before=not-run
mainFailures=""
mainTests=""
if [ -n "${VALIDATION:-}" ] && [ "$os" = Linux ]; then
    git diff --no-renames --name-only --diff-filter=A "$BASE" "$head" > verify-logs/files-added.txt
    git diff --no-renames --name-only --diff-filter=M "$BASE" "$head" > verify-logs/files-modified.txt
    git diff --no-renames --name-only --diff-filter=D "$BASE" "$head" > verify-logs/files-deleted.txt
    [ -s verify-logs/files-added.txt ] && xargs -a verify-logs/files-added.txt rm -f --
    cat verify-logs/files-modified.txt verify-logs/files-deleted.txt | grep . | xargs -r git checkout "$BASE" --
    if cmake --build build -j"$jobs" > verify-logs/build-main.log 2>&1; then
        mainAvailable=$(ctest --test-dir build -N | sed -nE 's/^ *Test +#[0-9]+: ([A-Za-z0-9_]+)$/\1/p')
        mainTests=$(for name in $names; do echo "$mainAvailable" | grep -qxF "$name" && echo "$name"; done | paste -sd'|' -)
        mainPattern=${mainTests:+^($mainTests)\$}
        [ -n "$regexes" ] && mainPattern="${mainPattern:+$mainPattern|}$regexes"
        if [ -n "$mainPattern" ]; then
            ctest --test-dir build -R "$mainPattern" $parallel $output --timeout 120 > verify-logs/before.log 2>&1 && before=pass || before=fail
        else
            before=no-tests-on-main
        fi
        suite main
        mainFailures=$(failed verify-logs/suite-main-*.log)
    else
        before=build-failed
        tail -30 verify-logs/build-main.log
    fi
    [ -s verify-logs/files-deleted.txt ] && xargs -a verify-logs/files-deleted.txt rm -f --
    cat verify-logs/files-added.txt verify-logs/files-modified.txt | grep . | xargs -r git checkout "$head" --
elif [ -n "$revert" ] && [ -z "${NO_BEFORE:-}" ]; then
    git checkout "$BASE" -- $revert
    if build main; then
        ctest --test-dir build -R "$pattern" ${BEFORE_REPEAT:+--repeat until-pass:$BEFORE_REPEAT} $parallel $output --timeout 120 > verify-logs/before.log 2>&1 && before=pass || before=fail
        suite main
        mainFailures=$(failed verify-logs/suite-main-*.log)
    else
        before=build-failed
        tail -30 verify-logs/build-main.log
    fi
    git checkout "$head" -- $revert
fi
if ! build fix; then
    tail -40 verify-logs/build-fix.log
    echo "RESULT os=$os before=$before after=build-failed"
    exit 1
fi
symbols=""
for target in $(echo "${SYMBOLS:-}" | tr '|' ' '); do
    binary=$(find build -type f \( -name "$target" -o -name "$target.exe" \) | head -1)
    if [ -z "$binary" ]; then symbols="$symbols$target: not built"$'\n'; continue; fi
    if [ "$os" = Windows ]; then
        objdump -p "$binary" > "verify-logs/objdump-$target.txt" 2>&1
        awk '/DLL Name:/ { dll = $NF; next } /^[^[:space:]]/ { dll = "" } dll != "" && NF >= 3 && $1 ~ /^[0-9a-fA-F]+$/ { print $NF " from " dll }' "verify-logs/objdump-$target.txt" > "verify-logs/symbols-$target.txt"
    else
        nm -D --undefined-only "$binary" | awk '{ print $NF }' > "verify-logs/symbols-$target.txt"
    fi
    imported=$(grep -E '^sce' "verify-logs/symbols-$target.txt" | sort -u)
    symbols="$symbols$target: $(echo "$imported" | grep -c .) sce imports: $(echo $imported | sed -E 's/ from [^ ]+//g')"$'\n'
done
ctest --test-dir build -R "$pattern" --repeat until-fail:"$REPEAT" $parallel $output --timeout 120 > verify-logs/after.log 2>&1 && after=pass || after=fail
ran=$(grep -hoE 'tests failed out of [0-9]+' verify-logs/after.log | tail -1 | grep -oE '[0-9]+$')
if grep -q 'No tests were found' verify-logs/after.log || [ "${ran:-0}" = 0 ]; then after=no-tests; fi
skipped=$(sed -n '/^The following tests did not run:/,/^$/p' verify-logs/after.log | grep -c '(Skipped)')
suite fix && suiteResult=pass || suiteResult=fail
fixFailures=$(failed verify-logs/suite-fix-*.log)
newFailures=$(comm -23 <(echo "$fixFailures" | grep .) <(echo "$mainFailures" | grep . | sort -u) | paste -sd, -)
passed=$(grep -hE 'tests passed' verify-logs/suite-fix-*.log | sed -E 's/.*tests passed, ([0-9]+) tests failed out of ([0-9]+).*/\2 run, \1 failed/' | paste -sd' ' -)
vuids() { cat "$1" 2>/dev/null | grep -oE 'VUID-[A-Za-z0-9_-]+' | sort -u; }
loaded() { cat "$1" 2>/dev/null | grep -cE 'Insert instance layer "?VK_LAYER_KHRONOS_validation'; }
missing() { cat "$1" 2>/dev/null | grep -c 'was not found but was requested by env var VK_INSTANCE_LAYERS'; }
loader() { cat "$1" 2>/dev/null | grep -c 'LAYER:'; }
vuidTests() {
    awk '/^ *Start +[0-9]+: / { line = $0; sub(/^ *Start +/, "", line); split(line, p, ": "); sub(/ .*/, "", p[2]); name[p[1]] = p[2]; next }
        /^[0-9]+: / { num = $0; sub(/:.*/, "", num); rest = $0
            while (match(rest, /VUID-[A-Za-z0-9_-]+/)) { print (num in name ? name[num] : num), substr(rest, RSTART, RLENGTH); rest = substr(rest, RSTART + RLENGTH) } }' "$1" 2>/dev/null | sort -u
}
if [ -n "${VALIDATION:-}" ] && [ "$os" = Linux ]; then
    newVuids=$(comm -13 <(vuids verify-logs/before.log) <(vuids verify-logs/after.log) | paste -sd, -)
    case $before in pass | fail) mainRan=1 ;; *) mainRan="" ;; esac
    if [ -z "$mainRan" ]; then validation=no-main-run
    elif [ "$(missing verify-logs/before.log)" -gt 0 ] || [ "$(missing verify-logs/after.log)" -gt 0 ]; then validation=layer-missing
    elif [ "$(loader verify-logs/after.log)" -eq 0 ]; then validation=no-vulkan-instance
    elif [ "$(loaded verify-logs/after.log)" -eq 0 ]; then validation=layer-missing
    elif [ "$(loader verify-logs/before.log)" -eq 0 ]; then validation=no-vulkan-instance-on-main
    elif [ "$(loaded verify-logs/before.log)" -eq 0 ]; then validation=layer-missing
    elif [ -n "$newVuids" ]; then validation=new-vuids:$newVuids
    else validation=clean
    fi
fi
{
    echo "with main's implementation${BEFORE_REPEAT:+, $BEFORE_REPEAT runs (fail means every run failed)}: $before"
    grep -hE -m6 'Require|abort|terminate|what\(\)|Failed|Timeout' verify-logs/before.log 2>/dev/null
    awk '/\*\*\*Failed|\*\*\*Exception|\*\*\*Timeout|Subprocess aborted/ {show=10; print; next} /Test +#[0-9]+:/ {show=0} show>0 && NF {print; show--}' verify-logs/before.log 2>/dev/null | head -24
    echo "with the fix, $REPEAT runs: $after"
    grep -hE -m6 'Require|abort|terminate|what\(\)|tests passed|tests failed' verify-logs/after.log
    awk '/\*\*\*Failed|\*\*\*Exception|\*\*\*Timeout|Subprocess aborted/ {show=10; print; next} /Test +#[0-9]+:/ {show=0} show>0 && NF {print; show--}' verify-logs/after.log | head -40
    echo "failed with the fix: $(failed verify-logs/after.log | paste -sd' ' -); skipped: $(grep -cE '\(Skipped\)' verify-logs/after.log)"
    echo "suites (libc, libkernel): $passed"
    echo "suite failures with the fix: $(echo $fixFailures)"
    echo "suite failures with main's implementation: $(echo $mainFailures)"
    [ -n "$symbols" ] && printf "imported sce* symbols of the fix build (%s):\n%s" "$([ "$os" = Windows ] && echo 'objdump -p import tables' || echo 'nm -D --undefined-only')" "$symbols"
    if [ -n "${VALIDATION:-}" ] && [ "$os" = Linux ]; then
        echo "main's tree ran: ${mainTests:-none of the named tests}${regexes:+, and $regexes}"
        echo "validation layer: inserted $(loaded verify-logs/before.log) times on main and $(loaded verify-logs/after.log) with the fix; loader layer lines $(loader verify-logs/before.log) and $(loader verify-logs/after.log); 'was not found' $(missing verify-logs/before.log) and $(missing verify-logs/after.log) times"
        cat verify-logs/after.log 2>/dev/null | grep -m1 -E 'Insert instance layer "?VK_LAYER_KHRONOS_validation'
        echo "VUIDs with main's implementation: $(vuids verify-logs/before.log | paste -sd' ' -)"
        echo "VUIDs with the fix: $(vuids verify-logs/after.log | paste -sd' ' -)"
        echo "VUIDs the fix adds (none of them anywhere in main's log): ${newVuids:-none}"
        if [ -n "$newVuids" ]; then
            echo "  by test, with whether main's tree has that test:"
            mainRun=$(cat verify-logs/before.log 2>/dev/null | sed -nE 's/^ *Start +[0-9]+: ([A-Za-z0-9_]+).*/\1/p' | sort -u)
            vuidTests verify-logs/after.log | grep -wF -f <(echo "$newVuids" | tr ',' '\n') | while read -r test vuid; do
                echo "$mainRun" | grep -qxF "$test" && where="also run on main" || where="not on main"
                echo "  $test $vuid ($where)"
            done
        fi
        echo "GPU: $(cat verify-logs/after.log | grep -ohE 'Selected GPU name=[^[:cntrl:]]*' | sort -u | paste -sd';' -)"
        echo "driver: $(env -u VK_INSTANCE_LAYERS -u VK_LOADER_DEBUG vulkaninfo --summary 2>/dev/null | grep -E 'deviceName|driverName|driverInfo|conformanceVersion' | sed -E 's/^[[:space:]]+//; s/[[:space:]]+= /=/' | paste -sd';' -)"
        echo "OS: $(. /etc/os-release && echo "$PRETTY_NAME"), kernel $(uname -r)"
    fi
    echo "RESULT os=$os before=$before after=$after count=${ran:-0} skipped=${skipped:-0}${unknown:+ not-on-this-host=$unknown} suite=$suiteResult new-failures=${newFailures:-none} tests=$tests${regexes:+ regex=$regexes}${validation:+ validation=$validation}"
} | tee verify-logs/summary.txt
{ echo "### $os"; echo '```'; cat verify-logs/summary.txt; echo '```'; } >> "${GITHUB_STEP_SUMMARY:-/dev/null}"
[ "$after" = pass ] && [ -z "$newFailures" ] && case $validation in "" | clean | not-on-this-host | no-vulkan-instance) true ;; *) false ;; esac
