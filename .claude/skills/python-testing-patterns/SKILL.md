---
name: python-testing-patterns
description: Use when writing or reviewing Python tests in MuRet (the mubmd tool under Client/tools/bmd, the research scripts) - pytest fixtures, parametrize, fakes, async tests, test-driven development, real-data round trips. Ranked below CODING_RULES §13 (one behavior, one test) and the tool's plan.
license: MIT
metadata:
  upstream: https://github.com/wshobson/agents/tree/c96b5f6552dba32e4492307493ee613581430715/plugins/python-development/skills/python-testing-patterns
---

# Python testing patterns in MuRet

This is the `python-testing-patterns` skill from [wshobson/agents](https://github.com/wshobson/agents) (MIT). It covers
pytest, fixtures, mocking, async tests and TDD. This file is MuRet's layer; the upstream skill is in
[`references/upstream-skill.md`](references/upstream-skill.md), with [`references/details.md`](references/details.md)
and [`references/advanced-patterns.md`](references/advanced-patterns.md).

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
| Arrange, act, assert; one behavior per test | CODING_RULES §13. A test's name says the behavior (`test_a_stale_file_is_not_overwritten`). |
| Fixtures | `tests/conftest.py`: `reference_data`, `working_copy` (a few files copied to `tmp_path`, never the 370 MB), `text_bytes`. |
| Mock external dependencies | Use cases get an in-memory `FileStore`; `os.replace` is patched only to prove a failed write leaves the original. |
| Parametrize | Over the real files of a format (`Text.bmd`, `Texttest.bmd`, `TKOU.bmd`), so a new file in the catalog is tested by adding it. |
| Async tests | Textual's `run_test()` and `Pilot` under pytest-asyncio (`asyncio_mode = "auto"`), plus SVG snapshots with pytest-textual-snapshot. |
| Coverage of error paths | Every `ErrorCode` the CLI can return has a test that reaches it and checks the exit code. |

## What MuRet adds

- **Real data is the oracle.** `Client/assets/Data` is in the repo: every format has a byte-exact round trip over its
  real files, and known values come from the research or the C++ golden tests (text 500 of `Text.bmd`, record 7's 300
  bytes from `GoldenTextTable.LoadsTheRealFile`).
- **The skill's recipes are tests.** `tests/skill/test_recipes.py` runs every recipe of `.claude/skills/muret-bmd`.

## Where `references/` does not apply to MuRet

| The skill says | In MuRet |
| --- | --- |
| Database fixtures, HTTP mocking, property-based testing frameworks | No database or network; property tests only if a codec needs them, and then through a library checked under §10. |

## Origin and license

[`references/upstream-skill.md`](references/upstream-skill.md) is a verbatim copy of
`plugins/python-development/skills/python-testing-patterns/SKILL.md` at commit `c96b5f65` of wshobson/agents (and
`references/details.md` and `references/advanced-patterns.md` from that skill's `references/`), under the MIT license in
[`LICENSE`](LICENSE). The upstream file's links are relative to the skill's root. This `SKILL.md` is an adaptation. To
update: bring the files from a new commit, change the commit in the frontmatter and in this section, and review both
tables.
