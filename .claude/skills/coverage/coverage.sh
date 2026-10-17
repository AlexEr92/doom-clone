#!/usr/bin/env bash
# Unit-test coverage of src/ with gcovr, in a separate build tree.
#
#   coverage.sh               configure + build + ctest + summary with thresholds
#   coverage.sh report        summary with thresholds over the existing tree
#   coverage.sh html          full run + line-by-line HTML report
#   coverage.sh changed [REV] full run + changed src/ lines no test executes,
#                             against the merge base with REV (default HEAD),
#                             working tree included
#
# Exits 0 when the thresholds hold, 1 when they do not, 2 when the build,
# the tests or the tooling fail.

set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$root" || exit 2

BUILD=build-cov
# A ratchet from the measured base (lines 81.6 %, decisions 71.4 %): raise
# them as tests are added, never lower them to let a change through.
LINE_MIN=78
DECISION_MIN=68

command -v gcovr >/dev/null 2>&1 || {
    echo "gcovr не найден:  sudo apt-get install -y gcovr" >&2
    exit 2
}

GCOVR=(gcovr --root . --filter 'src/' --decisions)

run_tests() {
    cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON \
        -DBUILD_TESTS=ON >/dev/null || {
        echo "конфигурация $BUILD не прошла" >&2
        exit 2
    }
    local log
    log="$(cmake --build "$BUILD" -j 2>&1)" || {
        printf '%s\n' "$log" >&2
        echo "сборка $BUILD не прошла" >&2
        exit 2
    }
    # gcov accumulates counters across runs: without this a test deleted or
    # weakened since the last run would still be credited with its lines.
    find "$BUILD" -name '*.gcda' -delete
    ctest --test-dir "$BUILD" --output-on-failure >"$BUILD/ctest.log" 2>&1 || {
        cat "$BUILD/ctest.log" >&2
        echo "юнит-тесты не прошли — покрытие не считается" >&2
        exit 2
    }
}

# The counters the last ctest wrote, file by file. Given the build tree,
# gcovr would also read the .gcno of test targets removed or renamed since:
# CMake leaves their object directories behind, gcov fails on a source that
# is gone, and gcovr fails with it. Only what ctest ran is measured.
datafiles() {
    mapfile -t DATA < <(find "$BUILD" -name '*.gcda' | sort)
    [ "${#DATA[@]}" -gt 0 ] || {
        echo "в $BUILD нет счётчиков — сначала coverage.sh без аргументов" >&2
        exit 2
    }
}

summary() {
    [ -f "$BUILD/CTestTestfile.cmake" ] || {
        echo "$BUILD не собран — сначала coverage.sh без аргументов" >&2
        exit 2
    }
    # `report` reads whatever counters the last run left. Say so when the
    # sources have moved on since, rather than print an old number as new.
    if [ ! -f "$BUILD/ctest.log" ]; then
        echo "внимание: в $BUILD нет следа прогона coverage.sh — числа ниже не от него" >&2
    elif [ -n "$(find src tests -newer "$BUILD/ctest.log" \( -name '*.c' -o -name '*.h' \) -print -quit)" ]; then
        echo "внимание: src/ или tests/ менялись после последнего прогона — числа ниже устарели;" >&2
        echo "  свежие даст coverage.sh без аргументов" >&2
    fi
    datafiles
    local out rc
    out="$("${GCOVR[@]}" --sort uncovered-percent --print-summary \
        --fail-under-line "$LINE_MIN" --fail-under-decision "$DECISION_MIN" \
        "$@" "${DATA[@]}" 2>&1)"
    rc=$?
    printf '%s\n' "$out" | grep -v '^(INFO)'
    # A bit mask: 2 is lines under the threshold, 8 decisions. Any other bit
    # is gcovr itself failing, and so is a bare 2, which is also what argparse
    # exits with on a bad argument: a threshold miss is one gcovr names.
    if [ "$rc" -ne 0 ] && { [ $((rc & ~10)) -ne 0 ] \
        || ! grep -q 'failed minimum' <<<"$out"; }; then
        echo "покрытие: НЕ ПОСЧИТАНО — gcovr завершился с кодом $rc" >&2
        exit 2
    fi
    if [ "$rc" -ne 0 ]; then
        echo "покрытие: НИЖЕ ПОРОГА (строки $LINE_MIN %, decisions $DECISION_MIN %)"
        return 1
    fi
    echo "покрытие: пороги выдержаны (строки $LINE_MIN %, decisions $DECISION_MIN %)"
}

