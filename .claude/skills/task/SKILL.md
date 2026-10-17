---
name: task
description: Take a task from docs/tasks/ through the whole cycle — implement it, check it, have the reviewer agent review it, act on the findings, close it and commit. Use when asked to do, take, implement or finish a task by its name, e.g. "сделай задачу 07-01", "/task bug-014".
argument-hint: "<task, e.g. 07-01 or bug-014>"
---

Do task `$ARGUMENTS` from start to commit. A task is done when it sits in
`docs/closed_tasks/` and is committed, not when the code works
(`CLAUDE.md` → Conventions). Talk to the user in Russian throughout.

## 1. Find it and decide whether it can start

```bash
ls docs/tasks docs/closed_tasks | grep -- "$ARGUMENTS"
```

- **In `docs/closed_tasks/`** — it is done or rejected: say so and stop.
- **Not found, or several matches** — ask which one.
- **"Зависит от" names a task still in `docs/tasks/`** — stop and say which
  one blocks it. Do not do the dependency on the side.

Read the task file in full, then what it points at: the code in
"Затрагиваемые файлы", the sections of `docs/architecture.md`,
`docs/requirements.md` and `docs/roadmap.md` it cites.

## 2. Check the task against the code

Task files are written ahead of the work and drift. Grep every symbol, file
and line the task names. When the drift changes a decision — a function the
task extends no longer exists, a criterion has already been met by another
change — tell the user what differs and ask before going on. When it does
not, note it for the "Закрыто" section and carry on.

If "Что сделать" leaves a design decision open, ask. Otherwise state the
plan in a few lines and start without waiting for approval.

## 3. Implement

Follow "Что сделать". Along the way:

- run `.claude/skills/check/check.sh` after each meaningful step; it takes
  ~15s and needs no agent;
- update the matching `tests/test_*.c` in the same change, and add the
  tests the change needs — that is part of the task, not follow-up work;
- a different bug you trip over goes to the `project-manager` agent as a
  new task, not into this diff — unless this task cannot be verified
  without fixing it; then fix it and say so.

## 4. Check the criteria yourself first

Walk "Критерий готовности" and run every check it names. The review costs
minutes and two agents; do not spend it on work you already know is
incomplete. Every criterion either holds, or you can say why it cannot be
checked here.

Then go through the programs you wrote in the scratchpad along the way.
Each one that goes through a project API and answers a question that will
come up again moves into `tests/manual/` now, before the review, so that
the review sees it (`CLAUDE.md` → Conventions, "A scratch tool worth
keeping"); the rest stay where they are.

## 5. Review

Start the `reviewer` agent with:

- the task file path;
- the scope — the uncommitted work against `HEAD`, or the branch point if
  part of the task is already committed;
- one sentence on what the change is meant to do;
- anything you already know diverges from the task file, and why.

Wait for its report; do not build or run the game while it works — it and
the tester it starts own `build/`, `build-cov/` and `:99`. Then act on it:

| Finding | What to do |
|---|---|
| **[критерий]**, **[дефект]**, **[инвариант]**, **[конвенция]**, **[нет теста]** | fix it, then re-run `check.sh` |
| **[необязательно]** | your call; list the ones you skipped and why in the final report |
| candidate for a separate task | hand it to `project-manager` verbatim |
| a scratch program the tester names as worth keeping | weigh it as your own in step 4; taking it into `tests/manual/` needs `check.sh`, not a second review |

Do not run a second review for fixes the first one asked for — `check.sh`
is enough. Run it again only if the fixes changed the design or touched
files the first review did not see.

If a criterion still fails and cannot be fixed within the task, do not
close it: leave it in `docs/tasks/`, say why, and stop before step 6.

## 6. Close

Follow "Как закрыть задачу" in `docs/tasks/README.md`: `git mv` to
`docs/closed_tasks/`, a "Закрыто" section with today's date and what
changed, the row moved to "Закрытые", and every document the change now
contradicts corrected — `docs/architecture.md`, `CLAUDE.md`, other task
files that describe the old state. No commit hashes anywhere.

## 7. Commit

Invoke the `git-commit` skill. The work and its closing go into the same
commit, typed by what the work is (`feat`, `fix`, `refactor`, …), as the
history does it. Split into several commits only where the work splits
naturally, with the closing in the last one. Do not push.

## 8. Report

Briefly, in Russian:

- what was done, and how it differs from "Что сделать", if at all;
- the last `check.sh` summary;
- each review finding and what became of it;
- tasks filed with `project-manager`;
- the scratch programs written along the way: which went into
  `tests/manual/`, which stayed in the scratchpad, and why;
- the commits made.
