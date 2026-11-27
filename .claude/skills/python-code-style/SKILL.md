---
name: python-code-style
description: Use when writing or reviewing Python style in MuRet (mutools, under Client/tools/mutools, and the research scripts) - ruff, mypy strict, naming, imports, line length, docstrings, README. Ranked below CODING_RULES, whose comment rule (§9) limits docstrings.
license: MIT
metadata:
  upstream: https://github.com/wshobson/agents/tree/c96b5f6552dba32e4492307493ee613581430715/plugins/python-development/skills/python-code-style
---

# Python code style in MuRet

This is the `python-code-style` skill from [wshobson/agents](https://github.com/wshobson/agents) (MIT). It covers
tooling (ruff, mypy), naming, imports, line length and documentation. This file is MuRet's layer; the upstream skill is
in [`references/upstream-skill.md`](references/upstream-skill.md).

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
| ruff for lint and format | `ruff check .` and `ruff format --check .` in `.github/workflows/bmd-tool.yml`, configured in `Client/tools/mutools/pyproject.toml`. |
| mypy strict | `mypy --strict` over `src` and `tests`, tests included. |
| 120-character lines | Same as the repo's C++ (`.clang-format`). |
| Descriptive names, snake_case, PascalCase, SCREAMING_SNAKE_CASE constants | As is; names say the MU concept (`TEXT_RECORD_SIZE`, `xor3`, `TextTable`), not the byte offset. |
| Absolute imports, grouped | As is (`from mutools.core.pointer import parse_pointer`). |
| README for the project | `Client/tools/mutools/README.md`: install, commands, JSON format, TUI keys. |

## Where `references/` does not apply to MuRet

| The skill says | In MuRet |
| --- | --- |
| Google-style docstrings on every public function | CODING_RULES §9: a comment only when strictly necessary. A docstring goes where the name and the types do not say it all (a research citation like `0x5495e0`, a fidelity quirk like Q-C07), in one or two lines. |
| A CHANGELOG | Git history and PR descriptions are the log. |
| Python 3.10 floor | The tool needs 3.12 (`type` aliases, PEP 695 generics). |

## Origin and license

[`references/upstream-skill.md`](references/upstream-skill.md) is a verbatim copy of
`plugins/python-development/skills/python-code-style/SKILL.md` at commit `c96b5f65` of wshobson/agents, under the MIT
license in [`LICENSE`](LICENSE). The upstream file's links are relative to the skill's root. This `SKILL.md` is an
adaptation. To update: bring the files from a new commit, change the commit in the frontmatter and in this section, and
review both tables.
