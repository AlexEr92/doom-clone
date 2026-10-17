---
name: check
description: Run every mechanical check on the working tree in one call — a clean build with warnings counted, unit tests, the lint gate, the smoke scenario, coverage of the changed lines — and print a summary a few lines long. Use while implementing a change, after each meaningful step, and before handing the change to review.
---

The mechanical half of verifying a change, as one script, so neither the
implementer nor the `tester` agent spends turns on running six commands and
reading their output. What it cannot do — drive the game through the
change itself, judge whether the tests would catch it breaking — is the
tester's.

All paths are relative to the project root.

## Running it

```bash
.claude/skills/check/check.sh           # uncommitted work against HEAD
.claude/skills/check/check.sh master    # everything since the branch point
```

About 10 seconds, and about 18 when smoke runs, which alone takes about 8.
It owns `build/`, `build-cov/`, `.run/` and the X display `:99` while it
runs: do not start it while the reviewer or tester is working.

| Step | What counts as a failure |
|---|---|
| clean build of `build/` | any warning outside `vendor/` (`docs/requirements.md` §9) |
| `ctest` | a failing suite, or no suites registered at all |
| `lint.sh all` | anything at gate level: cppcheck or the SDL check |
| `driver.sh smoke` | a `FAIL`; runs only when the diff reaches `src/`, `assets/` or `CMakeLists.txt` |
| `coverage.sh changed` | thresholds not held; skipped when `ctest` failed |

Exit codes: `0` everything holds, `1` a check failed, `2` the build or the
revision did. The full output of each step is in `.run/check/<step>.log`;
the summary names the log of whatever failed.

Verified output on a clean tree:

```
проверяю: рабочее дерево против HEAD
сборка:    ок, предупреждений вне vendor/ нет
ctest:     19/19
lint:      чисто (cppcheck, симуляция без SDL)
smoke:     не нужен — изменения не доходят до src/, assets/, CMakeLists.txt
покрытие:  строки 81.6%, decisions 71.4% — пороги выдержаны
  исполняемых изменённых строк нет
итог: ок
```

on a copy with a comment added to `src/world.c`, which brings smoke in:

```
проверяю: рабочее дерево против HEAD
сборка:    ок, предупреждений вне vendor/ нет
ctest:     19/19
lint:      чисто (cppcheck, симуляция без SDL)
smoke:     ок (6 PASS)
покрытие:  строки 81.6%, decisions 71.4% — пороги выдержаны
  исполняемых изменённых строк нет
итог: ок
```

and on a copy with an unused variable in `src/item.c`, an array overrun in
`src/door.c`, a wrong expected value in `tests/test_utils.c` and
`assets/maps/level1.txt` removed:

```
проверяю: рабочее дерево против HEAD
сборка:    ПРЕДУПРЕЖДЕНИЯ: 2
  src/door.c:13:9: warning: variable ‘probe’ set but not used [-Wunused-but-set-variable]
  src/item.c:12:9: warning: unused variable ‘unused_probe’ [-Wunused-variable]
ctest:     ПРОВАЛ — .run/check/ctest.log
  1 - utils (Failed)
lint:      НАХОДКИ — .run/check/lint.log
  src/door.c:14:10: error: Array 'probe[2]' accessed at index 2, which is out of bounds. [arrayIndexOutOfBounds]
      probe[2] = 1;
           ^
smoke:     ПРОВАЛ — .run/check/smoke.log
  ERROR: window never appeared
покрытие:  не считалось — юнит-тесты не прошли
итог: ПРОВАЛ — сборка ctest lint smoke
```

## Gotchas

- **The build is always clean.** `--clean-first` costs about two seconds
  and is what makes the warning count honest: an incremental build prints
  warnings only for the files it recompiles.
- **A missing Xvfb is not a pass.** The smoke line then says `НЕ ПРОВЕРЕН`
  without failing the run; read it before calling the change verified.
- **A green run is not a review.** It says the change builds, passes and
  starts; it says nothing about whether the tests check the new behaviour,
  which is what the `tester` and `reviewer` agents are for.
