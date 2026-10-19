---
name: run-doom-clone
description: Build, run, and drive doom-clone. Use when asked to start doom-clone, build it, take a screenshot of the game, compare rendered frames, or interact with the running app.
---

doom-clone is a C11/SDL2 raycaster — a desktop GUI app. Its unit tests
(`ctest`) cover the simulation and nothing that reaches the screen, so this
driver is how a change is seen working. Drive it with `.claude/skills/run-doom-clone/driver.sh`, which runs it under
Xvfb, injects keyboard input via `xdotool`, and captures frames with
`import`. Start with `driver.sh smoke`.

All paths below are relative to the project root.

## Prerequisites

```bash
sudo apt-get install -y xvfb xdotool imagemagick
```

Already required by the project itself (build deps):

```bash
sudo apt-get install -y cmake build-essential libsdl2-dev libsdl2-mixer-dev
```

No GPU and no sound card needed — rendering is a software raycaster, and the
driver sets `SDL_AUDIODRIVER=dummy`.

## Build

```bash
.claude/skills/run-doom-clone/driver.sh build
```

Equivalent to `cmake -S . -B build && cmake --build build`. Expect two
`-Wunused-function` warnings from `vendor/stb_image.h`; project sources build
clean under `-Wall -Wextra`.

## Run (agent path)

One command proves the whole stack — build, launch, menu, movement, turning,
firing, teardown:

```bash
.claude/skills/run-doom-clone/driver.sh smoke
```

Verified output:

```
[driver] PASS render loop alive (63 FPS)
[driver] PASS menu -> gameplay (1.024e+06 px changed)
[driver] PASS movement (854444 px changed)
[driver] PASS turn right (884812 px changed)
[driver] PASS turn left (884812 px changed)
[driver] PASS weapon fired (ammo 50 -> 49)
[driver] SMOKE OK
```

For step-by-step work:

```bash
D=.claude/skills/run-doom-clone/driver.sh
$D start --build          # Xvfb :99 + game, waits until telemetry appears
$D key Return             # menu -> PLAYING
$D hold w 1000            # walk forward 1s
$D turn right 600         # turn via keyboard
$D shot before            # -> .run/shots/before.png
$D key space              # fire (held 150ms)
sleep 0.7                 # title refreshes only every 0.5s — see Gotchas
$D status                 # doom-clone | 63 FPS | HP 100 | ammo 49/50
$D shot after
$D diff before after      # 0 == pixel-identical
$D stop
```

| command | what it does |
|---|---|
| `build` | cmake configure + build |
| `start [--build]` | start Xvfb (if needed), launch game from project root |
| `status` | window title — the app's own telemetry: FPS, HP, ammo |
| `key <k>...` | tap key(s), 150ms hold each (`Return`, `space`, `e`, `1`, `2`) |
| `hold <k> [ms]` | hold one key (`w`, `s`, `comma`, `period`) |
| `turn left\|right [ms]` | turn via keyboard — mouse look does not work headless |
| `shot <name>` | screenshot → `.run/shots/<name>.png` |
| `diff <a> <b>` | pixel difference between two shots (`0` = identical) |
| `log` | game stdout/stderr |
| `stop` | kill game + Xvfb |
| `smoke` | full scenario with assertions |

Artifacts: screenshots in `.run/shots/`, logs in `.run/game.log` and
`.run/xvfb.log`. `.run/` is gitignored.

`diff` is the tool for render refactors — a refactor that must leave the
image pixel-identical is answered directly by `compare -metric AE`. Compare frames captured at a **deterministic point**: right after
`start` (menu) or right after `key Return` (spawn position, before any
movement). Those reproduce across independent runs and across builds; a
frame taken after `hold`/`turn` does not.

The status bar in states the game reaches only with effort — each step of
health, the grimace after a hit, death, every weapon in hand — is drawn
without a window by `build/tests/hud_shot [out.png] [--no-files]`, from
the project root: one band per state, at the window's 2x, the band names on
stdout. `--no-files` shows it as a checkout without `assets/hud/` does.

## Run (human path)

```bash
cmake --build build --target run   # opens a 1280x800 window; Ctrl-C to stop
```

Useless headless — it needs a real display. Must run from the project root.

