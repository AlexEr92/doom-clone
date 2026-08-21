---
name: lint
description: Run static analysis over src/ with cppcheck — array overruns, null dereferences, leaks, uninitialised reads. Use when asked to lint, to run static analysis, to check C code for defects, or as part of reviewing a change before it lands.
---

Static analysis of `src/` with cppcheck, wrapped so the non-obvious flags are
not retyped each time. It is **not** a commit hook: nothing here blocks a
commit, and it is meant to be run deliberately — by hand, or by a reviewer
before a change lands.

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
```

Verified output on the current tree:

```
cppcheck: чисто (all)
```

Timings measured here: whole project 4.3s, a single file 0.03s.

`all` and `changed` exit non-zero when anything is reported. `strict` always
exits 0 — its findings are advice, not defects.

## The two levels

**Gate level** (`all`, `changed`) is `warning,performance,portability` plus
cppcheck's always-on `error` class. Everything it reports is a defect, and
`src/` reports none today, so any output means the change under review
introduced it. This is the level worth acting on without discussion.

It catches the class this codebase is most exposed to — fixed-size arrays
indexed by ids that double as entity handles:

```
src/foo.c:6:21: error: Array 'el->items[32]' accessed at index 35,
which is out of bounds. [arrayIndexOutOfBounds]
```

**Strict level** adds `style`. Useful when tidying, but its findings are
mixed and must be read rather than applied. As of this writing it reports
seven things in `src/`, and they fall into three groups:

- **Worth taking** — const-correctness on parameters and locals that are only
  read (`game_handle_event`'s `SDL_Event *ev`, `item_update`'s `Sprite *sp`,
  `map_is_wall_door`'s `DoorList *dl`, whose callee already takes a const).
- **About to evaporate** — `perpDist` never assigned and `Enemy *e` in
  `weapon_try_fire`. Both live in the screen-space hitscan that task
  `docs/tasks/05-04` deletes outright; fixing them is work that gets thrown
  away.
- **Wrong to take** — cppcheck suggests `const Framebuffer *fb` for
  `sprite_render`. `Framebuffer` holds nothing but a `uint32_t *pixels`, so
  the const would forbid reassigning the pointer while leaving the pixels
  writable — and `sprite_render` writes to them on every call. Following the
  advice makes the signature claim something untrue.

## Gotchas

- **`--max-configs=1` is what makes this usable.** `vendor/stb_image.h`
  carries enough `#ifdef` combinations that cppcheck spends 30 seconds
  exploring configurations the project never compiles. Pinning it to one
  brings the run to 4.3s with no loss for `src/`. Suppressing vendor paths
  does not help: the header is `#include`d from `assets.c` and gets parsed
  regardless.
- **A `style` run that reports nothing new does not mean the code is clean**,
  and one that reports something does not mean the code is wrong. Read the
  three groups above before changing anything.
- **`changed` compares against `HEAD`**, staged and unstaged together, so it
  covers work in progress. It gives up the cross-file analysis the whole
  project run does — use it for speed, not as the final word.
- **Findings inside `vendor/` are suppressed**, including a `[internalError]`
  cppcheck raises on `stb_image.h` about its own analysis. That file is not
  this project's code and is not being fixed.
