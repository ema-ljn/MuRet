---
name: cpp-coding-standards
description: Use when writing or reviewing MuRet C++ against the C++ Core Guidelines (RAII, immutability, strong types, parameter passing, classes, concepts, enum class) and you want to cite the rule by number (F.16, C.21, ES.46). Complements cpp-pro; CODING_RULES takes precedence.
license: MIT
metadata:
  upstream: https://github.com/affaan-m/everything-claude-code/tree/4adeee6732a12d1540f49a4d50a38ce8c97f9a53/skills/cpp-coding-standards
---

# C++ Core Guidelines in MuRet

This is the `cpp-coding-standards` skill from
[affaan-m/everything-claude-code](https://github.com/affaan-m/everything-claude-code) (MIT): a summary of the
[C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) with each rule's number. It is for
reviewing and for justifying a change by citing the rule. `cpp-pro` explains the language and the build; this one says
which Core Guidelines rule is broken. The summary is in [`references/core-guidelines.md`](references/core-guidelines.md).

## What takes precedence

1. What Emanuel asks for in the conversation.
2. Fidelity to the original `main.exe` (see the `refactoring` skill, "The fidelity cautions").
3. [`docs/CODING_RULES.md`](../../../docs/CODING_RULES.md) and `.clang-format`.
4. The `cpp-pro` skill (C++20, CMake, vcpkg, GoogleTest in MuRet).
5. This skill and, last, `references/`.

## Where `references/` does not apply to MuRet

| `references/` says | In MuRet |
| --- | --- |
| C++17/20/23 | C++20 only, no extensions: no C++23 (`std::expected`, `std::print`). |
| E.2, E.14, E.15: exceptions for errors | MuRet returns errors as values (`Core::Expected`, `std::optional`, `std::error_code`), as `cpp-pro` and CODING_RULES say. What does apply from E.*: RAII (E.6), `noexcept` where failure is impossible (E.12) and destructors that do not fail (E.16). |
| NL.10 `underscore_style`, members with a trailing `_`, uppercase constants in the examples | CODING_RULES §7: types, functions and constants in `PascalCase`, variables in `camelCase`, `m_` on private members, `I` on interfaces, `s_` on statics. |
| SF.8 include guards | `#pragma once`, like the whole repo. |
| I.2 no non-`const` globals | That is AR3's goal: the original's globals become per-module state (`globals.tsv`, `GameContext`). Until then, today's global stays where it is. |
| ES.45 no magic constants | CODING_RULES §3: wire constants get a name in `Protocol/` and the citation of the research section that fixes them. |
| CP.* concurrency | CODING_RULES §8: each piece of data has an owning thread. The client is almost entirely single-threaded. |
| The checklist at the end (own exceptions, guards) | It applies without those two lines. |

## When to use it and when not

- **Yes**: when writing or reviewing a class, a packet type (`Field<T, Offset>`), the ownership of the `GameContext`, a
  handler's parameter passing (F.16, F.20, I.23), or when turning an `enum` into an `enum class` (Enum.3).
- **No** for changing the shape of a transcription of the original "because the guideline says so": fidelity wins, and
  the shape change goes to a design PR with the golden green.

## Origin and license

[`references/core-guidelines.md`](references/core-guidelines.md) is a verbatim copy of
`skills/cpp-coding-standards/SKILL.md` at commit `4adeee67` of affaan-m/everything-claude-code, under the MIT license in
[`LICENSE`](LICENSE). This `SKILL.md` is an adaptation. To update: bring the file from a new commit, change the commit
in the frontmatter and in this section, and review the "Where `references/` does not apply to MuRet" table.