## Network game

`driver.sh` drives one single-player window. A network game needs a server
and clients: the server is the game itself, and the bots live in
`tests/manual/` (built into `build/tests/` with `ENABLE_MULTIPLAYER`, not
run by CTest). Run every one of them from the project root — the server
opens `assets/maps/level1.txt`, the client the map `MSG_ACCEPT` names under
`assets/maps/`.

```bash
env -u DISPLAY build/doom-clone --host --dedicated & srv=$!   # the server: no window, logs on stdout
build/doom-clone --connect 127.0.0.1 --name p1 # a client with a window
build/doom-clone --host                        # instead of the above: a server with a player at a window
build/tests/net_bot look [--rate 3]            # the world as the server has it; --rate: ticks, snapshots, KB/s
build/tests/net_bot run go 3.5 12.5 kill 1 kill 2 go 10.5 12.5 face 10.5 11 use
build/tests/net_bot swarm 9 60                 # nine players walking and firing for 60 s
build/tests/net_bot watch 0 10                 # how player 0 moves on a client's screen, frame by frame
```

`--port N` moves the server and everyone joining it off 27015 (`net_bot
--port N …`); `--snaprate N`, 15–60, sets the server's snapshots a second.
The dedicated server prints `serving …` on start, `player N joined` /
`player N left` as players come and go, once a second
`N ticks, N players, tick mean … ms worst … ms, late worst … ms` (P4), and
`stopped` on its way out. `doom-clone --help` lists the flags.

