---
name: ai-native-cli
description: Use when designing, changing or reviewing a MuRet command-line tool that agents call (mubmd under Client/tools/bmd first) - JSON output by default, the success and error envelopes, error and exit codes, self-description, dry runs, paging and token economy. A CLI design spec for agents, ranked below CODING_RULES and the tool's spec.
license: CC-BY-4.0
metadata:
  upstream: https://github.com/sickn33/agentic-awesome-skills/tree/166f581cfdc37d94cf56efe4454fb06a5759776c/skills/ai-native-cli
---

# CLIs for agents in MuRet

This is the `ai-native-cli` skill from
[sickn33/agentic-awesome-skills](https://github.com/sickn33/agentic-awesome-skills), one of the most-starred skill
collections and the source of the `clean-code` skill; it presents itself as a proposal from
[ChaosRealmsAI/agent-cli-spec](https://github.com/ChaosRealmsAI/agent-cli-spec). It lists 98 rules for CLIs that AI
agents call, in three levels (Agent-Friendly, Agent-Ready, Agent-Native). Emanuel, 2026-11-25: MuRet's tools have to be
AI friendly, with a CLI optimized for agents and a SKILL.md for its use. This file is MuRet's layer; the upstream skill
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

## What MuRet adopts, and how

MuRet adopts the core and recommended levels, with these choices (the tool's spec, "The CLI: an API for agents first",
is the full contract):

| The skill says | In MuRet |
| --- | --- |
| O1-O3: JSON by default, valid, stable within a version | JSON by default with no flag; `--human` for Rich output. The envelope carries `"schema": 1`, and CI checks every command's output against its JSON Schema. |
| E1-E8: structured errors on stderr with `code`, `message`, `suggestion`; codes are a contract | `{"ok": false, "schema": 1, "error": {"code", "message", "suggestion", "input", "pointer"}}`. The codes are a closed `ErrorCode` enum, never renamed within a schema version. |
| X1-X9: exit codes | 0 done, 1 done with findings or differences, 2 usage, 20 not found, 30 precondition. The skill's 10 and 11 (auth, permission) do not exist: there is no auth, and the read-only reference folder is a precondition (30). |
| C1, C2, C6: stdout for data, stderr for the rest, no prompts | As is. Only the `tui` command is interactive. |
| I1-I7: long flags, typed parameters, no ambiguity | Typer declares every parameter with its type; positional arguments are only the file, the JSON Pointer and the value. |
| S1, S3: `--yes` for destructive operations, `--dry-run` | `--dry-run` on every command that writes, returning the diff as JSON. `--yes` only for `workspace init` into a folder that is not empty: other writes keep a `.bak`, check the file did not change since it was read, and never touch the reference folder. |
| D1-D16: self-description | `mubmd` and `--help` return JSON (commands, typed parameters, examples, output schemas, exit and error codes), generated from the Typer app; `--brief` prints the paragraph the project skill opens with; `--human --help` is the classic help. |
| N4: reserved flags, `--fields` | `--human`, `--brief`, `--help`, `--version`, `--yes`, `--dry-run`, `--fields`, plus `--limit` and `--offset` for paging. |
| C7: idempotency | Setting a value a file already has returns `"changed": false` and writes nothing. |

MuRet adds two things the skill does not have: addresses are JSON Pointers (RFC 6901) in every command, and batch edits
are JSON Patch (RFC 6902) limited to `test` and `replace`, so an agent guards each edit with the value it expects.

## What MuRet skips

| The skill says | In MuRet |
| --- | --- |
| Ecosystem level: an `agent/` folder, `skills` subcommand, `skills[]` and `issue` in every response, a local issue system | The project skill (`.claude/skills/muret-bmd`) is the agent's manual, and GitHub issues are the feedback loop. Responses stay small. |
| G2, G3: reject arguments that look like secrets or sensitive paths | The tools take no secrets and read only `.bmd` files and JSON. |
| G8: reject shell metacharacters | Arguments arrive as argv, never through a shell, and MU texts legitimately contain `%`, `&` and `;`. |
| S4: reject `../` paths | Paths are the user's own files; what is guarded is the reference folder, after resolving symlinks. |

## Origin and license

[`references/upstream-skill.md`](references/upstream-skill.md) is a verbatim copy of `skills/ai-native-cli/SKILL.md` at
commit `166f581c` of sickn33/agentic-awesome-skills (formerly antigravity-awesome-skills), whose written content is
licensed under Creative Commons Attribution 4.0 International ([`LICENSE`](LICENSE),
https://creativecommons.org/licenses/by/4.0/). This `SKILL.md` is an adaptation of it ("Agent-Friendly CLI Spec v0.1"),
changed as the tables above say. To update: bring the file from a new commit, change the commit in the frontmatter and
in this section, and review both tables.
