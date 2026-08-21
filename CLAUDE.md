# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

A Doom-style raycaster demo in C11 on SDL2 — roughly 2400 lines across
`src/`. One map, two weapons, two enemy types, doors, pickups, procedural
audio. It is currently being refactored into a networked deathmatch; see
[Ongoing work](#ongoing-work).

## Commands

```bash
cmake -S . -B build                    # configure (once)
cmake --build build                    # build
cmake --build build --target run       # build + run from the project root
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
```

The game **must** run with the project root as the working directory —
`assets/maps/level1.txt` is opened by relative path. The `run` target does
this; a bare `./build/doom-clone` from elsewhere exits with an error.

New `.c` files must be added to the source list in `CMakeLists.txt`; there
is no glob.

### Running headless / driving the game

There is no test suite (see [Ongoing work](#ongoing-work)). To verify a
change actually works, use the run skill — it builds, launches under Xvfb,
injects input and captures frames:

```bash
.claude/skills/run-doom-clone/driver.sh smoke
```

Read `.claude/skills/run-doom-clone/SKILL.md` before driving the game
manually. It documents the non-obvious traps — mouse look does not work
headless, a key tap shorter than one frame is swallowed, `pgrep -f`
patterns match the calling shell.

## Architecture

### Simulation and rendering are entangled

This is the single most important thing to know, and it is invisible from
any one file:

- **`Player` mixes state with camera.** `player.h` holds `dir_x/dir_y` *and*
  `plane_x/plane_y` — the latter is a rendering artifact (FOV), not gameplay.
- **Hitscan runs in screen space.** `weapon_try_fire()` (`weapon.c`) finds
  hits by projecting enemies to screen columns via `enemy_screen_band()`
  and testing the global `zBuffer[SCREEN_W]`, which is filled by
  `raycast_render()`. Shooting therefore depends on a frame having been
  rendered, and on `SCREEN_W`/`SCREEN_H`.
- **`SpriteList` is not just a render list.** Item positions live *only* in
  their sprite (`item.c` reads `sp->x`/`sp->y`; `Item.x/y` are never
  filled), and enemy positions are mirrored into sprites every tick
  (`enemy.c`). `enemy_damage()` mutates sprite type to switch to a corpse.

Do not deepen this coupling. The multiplayer work exists largely to undo
it, since none of the above can run on a server with no framebuffer.

### Entity model

All entity lists are fixed-size arrays with a `count`, and nothing is ever
removed: items deactivate (`active = 0`), enemies become corpses
(`ESTATE_DEAD`). An index into the array is therefore a stable id, and
`Enemy.sprite_id` / `Item.sprite_id` are indices into `SpriteList`. Limits
live in the individual headers (`MAX_ENEMIES`, `MAX_SPRITES`, …).

### Main loop

`main.c` runs a fixed-timestep loop: `FIXED_DT` (1/60) with an accumulator,
rendering once per frame. Note that `input_end_frame()` is called *inside*
the fixed step, and `mouse_dx` accumulates per frame — so input is coupled
to frames, not to ticks, and a frame containing two ticks feeds the second
one empty input.

Game states (`GSTATE_MENU`, `PLAYING`, `PAUSED`, `DEAD`, `WIN`) live in
`game.c`, which also owns the 3×5 bitmap font used by every overlay.

### Colour packing — easy to break silently

`make_color()` (`utils.h`) packs `0xAABBGGRR`, with **red in the low byte**.
The screen texture is created as `SDL_PIXELFORMAT_ABGR8888` to match, and
`shade_color()`, `overlay_dim()` (`game.c`) and the stb_image conversion in
`assets.c` all decompose the same way. Changing any one of these in
isolation swaps red and blue across the entire game — this was a real bug
(fixed in `851dc22`), and it is invisible in greys because they are
symmetric.

### Assets are generated, not loaded

`assets.c` procedurally generates every wall texture, sprite and weapon
graphic; `audio.c` synthesises every sound as a WAV in memory. The only
external asset is the map. `assets_load_png()` (stb_image, in `vendor/`)
exists but is unused — it is there so file-based assets can be swapped in.

### Map format

ASCII grid in `assets/maps/level1.txt`, currently 24×24 (`MAP_MAX_W/H` in
`utils.h`). `#` wall, `D` door, `P` player start, `.` empty, and sprite
spawn markers `E`/`S` (enemies), `B` barrel, `M` medkit, `A` ammo,
`R` armor. Doors are discovered by scanning for cell value 2 at load time.

## Ongoing work

The repository is mid-way through a planned multiplayer conversion. Read
these before making changes:

| Document | What it holds |
|---|---|
| `docs/plan.md` | Original single-player plan (weeks 1–4, complete) |
| `docs/plan-multiplayer.md` | Multiplayer plan (weeks 5–9), target config, key decisions |
| `docs/requirements.md` | Non-functional requirements: performance, latency budget, limits, platforms, testing |
| `docs/tasks/` | 30 task files with dependencies and acceptance criteria; `docs/tasks/README.md` is the index |

Target: up to 10 players, authoritative server over ENet, deathmatch as the
primary mode with co-op second. Task `docs/tasks/05-04` (replace
screen-space hitscan with a world raycast) blocks nearly everything else.

`docs/requirements.md` §9 mandates `tests/replay_test.c` — a determinism
check on `world_step()` — introduced by task `docs/tasks/06-01`. It does not
exist yet; until it does, `driver.sh smoke` is the only regression check.

## Conventions

- **Prose documentation is written in Russian**; code, identifiers and code
  comments are in English. Follow the language of the file you are editing.
- **Task and plan files are specifications, not changelogs.** When updating
  them, describe the target state as given. Do not add "what changed"
  sections, "previously N players, now 10", or any trace of the conversation
  that produced the edit — the reader did not participate in it and git
  already records the history.

### Code style

Formatting is defined by `.clang-format` (K&R, 4-space indent, no tabs) —
run it on any file you touch:

```bash
clang-format -i src/foo.c                        # format one file
clang-format --dry-run -Werror src/*.c src/*.h   # check without writing
```

Two rules it enforces that are easy to violate by hand: the opening brace
of a **function** goes on its own line (control statements keep theirs on
the same line), and every `if`/`else` body is braced and starts on the next
line — no `if (x) return;` one-liners.

Naming is **not** enforced by clang-format, so it is on you:

| Kind | Case | Example |
|---|---|---|
| Functions | `snake_case` | `enemy_update_all`, `map_is_wall_door` |
| Struct / enum types | `CamelCase` | `WeaponSystem`, `EnemyState` |
| Struct fields | `snake_case` | `attack_cooldown`, `plane_x` |
| Enum constants | `SCREAMING_SNAKE` | `WEAPON_SHOTGUN`, `ESTATE_CHASE` |

Enum constants carry a short prefix shared by every member of that enum.
The prefix is not mechanically derived from the type name — it is chosen to
stay short and unambiguous, and two enums on the same subject get different
ones (`EnemyType` → `ENEMY_`, `EnemyState` → `ESTATE_`; `SoundId` → `SND_`,
`GameState` → `GSTATE_`). Match the existing prefix when adding a member,
and pick a free one when adding an enum.

Where an enum needs a size sentinel it goes last and is named
`<PREFIX>_COUNT` (`ENEMY_COUNT`, `WEAPON_COUNT`, `SND_COUNT`).

### Commits

Written in English, following the 50/72 rule and prefixed with the type of
change.

- **Subject: at most 50 characters, including the `type: ` prefix.**
  Imperative mood, no trailing period.
- **Body: wrapped at 72 characters**, separated from the subject by a blank
  line. Explain why the change was needed and what it does, not how — the
  diff already shows how.
- **Type prefix** is one of `feat`, `fix`, `docs`, `refactor`, `perf`,
  `test`, `build`, `chore`.
- **Do not add a `Co-Authored-By` trailer.**

```
fix: match texture format to make_color packing

The screen texture was created as SDL_PIXELFORMAT_ARGB8888, which
reads a pixel as 0xAARRGGBB, but make_color() (utils.h) packs
0xAABBGGRR with red in the low byte, so every colour in the game
had its red and blue channels swapped.

make_color(), shade_color(), overlay_dim() and the stb_image
conversion in assets.c all agree with ABGR8888, so the texture
format was the single point of disagreement.
```