`run` steps: `go X Y`, `face X Y`, `kill N`, `use`, `fire`, `wait S`; each
prints `done …`, a step that times out (20 s) or that the bot dies in ends
the run with status 1 and names it, and the world is printed at the end.
`look` prints one line an entity (`player`, `enemy`, `door`, `item`, and
`me` — the bot's own id), so `grep`/`awk` assert on it. `swarm` prints a
line a second: how many are in, what one player receives and sends.
`watch ID [SECONDS]` stands still at 60 frames a second and prints two
lines on player ID, `view` (what the game draws, interpolated) and `world`
(the snapshots as they come): frames moved, average step, jerks — frames
whose step differs from the last by over half the average — and the most a
frame turned them. Smooth motion is `view` moving in nearly every frame
with jerks only where the player starts or stops; give it a moving target
with `run go …` from another bot, and clear the enemies first or they kill
it.

Two clients with windows go on **two Xvfb displays**, not on `:99`: input
goes to the focused window and there is one focus a display, and
`driver.sh` finds its game by process name, which two clients share. `:99`
stays for `smoke`; for each client:

```bash
Xvfb :100 -screen 0 1280x800x24 -nolisten tcp &
DISPLAY=:100 SDL_AUDIODRIVER=dummy build/doom-clone --connect 127.0.0.1 &
W=$(DISPLAY=:100 xdotool search --name doom-clone | head -1)
DISPLAY=:100 xdotool windowfocus $W; DISPLAY=:100 xdotool keydown Return; sleep 0.15; DISPLAY=:100 xdotool keyup Return
DISPLAY=:100 import -window root shot.png
```

- **Read the state with `net_bot look`, not from screenshots.** A frame
  says "something is drawn there"; `look` says where everyone is, what
  died, which door is open.
- **The network game never pauses.** The world goes on between two tool
  calls, so enemies kill players while one reads screenshots. Do a
  sequence of actions in one call, or clear the enemies first with
  `net_bot run … kill N …`.
- **A bot is a real player.** It takes a slot — `look` included — and
  enemies go for it; there is no spectator. Count it in when asserting the
  number of players (`me` names it).
- **Stop the server by its pid, not by name**: `kill -INT $srv` lets the
  clients go at once — each prints `Disconnected from 127.0.0.1: the server
  closed the game` — and `kill -9 $srv` leaves them to notice by the 5 s
  timeout. The server is a `doom-clone` like every client, so `pkill -x
  doom-clone` takes them all.
- **`driver.sh` takes a running server for its game.** It finds the game
  by process name: with a server up, `driver.sh start` reports the game
  already running and launches nothing, and `driver.sh stop` kills the
  server. Stop the server before `driver.sh` or `check.sh`.

## Test

Unit tests run under CTest — `ctest --test-dir build --output-on-failure` —
and include `tests/replay_test.c`, the determinism check on `world_step()`
that `docs/requirements.md` §9 mandates. None of them render, so
`driver.sh smoke` stays the only check on anything that reaches the screen.

## Gotchas

- **`pkill -f` / `pgrep -f` kills the calling shell.** The pattern matches the
  script's own command line, so `pkill -f "Xvfb :99"` suicides with exit 144
  and no output → use `pgrep -x doom-clone` / `pkill -x doom-clone`, which
  match the process name only.
- **The game must run from the project root.** The map is opened as
  `assets/maps/level1.txt`. From any other cwd it logs `Cannot open map` and
  **keeps running with an empty world** — it does not crash, so this looks
  like a rendering bug. `driver.sh start` cds first and fails loudly if the
  message appears.
- **Mouse look does not work headless.** `engine_init()` (`engine.c`) calls
  `SDL_SetRelativeMouseMode(SDL_TRUE)`; SDL then consumes XInput2 raw motion,
  which xdotool's synthetic events do not reliably reach. Measured under Xvfb:
  a 300px `mousemove` changed 0 pixels, a 600px round trip changed 564. Turn
  with `a`/`d` instead (`driver.sh turn`).
- **Timed input is not reproducible.** `hold`/`turn` take a wall-clock
  duration, so the number of 60Hz ticks that observe the key varies between
  runs — the same `turn right 600` was measured changing between 232k and
  544k pixels. Never assert an exact angle or position, and never compare
  frames captured after timed movement, not even against the same build.
- **A movement or turn key let go within one frame is swallowed.** `w`, `s`,
  `a`, `d`, `comma` and `period` are read as *held* state, so a keydown+keyup
  landing in one `SDL_PollEvent` sweep leaves nothing held by the time a tick
  (or `input_turn()`) looks: five instant `xdotool key w` at 22 FPS moved the
  player 0. Fire, `e` and `1`–`5` are latched instead and reach exactly one
  tick (`input_sample()`, `input.c`) — five instant `xdotool key space` spent
  5 ammo at both 22 and 62 FPS. Hold ≥ 1 frame for the first kind; the
  driver uses 150ms for everything.
- **Keys need explicit focus.** There is no window manager, so nothing focuses
  the window — call `xdotool windowfocus <wid>` before sending keys.
- **The window title is empty telemetry for the first ~0.5s.** It is plain
  `doom-clone` until the first FPS window closes (`fps_timer` in the main
  loop, `main.c`). Poll for `FPS` rather than sleeping.
- **`status` can be up to 0.5s stale.** The title is rebuilt on the same
  `fps_timer`, not when HP/ammo change, so reading it right after an
  action is a race — the same `key space` reported both `50/50` and `49/50` on
  consecutive runs. Sleep ≥ 0.5s before asserting on HP or ammo.
- **Title fields parse differently.** The FPS number *precedes* its label
  (`| 63 FPS`), while HP and ammo *follow* theirs (`HP 100`, `ammo 49/50`).
- **A colour check needs a coloured sample.** A regression in the colour
  packing (`CLAUDE.md` → "Colour packing") swaps red and blue across the
  whole game and is invisible in grey, which is symmetric. Sample the menu
  title instead: it is `make_color(220, 40, 40)` in `game.c` and must come
  out as `srgb(220,40,40)` in a `shot` taken right after `start`, not as
  `srgb(40,40,220)`.

## Troubleshooting

- **Shell dies with exit 144 and no output**: a `pgrep -f`/`pkill -f` pattern
  matched the shell itself. Use `-x`.
- **`window never appeared`**: check `.run/game.log`. If it is empty, Xvfb
  probably is not up — `DISPLAY=:99 xdpyinfo` should print dimensions.
- **`map failed to load — game is running with an empty world`**: launched
  from the wrong cwd. Run the driver from the project root.
- **`diff` returns `1.024e+06` when you expected 0**: that is the full frame
  (1280×800) changing — you are comparing the menu against gameplay.
- **Game already running from a previous session**: `driver.sh start` reuses
  it and reports `game already running (pid N)`. Use `driver.sh stop` first
  for a clean slate.
