#!/usr/bin/env bash
# Static analysis of src/: cppcheck, plus the check that the simulation
# compiles with no SDL headers on the include path.
#
#   lint.sh            whole project, gate level (must stay clean)
#   lint.sh strict     adds the style checks (advisory, not all worth taking)
#   lint.sh changed    only files that differ from HEAD, gate level
#   lint.sh sim        the SDL check alone, no cppcheck (~0.1s)
#
# Exits 0 when nothing is reported at the chosen level.

set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$root" || exit 2

# The simulation must build on a server with no framebuffer, so these files
# compile with src/ alone on the include path (CLAUDE.md, "Simulation and
# rendering are kept apart"). This is the one place that lists them: a new
# simulation module goes here.
SIM=(src/world.c src/enemy.c src/weapon.c src/item.c src/door.c
     src/player.c src/map.c src/raycast_world.c)

check_sim() {
    local out
    if ! out="$("${CC:-cc}" -std=c11 -fsyntax-only -Isrc "${SIM[@]}" 2>&1)"; then
        printf '%s\n' "$out"
        echo "симуляция не компилируется без SDL: ${SIM[*]}"
        return 1
    fi
    echo "симуляция без SDL: чисто"
}

mode="${1:-all}"
if [ "$mode" = sim ]; then
    check_sim
    exit $?
fi

command -v cppcheck >/dev/null 2>&1 || {
    echo "cppcheck не найден:  sudo apt-get install -y cppcheck" >&2
    exit 2
}

# --max-configs=1 is what makes this usable: vendor/stb_image.h carries enough
# #ifdef combinations to take the run from 4s to 30s, and none of them are the
# configuration this project actually compiles.
COMMON=(
    --quiet
    --std=c11
    --max-configs=1
    --inline-suppr
    --suppress='*:vendor/*'
    --suppress=toomanyconfigs
    --suppress=missingInclude
    --suppress=checkersReport
    -I src
)

# Gate level: everything here is a defect, and src/ reports none today.
# `style` is deliberately not in it — see SKILL.md for why.
GATE=(--enable=warning,performance,portability)
STRICT=(--enable=warning,performance,portability,style)

case "$mode" in
    all)
        set -- "${COMMON[@]}" "${GATE[@]}" src/*.c
        ;;
    strict)
        set -- "${COMMON[@]}" "${STRICT[@]}" src/*.c
        ;;
    changed)
        mapfile -t files < <(git diff --name-only HEAD -- 'src/*.c'; git diff --cached --name-only -- 'src/*.c')
        mapfile -t files < <(printf '%s\n' "${files[@]}" | sort -u | grep -v '^$')
        if [ "${#files[@]}" -eq 0 ]; then
            echo "нет изменённых .c в src/"
            check_sim
            exit $?
        fi
        echo "проверяю: ${files[*]}"
        set -- "${COMMON[@]}" "${GATE[@]}" "${files[@]}"
        ;;
    *)
        echo "использование: lint.sh [all|strict|changed|sim]" >&2
        exit 2
        ;;
esac

out="$(cppcheck "$@" 2>&1)"
if [ -n "$out" ]; then
    printf '%s\n' "$out"
    [ "$mode" = strict ] && exit 0   # style findings are advisory
    rc=1
else
    echo "cppcheck: чисто ($mode)"
    rc=0
fi
[ "$mode" = strict ] && exit 0
check_sim || rc=1
exit $rc
