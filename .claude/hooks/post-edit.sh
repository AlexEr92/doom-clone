#!/usr/bin/env bash
# PostToolUse hook for Edit/Write: keeps what Claude just wrote in the shape
# CLAUDE.md asks for, so neither step depends on remembering it.
#
#   - a .c/.h file under src/, tests/ or tools/ is run through clang-format;
#   - an edit under src/ re-runs `lint.sh sim`, and a simulation that no
#     longer compiles without SDL is reported back to Claude (exit 2).
#
# Formatting is applied, not suggested: unlike .githooks/pre-commit, which
# never rewrites a human's staged work, this only ever touches the file the
# edit tool has just written.
set -uo pipefail

root="${CLAUDE_PROJECT_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
path="$(jq -r '.tool_input.file_path // empty')"
[ -n "$path" ] || exit 0
rel="${path#"$root"/}"

case "$rel" in
    src/*.c | src/*.h | tests/*.c | tests/*.h | tools/*.c | tools/*.h) ;;
    *) exit 0 ;;
esac
[ -f "$root/$rel" ] || exit 0

if command -v clang-format >/dev/null 2>&1; then
    # Only rewrite when something changes, so an already clean edit does not
    # touch the file's mtime under the editor.
    if ! clang-format --dry-run -Werror "$root/$rel" >/dev/null 2>&1; then
        clang-format -i "$root/$rel"
    fi
fi

case "$rel" in
    src/*)
        if ! out="$("$root/.claude/skills/lint/lint.sh" sim 2>&1)"; then
            {
                printf '%s\n' "$out"
                echo "После правки $rel симуляция не компилируется без SDL"
                echo "(CLAUDE.md, «Simulation and rendering are kept apart»)."
            } >&2
            exit 2
        fi
        ;;
esac
exit 0
