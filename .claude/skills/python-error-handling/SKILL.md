---
name: python-error-handling
description: Use when designing or reviewing how Python code in MuRet (mutools, under Client/tools/mutools, and the research scripts) validates input and reports errors - fail fast, one exception with stable codes, findings for partial failures, exception chaining. Ranked below CODING_RULES and the tool's spec, whose error codes are a contract.
license: MIT
metadata:
  upstream: https://github.com/wshobson/agents/tree/c96b5f6552dba32e4492307493ee613581430715/plugins/python-development/skills/python-error-handling
---

# Python error handling in MuRet

This is the `python-error-handling` skill from [wshobson/agents](https://github.com/wshobson/agents) (MIT). It covers
early validation, meaningful exceptions, partial failures and preserving context. This file is MuRet's layer; the
upstream skill is in [`references/upstream-skill.md`](references/upstream-skill.md), with
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

## Which idea is which part of mutools

| The skill says | In mutools |
| --- | --- |
| Fail fast, validate at the boundary | The CLI and `from_view` check every value against the format before anything is written; a write is verified by decoding it again. |
| Meaningful, specific exceptions | One `MutoolsError` with an `ErrorCode` (`TEXT_TOO_LONG`, `STALE_FILE`, ...), a message with the limit, a `suggestion` and a JSON Pointer. The codes are an API contract: never renamed within an output schema version. |
| Partial failures do not abort the batch | Findings, not exceptions: `validate` and `roundtrip` over a folder report every file; a warning (`PRINTF_CHANGED`) never stops a write. `apply` is the exception on purpose: all operations or none. |
| Chain exceptions | `raise MutoolsError(...) from error` when an `OSError` or `UnicodeEncodeError` becomes a code. |
| Map errors to standard outcomes | The CLI maps each code to one exit code (2 usage, 20 not found, 30 precondition) and writes the error as JSON on stderr. |
| Test error paths | Every code the CLI can return has a test. |

## Where `references/` does not apply to MuRet

| The skill says | In MuRet |
| --- | --- |
| Pydantic for input validation | Inputs are MU formats, validated by their codecs in `core/` (standard library only). pydantic appears only in the CLI adapter, to emit output JSON Schemas. |
| Logging with context | The CLI writes progress and warnings to stderr; there is no logging stack. |

## Origin and license

[`references/upstream-skill.md`](references/upstream-skill.md) is a verbatim copy of
`plugins/python-development/skills/python-error-handling/SKILL.md` at commit `c96b5f65` of wshobson/agents (and
`references/details.md` from that skill's `references/`), under the MIT license in [`LICENSE`](LICENSE). The upstream
file's links are relative to the skill's root. This `SKILL.md` is an adaptation. To update: bring the files from a new
commit, change the commit in the frontmatter and in this section, and review both tables.
