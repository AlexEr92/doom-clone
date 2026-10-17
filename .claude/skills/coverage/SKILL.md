---
name: coverage
description: Measure unit-test coverage of src/ with gcovr and hold the project thresholds; list the changed lines of src/ that no test executes. Use when asked about test coverage, which code the tests reach, whether a change is covered, or as part of verifying a change before it lands.
---

Coverage of `src/` by the unit tests, in a separate build tree
(`build-cov/`, gitignored), so the normal `build/` is never instrumented.
Only the test binaries are compiled with `--coverage` (`doom_test()` in
`tests/CMakeLists.txt`, under `-DENABLE_COVERAGE=ON`); `doom-clone` is
not, since `ctest` never runs it.

All paths are relative to the project root.

## Prerequisites

```bash
sudo apt-get install -y gcovr
```

Verified against gcovr 7.0 and gcc 13.3.

## Running it

```bash
.claude/skills/coverage/coverage.sh                 # build + ctest + summary
.claude/skills/coverage/coverage.sh changed         # + changed lines no test runs
.claude/skills/coverage/coverage.sh changed master  # the same, against the branch point
.claude/skills/coverage/coverage.sh html            # + build-cov/coverage/index.html
.claude/skills/coverage/coverage.sh report          # summary only, no rebuild
```

A full run takes about a second once `build-cov/` exists. Counters are
reset before every `ctest`, so a test deleted since the last run is not
credited with its lines. `report` rebuilds nothing and reads what the last
run left: when `src/` or `tests/` changed after it, it warns on stderr
before the numbers. A revision `changed` cannot resolve fails at once,
before the build.

Exit codes: `0` thresholds hold, `1` they do not, `2` the build, the tests
or the tooling failed — failing tests are never reported as coverage.

Verified summary on the current tree (the per-file table above it is
gcovr's, sorted by uncovered share):

```
lines: 81.6% (2118 out of 2597)
functions: 90.8% (178 out of 196)
branches: 74.4% (1259 out of 1692)
decisions: 71.4% (858 out of 1201)
покрытие: пороги выдержаны (строки 78 %, decisions 68 %)
```

and the tail of `changed` against a base that touched `assets.c` and
`sprite.c`:

```
изменённые строки src/ против HEAD~8:
  src/assets.c: 624, 637-639, 669
  src/sprite.c: не линкуется ни в один тест — не измеряется
  src/sprite.h: исполняемого кода под тестами нет
  не исполнено тестами: 5 из 7 исполняемых изменённых строк
```

## Thresholds

`--fail-under-line 78 --fail-under-decision 68`, just under the measured
base. They are a ratchet: raise them when tests are added, never lower them
to let a change through. There are no per-file thresholds, on purpose —
see below.

## Reading the numbers

- **Only modules linked into some test are in the denominator.** `engine.c`,
  `hud.c`, `main.c`, `raycast.c`, `sprite.c` and `utils.c` are out of scope
  by design (`docs/closed_tasks/test-001-unit-test-harness.md`) and produce
  no data at all; `changed` says so rather than counting them as zero.
- **`assets.c` (20 %) and `game.c` (32 %) are not badly tested.** Each
  holds testable code next to render code: the procedural texture
  generators and the bitmap font are never run by a test. A low number
  there says nothing about the sheet loader or the menu state machine.
- **Executed is not checked.** `tests/replay_test.c` runs 600 ticks of the
  whole simulation and asserts only that two runs agree, so it lifts
  `world.c`, `enemy.c`, `item.c` and `door.c` without asserting anything
  about them. A covered line means some test reached it; whether a test
  would fail if that line were wrong takes reading the test.
- **What the metric is good at** is the branches no test ever enters:
  `map_load()` failures, `item_add`/`enemy_spawn` overflows, a sheet
  `assets_load_sheet()` refuses. Use `changed` to ask that of a diff.
