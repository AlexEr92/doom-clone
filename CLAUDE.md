# CLAUDE.md

## Project

A Doom-style raycaster in C11 on SDL2, about 2400 lines in `src/`: one map,
two weapons, two enemy types, doors, pickups, procedural audio. It is being
turned into a networked deathmatch — see [Ongoing work](#ongoing-work).

## Commands

```bash
cmake -S . -B build                    # configure (once)
cmake --build build                    # build
cmake --build build --target run       # build + run from the project root
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
```

The game **must** run from the project root: `assets/maps/level1.txt` is
opened by relative path, and a bare `./build/doom-clone` from elsewhere
exits with an error. A new `.c` file goes into the source list in
`CMakeLists.txt` by hand; there is no glob.

ENet comes only from the `vendor/enet` submodule, built and linked
statically with the game — no system package is looked for. A fresh clone
needs `git submodule update --init` (configure says so if `vendor/enet/` is
empty); `-DENABLE_MULTIPLAYER=OFF` builds the single-player game with no
ENet at all. Network code reaches ENet only through the `doom_enet` target,
and its sources go into the `if(ENABLE_MULTIPLAYER)` block that links it,
so the OFF build never compiles them.

Configuring writes `build/compile_commands.json` and links it from the
project root (only the `build/` tree sets the link). The `clangd-lsp`
plugin reads it for the `LSP` tool, which the main session, `reviewer` and
`tester` all have: `incomingCalls` on a function lists the tests that call
it; `outgoingCalls` the installed clangd does not implement. `tools/` is in
the database only with `-DBUILD_TOOLS=ON`.

**Git hooks** in `.githooks/` are off until `git config core.hooksPath
.githooks`. `commit-msg` runs the `git-commit` checker; `pre-commit` refuses
staged `src/*.c`/`*.h` that `.clang-format` would change — it reads the
staged content and never rewrites, so format and re-stage; `pre-push`
builds, runs `ctest` and `driver.sh smoke` (~9s). `--no-verify` bypasses
them.

### Tests

Unit tests are in `tests/`, one `test_<module>.c` per module on a vendored
Unity (`vendor/unity/`), built by default and run through CTest:

```bash
ctest --test-dir build --output-on-failure           # all suites
ctest --test-dir build -R door --output-on-failure   # one of them
```

A new test file is registered in `tests/CMakeLists.txt` with `doom_test()`,
which takes the module name and the `src/*.c` it needs (plus `FILE` when
the file is not `test_<module>.c`, as `replay_test.c` is not). Each
executable links only its own sources and stubs the rest — that is how
`test_enemy.c` avoids `sprite.c` and, through it, the renderer.

Only what runs without a framebuffer is tested: not the renderer, and
`audio.c` only as far as chunk lengths (`test_audio.c` uses the `dummy`
driver); `docs/closed_tasks/test-001-unit-test-harness.md` says why.
The transport is tested over real sockets on 127.0.0.1, server and clients
in one process (`test_net_transport.c`), and its timeouts by moving ENet's
clock with `enet_time_set()` rather than by waiting. Between two processes
— an echo, a `kill -9` of either side — `build/tests/net_echo
server|client` checks it by hand; it never exits, so CTest does not run it.
Coverage is measured in `build-cov/` by `.claude/skills/coverage/coverage.sh`,
whose thresholds (lines 65 %, decisions 55 %) are a ratchet that only goes
up; its `SKILL.md` explains how to read the numbers.

### Asset tools

`tools/prepare_sheet` and `tools/check_sheets` turn a picture from a
generative model, kept in the unversioned `raw/`, into loadable sheets.
They share nothing with the game and build only with `-DBUILD_TOOLS=ON`;
`docs/assets-spec.md` has the commands, the directory layout and the frame
grids.

### Running the game headless

To see a change work in the game, use the run skill: it builds, launches
under Xvfb, injects input and captures frames
(`.claude/skills/run-doom-clone/driver.sh smoke`). Read its `SKILL.md`
before driving the game by hand — mouse look does not work headless, a key
let go within one frame is swallowed, `pgrep -f` matches the calling shell.

### Verifying and reviewing a change

| What | Checks | When |
|---|---|---|
| `.claude/skills/check/check.sh` | clean build with warnings counted, `ctest`, the lint gate, `driver.sh smoke`, coverage of the changed lines — a summary a few lines long, ~15s | while implementing, as often as useful; no agent needed |
| `reviewer` agent | acceptance criteria, `lint`, the architecture invariants below, conventions; starts `tester` and merges its report | once per task, before closing it — `/task` starts it |
| `tester` agent | runs `check.sh`, drives the game through the change itself, reads the changed lines no test reaches, judges whether the tests would catch the change breaking | started by `reviewer`; by hand through `/verify` |

The review costs two agents and minutes, so it runs once, at the end of a
task, and `/verify` is deliberately not run before each commit. Neither
agent edits anything or gives a verdict, and a reviewer asked for gaps
finds some in sound work too: act on what breaks a criterion, an invariant
or a written rule, treat the rest as optional, and hand a finding that
becomes a task to `project-manager`. While `check.sh` or the tester runs it
owns `build/`, `build-cov/` and the X display `:99` — do not build or
launch the game meanwhile.

A `PostToolUse` hook (`.claude/hooks/post-edit.sh`) formats every `.c`/`.h`
Claude edits in `src/`, `tests/` and `tools/`, and after an edit in `src/`
reports a simulation that no longer compiles without SDL.

## Architecture

`docs/architecture.md` describes how the game works. What follows are the
invariants no compiler checks.

### Simulation and rendering are kept apart

The most important rule, and invisible from any one file: the simulation
never reaches for the frame it is drawn into, because none of it may need
a framebuffer on a server. Undoing the places where it did is most of what
the multiplayer work has been; do not add a new one.

- **The camera is derived, not stored.** `PlayerState` carries one `angle`;
  `player_camera()` (`camera.h`) builds `dir` and `plane` on demand — the
  length of `plane` is the FOV, a rendering quantity.
- **`SpriteList` is a render list only.** `sprites_build_from_world()`
  rebuilds it from the world every frame. No entity holds a sprite id, and
  `sprite.h` is included from `main.c` and `sprite.c` only.
- **Hitscan is world-space.** `weapon_try_fire()` traces `world_raycast()`
  and reads neither the camera nor `zBuffer`.
- **Pickups carry their own position**; which sprite draws one is decided
  at render time from `ItemType`.
- **Sound is events, not calls.** The simulation never calls `audio_play()`:
  it appends `GameEvent`s to an `EventQueue` (`event.h`), and the client
  drains it after each tick, scaling volume by the distance to *its own*
  player.

`world.c`, `enemy.c`, `weapon.c`, `item.c`, `door.c`, `player.c`, `map.c`
and `raycast_world.c` therefore compile with no SDL headers on the include
path. `.claude/skills/lint/lint.sh sim` checks it; a new simulation module
goes into the `SIM=` list at the top of that script.

### One tick is one function

`world_step()` (`world.c`) is the whole tick and takes one `PlayerInput` per
player, never the raw `InputState`. The server runs it and the client runs
it again to predict, so the order inside — players, doors, enemies, items,
weapons — is fixed, and nothing in it may read the wall clock or `rand()`.
`tests/replay_test.c` holds the line: two runs of one input sequence from
one `world_load()` must come out bit-identical. The one `rand()` left is
shotgun spread in `weapon_try_fire()`, which stage 7 seeds from
`MSG_ACCEPT` (`docs/roadmap.md`, key decision 9); until then the replay
never switches to the shotgun. Do not add a second one.

`src/config.h` is the single source of truth for the limits (`MAX_PLAYERS`,
`MAX_ENEMIES`, `MAX_SPRITES`, …), the tick rate and the network constants,
with the values `docs/requirements.md` §5 requires. A headless server
includes it, so it includes nothing and holds `#define` and
`_Static_assert` only.

### Entity model

Entity lists are fixed-size arrays with a `count`, and nothing is ever
removed: items deactivate (`active = 0`), enemies become corpses
(`ESTATE_DEAD`). An index is therefore a stable id — what a snapshot puts on
the wire and what the client keys a sprite by.

### Main loop

`main.c` runs a fixed step (`FIXED_DT`, 1/60) with an accumulator and
renders once a frame. `InputState` is the frame's raw SDL state and never
leaves the client. `input_turn()` turns the client's angle once a frame;
`input_sample()` takes exactly one `PlayerInput` per tick. Edge flags —
fire, use, weapon switch — are cleared by the sample that carries them, so
a press reaches exactly one tick however many ticks the frame holds;
`input_end_frame()` clears only what belongs to the frame. Game states
(`GSTATE_*`) and the 3×5 bitmap font every overlay uses live in `game.c`.

### Colour packing — easy to break silently

`make_color()` (`utils.h`) packs `0xAABBGGRR`, **red in the low byte**, and
the screen texture is created as `SDL_PIXELFORMAT_ABGR8888` in
`engine_init()` to match. `shade_color()`, `overlay_dim()` (`game.c`) and
the stb_image conversion in `assets.c` decompose the same way. Change one
in isolation and red and blue swap across the whole game — invisibly in
greys, which are symmetric.

### Assets are generated, then overridden by files

`assets.c` generates every wall texture, sprite and weapon graphic, and
`audio.c` synthesises every sound. `assets_init()` then replaces the slots
it finds sheets for under `assets/`: a missing file is normal and silent, a
broken one is a warning and the procedural texture. A `Texture` is a sheet
of frames (`fw`/`fh`, `cols`/`rows`, row stride the width of the whole
image), addressed through `texture_frame()` (`assets.h`).
`docs/assets-spec.md` says what the files must contain.

### Map format

ASCII grid in `assets/maps/level1.txt`, 24×24 (`MAP_MAX_W/H` in `utils.h`):
`#` wall, `D` door, `P` player start, `.` empty, `E`/`S` enemies, `B`
barrel, `M` medkit, `A` ammo, `R` armor.

## Ongoing work

The repository is mid-way through a planned multiplayer conversion. Read
these before making changes:

| Document | What it holds |
|---|---|
| `docs/architecture.md` | How the game works today and why, with a diagram of the frame |
| `docs/roadmap.md` | Where it is going: target config, key decisions, risks, unscheduled ideas |
| `docs/requirements.md` | Non-functional requirements: performance, latency budget, limits, platforms, testing |
| `docs/assets-spec.md` | What the graphics files must contain: directory layout, frame grids, transparency rules |
| `docs/harness.md` | The Claude Code harness — agents, skills, hooks, plugins and how to call them; updated with any change under `.claude/` or `.githooks/` |
| `docs/tasks/` | Open tasks with dependencies and acceptance criteria; `docs/tasks/README.md` is the index and states how to close one |
| `docs/closed_tasks/` | Finished and rejected tasks, each ending in a "Закрыто" note |

Target: up to 10 players, an authoritative server over ENet, deathmatch
first and co-op second. Stages 5 and 6 are done — the simulation runs
without a frame on one `PlayerInput` per tick — and stage 7, the network
itself, is next. Stages 7–10 change what `tests/replay_test.c` (required by
`docs/requirements.md` §9) and the unit tests pin down — `PlayerState`,
`World`, the `*_update_all` signatures — so keeping them passing and
updating the matching `tests/test_*.c` is part of those tasks, not
follow-up work.

## Conventions

- **Talk to the user in Russian**, whatever language the request arrives in:
  chat replies, questions, plans, progress notes, commit-time explanations.
- **Prose documentation is written in Russian**; code, identifiers and code
  comments in English. Follow the language of the file you are editing.
- **Task and plan files are specifications, not changelogs.** Describe the
  target state as given: no "what changed" sections, no "previously N
  players, now 10", no trace of the conversation that produced the edit —
  the reader was not there, and git keeps the history.
- **A task is finished when it sits in `docs/closed_tasks/`**, not when the
  code works: "do task 05-01" means the work, the review by the `reviewer`
  agent *and* the closing steps in `docs/tasks/README.md`. The `task` skill
  (`/task 05-01`) runs that whole cycle through to a commit — use it for
  any request to do a task. Say so if you deliberately leave one open.
- **A bug you find in passing becomes a task, not a detour.** Hand it to
  the `project-manager` agent, which writes `docs/tasks/bug-NNN-slug.md` and
  checks the existing tasks for overlap first, and keep going; do not fix it
  inside the diff of whatever you were doing — unless the current task
  cannot be verified without the fix, and then say so.
- **Something missing from the system is a question for the user, not a
  puzzle to solve.** When a build, test or check needs a package or tool
  that is not installed, stop and tell the user what is missing and the
  command that installs it (`! sudo apt install …` runs it in the session);
  the user installs it. Do not work around it on your own — no vendoring
  instead, no substitute tool, no skipped check, no reordering the work to
  dodge it. Whether the project should depend on that package at all is a
  design decision, and it is the user's too.
- **Never reference a commit hash** — not in documentation, not in a commit
  message; hashes do not survive rebase or squash-merge. Name the change and
  the place ("the texture format in `engine_init()`"); `git log -S` finds
  the commit anyway.

### Code style

Formatting is `.clang-format` (K&R, 4-space indent, no tabs); run
`clang-format -i` on any file you touch, `--dry-run -Werror` to check. It
puts a **function's** opening brace on its own line (control statements keep
theirs) and braces every `if`/`else` body on its own line — no
`if (x) return;` one-liners.

