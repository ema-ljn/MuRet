# mubmd

Read, check and edit MU Online's `.bmd` files byte-exact. A file read and written without changes comes back
identical, and an edited file is read by the client as the tool showed it.

- **For agents:** a CLI that prints JSON, with stable error and exit codes and a JSON description of itself. The
  project skill [`muret-bmd`](../../../.claude/skills/muret-bmd/SKILL.md) teaches it.
- **For people:** the same operations in a console UI (`mubmd tui`).

This version reads the text tables (`Local/Text.bmd`, `Texttest.bmd`, `TKOU.bmd`). The other tables and the models
come in the next stages. The design is in
[`docs/superpowers/specs/2026-11-25-bmd-editor-design.md`](../../../docs/superpowers/specs/2026-11-25-bmd-editor-design.md).

## Install

Python 3.12 or newer.

```bash
python3 -m venv .venv && . .venv/bin/activate
pip install -e "Client/tools/bmd[dev]"
mubmd --version
```

## The working copy

`Client/assets/Data` is the reference: a folder with a `.sha256` manifest beside it is never written. Edit a copy:

```bash
mubmd workspace init ~/mu/Data            # copies Client/assets/Data (about 370 MB) and checks every file
mubmd tui ~/mu/Data
```

Any `Data` folder without a manifest beside it works as a working copy. The client runs it with
`Main --data ~/mu/Data`.

## Commands

| Command | What it does |
| --- | --- |
| `mubmd` or `mubmd --help` | Describes every command as JSON: parameters, an example, the output's JSON Schema, exit and error codes |
| `mubmd --brief` | One paragraph on how to use the tool |
| `mubmd formats` | The formats this version reads and their files |
| `mubmd schema <format>` | The fields of a format |
| `mubmd workspace init <dest>` | Copy the reference `Data` folder and check it |
| `mubmd info <file>` | Format, encoding, cipher, records, findings |
| `mubmd find <file> <query>` | Texts that contain the query (`--regex`, `--limit`, `--offset`, `--fields`) |
| `mubmd get <file> <pointer>` | One value or subtree |
| `mubmd set <file> <pointer> <value>` | One edit |
| `mubmd apply <file> <patch>` | Several edits, all or none (JSON Patch `test` and `replace`) |
| `mubmd validate <file or folder>` | Findings (`--ndjson`) |
| `mubmd diff <a> <b>` | Differences between two files, or a file and a JSON view |
| `mubmd dump <file> --output <json>` | The whole file as JSON |
| `mubmd build <json> --output <file>` | The reverse |
| `mubmd roundtrip <folder>` | Decode and encode every file and report any that changes |
| `mubmd tui [<folder or file>]` | The console UI |

`set`, `apply`, `build`, `dump` and `workspace init` take `--dry-run`. `--human` anywhere prints tables instead of JSON.

Output is one JSON object on stdout, `{"ok": true, "schema": 1, "command", "data", "warnings"}`. An error is one JSON
object on stderr, `{"ok": false, "schema": 1, "error": {"code", "message", "suggestion", "input", "pointer"}}`.

| Exit code | Meaning |
| ---: | --- |
| 0 | Done |
| 1 | Done, with findings (`validate`) or differences (`diff`, `roundtrip`) |
| 2 | Bad usage, or a value that does not fit its field |
| 20 | A file, format or pointer that does not exist |
| 30 | A precondition failed: read-only reference, corrupt file, file changed since it was read, a `test` that did not hold |

Places inside a file are JSON Pointers (RFC 6901): `/texts/500` is text 500 in `get`, `set`, `apply`, the findings and
the dumped JSON.

## The JSON view

```json
{
  "format_version": 1,
  "format": "text",
  "encoding": "cp1252",
  "texts": ["", "Atencao !!! Hack detectado, ...", "..."]
}
```

Every record is there, so an index in `texts` is the text's number in the client. A record its encoding cannot read is
kept as `{"hex": "<600 hex digits>"}`. A text holds up to 300 bytes in the file's encoding.

## The console UI

`mubmd tui ~/mu/Data` opens the folder: the `.bmd` files on the left, the open table on the right, the selected
record's editor below.

| Key | Action |
| --- | --- |
| `/` | Search the texts |
| `enter` | Edit the selected record; `enter` again keeps the edit, `escape` drops it |
| `ctrl+z`, `ctrl+y` | Undo, redo |
| `ctrl+s` | Show the changes and write them after `y` |
| `d` | Show the unsaved changes |
| `t` | Switch between the dark and the light theme |
| `ctrl+p` | Every action, by name |
| `q` | Quit |

## Development

```bash
cd Client/tools/bmd
ruff check . && ruff format --check . && mypy src tests && pytest
```

The layers are `core` (formats, documents, checks; standard library only), `app` (use cases) and `adapters` (file
system, CLI, console UI); `tests/test_layers.py` keeps them apart. The snapshot tests compare the console UI against
`tests/adapters/tui/__snapshots__/`; after a change that is meant to be seen, look at `snapshot_report.html` and run
`pytest --snapshot-update`.

## Stages

1. Design and skills.
2. **Texts end to end (this version).**
3. Every table: items, skills, gates, filters, item sets, credits, slide, move requirements, monster skills,
   dialogs, quests.
4. Models, with the K16 cipher.
5. A wireframe preview of a model in the console UI.
