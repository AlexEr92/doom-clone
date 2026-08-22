#!/usr/bin/env bash
# Driver for doom-clone: builds, launches under Xvfb, injects input,
# captures screenshots. Headless-container friendly (no WM, no sound card).
#
# Usage: .claude/skills/run-doom-clone/driver.sh <command> [args]
# Run `driver.sh help` for the command list.
#
# Env overrides: DISPLAY_NUM (:99), SCREEN (1280x800x24), BUILD_DIR (build)

set -uo pipefail

SKILL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SKILL_DIR/../../.." && pwd)"     # unit root (project root)

DISPLAY_NUM="${DISPLAY_NUM:-:99}"
SCREEN="${SCREEN:-1280x800x24}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
RUN_DIR="$ROOT/.run"
SHOTS="$RUN_DIR/shots"
BIN="$BUILD_DIR/doom-clone"
GAME_LOG="$RUN_DIR/game.log"
XVFB_LOG="$RUN_DIR/xvfb.log"
XVFB_PID="$RUN_DIR/xvfb.pid"

# Fire/use are read as HELD state inside the fixed timestep (main.c:166), so a
# tap shorter than one 60Hz frame is swallowed. 150ms is reliably >= 1 frame.
HOLD_MS_DEFAULT=150

mkdir -p "$SHOTS"
export DISPLAY="$DISPLAY_NUM"

die()  { echo "ERROR: $*" >&2; exit 1; }
info() { echo "[driver] $*"; }

# Never use pgrep/pkill -f here: the pattern would match this script's own
# command line and kill the calling shell (exit 144). -x matches process name.
game_pid() { pgrep -x doom-clone 2>/dev/null | head -1; }

win_id() {
  local id
  id="$(xdotool search --name 'doom-clone' 2>/dev/null | head -1)"
  [ -n "$id" ] || return 1
  echo "$id"
}

need_window() {
  local w; w="$(win_id)" || die "game window not found — is it running? (driver.sh start)"
  echo "$w"
}

focus() { xdotool windowfocus "$(need_window)" 2>/dev/null; sleep 0.15; }

cmd_build() {
  info "configuring + building in $BUILD_DIR"
  cmake -S "$ROOT" -B "$BUILD_DIR" >/dev/null || die "cmake configure failed"
  cmake --build "$BUILD_DIR" -j"$(nproc)" 2>&1 | grep -Ev 'stb_image|warning:|^\s+\||^\s+\^|In file included' | tail -5
  [ -x "$BIN" ] || die "binary not produced at $BIN"
  info "built: $BIN"
}

cmd_start() {
  [ "${1:-}" = "--build" ] && cmd_build
  [ -x "$BIN" ] || die "no binary at $BIN — run: driver.sh build"

  if [ -z "$(game_pid)" ]; then
    if ! xdpyinfo >/dev/null 2>&1; then
      info "starting Xvfb on $DISPLAY_NUM ($SCREEN)"
      setsid nohup Xvfb "$DISPLAY_NUM" -screen 0 "$SCREEN" -nolisten tcp \
        >"$XVFB_LOG" 2>&1 </dev/null &
      disown
      echo $! >"$XVFB_PID"
      for _ in $(seq 20); do xdpyinfo >/dev/null 2>&1 && break; sleep 0.2; done
      xdpyinfo >/dev/null 2>&1 || { cat "$XVFB_LOG" >&2; die "Xvfb failed to start"; }
    fi
    # cwd MUST be the project root: the map is opened via the relative path
    # assets/maps/level1.txt. A wrong cwd does NOT crash the game — it logs
    # "Cannot open map" and runs with an empty world.
    info "launching game (cwd=$ROOT)"
    cd "$ROOT" || die "cannot cd to $ROOT"
    setsid nohup env DISPLAY="$DISPLAY_NUM" SDL_AUDIODRIVER=dummy "$BIN" \
      >"$GAME_LOG" 2>&1 </dev/null &
    disown
  else
    info "game already running (pid $(game_pid))"
  fi

  for _ in $(seq 30); do win_id >/dev/null 2>&1 && break; sleep 0.2; done
  win_id >/dev/null 2>&1 || { cat "$GAME_LOG" >&2; die "window never appeared"; }
  if grep -q 'Cannot open map' "$GAME_LOG" 2>/dev/null; then
    die "map failed to load — game is running with an empty world (wrong cwd?)"
  fi
  # The title starts as plain "doom-clone" and only gains the telemetry suffix
  # after the first FPS window closes (main.c:121, fps_timer >= 0.5).
  for _ in $(seq 30); do
    xdotool getwindowname "$(win_id)" 2>/dev/null | grep -q 'FPS' && break
    sleep 0.2
  done
  info "ready: window $(win_id), pid $(game_pid)"
}

