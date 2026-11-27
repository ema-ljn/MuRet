---
name: python-type-safety
description: Use when typing Python in MuRet (mutools, under Client/tools/mutools, and the research scripts) - annotations, generics, protocols, type narrowing, mypy strict. Ranked below CODING_RULES and the tool's spec.
license: MIT
metadata:
  upstream: https://github.com/wshobson/agents/tree/c96b5f6552dba32e4492307493ee613581430715/plugins/python-development/skills/python-type-safety
---

# Python type safety in MuRet

This is the `python-type-safety` skill from [wshobson/agents](https://github.com/wshobson/agents) (MIT). It covers type
annotations, generics, protocols, narrowing and strict checking. This file is MuRet's layer; the upstream skill is in
[`references/upstream-skill.md`](references/upstream-skill.md), with [`references/details.md`](references/details.md).

## What takes precedence

1. What Emanuel asks for in the conversation.
2. **Byte-exact fidelity**: a `.bmd` read and written without changes comes back identical, and an edited one is read by
   the client as the tool showed it.
3. [`docs/CODING_RULES.md`](../../../docs/CODING_RULES.md) (§7 English, §9 comments only when strictly necessary, §10
   libraries, §13 one behavior, one test) and the tool's
   [spec](../../../docs/superpowers/specs/2026-11-25-bmd-editor-design.md) and
   [plan](../../../docs/superpowers/plans/2026-11-25-bmd-editor.md).
4. This skill and, last, `references/`.

## Which idea is which part of mutools

| The skill says | In mutools |
| --- | --- |
| Annotate every public signature; strict mypy in CI | `mypy --strict` over `src` and `tests` in `bmd-tool.yml`. |
| `T \| None`, modern syntax | Python 3.12: `type JsonValue = ...` aliases and `class FormatCodec[D](Protocol)` generics. |
| Protocols for interfaces | The ports are protocols: `FormatCodec[D]` and `FileStore`; nothing inherits from them. |
| Type narrowing with guards | The JSON view is `JsonValue`; code that reads it narrows with `isinstance` before use, and bad shapes become `INVALID_VALUE`. |
| Minimize `Any` | `Any` only where a document of any format passes through (the catalog's codec registry, `OpenDocument.document`). |
| Type aliases with meaning | `JsonObject`, `Pointer` text as `str` only at the edges; byte sizes as `int` constants named after the format (`TEXT_RECORD_SIZE`). |

## Where `references/` does not apply to MuRet

| The skill says | In MuRet |
| --- | --- |
| pyright as an alternative | mypy only, one checker. |

## Origin and license

[`references/upstream-skill.md`](references/upstream-skill.md) is a verbatim copy of
`plugins/python-development/skills/python-type-safety/SKILL.md` at commit `c96b5f65` of wshobson/agents (and
`references/details.md` from that skill's `references/`), under the MIT license in [`LICENSE`](LICENSE). The upstream
file's links are relative to the skill's root. This `SKILL.md` is an adaptation. To update: bring the files from a new
commit, change the commit in the frontmatter and in this section, and review both tables.
