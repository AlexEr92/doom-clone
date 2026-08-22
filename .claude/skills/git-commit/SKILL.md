---
name: git-commit
description: Commit the current work using this project's message rules — 50/72, a type prefix, no Co-Authored-By trailer, no commit hashes. Use when asked to commit, to write a commit message, to split changes into commits, or to check whether a message conforms.
---

Writes and makes commits under the rules in `CLAUDE.md` → Conventions →
Commits, and validates the message mechanically before committing so the
length limits are never a matter of eyeballing.

All paths are relative to the project root.

## Rules being enforced

| Rule | Limit |
|---|---|
| Subject, including the `type: ` prefix | ≤ 50 characters |
| Body lines | ≤ 72 characters |
| Type prefix | `feat` `fix` `docs` `refactor` `perf` `test` `build` `chore` |
| Blank line between subject and body | required |
| Body | required — a bare subject is not enough |
| `Co-Authored-By` trailer | forbidden |
| Commit hashes anywhere in the message | forbidden |

Subjects are imperative and carry no trailing period. The body explains why
the change was needed and what it does — the diff already shows how.

## How to commit

**1. See what is there.**

```bash
git status --short
git diff --stat HEAD
```

**2. Decide whether it is one commit.** If the changes cover unrelated
things, say so and propose a split rather than one mushy message. Staging
per file is enough for the usual case:

```bash
git add src/engine.c src/main.c
```

**3. Write the message to a file**, then validate it:

```bash
.claude/skills/git-commit/check-message.sh /tmp/msg.txt
```

Verified output on a conforming message:

```
msg.txt: ок (заголовок 47)
```

and on a broken one:

```
  заголовок 60 символов, лимит 50: docs: this subject is far too long ...
  строка 3 длиннее 72 ( 76 ): This body line is deliberately made much ...
  трейлер Co-Authored-By запрещён
msg.txt: НЕ ПРОШЁЛ (3)
```

Fix and re-run until it passes. Do not commit a message that fails.

**4. Commit.**

```bash
git commit -q -F /tmp/msg.txt
```

**5. Confirm what landed:**

```bash
git log --format=%B -1 | .claude/skills/git-commit/check-message.sh
git log --oneline -1
```

## Checking messages that already exist

Validate recent history — useful after a rebase or when adopting the rules:

```bash
.claude/skills/git-commit/check-message.sh --history 8
```

Verified output:

```
346b92c docs: file the unit test harness task: ок (заголовок 37)
da315d2 docs: file the file-based texture task: ок (заголовок 38)
e2998ed chore: run project-manager on sonnet: ок (заголовок 36)
...
```

Commits made before these rules existed do not pass, which is expected —
`bf543ad Infrastructure. Replace Makefile...` fails on both subject length
and the missing type prefix. Do not rewrite published history to satisfy
the checker.

## Writing the body

The commit message is where the reasoning goes, since the task files are
specifications and carry none of it. Say what was wrong and why the change
is the right shape, not what the diff plainly shows.

Refer to changes by name and place, never by hash — hashes do not survive
rebase or squash-merge. "the config added in the preceding commit" and
"the texture format in `engine_init()`" both stay true; `c3445fa` does not.
The checker rejects anything that looks like a hash.

## Gotchas

- **The subject budget includes the prefix.** `refactor: ` alone is 10 of
  the 50 characters, so `refactor:` and `docs:` subjects are not equally
  roomy.
- **A message that is only a subject fails.** This is deliberate: a
  one-line commit here is nearly always one that skipped explaining itself.
- **Lines holding a single long token pass.** A URL or a path that cannot
  be wrapped is exempt; a long line made of ordinary words is not.
- **`0x`-prefixed hex is safe**, so `0xAABBGGRR` and `ARGB8888` do not trip
  the hash check, and neither does a run of digits like `12345678`. A bare
  lowercase hex word such as `deadbeef` does trip it.
- **`git commit -m` bypasses the checker.** Use `-F` with a file.
