#!/usr/bin/env bash
# Validate a commit message against this project's rules (see CLAUDE.md).
#
#   check-message.sh <file>        validate a message file
#   git log --format=%B -1 | check-message.sh    validate from stdin
#   check-message.sh --history 10  validate the last N commits
#
# Exits 0 when the message conforms, 1 with one line per problem otherwise.

set -uo pipefail

TYPES="feat fix docs refactor perf test build chore"
MAX_SUBJECT=50
MAX_BODY=72

problems=0
say() { echo "  $*"; problems=$((problems + 1)); }

validate() {
    local msg="$1" label="${2:-message}"
    local n=0 subject="" body_started=0
    problems=0

    # Trailing comment lines (git's own template) are not part of the message.
    msg="$(printf '%s\n' "$msg" | grep -v '^#')"

    subject="$(printf '%s\n' "$msg" | head -1)"

    if [ -z "${subject// /}" ]; then
        say "пустой заголовок"
        echo "$label: НЕ ПРОШЁЛ"
        return 1
    fi

    # --- subject ---
    local len=${#subject}
    if [ "$len" -gt "$MAX_SUBJECT" ]; then
        say "заголовок $len символов, лимит $MAX_SUBJECT: $subject"
    fi

    local type="${subject%%:*}"
    if [ "$type" = "$subject" ]; then
        say "нет префикса типа ('type: ...'): $subject"
    elif ! printf '%s\n' $TYPES | grep -qx -- "$type"; then
        say "неизвестный тип '$type', допустимы: $TYPES"
    elif [ "${subject:${#type}:2}" != ": " ]; then
        say "после типа нужен ': ' (двоеточие и один пробел)"
    fi

    case "$subject" in
        *.) say "заголовок заканчивается точкой" ;;
    esac

    # --- structure and body ---
    while IFS= read -r line; do
        n=$((n + 1))
        [ "$n" -eq 1 ] && continue

        if [ "$n" -eq 2 ] && [ -n "${line// /}" ]; then
            say "между заголовком и телом нужна пустая строка"
        fi
        [ -n "${line// /}" ] && body_started=1

        if [ "${#line}" -gt "$MAX_BODY" ]; then
            # A line holding one unbreakable token (a URL, a long path) stays
            # over the limit however it is rewrapped — do not flag it.
            local longest=0 tok
            for tok in $line; do
                [ "${#tok}" -gt "$longest" ] && longest=${#tok}
            done
            if [ "$longest" -le "$MAX_BODY" ]; then
                say "строка $n длиннее $MAX_BODY ( ${#line} ): ${line:0:60}..."
            fi
        fi

        case "$line" in
            *Co-Authored-By:*|*Co-authored-by:*)
                say "строка $n: трейлер Co-Authored-By запрещён" ;;
        esac
    done < <(printf '%s\n' "$msg")

    # --- commit hashes ---
    # Lowercase hex runs of 7+ that are not 0x-prefixed and contain a letter.
    local hashes
    hashes="$(printf '%s\n' "$msg" \
        | grep -oE '(^|[^0-9a-fx])[0-9a-f]{7,40}([^0-9a-z]|$)' \
        | grep -oE '[0-9a-f]{7,40}' \
        | grep -E '[a-f]' || true)"
    if [ -n "$hashes" ]; then
        while IFS= read -r h; do
            [ -n "$h" ] && say "похоже на хеш коммита: $h — назови изменение и место"
        done <<< "$hashes"
    fi

    (( body_started )) || say "нет тела: одного заголовка недостаточно"

    if [ "$problems" -eq 0 ]; then
        echo "$label: ок (заголовок $len)"
        return 0
    fi
    echo "$label: НЕ ПРОШЁЛ ($problems)"
    return 1
}

if [ "${1:-}" = "--history" ]; then
    count="${2:-10}"
    rc=0
    for i in $(seq 0 $((count - 1))); do
        validate "$(git log --format=%B --skip="$i" -1)" "$(git log --format='%h %s' --skip="$i" -1 | cut -c1-46)" || rc=1
    done
    exit "$rc"
fi

if [ $# -ge 1 ]; then
    [ -f "$1" ] || { echo "нет файла: $1" >&2; exit 2; }
    validate "$(cat "$1")" "$(basename "$1")"
else
    validate "$(cat)" "stdin"
fi
