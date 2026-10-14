#!/usr/bin/env bash
# Runs on a GitHub Linux runner. HEAD is the measure commit; its parent is the measured branch.
# config.sh sets TREES ("label=commit ...", commit "head" for the branch itself), TARGETS_<label>,
# SUITE_<label> (a ctest regex, may be empty), RUNS (";"-separated "target args"), REPEATS and
# CONFIGURE_EXTRA. Each tree is the head's files, with every non-test, non-doc C/C++ file that
# differs from its commit taken from that commit.
set -u
. .github/measure/config.sh
mkdir -p measure-logs
exec > >(tee measure-logs/measure.log) 2>&1
head=$(git rev-parse HEAD~1)
jobs=$(nproc)
echo "== measuring $(git log -1 --format='%h %s' "$head")"

build_tree() {
    local label=$1 commit=$2 targets=$3 files f
    git checkout -q "$head" -- .
    if [ "$commit" != head ]; then
        files=$(git diff --name-only "$commit" "$head" | grep -vE '/tests/|^docs/|^\.github/' | grep -E '\.(c|cc|cpp|h|hpp|inc)$')
        for f in $files; do
            if git cat-file -e "$commit:$f" 2>/dev/null; then git checkout -q "$commit" -- "$f"; else rm -f "$f"; fi
        done
        echo "tree $label: $(echo $files | wc -w) files from $(git rev-parse --short "$commit")"
    else
        echo "tree $label: the branch head"
    fi
    if ! cmake -S . -B "build-$label" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
        -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
        -DANYPS5_ENABLE_SPIRV_TOOLS=ON $CONFIGURE_EXTRA > "measure-logs/configure-$label.log" 2>&1; then
        echo "tree $label: CONFIGURE FAILED"; tail -30 "measure-logs/configure-$label.log"
        return 1
    fi
    if cmake --build "build-$label" -j"$jobs" --target $targets > "measure-logs/build-$label.log" 2>&1; then
        echo "tree $label: built $targets"
    else
        echo "tree $label: BUILD FAILED"; grep -E 'error:|Error [0-9]' "measure-logs/build-$label.log" | head -40
        return 1
    fi
}

built=""
for spec in $TREES; do
    label=${spec%%=*}
    commit=${spec#*=}
    targets_var="TARGETS_$label"
    if build_tree "$label" "$commit" "${!targets_var}"; then built="$built $label"; fi
done
git checkout -q "$head" -- .
echo "== built:$built"

IFS=';' read -ra runs <<< "$RUNS"
for repeat in $(seq 1 "$REPEATS"); do
    for run in "${runs[@]}"; do
        target=${run%% *}
        args=${run#"$target"}
        for label in $built; do
            exe=$(find "build-$label" -type f -name "$target" | head -1)
            if [ -z "$exe" ]; then echo "RUN $label repeat=$repeat [$run]: no $target"; continue; fi
            out=$(ANYPS5_NO_SHADER_CACHE=1 timeout 600 "$exe" $args 2>&1)
            status=$?
            echo "$out" >> "measure-logs/run-$label.log"
            echo "$out" | grep -E '^\[address-reuse\]' | sed "s/^/RUN $label repeat=$repeat args=[$args] /"
            if [ "$status" -ne 0 ]; then
                echo "RUN $label repeat=$repeat args=[$args] exit $status: $(echo "$out" | grep -E 'Require|abort|what\(\)|failed' | head -2 | tr '\n' ' ')"
            fi
        done
    done
done

for label in $built; do
    suite_var="SUITE_$label"
    [ -n "${!suite_var:-}" ] || continue
    ctest --test-dir "build-$label" -R "${!suite_var}" --output-on-failure --timeout 600 > "measure-logs/suite-$label.log" 2>&1
    echo "SUITE $label: $(grep -E 'tests passed|tests failed' "measure-logs/suite-$label.log" | tail -1)"
    grep -E '^\s*[0-9]+ - ' "measure-logs/suite-$label.log" | sed "s/^/SUITE $label failed: /"
done
grep -E '^(RUN|SUITE|== built|tree )' measure-logs/measure.log > measure-logs/summary.txt
{ echo '```'; cat measure-logs/summary.txt; echo '```'; } >> "${GITHUB_STEP_SUMMARY:-/dev/null}"