# Window title is the app's own telemetry channel (main.c:123):
#   "doom-clone | 63 FPS | HP 100 | ammo 50/50"
cmd_status() {
  local t; t="$(xdotool getwindowname "$(need_window)")"
  echo "$t"
}
# Note the FPS number precedes its label, the others follow theirs.
get_fps()  { cmd_status | sed -n 's/.*| \([0-9]*\) FPS.*/\1/p'; }
get_hp()   { cmd_status | sed -n 's/.*HP \([0-9]*\).*/\1/p'; }
get_ammo() { cmd_status | sed -n 's/.*ammo \([0-9]*\).*/\1/p'; }

cmd_key() {
  focus
  local ms="$HOLD_MS_DEFAULT"
  for k in "$@"; do
    xdotool keydown "$k"; sleep "$(awk "BEGIN{print $ms/1000}")"; xdotool keyup "$k"
    sleep 0.1
  done
}

cmd_hold() {
  local k="${1:?key}" ms="${2:-500}"
  focus
  xdotool keydown "$k"; sleep "$(awk "BEGIN{print $ms/1000}")"; xdotool keyup "$k"
}

# Turning. Use the KEYBOARD (a/d, ROT_SPEED 3.0 rad/s), never the mouse.
# engine.c:50 calls SDL_SetRelativeMouseMode(SDL_TRUE); SDL then reads XInput2
# raw motion, which xdotool's synthetic events do not reliably reach. Measured
# under Xvfb: a 300px `mousemove` produced a 0-pixel frame change, and a
# 600px round trip produced 564px — i.e. mouse look is effectively dead here.
#
# The hold is wall-clock, so the number of 60Hz ticks that observe the key
# varies between runs: the same `turn right 600` was measured changing
# anywhere from 232k to 544k pixels. Never assert an exact angle, and never
# compare frames taken after timed movement.
cmd_turn() {
  local dir="${1:?left|right}" ms="${2:-500}" k
  case "$dir" in
    left)  k=a ;;
    right) k=d ;;
    *) die "turn: expected left|right, got '$dir'" ;;
  esac
  cmd_hold "$k" "$ms"
  sleep 0.2
}

cmd_shot() {
  local name="${1:?name}"
  need_window >/dev/null
  import -window root "$SHOTS/$name.png" 2>/dev/null || die "screenshot failed"
  echo "$SHOTS/$name.png"
}

# Pixel-exact frame comparison. AE=0 means identical — used to prove a
# refactor did not change rendering (see tasks/05-03).
cmd_diff() {
  local a="$SHOTS/${1:?a}.png" b="$SHOTS/${2:?b}.png"
  [ -f "$a" ] && [ -f "$b" ] || die "missing $a or $b"
  local ae; ae="$(compare -metric AE "$a" "$b" null: 2>&1)"
  echo "$ae"
}

cmd_log()  { cat "$GAME_LOG" 2>/dev/null || echo "(no log)"; }

cmd_stop() {
  pkill -x doom-clone 2>/dev/null && info "game stopped"
  if [ -f "$XVFB_PID" ]; then
    kill "$(cat "$XVFB_PID")" 2>/dev/null && info "Xvfb stopped"
    rm -f "$XVFB_PID"
  fi
  return 0
}

