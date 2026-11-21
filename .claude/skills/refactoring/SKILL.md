---
name: refactoring
description: Use when MuRet code has to be refactored, cleaned up or simplified without changing what it does (the client refactor's design PRs, the AR6 deduplication, a code smell blocking a change). Martin Fowler's method with its catalogs of smells and refactorings, plus the port's fidelity cautions.
license: MIT
metadata:
  upstream: https://github.com/luongnv89/claude-howto/tree/5609e68a6ce838579e15fd31ceb6025be08841df/03-skills/refactor
---

# Refactoring in MuRet

This is the `refactor` skill from [luongnv89/claude-howto](https://github.com/luongnv89/claude-howto) (MIT), one of the
most-starred Claude Code guides, renamed `refactoring` here. It is Martin Fowler's *Refactoring* (2nd edition) as six
phases (research, test coverage, smells, plan, incremental implementation, review), with a catalog of 21 smells in 5
families, a catalog of 28 refactorings with their mechanics, and a plan template. The `clean-code` skill says what clean
code looks like; this one says how to get there without changing behavior. The upstream skill is in
[`references/upstream-skill.md`](references/upstream-skill.md).

## What takes precedence

1. What Emanuel asks for in the conversation.
2. **Fidelity to the original `main.exe`** and the refactor's behavior golden: if something changes, it is a refactor
   bug and not a golden to regenerate.
3. [`docs/CODING_RULES.md`](../../../docs/CODING_RULES.md) and the Global Constraints of the
   [refactor plan](../../../docs/superpowers/plans/2026-11-20-client-architecture-refactor.md).
4. This skill and, last, `references/`.

## The six phases in the refactor

| Phase | In MuRet |
| --- | --- |
| 1. Research, and the questions to the user | The spec and the plan already answer scope, goals and what must not change; per milestone, `module-map.tsv` says what goes where. Emanuel is asked only what they leave open (the spec's "Left to the builder"), not for approval of each phase: the milestone's PR is the approval. |
| 2. Test coverage | The safety net is AR0: the behavior golden (`FrameDigest`) and the module's tests, run with CTest (in the cloud the build goes through GitHub CI). Without tests around the code, first a characterization test that pins what it does today (`Client/tests/characterization/`, AR0.3), then the refactor. |
| 3. Smells | [`references/code-smells.md`](references/code-smells.md). Its thresholds (a long method at 30 to 50 lines, 4 or more parameters) give way to CODING_RULES §1 and the spec's sizes. First the smells that block changes (*Divergent Change*, *Shotgun Surgery*, god classes like `WorldRuntime`), cosmetic ones last. |
| 4. Plan | The milestone in the plan is the plan: the scripted move, the comments PR, then the design PR. [`templates/refactoring-plan.md`](templates/refactoring-plan.md) helps lay out a large design PR. |
| 5. Incremental implementation | Change, test, green, next step. The tests of what was touched run after each coherent batch: the module's tests and, if it touches ported logic, the golden. What did not change is not rerun. A red test means undo the step, not adjust the test. |
| 6. Review | Read the result: if it does not read better than before, revert. The numbers come from the reference (`consultar.py`, the metrics) and `check_layers.py`'s baseline. |

## The ground rules

- **Only behavior-preserving transformations.** If something changes what the program does, it is a fix or a new
  feature: another commit and another review. In MuRet, also another lane (the port's).
- **Small steps, always green**, and **one topic per commit**, with Angular titles and the `AR<epic>.<milestone>`
  scope (`refactor(AR1.2): …`).
- **Follow the surrounding code** and **be surgical**: touch what the task asks for; the format and names of the rest
  stay as they were.
- **Cohesion before line counts.** CODING_RULES sizes detect smells, they are not law: split by responsibility.

## The fidelity cautions (MuRet's own)

Some catalog refactorings preserve behavior in a JavaScript example but not in an x87 port:

| Refactoring | The caution |
| --- | --- |
| *Extract Variable*, *Inline Variable*, *Replace Temp with Query* | In the original's ported math, a `float` temporary rounds differently from the expression recomputed in extended precision. Keep the type of every intermediate (`F32()`, `Ftol`) and the number of times it is evaluated. |
| *Replace Conditional with Polymorphism* (and the *Switch Statements* smell) | A `switch` that transcribes the original's stays as a marked `switch` or becomes a table in the same order (AR2); it is not spread over classes. |
| *Decompose Conditional*, *Consolidate Conditional Expression*, *Replace Nested Conditional with Guard Clauses*, *Split Phase*, *Move Statements into Function*, *Move Statements to Callers* | They can change the order of effects or of evaluation (sounds, particles, a `rand()` inside a condition that `&&` now skips): only if the golden does not change. |
| *Substitute Algorithm* (Fowler's, not in this catalog) | Never on ported logic, unless the golden proves the result is bit-for-bit identical and the `rand()` calls come out in the same order. |
| *Introduce Special Case* | Fine in new code. In a transcription, the original's null checks are behavior and stay as they read. MuRet does not throw: errors come back as values (`Core::Expected`). |
| *Encapsulate Variable* | It is how AR3 moves the original's globals into per-module state (`globals.tsv`): one global at a time, with the golden green. |
| *Extract Method*, *Move Method*, *Extract Class*, *Combine Functions into Class* | They are the basis of the design PRs. The citation to the original (`// 0x...`) travels with the code it cites. |

## The references

| File | For |
| --- | --- |
| [`references/upstream-skill.md`](references/upstream-skill.md) | the six phases, the smell-to-refactoring table and the safety rules |
| [`references/code-smells.md`](references/code-smells.md) | naming the problem: 21 smells in 5 families (bloaters, object-orientation abusers, change preventers, dispensables, couplers) with signs and treatment |
| [`references/refactoring-catalog.md`](references/refactoring-catalog.md) | the refactoring: 28, each with its mechanics and a before and after |
| [`templates/refactoring-plan.md`](templates/refactoring-plan.md) | laying out a large design PR |

The examples are JavaScript and Python: they explain the technique, they are not copied into the repo.

## Origin and license

[`references/upstream-skill.md`](references/upstream-skill.md) is a copy of `03-skills/refactor/SKILL.md` at commit
`5609e68a` of luongnv89/claude-howto, and `references/` and `templates/` are copies of that skill's folders of the same
name, under the MIT license in [`LICENSE`](LICENSE). The copies are verbatim except that each file's build footer (last
update, Claude Code version and the list of compatible models) is left out. Upstream's `scripts/` (a smell detector and
a complexity analyzer) are not vendored: they read only Python, JavaScript and TypeScript. The upstream file's links are
relative to the skill's root. This `SKILL.md` is an adaptation. To update: bring the files from a new commit, drop the
footers again, change the commit in the frontmatter and in this section, and review "The fidelity cautions".
