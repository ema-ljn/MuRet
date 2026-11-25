---
name: python-design-patterns
description: Use when designing or reviewing the structure of Python code in MuRet (the mubmd tool under Client/tools/bmd, the research scripts) - choosing layers, splitting a class or function that does too much, deciding whether to abstract or keep duplication, composition over inheritance. KISS, SRP, separation of concerns and the rule of three, ranked below CODING_RULES and the tool's spec.
license: MIT
metadata:
  upstream: https://github.com/wshobson/agents/tree/c96b5f6552dba32e4492307493ee613581430715/plugins/python-development/skills/python-design-patterns
---

# Python design patterns in MuRet

This is the `python-design-patterns` skill from [wshobson/agents](https://github.com/wshobson/agents) (MIT, one of the
most-starred Claude Code plugin repositories, the source of the `clean-architecture` skill too). It covers KISS, single
responsibility, separation of concerns, composition over inheritance and the rule of three, with Python examples. This
file is MuRet's layer; the upstream skill is in [`references/upstream-skill.md`](references/upstream-skill.md), with
[`references/details.md`](references/details.md).

## What takes precedence

1. What Emanuel asks for in the conversation.
2. **Byte-exact fidelity**: a `.bmd` read and written without changes comes back identical, and an edited one is read by
   the client as the tool showed it.
3. [`docs/CODING_RULES.md`](../../../docs/CODING_RULES.md) (§7 English, §9 comments only when strictly necessary, §10
   libraries, §13 one behavior, one test) and the tool's
   [spec](../../../docs/superpowers/specs/2026-11-25-bmd-editor-design.md) and
   [plan](../../../docs/superpowers/plans/2026-11-25-bmd-editor.md).
4. This skill and, last, `references/`.

## Which idea is which part of mubmd

| The skill says | In mubmd |
| --- | --- |
| Separate concerns into layers with one direction | `core/` (documents, formats, views, checks) imports only the standard library; `app/` holds the use cases; `adapters/` holds storage, the CLI and the TUI. `tests/test_layers.py` checks it. Hexagonal details are in the `clean-architecture` skill. |
| Single responsibility, small functions | One module per format and per concern (`text_format.py`, `text_checks.py`); a use case orchestrates and does not decode bytes itself. |
| Composition over inheritance | One `FormatCodec` protocol, one implementation per format, chosen by the catalog (Strategy and Registry). No codec base class with hooks. |
| Inject dependencies | Use cases take a `FileStore`; tests pass an in-memory one. No container: the CLI and the TUI build the `LocalFileStore` and pass it. |
| Rule of three | The record tables of stage 3 share one `RecordTableFormat` because there are fourteen of them; texts and models keep their own codecs. |
| Explicit over clever | Frozen dataclasses and plain functions; edits return new values, so undo is a stack. |

## Where `references/` does not apply to MuRet

| The skill says | In MuRet |
| --- | --- |
| API, service and repository layers of a web backend | There is no HTTP or database. The same direction holds with other names: adapters, use cases, core. |
| Constructor injection everywhere | Plain functions that take their ports are enough; a class only where it holds state (the TUI app). |

## Origin and license

[`references/upstream-skill.md`](references/upstream-skill.md) is a verbatim copy of
`plugins/python-development/skills/python-design-patterns/SKILL.md` at commit `c96b5f65` of wshobson/agents (and
`references/details.md` from that skill's `references/`), under the MIT license in [`LICENSE`](LICENSE). The upstream
file's links are relative to the skill's root. This `SKILL.md` is an adaptation. To update: bring the files from a new
commit, change the commit in the frontmatter and in this section, and review both tables.
