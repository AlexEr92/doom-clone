---
name: lint
description: Run static analysis over src/ — cppcheck for array overruns, null dereferences, leaks and uninitialised reads, plus the check that the simulation compiles without SDL. Use when asked to lint, to run static analysis, to check C code for defects, or as part of reviewing a change before it lands.
---

Static analysis of `src/`, wrapped so the non-obvious flags are not
retyped each time. Two checks make up the gate:

- **cppcheck** over `src/*.c`;
- **the simulation compiles without SDL** — `world.c`, `enemy.c`,
  `weapon.c`, `item.c`, `door.c`, `player.c`, `map.c` and
  `raycast_world.c` pass `cc -fsyntax-only -Isrc` with no SDL headers on
  the include path. This is the mechanical form of "Simulation and
  rendering are kept apart" in `CLAUDE.md`; the list of files lives in
  `SIM=` at the top of `lint.sh`, and a new simulation module is added
  there.

It is **not** a commit hook: nothing here blocks a commit. The SDL check
alone also runs after every edit Claude makes in `src/` (the `PostToolUse`
hook in `.claude/settings.json`), because it takes 0.1s.

All paths are relative to the project root.

## Prerequisites

```bash
sudo apt-get install -y cppcheck
```

Verified against cppcheck 2.13.0.

## Running it

```bash
.claude/skills/lint/lint.sh            # whole project, gate level
.claude/skills/lint/lint.sh changed    # only .c files differing from HEAD
.claude/skills/lint/lint.sh strict     # adds style checks, advisory
.claude/skills/lint/lint.sh sim        # the SDL check alone
```

Verified output on the current tree:

```
cppcheck: чисто (all)
симуляция без SDL: чисто
```

Timings measured here: whole project 4.3s, a single file 0.03s, `sim`
0.13s.

`all`, `changed` and `sim` exit non-zero when anything is reported. `strict`
always exits 0 and skips the SDL check — its findings are advice, not
defects.

## The two levels

**Gate level** (`all`, `changed`, `sim`) is cppcheck's
`warning,performance,portability` plus its always-on `error` class, and
the SDL check. Everything it reports is a defect, and `src/` reports none
today, so any output means the change under review introduced it. This is
the level worth acting on without discussion.

cppcheck catches the class this codebase is most exposed to — fixed-size
arrays indexed by ids that double as entity handles:

```
src/foo.c:6:21: error: Array 'el->items[32]' accessed at index 35,
which is out of bounds. [arrayIndexOutOfBounds]
```

**Strict level** adds `style`. As of this writing it reports seven things
in `src/`, all already filed:

- `constParameterPointer` / `constVariablePointer` on pointers that are
  only read — `DoorList *dl` in `enemy.c` and `player.c`, `PlayerState *p`
  in `enemy.c` and `world.c`, `SDL_Event *ev` in `game_handle_event()`.
  `docs/tasks/chore-003-const-correctness-lint.md`.
- `duplicateExpressionTernary` in `text_width()` (`game.c`).
  `docs/tasks/chore-002-dead-ternary-text-width.md`.

Read a `style` finding before taking it. cppcheck suggests `const T *` by
looking at the pointer, not at what the struct points to: a struct holding
nothing but a `uint32_t *pixels` that the function writes through will
still be offered a `const`, and taking it makes the signature claim
something untrue.

## Gotchas

- **`--max-configs=1` is what makes this usable.** `vendor/stb_image.h`
  carries enough `#ifdef` combinations that cppcheck spends 30 seconds
  exploring configurations the project never compiles. Pinning it to one
  brings the run to 4.3s with no loss for `src/`. Suppressing vendor paths
  does not help: the header is `#include`d from `assets.c` and gets parsed
  regardless.
- **A `style` run that reports nothing new does not mean the code is clean**,
  and one that reports something does not mean the code is wrong.
- **`changed` compares against `HEAD`**, staged and unstaged together, so it
  covers work in progress. It gives up the cross-file analysis the whole
  project run does — use it for speed, not as the final word. With no
  changed `.c` it still runs the SDL check, since a header alone can break
  it.
- **The SDL check fails on any compile error**, not only on an SDL include:
  `-fsyntax-only` stops at the first file that does not parse. Read the
  compiler output it prints before concluding SDL crept in.
- **Findings inside `vendor/` are suppressed**, including a `[internalError]`
  cppcheck raises on `stb_image.h` about its own analysis. That file is not
  this project's code and is not being fixed.
