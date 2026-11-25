---
name: python-project-structure
description: Use when deciding where a Python file or module goes in MuRet (the mubmd tool under Client/tools/bmd, the research scripts), splitting a module, defining a package's public API with __all__, or laying out tests. Ranked below CODING_RULES and the tool's spec.
license: MIT
metadata:
  upstream: https://github.com/wshobson/agents/tree/c96b5f6552dba32e4492307493ee613581430715/plugins/python-development/skills/python-project-structure
---

# Python project structure in MuRet

This is the `python-project-structure` skill from [wshobson/agents](https://github.com/wshobson/agents) (MIT). It covers
cohesive modules, explicit public APIs, flat hierarchies and test layout. This file is MuRet's layer; the upstream skill
is in [`references/upstream-skill.md`](references/upstream-skill.md).

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
| One concept per file | One format, one document kind, one check set per module; CODING_RULES §6 says the same for C++ classes. |
| Explicit public API with `__all__` | Each package's `__init__.py` lists what the next layer may use; adapters import only those names. |
| Flat structure, depth only for real sub-domains | `core/{documents,formats,checks,ciphers}`, `app/`, `adapters/{cli,tui}`: one level under each layer. |
| Tests mirror the source | `tests/core`, `tests/app`, `tests/adapters/{cli,tui}`, plus `tests/skill` for the skill's recipes. |
| `src/` layout | `Client/tools/bmd/src/mubmd`, so tests import the installed package. |

## Where `references/` does not apply to MuRet

| The skill says | In MuRet |
| --- | --- |
| Domain-driven folders for services | The domain is MU's file formats; the layers above are the structure. |

## Origin and license

[`references/upstream-skill.md`](references/upstream-skill.md) is a verbatim copy of
`plugins/python-development/skills/python-project-structure/SKILL.md` at commit `c96b5f65` of wshobson/agents, under the
MIT license in [`LICENSE`](LICENSE). The upstream file's links are relative to the skill's root. This `SKILL.md` is an
adaptation. To update: bring the files from a new commit, change the commit in the frontmatter and in this section, and
review both tables.
