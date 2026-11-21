---
name: clean-code
description: Use when writing, reviewing or refactoring MuRet code for readability and maintainability (names, functions, comments, errors, classes, tests), and in every design PR of the client refactor. Robert C. Martin's Clean Code rules, ranked below CODING_RULES and fidelity to the original.
license: MIT
metadata:
  upstream: https://github.com/pproenca/dot-skills/tree/cf93c57cac89d6fc3e4194686000411567f5caf3/skills/.experimental/clean-code
  upstream-version: "1.0.7"
---

# Clean Code in MuRet

This is the `clean-code` skill from [pproenca/dot-skills](https://github.com/pproenca/dot-skills) (MIT): the 48 rules of
Robert C. Martin's *Clean Code*, with today's corrections and a "when not to" in each one. Emanuel chose Clean Code as
the guide of the client refactor (2026-11-21). This file is MuRet's layer; the rules are in `references/`, with Java
examples.

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

| The rule says | In MuRet |
| --- | --- |
| `name-avoid-encodings`: no `m_` or `I` | They stay: `m_` on private members, `I` on interfaces and `s_` on statics (CODING_RULES §7, decided 2026-11-02). What does apply: no Hungarian type notation (`strName`, `iCount`). |
| `name-method-verb`: `getName()`, `setName()` | Names follow CODING_RULES §7 (`PascalCase` for functions and methods). The principle (verbs for what it does, nouns for what it is) applies the same. |
| `func-small`: hardly ever 20 lines | CODING_RULES §1 (about 40 lines) and the spec's sizes (handler ~80, function ~60, file ~400). A transcription of a `switch` from the original is not split to hit a number: it is marked as a transcription (or becomes a table in AR2). |
| `err-use-exceptions`, `err-write-try-catch-first`, `err-define-by-caller-needs` | MuRet does not throw: errors come back as values (`Core::Expected`, `std::optional`, `std::error_code`). The principle applies the same: the happy path in plain sight, the error with context, and one error type per what the caller needs. |
| `err-avoid-null` | `std::optional` for what may be missing; a raw pointer is a non-owning observer that may be `nullptr` only if its contract says so. |
| `cmt-*`: the code explains itself | It applies, and stricter (Emanuel, 2026-11-21; CODING_RULES §9): a comment goes in only if it is strictly necessary, in English and on one line. What stays: the citation to the original (`// 0x4e2b10`, at most with a short label), which tracking reads; the research section of a constant (`// 04-protocolo § ...`); `// Contiguous in the original:`; and a why the code cannot say. No prose, no commented-out code. |
| `obj-data-object-asymmetry`, `entity`/rich models | State that mimics the original's `.bss` stays as data (`struct` without invariants, fields in `camelCase`) and the logic stays in its module's functions. It is the rule's data/object asymmetry, not an anemic model by neglect. |
| `emerge-simple-design`: with a single implementation, doubt the interface | Interfaces go at the engine's edges (`ISocket`, `IPlayback`, `IObfuscation`) and where a test needs them, as the rule itself says ("tests need seams"). |
| `class-separate-concerns`: a dependency injection framework | No framework: the composition root is `app/`, wired by hand in order (`MakeObfuscation()` is the example). `GameContext` passes the parts of the state through the signature; it is not a service locator. |
| `fmt-*` | `.clang-format` decides; nothing is aligned by hand. |
| `test-*` | GoogleTest and CTest, one executable per test file until AR1.2 decides otherwise, and CODING_RULES rule 13 (each behavior, one test). |

## How it is used in the refactor

- **When designing a module** (the AR1 to AR6 design PRs): names that say why (`name-intention-revealing`), one thing per
  function at a single level of abstraction (`func-one-thing`, `func-abstraction-level`), one reason to change per class
  (`class-small`, `class-cohesion`) and construction separated from use (`class-separate-concerns`).
- **When deduplicating** (AR6): `func-dry` with its "when not to". Two functions of the original that look alike are
  merged only if the body is identical and the golden confirms it; incidental duplication stays.
- **When reviewing**: cite the rule by name (`func-minimize-arguments`) and the `references/` file that explains it.
- **Not in moves**: a PR that only moves files cleans nothing up (the plan's Global Constraints).

## The rules

| Priority | Category | Prefix | Files |
| --- | --- | --- | --- |
| 1 | Meaningful Names | `name-` | `intention-revealing`, `avoid-disinformation`, `meaningful-distinctions`, `pronounceable`, `searchable`, `avoid-encodings`, `class-noun`, `method-verb` |
| 2 | Functions | `func-` | `small`, `one-thing`, `abstraction-level`, `minimize-arguments`, `no-side-effects`, `command-query-separation`, `dry` |
| 3 | Comments | `cmt-` | `express-in-code`, `explain-intent`, `avoid-redundant`, `avoid-commented-out-code`, `warning-consequences` |
| 4 | Formatting | `fmt-` | `vertical-formatting`, `horizontal-alignment`, `team-rules`, `indentation` |
| 5 | Error Handling | `err-` | `use-exceptions`, `write-try-catch-first`, `provide-context`, `define-by-caller-needs`, `avoid-null` |
| 6 | Objects and Data Structures | `obj-` | `data-abstraction`, `data-object-asymmetry`, `law-of-demeter`, `avoid-hybrids`, `dto` |
| 7 | Boundaries | `bound-` | `wrap-third-party`, `learning-tests` |
| 8 | Classes and Systems | `class-` | `small`, `cohesion`, `organize-for-change`, `isolate-from-change`, `separate-concerns` |
| 9 | Unit Tests | `test-` | `first-law`, `keep-clean`, `one-assert`, `first-principles`, `build-operate-check` |
| 10 | Emergence and Simple Design | `emerge-` | `simple-design`, `expressiveness` |

Each rule is in `references/<prefix><file>.md` (for example `references/func-small.md`), and the categories in
`references/_sections.md`.

## Origin and license

`references/` is a verbatim copy of `skills/.experimental/clean-code/references/` at commit `cf93c57c` of
pproenca/dot-skills (skill version 1.0.7), under the MIT license in [`LICENSE`](LICENSE). This `SKILL.md` is an
adaptation. To update: bring `references/` from a new commit, change the commit and version in the frontmatter and in
this section, and review the "Where `references/` does not apply to MuRet" table.
