#!/usr/bin/env bash
# Runs on a GitHub runner for one finding. HEAD is the verify commit and its parent is the finding.
# Builds the finding's tests and the libc and libkernel suites, then runs the tests with main's
# implementation (they should fail), with the fix REPEAT times (they should pass), and the suites.
# config.sh sets BASE (the main commit the finding is on), REPEAT and EXTRA_TESTS (|-separated).
# An EXTRA_TESTS item starting with ^ is a regex: every test it matches runs, with --parallel 4, and
# the whole tree is built. BEFORE_REPEAT repeats the run with main's implementation until a run
# passes, so "fail" means every run failed; NO_BEFORE skips that run.
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
names=""
for target in $(echo $targets | tr ' ' '\n' | sort -u); do
    for cmake in $(grep -rl --include=CMakeLists.txt "COMMAND $target" core); do
        names="$names $(grep -oE "add_test\(NAME [A-Za-z0-9_]+ COMMAND $target\b" "$cmake" | sed -E 's/add_test\(NAME ([A-Za-z0-9_]+) .*/\1/')"
    done
done
regexes=""
for name in $(echo "${EXTRA_TESTS:-}" | tr '|' ' '); do
    case $name in ^*) regexes="${regexes:+$regexes|}$name"; continue ;; esac
    target=$(grep -rhoE --include=CMakeLists.txt "add_test\(NAME $name COMMAND [A-Za-z0-9_]+" core | head -1 | awk '{print $4}')
    # Tests registered in a foreach (add_test(NAME ${agcTestName} COMMAND ${agcTest})) run <name>_tests.
    [ -z "$target" ] && echo "$available" | grep -qxF "$name" && target=${name}_tests
    [ -n "$target" ] && { targets="$targets $target"; names="$names $name"; }
done
names=$(for name in $names; do echo "$available" | grep -qxF "$name" && echo "$name"; done | sort -u)
tests=$(echo "$names" | paste -sd'|' -)
pattern=${tests:+^($tests)\$}
[ -n "$regexes" ] && pattern="${pattern:+$pattern|}$regexes"
parallel=${regexes:+--parallel 4}
echo "changed: $(echo $changed)"
echo "reverted for the run with main's implementation: ${revert:- none}${NO_BEFORE:+ (not run)}"
echo "tests on $os: ${tests:-none}${regexes:+, and every test matching $regexes ($(echo "$available" | grep -cE "$regexes") in all)}"
[ -n "$pattern" ] || { echo "RESULT os=$os no-tests-on-this-host"; exit 0; }
suites="core/libs/prx/libc core/libs/prx/libkernel"
build() {
    if [ -n "$regexes" ]; then cmake --build build -j"$jobs" > "verify-logs/build-$1.log" 2>&1; return; fi
    cmake --build build -j"$jobs" --target $(echo $targets | tr ' ' '\n' | sort -u) $(for dir in $suites; do echo "$dir/all"; done) > "verify-logs/build-$1.log" 2>&1
}
failed() { awk '/The following tests FAILED:/ { f = 1; next } f && /^[[:space:]]*[0-9]+ - / { sub(/^[[:space:]]*[0-9]+ - /, ""); sub(/[^A-Za-z0-9_].*/, ""); print; next } { f = 0 }' "$@" 2>/dev/null | sort -u; }
suite() {
    local result=0
    for dir in $suites; do
        ctest --test-dir "build/$dir" --parallel 4 --timeout 120 > "verify-logs/suite-$1-$(basename "$dir").log" 2>&1 || result=1
    done
    return $result
}
before=not-run
mainFailures=""
if [ -n "$revert" ] && [ -z "${NO_BEFORE:-}" ]; then
    git checkout "$BASE" -- $revert
    if build main; then
        ctest --test-dir build -R "$pattern" ${BEFORE_REPEAT:+--repeat until-pass:$BEFORE_REPEAT} $parallel --output-on-failure --timeout 120 > verify-logs/before.log 2>&1 && before=pass || before=fail
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
ctest --test-dir build -R "$pattern" --repeat until-fail:"$REPEAT" $parallel --output-on-failure --timeout 120 > verify-logs/after.log 2>&1 && after=pass || after=fail
suite fix && suiteResult=pass || suiteResult=fail
fixFailures=$(failed verify-logs/suite-fix-*.log)
newFailures=$(comm -23 <(echo "$fixFailures" | grep .) <(echo "$mainFailures" | grep . | sort -u) | paste -sd, -)
passed=$(grep -hE 'tests passed' verify-logs/suite-fix-*.log | sed -E 's/.*tests passed, ([0-9]+) tests failed out of ([0-9]+).*/\2 run, \1 failed/' | paste -sd' ' -)
{
    echo "with main's implementation${BEFORE_REPEAT:+, $BEFORE_REPEAT runs (fail means every run failed)}: $before"
    grep -hE -m6 'Require|abort|terminate|what\(\)|Failed|Timeout' verify-logs/before.log 2>/dev/null
    echo "with the fix, $REPEAT runs: $after"
    grep -hE -m6 'Require|abort|terminate|what\(\)|tests passed|tests failed' verify-logs/after.log
    echo "failed with the fix: $(failed verify-logs/after.log | paste -sd' ' -); skipped: $(grep -cE '\(Skipped\)' verify-logs/after.log)"
    echo "suites (libc, libkernel): $passed"
    echo "suite failures with the fix: $(echo $fixFailures)"
    echo "suite failures with main's implementation: $(echo $mainFailures)"
    echo "RESULT os=$os before=$before after=$after suite=$suiteResult new-failures=${newFailures:-none} tests=$tests${regexes:+ regex=$regexes}"
} | tee verify-logs/summary.txt
{ echo "### $os"; echo '```'; cat verify-logs/summary.txt; echo '```'; } >> "${GITHUB_STEP_SUMMARY:-/dev/null}"
[ "$after" = pass ] && [ -z "$newFailures" ]
