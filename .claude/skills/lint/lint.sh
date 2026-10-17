#!/usr/bin/env bash
# Static analysis of src/: cppcheck, plus the check that the simulation and
# the server-side files beside it compile without SDL and reach no SDL header.
#
#   lint.sh            whole project, gate level (must stay clean)
#   lint.sh strict     adds the style checks (advisory, not all worth taking)
#   lint.sh changed    only .c files that differ from HEAD or are new and
#                      untracked, gate level
#   lint.sh sim        the SDL check alone, no cppcheck (~0.2s)
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

# Not simulation, but linked into the dedicated server all the same, which has
# no client headers: these must not see SDL either. Kept out of SIM= because
# that list is where CLAUDE.md draws the simulation boundary. The check only
# reads the source, so a file built under ENABLE_MULTIPLAYER alone fits here.
NOSDL=(src/net_protocol.c)

check_sim() {
    local cc="${CC:-cc}" out sdl
    if ! out="$("$cc" -std=c11 -fsyntax-only -Isrc "${SIM[@]}" "${NOSDL[@]}" 2>&1)"; then
        printf '%s\n' "$out"
        echo "не компилируется без SDL: ${SIM[*]} ${NOSDL[*]}"
        return 1
    fi
    # A clean compile is not proof: <SDL2/SDL.h> resolves through /usr/include
    # with no -I at all. List every header each file reaches and refuse any on
    # an SDL path. An -M rule opens with "<target>:" and then the source.
    if ! sdl="$("$cc" -std=c11 -M -Isrc "${SIM[@]}" "${NOSDL[@]}" | awk '
        $1 ~ /:$/ { src = $2 }
        {
            for (i = 1; i <= NF; i++) {
                if ($i ~ /(^|\/)SDL/ && !(src in seen)) {
                    seen[src] = 1
                    print src ": " $i
                }
            }
        }')"; then
        echo "не удалось перечислить заголовки (cc -M): ${SIM[*]} ${NOSDL[*]}"
        return 1
    fi
    if [ -n "$sdl" ]; then
        printf '%s\n' "$sdl"
        echo "видят SDL: $(printf '%s\n' "$sdl" | cut -d: -f1 | paste -sd' ')"
        return 1
    fi
    echo "без SDL: чисто (симуляция и ${NOSDL[*]})"
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
        # A new file never added to the index is in neither diff.
        mapfile -t files < <(
            git diff --name-only HEAD -- 'src/*.c'
            git diff --cached --name-only -- 'src/*.c'
            git ls-files --others --exclude-standard -- 'src/*.c'
        )
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
