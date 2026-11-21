---
name: clean-code
description: Use when writing, reviewing or refactoring MuRet code for readability and maintainability (names, functions, comments, errors, classes, tests), and in every design PR of the client refactor. Robert C. Martin's Clean Code principles, ranked below CODING_RULES and fidelity to the original.
license: MIT
metadata:
  upstream: https://github.com/sickn33/agentic-awesome-skills/tree/e6fb3ca87fc820e176028dfd2333aecc1856beff/skills/clean-code
---

# Clean Code in MuRet

This is the `clean-code` skill from [sickn33/agentic-awesome-skills](https://github.com/sickn33/agentic-awesome-skills)
(MIT), one of the most-starred agent skill collections; that collection takes it from
[jackjin1997/ClawForge](https://github.com/jackjin1997/ClawForge). It sums up Robert C. Martin's *Clean Code* in nine
parts: names, functions, comments, formatting, objects and data structures, error handling, unit tests, classes, and
smells. Emanuel chose Clean Code as the guide of the client refactor and asked for skills from popular repositories
(2026-11-21). This file is MuRet's layer; the summary is in
[`references/upstream-skill.md`](references/upstream-skill.md), with Python examples.

## What takes precedence

1. What Emanuel asks for in the conversation.
2. **Fidelity to the original `main.exe`**: the order of calls, the order of `rand()` calls and x87 arithmetic do not
   change because of a style rule. A golden that changes is a bug, not a cleanup
   ([refactor spec](../../../docs/superpowers/specs/2026-11-20-client-architecture-design.md), "Non-negotiable:
   fidelity").
3. [`docs/CODING_RULES.md`](../../../docs/CODING_RULES.md) and `.clang-format`. Several of its rules already are Clean
   Code (1 short functions, 2 return early, 3 nothing hardcoded, 4 do not duplicate, 9 readable over clever).
4. The plans in `docs/superpowers/` and the `refactoring` (how to refactor without changing behavior) and
   `clean-architecture` (modules and their dependencies) skills.
5. This skill and, last, `references/`.

## Where `references/` does not apply to MuRet

| The summary says | In MuRet |
| --- | --- |
| §1 Names | It applies. Casing and prefixes are CODING_RULES §7: types, functions and constants in `PascalCase`, variables in `camelCase`, `m_` on private members, `I` on interfaces and `s_` on statics (decided 2026-11-02). No Hungarian type notation (`strName`, `iCount`). |
| §2 Functions, "Small!", and the checklist's "smaller than 20 lines" | CODING_RULES §1 (about 40 lines) and the spec's sizes (handler ~80, function ~60, file ~400). A transcription of a `switch` from the original is not split to hit a number: it is marked as a transcription (or becomes a table in AR2). |
| §2 "No Side Effects: functions shouldn't secretly change global state" | It is AR3's goal: the original's globals become per-module state (`globals.tsv`, `GameContext`) and each function receives in its signature the state it touches. Until its milestone, today's global stays where it is. |
| §3 Comments, "Good Comments" | It applies, and stricter (Emanuel, 2026-11-21; CODING_RULES §9): a comment goes in only if it is strictly necessary, in English and on one line. What stays: the citation to the original (`// 0x4e2b10`, at most with a short label), which tracking reads; the research section of a constant (`// 04-protocolo § ...`); `// Contiguous in the original:`; and a why the code cannot say. No prose, no commented-out code. |
| §4 Formatting | `.clang-format` decides; nothing is aligned by hand. |
| §5 Objects and data structures, DTOs | State that mimics the original's `.bss` stays as data (`struct` without invariants, fields in `camelCase`) and the logic stays in its module's functions: the book's data/object asymmetry, not an anemic model by neglect. The Law of Demeter applies between modules (a module does not reach into another's state); inside a transcription, the original's chains of fields read as they do in the original. |
| §6 Error handling: exceptions instead of return codes, try-catch first, no null | MuRet does not throw: errors come back as values (`Core::Expected`, `std::optional`, `std::error_code`). The principle applies the same: the happy path in plain sight and the error with context. What may be missing is a `std::optional`; a raw pointer is a non-owning observer that may be `nullptr` only if its contract says so. |
| §7 Unit tests | GoogleTest and CTest, and CODING_RULES rule 13 (each behavior, one test). In the refactor the safety net is the behavior golden (`FrameDigest`) and the characterization tests, which pin today's behavior before the shape changes. |
| §8 Classes, "Small!" | It applies by responsibility, not by line count: cohesion first, CODING_RULES sizes only detect the smell. |
| "Limitations": stop and ask if scope or success criteria are missing | In the refactor the spec and the plan give both; what they leave open is in the spec's "Left to the builder". |

## How it is used in the refactor

- **When designing a module** (the AR1 to AR6 design PRs): names that say why (§1), one thing per function at a single
  level of abstraction (§2), one reason to change per class (§8) and no reaching into another module's state (§5).
- **When deduplicating** (AR6): two functions of the original that look alike are merged only if the body is identical
  and the golden confirms it; incidental duplication stays.
- **When reviewing**: cite the part and the principle (§2, "Do One Thing"); for a smell, the `refactoring` skill's
  catalog names the technique that treats it.
- **Not in moves**: a PR that only moves files cleans nothing up (the plan's Global Constraints).

## Origin and license

[`references/upstream-skill.md`](references/upstream-skill.md) is a verbatim copy of `skills/clean-code/SKILL.md` at
commit `e6fb3ca8` of sickn33/agentic-awesome-skills (formerly antigravity-awesome-skills), under the MIT license in
[`LICENSE`](LICENSE); its frontmatter names ClawForge, MIT according to its README, as the source. This `SKILL.md` is an
adaptation. To update: bring the file from a new commit, change the commit in the frontmatter and in this section, and
review the "Where `references/` does not apply to MuRet" table.
