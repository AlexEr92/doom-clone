---
name: verify
description: Verify the current change through the tester agent — the mechanical checks plus a scenario aimed at the change and a judgement on whether the tests would catch it breaking. Run by hand; the reviewer starts the tester itself.
argument-hint: "[task file] [what the change should do]"
context: fork
agent: tester
background: false
# Manual only. A model-invocable skill named `verify` makes Claude Code run
# it before every commit, which repeats a full tester run per commit; the
# review at the end of a task runs it once.
disable-model-invocation: true
---

Verify the uncommitted change in this working tree — staged and unstaged
together, against `HEAD` — following your instructions section by section.

What the caller said about the change, possibly a task file under
`docs/tasks/` and a sentence on what it should do:

$ARGUMENTS

When that names a task file, read it and check every item of its
"Критерий готовности" that needs running something — the build, `ctest`,
`driver.sh`, coverage. Leave the items that only need reading code to the
reviewer, and say which ones you left.

When it names nothing, work out what the change is for from the diff, and
say so in the report.
