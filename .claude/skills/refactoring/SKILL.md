---
name: refactoring
description: Use when MuRet code has to be refactored, cleaned up or simplified without changing what it does (the client refactor's design PRs, the AR6 deduplication, a code smell blocking a change). Martin Fowler's refactoring loop with its catalog of smells and techniques, plus the port's fidelity cautions.
license: MIT
metadata:
  upstream: https://github.com/cskwork/clean-code-skill/tree/acd750b18894b4bd03a0c929c2198336fce638d2
---

# Refactoring in MuRet

This is the `clean-code` skill from [cskwork/clean-code-skill](https://github.com/cskwork/clean-code-skill) (MIT),
renamed `refactoring` so it does not clash with Robert C. Martin's `clean-code` skill: the method and catalog of Martin
Fowler's *Refactoring*, organized like refactoring.guru. The `clean-code` skill says what clean code looks like; this
one says how to get there without changing behavior. The original is in `references/upstream-skill.md`.

## What takes precedence

1. What Emanuel asks for in the conversation.
2. **Fidelity to the original `main.exe`** and the refactor's behavior golden: if something changes, it is a refactor
   bug and not a golden to regenerate.
3. [`docs/CODING_RULES.md`](../../../docs/CODING_RULES.md) and the Global Constraints of the
   [refactor plan](../../../docs/superpowers/plans/2026-11-20-client-architecture-refactor.md).
4. This skill and, last, `references/`.

## The loop

For every cleanup, never skipping step 4 or 5:

1. **Detect**: name the concrete readability or change-cost problem. The smell catalog is in
   [`references/code-smells.md`](references/code-smells.md).
2. **Choose** the smallest transformation that works, from
   [`references/refactoring-techniques.md`](references/refactoring-techniques.md).
3. **One small step** that does not change behavior.
4. **Verify** with the tests of what was touched after each coherent batch: the module's tests and, if it touches ported
   logic, the behavior golden (`FrameDigest`). Verification grows when the surface calls for it; what did not change is
   not rerun.
5. **Verify it got cleaner**: read the result. If it does not read better than before, revert. Meeting a rule at the
   cost of readability is a loss.

## The ground rules

- **Only behavior-preserving transformations.** If something changes what the program does, it is a fix or a new
  feature: another commit and another review. In MuRet, also another lane (the port's).
- **Tests are the safety net.** Without tests around it, first a characterization test that pins what it does today,
  then the refactor.
- **Small steps, always green**, and **one topic per commit**, with Angular titles and the `AR<epic>.<milestone>`
  scope (`refactor(AR1.2): …`).
- **Follow the surrounding code** and **be surgical**: touch what the task asks for; the format and names of the rest
  stay as they were.
- **Cohesion before line counts.** CODING_RULES sizes detect smells, they are not law: split by responsibility.

## The fidelity cautions (MuRet's own)

Some catalog techniques preserve behavior in Java but not in an x87 port:

| Technique | The caution |
| --- | --- |
| *Extract Variable*, *Inline Temp*, *Replace Temp with Query* | In the original's ported math, a `float` temporary rounds differently from the expression recomputed in extended precision. Keep the type of every intermediate (`F32()`, `Ftol`) and the number of times it is evaluated. |
| *Replace Conditional with Polymorphism*, *Replace Type Code with Subclasses* | A `switch` that transcribes the original's stays as a marked `switch` or becomes a table in the same order (AR2); it is not spread over classes. |
| *Substitute Algorithm* | Never on ported logic, unless the golden proves the result is bit-for-bit identical and the `rand()` calls come out in the same order. |
| *Consolidate Duplicate Conditional Fragments*, *Remove Control Flag* | Moving code can change the order of effects (sounds, particles, `rand()`): only if the golden does not change. |
| *Replace Error Code with Exception*, *Replace Exception with Test* | MuRet does not throw: errors come back as values (`Core::Expected`). |
| *Extract Method*, *Move Method*, *Extract Class* | They are the basis of the design PRs. The citation to the original (`// 0x...`) travels with the code it cites. |

## A large cleanup

The client refactor follows upstream's flow for cleaning a whole codebase, with the plan's names:

1. **Safety net first**: AR0 (the behavior golden, the layer check, tracking).
2. **By impact**: first the smells that block changes (*Divergent Change*, *Shotgun Surgery*, god classes like
   `WorldRuntime`), cosmetic ones last.
3. **By units**: one group of modules per milestone (AR4), each with its scripted move and its design PR.
4. **Verify each unit** and say exactly what was verified.
5. **The why in the commit**: the what is in the diff.

## The references

| File | For |
| --- | --- |
| [`references/code-smells.md`](references/code-smells.md) | naming the problem: 23 smells in 5 families, with the technique that treats each |
| [`references/refactoring-techniques.md`](references/refactoring-techniques.md) | the technique: more than 60, with before and after for the common ones |
| [`references/clean-code-principles.md`](references/clean-code-principles.md) | the habits that prevent smells (names, functions, guards, constants, DRY, comments) |
| [`references/upstream-skill.md`](references/upstream-skill.md) | the original `SKILL.md` |

The `references/` examples are JavaScript and Python: they explain the technique, they are not copied into the repo.

## Origin and license

`references/` is a verbatim copy of `references/` and of the `SKILL.md` (as `references/upstream-skill.md`) at commit
`acd750b1` of cskwork/clean-code-skill, under the MIT license in [`LICENSE`](LICENSE). This `SKILL.md` is an adaptation.
To update: bring the files from a new commit, change the commit in the frontmatter and in this section, and review "The
fidelity cautions".
