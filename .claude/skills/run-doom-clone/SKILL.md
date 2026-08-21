---
name: run-doom-clone
description: Build, run, and drive doom-clone. Use when asked to start doom-clone, build it, take a screenshot of the game, compare rendered frames, or interact with the running app.
---

doom-clone is a C11/SDL2 raycaster — a desktop GUI app with no test suite.
Drive it with `.claude/skills/run-doom-clone/driver.sh`, which runs it under
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
[driver] PASS rotation is reversible (pixel-identical)
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

`diff` is the tool for render refactors — task `docs/tasks/05-03` requires
the image to stay pixel-identical, and `compare -metric AE` answers that
directly.

## Run (human path)

```bash
cmake --build build --target run   # opens a 1280x800 window; Ctrl-C to stop
```

Useless headless — it needs a real display. Must run from the project root.

## Test

There is no test suite. `docs/requirements.md` §9 mandates
`tests/replay_test.c` (determinism of `world_step()`), introduced by task
`docs/tasks/06-01`; it does not exist yet. Until then `driver.sh smoke` is
the regression check.

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
- **Mouse look does not work headless.** `engine.c:50` calls
  `SDL_SetRelativeMouseMode(SDL_TRUE)`; SDL then consumes XInput2 raw motion,
  which xdotool's synthetic events do not reliably reach. Measured under Xvfb:
  a 300px `mousemove` changed 0 pixels, a 600px round trip changed 564. Turn
  with `a`/`d` instead (`driver.sh turn`) — exact, and reversible to a
  pixel-identical frame.
- **A quick key tap does not fire.** `main.c:166` reads the *held*
  `input.fire`, not the edge `input.fire_pressed`, so keydown+keyup landing in
  one `SDL_PollEvent` sweep is swallowed before the fixed step runs. Hold
  ≥ 1 frame; the driver uses 150ms.
- **Keys need explicit focus.** There is no window manager, so nothing focuses
  the window — call `xdotool windowfocus <wid>` before sending keys.
- **The window title is empty telemetry for the first ~0.5s.** It is plain
  `doom-clone` until the first FPS window closes (`main.c:121`). Poll for
  `FPS` rather than sleeping.
- **`status` can be up to 0.5s stale.** The title is rebuilt on the same FPS
  timer (`main.c:121`), not when HP/ammo change, so reading it right after an
  action is a race — the same `key space` reported both `50/50` and `49/50` on
  consecutive runs. Sleep ≥ 0.5s before asserting on HP or ammo.
- **Title fields parse differently.** The FPS number *precedes* its label
  (`| 63 FPS`), while HP and ammo *follow* theirs (`HP 100`, `ammo 49/50`).
- **Screenshots have red and blue swapped.** `make_color()` (`utils.h:38`)
  packs `0xAABBGGRR`, but the texture is created as `SDL_PIXELFORMAT_ARGB8888`
  (`engine.c`), which reads `0xAARRGGBB`. Verified: the menu title coded as
  `make_color(220,40,40)` (red) samples as `srgb(40,40,220)` (blue); grey text
  is unaffected because it is symmetric. This is a real bug in the app, not a
  capture artifact — do not "fix" screenshots to compensate.

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
