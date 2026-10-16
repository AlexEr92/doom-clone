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

About 15 seconds, most of it smoke and cppcheck. It owns `build/`, `build-cov/`,
`.run/` and the X display `:99` while it runs: do not start it while the
reviewer or tester is working.

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
ctest:     12/12
lint:      чисто (cppcheck, симуляция без SDL)
smoke:     ок (6 PASS)
покрытие:  строки 68.1%, decisions 56.3% — пороги выдержаны
  исполняемых изменённых строк нет
итог: ок
```

and on a copy with a warning, a broken test, SDL in `world.c` and the map
removed:

```
сборка:    ПРЕДУПРЕЖДЕНИЯ: 1
  src/item.c:11:9: warning: unused variable ‘unused_probe’ [-Wunused-variable]
ctest:     ПРОВАЛ — .run/check/ctest.log
  1 - utils (Failed)
lint:      НАХОДКИ — .run/check/lint.log
  src/world.c:1:10: fatal error: SDL.h: Нет такого файла или каталога
  ...
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