cmd_smoke() {
  local fail=0
  cmd_stop >/dev/null 2>&1; sleep 0.5
  cmd_start --build || return 1

  local fps; fps="$(get_fps)"
  if [ -n "$fps" ] && [ "$fps" -gt 0 ] 2>/dev/null; then
    info "PASS render loop alive (${fps} FPS)"
  else
    info "FAIL no FPS in title: $(cmd_status)"; fail=1
  fi

  cmd_shot menu >/dev/null
  cmd_key Return                      # menu -> PLAYING
  sleep 0.5
  cmd_shot game >/dev/null
  local d; d="$(cmd_diff menu game)"
  if [ "$d" != "0" ]; then
    info "PASS menu -> gameplay ($d px changed)"
  else
    info "FAIL menu did not transition"; fail=1
  fi

  cmd_hold w 1000
  cmd_shot moved >/dev/null
  d="$(cmd_diff game moved)"
  if [ "$d" != "0" ]; then
    info "PASS movement ($d px changed)"
  else
    info "FAIL movement had no effect"; fail=1
  fi

  cmd_turn right 600
  cmd_shot turned >/dev/null
  d="$(cmd_diff moved turned)"
  if [ "$d" != "0" ]; then
    info "PASS turn right ($d px changed)"
  else
    info "FAIL turning had no effect"; fail=1
  fi

  # Only that the opposite key also moves the view. Turning back by the same
  # duration does NOT restore the frame: hold times are wall-clock, so the two
  # turns see different numbers of ticks.
  cmd_turn left 600
  cmd_shot turned_back >/dev/null
  d="$(cmd_diff turned turned_back)"
  if [ "$d" != "0" ]; then
    info "PASS turn left ($d px changed)"
  else
    info "FAIL turning left had no effect"; fail=1
  fi

  local before after
  before="$(get_ammo)"
  cmd_key space
  sleep 0.7
  after="$(get_ammo)"
  if [ -n "$before" ] && [ -n "$after" ] && [ "$after" -lt "$before" ]; then
    info "PASS weapon fired (ammo $before -> $after)"
  else
    info "FAIL ammo unchanged ($before -> $after)"; fail=1
  fi

  cmd_shot fired >/dev/null
  cmd_stop >/dev/null
  if [ "$fail" = 0 ]; then
    info "SMOKE OK — screenshots in $SHOTS"
  else
    info "SMOKE FAILED"
  fi
  return "$fail"
}

cmd_help() {
  cat <<'EOF'
doom-clone driver

  build              cmake configure + build
  start [--build]    start Xvfb (if needed) + launch game from project root
  status             window title: "doom-clone | 63 FPS | HP 100 | ammo 50/50"
  key <k>...         tap key(s), held 150ms each (Return, space, e, 1, 2, ...)
  hold <k> [ms]      hold one key (movement: w s, strafe: comma period)
  turn left|right [ms]  turn via keyboard (mouse look does NOT work headless)
  shot <name>        screenshot -> .run/shots/<name>.png
  diff <a> <b>       pixel difference between two shots (AE; 0 = identical)
  log                game stdout/stderr
  stop               kill game + Xvfb
  smoke              full scenario with assertions (build -> fire -> stop)
EOF
}

case "${1:-help}" in
  build) shift; cmd_build "$@" ;;
  start) shift; cmd_start "$@" ;;
  status) shift; cmd_status "$@" ;;
  key) shift; cmd_key "$@" ;;
  hold) shift; cmd_hold "$@" ;;
  turn) shift; cmd_turn "$@" ;;
  shot) shift; cmd_shot "$@" ;;
  diff) shift; cmd_diff "$@" ;;
  log) shift; cmd_log "$@" ;;
  stop) shift; cmd_stop "$@" ;;
  smoke) shift; cmd_smoke "$@" ;;
  help|-h|--help) cmd_help ;;
  *) echo "unknown command: $1" >&2; cmd_help; exit 1 ;;
esac
