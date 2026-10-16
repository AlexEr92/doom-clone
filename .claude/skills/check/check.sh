#!/usr/bin/env bash
# Every mechanical check on the working tree in one call, with a summary a
# few lines long. The full output of each step goes to .run/check/<step>.log.
#
#   check.sh        the uncommitted work against HEAD
#   check.sh REV    the work since the merge base with REV (e.g. master)
#
# Steps: a clean build (any warning outside vendor/ counts), ctest, the lint
# gate, driver.sh smoke when the diff reaches the game, and coverage of the
# changed lines. Exits 0 when everything holds, 1 when a check fails, 2 when
# the build or the revision does.

set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$root" || exit 2

rev="${1:-HEAD}"
base="$(git merge-base "$rev" HEAD 2>/dev/null)" || {
    echo "нет такой ревизии или общей истории с ней: $rev" >&2
    exit 2
}

LOG=.run/check
mkdir -p "$LOG"
failed=()

# printf pads by bytes, and a Cyrillic label is two bytes a letter.
say() {
    local pad=$((10 - ${#1}))
    [ "$pad" -gt 0 ] || pad=0
    printf '%s%*s %s\n' "$1" "$pad" '' "$2"
}
indent() { sed 's/^/  /'; }

echo "проверяю: рабочее дерево против $rev"

# --- build ---
# --clean-first costs about 2s and is what makes the warning count honest:
# an incremental build only prints warnings for the files it recompiles.
if ! { cmake -S . -B build && cmake --build build --clean-first -j; } >"$LOG/build.log" 2>&1; then
    say "сборка:" "ПРОВАЛ — $LOG/build.log"
    grep -E 'error|Error' "$LOG/build.log" | head -20 | indent
    exit 2
fi
warnings="$(grep -E 'warning:' "$LOG/build.log" | grep -v 'vendor/' | sed "s|^$root/||" | sort -u)"
if [ -n "$warnings" ]; then
    say "сборка:" "ПРЕДУПРЕЖДЕНИЯ: $(printf '%s\n' "$warnings" | wc -l)"
    printf '%s\n' "$warnings" | indent
    failed+=(сборка)
else
    say "сборка:" "ок, предупреждений вне vendor/ нет"
fi

# --- unit tests ---
tests_ok=0
total="$(ctest --test-dir build -N 2>/dev/null | sed -n 's/^Total Tests: //p')"
if [ -z "$total" ] || [ "$total" -eq 0 ]; then
    say "ctest:" "ПРОВАЛ — наборов нет (сборка с -DBUILD_TESTS=OFF?)"
    failed+=(ctest)
elif ctest --test-dir build --output-on-failure >"$LOG/ctest.log" 2>&1; then
    say "ctest:" "$total/$total"
    tests_ok=1
else
    say "ctest:" "ПРОВАЛ — $LOG/ctest.log"
    sed -n '/tests FAILED:/,$p' "$LOG/ctest.log" | tail -n +2 | grep -v '^Errors while' \
        | sed 's/^[[:space:]]*//' | indent
    failed+=(ctest)
fi

# --- lint gate ---
.claude/skills/lint/lint.sh all >"$LOG/lint.log" 2>&1
case $? in
    0) say "lint:" "чисто (cppcheck, симуляция без SDL)" ;;
    1)
        say "lint:" "НАХОДКИ — $LOG/lint.log"
        grep -vE ': чисто' "$LOG/lint.log" | head -15 | indent
        failed+=(lint)
        ;;
    *)
        say "lint:" "не запущен — $(head -1 "$LOG/lint.log")"
        failed+=(lint)
        ;;
esac

# --- smoke ---
changed="$({ git diff --name-only "$base"; git ls-files --others --exclude-standard; } | sort -u)"
if ! printf '%s\n' "$changed" | grep -qE '^(src/|assets/|CMakeLists\.txt$)'; then
    say "smoke:" "не нужен — изменения не доходят до src/, assets/, CMakeLists.txt"
elif ! command -v Xvfb >/dev/null 2>&1 || ! command -v xdotool >/dev/null 2>&1; then
    say "smoke:" "НЕ ПРОВЕРЕН — нет Xvfb или xdotool (см. run-doom-clone/SKILL.md)"
else
    driver=.claude/skills/run-doom-clone/driver.sh
    "$driver" smoke >"$LOG/smoke.log" 2>&1
    rc=$?
    "$driver" stop >/dev/null 2>&1
    passes="$(grep -c '\] PASS' "$LOG/smoke.log")"
    if [ "$rc" -eq 0 ]; then
        say "smoke:" "ок ($passes PASS)"
    else
        say "smoke:" "ПРОВАЛ — $LOG/smoke.log"
        grep -E '\] FAIL|ERROR' "$LOG/smoke.log" | indent
        failed+=(smoke)
    fi
fi

# --- coverage ---
if [ "$tests_ok" -eq 0 ]; then
    say "покрытие:" "не считалось — юнит-тесты не прошли"
else
    .claude/skills/coverage/coverage.sh changed "$rev" >"$LOG/coverage.log" 2>&1
    rc=$?
    lines="$(sed -n 's/^lines: \([0-9.]*%\).*/\1/p' "$LOG/coverage.log")"
    decisions="$(sed -n 's/^decisions: \([0-9.]*%\).*/\1/p' "$LOG/coverage.log")"
    case $rc in
        0) say "покрытие:" "строки $lines, decisions $decisions — пороги выдержаны" ;;
        1)
            say "покрытие:" "строки $lines, decisions $decisions — НИЖЕ ПОРОГА"
            failed+=(покрытие)
            ;;
        *)
            say "покрытие:" "не посчитано — $LOG/coverage.log"
            failed+=(покрытие)
            ;;
    esac
    sed -n '/^изменённые строки/,$p' "$LOG/coverage.log" | tail -n +2
fi

if [ "${#failed[@]}" -eq 0 ]; then
    echo "итог: ок"
    exit 0
fi
echo "итог: ПРОВАЛ — ${failed[*]}"
exit 1