merge_base() {
    git merge-base "$1" HEAD 2>/dev/null || {
        echo "нет такой ревизии или общей истории с ней: $1" >&2
        exit 2
    }
}

changed_lines() {
    local rev="$1" base="$2" json diff log
    json="$BUILD/coverage.json"
    diff="$BUILD/changed.diff"
    log="$BUILD/gcovr-json.log"
    datafiles
    "${GCOVR[@]}" --json -o "$json" "${DATA[@]}" 2>"$log" || {
        grep -v '^(INFO)' "$log" >&2
        echo "gcovr не выгрузил покрытие в JSON — изменённые строки не посчитаны" >&2
        exit 2
    }
    git diff -U0 "$base" -- 'src/*.c' 'src/*.h' >"$diff"
    # New files git does not track yet are changed in full.
    git ls-files --others --exclude-standard -- 'src/*.c' 'src/*.h' \
        | while read -r f; do printf 'NEW %s\n' "$f"; done >>"$diff"

    echo
    echo "изменённые строки src/ против $rev:"
    python3 -I - "$json" "$diff" <<'PY'
import json, re, sys

cov = {f["file"]: {l["line_number"]: l["count"] for l in f["lines"]}
       for f in json.load(open(sys.argv[1]))["files"]}

changed, cur = {}, None
for line in open(sys.argv[2]):
    if line.startswith("NEW "):
        path = line[4:].strip()
        changed[path] = None          # the whole file
    elif line.startswith("+++ "):
        cur = line[6:].strip() if line.startswith("+++ b/") else None
        if cur:
            changed.setdefault(cur, set())
    elif line.startswith("@@") and cur and changed[cur] is not None:
        m = re.search(r"\+(\d+)(?:,(\d+))?", line)
        start, n = int(m.group(1)), int(m.group(2) or 1)
        changed[cur].update(range(start, start + n))

def ranges(nums):
    out, nums = [], sorted(nums)
    for n in nums:
        if out and n == out[-1][1] + 1:
            out[-1][1] = n
        else:
            out.append([n, n])
    return ", ".join(str(a) if a == b else f"{a}-{b}" for a, b in out)

total = missed = 0
for path in sorted(changed):
    if path not in cov:
        if path.endswith(".c"):
            print(f"  {path}: не линкуется ни в один тест — не измеряется")
        else:
            print(f"  {path}: исполняемого кода под тестами нет")
        continue
    lines = cov[path]
    wanted = set(lines) if changed[path] is None else changed[path] & set(lines)
    if not wanted:
        continue
    cold = {n for n in wanted if lines[n] == 0}
    total += len(wanted)
    missed += len(cold)
    if cold:
        print(f"  {path}: {ranges(cold)}")
if total == 0:
    print("  исполняемых изменённых строк нет")
elif missed == 0:
    print(f"  исполняемых изменённых строк: {total}, все исполняются тестами")
else:
    print(f"  не исполнено тестами: {missed} из {total} исполняемых изменённых строк")
PY
}

mode="${1:-run}"
case "$mode" in
    run)
        run_tests
        summary
        ;;
    report)
        summary
        ;;
    html)
        run_tests
        mkdir -p "$BUILD/coverage"
        summary --html-details "$BUILD/coverage/index.html"
        rc=$?
        echo "HTML: $BUILD/coverage/index.html"
        exit $rc
        ;;
    changed)
        # Resolve the base first: a typo in it should not cost a full run.
        rev="${2:-HEAD}"
        base="$(merge_base "$rev")" || exit 2
        run_tests
        summary
        rc=$?
        changed_lines "$rev" "$base"
        exit $rc
        ;;
    *)
        echo "использование: coverage.sh [run|report|html|changed [REV]]" >&2
        exit 2
        ;;
esac