Naming is **not** enforced by any tool:

| Kind | Case | Example |
|---|---|---|
| Functions | `snake_case` | `enemy_update_all`, `map_is_wall_door` |
| Struct / enum types | `CamelCase` | `WeaponSystem`, `EnemyState` |
| Struct fields | `snake_case` | `attack_cooldown`, `plane_x` |
| Enum constants | `SCREAMING_SNAKE` | `WEAPON_SHOTGUN`, `ESTATE_CHASE` |

Every member of an enum shares one short prefix, chosen to be unambiguous
rather than derived from the type name — two enums on one subject get
different ones (`EnemyType` → `ENEMY_`, `EnemyState` → `ESTATE_`; `SoundId`
→ `SND_`, `GameState` → `GSTATE_`). Match the existing prefix when adding a
member, pick a free one for a new enum, and put a size sentinel last as
`<PREFIX>_COUNT`.

### Commits

In English, 50/72, with a type prefix; the `git-commit` skill writes the
message and checks it mechanically.

- **Subject: at most 50 characters, including the `type: ` prefix.**
  Imperative mood, no trailing period.
- **Body: wrapped at 72 characters**, after a blank line. Why the change
  was needed and what it does, not how — the diff shows how.
- **Type prefix** is one of `feat`, `fix`, `docs`, `refactor`, `perf`,
  `test`, `build`, `chore`.
- **Do not add a `Co-Authored-By` trailer.**
