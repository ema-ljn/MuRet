# mubmd

Read, check and edit MU Online's `.bmd` files byte-exact. A file read and written without changes comes back
identical, and an edited file is read by the client as the tool showed it.

- **For agents:** a CLI that prints JSON, with stable error and exit codes and a JSON description of itself. The
  project skill [`muret-bmd`](../../../.claude/skills/muret-bmd/SKILL.md) teaches it.
- **For people:** the same operations in a console UI (`mubmd tui`).

This version reads the text tables (`Local/Text.bmd`, `Texttest.bmd`, `TKOU.bmd`) and the item tables
(`Local/item.bmd`, `itemtest.bmd`, `n380.bmd`). The design is in
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

`workspace init` on a working copy that already exists keeps it and says how many files were edited; `--yes` copies
the reference over it, edits included. A write keeps the file as it was before its first write beside it, as
`<name>.bak`, and writers take turns on a `.mubmd.lock` file in the folder, so several people and agents can edit the
same working copy: the one whose file changed under it writes nothing and is told so (`STALE_FILE`).

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
| `mubmd get <file> <pointer>` | One value, or a page of a list (`--limit`, `--offset`) |
| `mubmd set <file> <pointer> <value>` | One edit |
| `mubmd apply <file> <patch>` | Several edits, all or none (JSON Patch `test` and `replace`) |
| `mubmd validate <file or folder>` | Findings (`--ndjson`) |
| `mubmd diff <a> <b>` | Differences between two files, or a file and a JSON view (`--limit`, `--offset`) |
| `mubmd dump <file> --output <json>` | The whole file as JSON |
| `mubmd build <json> --output <file>` | The reverse |
| `mubmd roundtrip <folder>` | Decode and encode every file and report any that changes |
| `mubmd tui [<folder or file>]` | The console UI |

`set`, `apply`, `build`, `dump` and `workspace init` take `--dry-run`. `--human` anywhere prints tables instead of JSON.
Every list pages the same way: 50 items by default, with `total` and `next_offset` (`null` on the last page).

Output is one JSON object on stdout, `{"ok": true, "schema": 1, "command", "data", "warnings"}`. An error is one JSON
object on stderr, `{"ok": false, "schema": 1, "error": {"code", "message", "suggestion", "input", "pointer"}}`.

| Exit code | Meaning |
| ---: | --- |
| 0 | Done |
| 1 | Done, with findings (`validate`) or differences (`diff`, `roundtrip`) |
| 2 | Bad usage, or a value that does not fit its field |
| 20 | A file, format or pointer that does not exist |
| 30 | A precondition failed: read-only reference, corrupt file, file changed since it was read, a `test` that did not hold |
| 70 | A bug in the tool (`INTERNAL_ERROR`): report the command |

Use JSON Pointers (RFC 6901) in `get`, `set`, and `apply`. `/texts/500` addresses text 500, and `/records/0/name`
addresses the first item name.

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
kept as `{"hex": "<600 hex digits>"}`, and so is one with bytes after its first 0, which the client never shows. A text
holds up to 300 bytes in the file's encoding. `format_version`, `format` and `encoding` describe the file and are not
edited.

The item view has 8,192 `records`, indexed by `section * 512 + index`. Each record includes `name`, `two_handed`,
`level`, `width`, `height`, and `raw_0xNN` fields for the remaining bytes. Item names hold up to 29 encoded bytes
and a NUL terminator. Existing names with undecodable bytes, missing terminators, or nonzero bytes after a terminator
are kept as hex. Use `mubmd schema item` for the field list. The dump includes `records_sha256`, `stored_checksum`,
and `trailing`. Keep this metadata when editing the dump. An unchanged item table keeps its stored checksum and
trailing bytes. Editing a record calculates a new checksum.

## The console UI

`mubmd tui ~/mu/Data` opens the folder: the `.bmd` files on the left, the open table on the right, the selected
record's editor below.

| Key | Action |
| --- | --- |
| `/` | Search the texts or item names |
| `enter` | Edit the selected record; `enter` again keeps the edit, `escape` drops it. The editor counts the bytes and warns (`⚠`) of a text the client would misread |
| `ctrl+z`, `ctrl+y` | Undo, redo |
| `ctrl+s` | Show the changes and their warnings, and write them after `y` |
| `d` | Show the unsaved changes |
| `r` | Read the file again and keep the unsaved edits, after someone else wrote it |
| `t` | Switch between the dark and the light theme |
| `ctrl+p` | Every action, by name |
| `q` | Quit |

A file inside the reference folder opens read-only: it can be searched, not edited.

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
2. Texts end to end.
3. **Record tables.** Items are available. This stage also covers skills, gates, filters, item sets, credits, slide,
   move requirements, monster skills, dialogs, and quests.
4. Models, with the K16 cipher.
5. A wireframe preview of a model in the console UI.
