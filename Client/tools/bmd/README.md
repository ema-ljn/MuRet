# mubmd

Read, check and edit MU Online's `.bmd` files byte-exact. A file read and written without changes comes back
identical, and an edited file is read by the client as the tool showed it.

- **For agents:** a CLI that prints JSON, with stable error and exit codes and a JSON description of itself. The
  project skill [`muret-bmd`](../../../.claude/skills/muret-bmd/SKILL.md) teaches it.
- **For people:** the same operations in a console UI (`mubmd tui`).

This version reads every table of the game, 29 files in 14 formats ([the list](#the-tables)); the 3D models are the
next stage. The design is in
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
`<name>.orig` (the `.bak` files in the folder are the game's own), and writers take turns on a `.mubmd.lock` file in
the folder, so several people and agents can edit the same working copy: the one whose file changed under it writes
nothing and is told so (`STALE_FILE`).

## The tables

The files are in `Data/Local`, except the gates, which are in `Data`. A record's number is the game's own, from 0.

| Format | Files | Records | A record is |
| --- | --- | ---: | --- |
| `text` | `Text.bmd`, `Texttest.bmd`, `TKOU.bmd` | 2,048 | a text of up to 300 bytes |
| `item` | `item.bmd`, `itemtest.bmd`, `n380.bmd` | 8,192 | an item, at `section * 512 + index` |
| `skill` | `skill.bmd`, `Skilltest.bmd` | 256 | a skill |
| `gate` | `Gate.bmd`, `Gatetest.bmd` | 256 | a gate between maps |
| `filter`, `filter_name` | `Filter.bmd`, `FilterName.bmd` | 1,000 | a forbidden word |
| `item_set_type` | `itemsettype.bmd`, `itemsettypetest.bmd` | 8,192 | the sets of the item with that number |
| `item_set_option` | `itemsetoption.bmd`, `itemsetoptiontest.bmd` | 64 | a set: its name and options |
| `credit` | `credit.bmd`, `credittest.bmd` | 400 | a line of the credits |
| `slide` | `slide.bmd`, `slidetest.bmd` | 1 | the whole file: the notices by level |
| `move_requirement` | `movereq.bmd`, `movereqtest.bmd`, `TKOUREQ.bmd` | the file's count | a `/move` destination |
| `monster_skill` | `MonsterSkill.bmd`, `MonsterSkilltest.bmd` | the file's count | a monster type and its skills |
| `dialog` | `Dialog.bmd`, `Dialogtest.bmd` | 200 | an NPC's line and its answers |
| `quest` | `quest.bmd`, `questtest.bmd` | 200 | a quest: name, entries, conditions |

The client reads the first file of each row; the others are other editions, most of them in Korean (cp949) or Chinese
(gb2312).
`mubmd formats` lists every file with its encoding and `mubmd schema <format>` every field with its type and size.
`Data/Item/item.bmd`, `Data/Item/Itemtest.bmd` and `Data/skill/skill.bmd` are named like a table and are not one:
they answer `UNKNOWN_FORMAT`, as the models do.

## Commands

| Command | What it does |
| --- | --- |
| `mubmd` or `mubmd --help` | Describes every command as JSON: parameters, an example, the output's JSON Schema, exit and error codes |
| `mubmd --brief` | One paragraph on how to use the tool |
| `mubmd formats` | The formats this version reads and their files |
| `mubmd schema <format>` | The fields of a format |
| `mubmd workspace init <dest>` | Copy the reference `Data` folder and check it |
| `mubmd info <file>` | Format, encoding, cipher, records, findings |
| `mubmd find <file> <query>` | Texts that contain the query (`--regex`, `--field`, `--limit`, `--offset`, `--fields`) |
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

Use JSON Pointers (RFC 6901) in `get`, `set`, and `apply`. `/texts/500` addresses text 500, `/records/1/mana` the
mana of skill 1 and `/records/53/answers/0` the first answer of dialog 53. On `set`, a value for a text is taken as
written and any other value is JSON (`45`, `2.5`, `{"hex": "00"}`).

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

Every other table is a list of `records`, each an object with the names the research gives its fields:

```json
{
  "format_version": 1,
  "format": "dialog",
  "encoding": "cp1252",
  "records": [
    {
      "text": {"text": "Aew! Precisa de alguma coisa?", "tail": "643f"},
      "answer_count": 0,
      "next": [-1, 0, 0, 0, 0, 0, 0, 0, 0, 0],
      "actions": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
      "answers": ["", "", "", "", "", "", "", "", "", ""]
    }
  ],
  "trailing": ""
}
```

- **A text field has three shapes.** A string is the usual one. `{"text": "...", "tail": "<hex>"}` is a text with
  leftover bytes after the 0 that ends it, which the client never shows: the tool keeps them, and writing a plain
  string clears them. `{"hex": "..."}` is a text the file's encoding cannot read, or one with no 0 to end it.
- **A text field holds one byte less than its size**, for the 0 that ends it: a 32-byte name holds 31.
- **Numbers are JSON numbers.** One outside its field is refused (`INVALID_VALUE`); one the field holds and the
  client does not handle, such as an `answer_count` over 10, is written and reported as `OUT_OF_RANGE`.
- **Lists have a fixed length**, and a group of fields is a nested object: `/records/0/entries/0/dialogs/3` in a
  quest. The bytes the research has not named yet are `raw_0xNN` fields, kept as `{"hex": "..."}`.
- **A table with a checksum** (items, skills, filters, item sets) also has `records_sha256` and `stored_checksum`:
  keep them when editing a dump. An unchanged table keeps its stored checksum, even a wrong one, and a changed one
  gets a new one. `trailing` is what the file has after its records, kept as it is.
- **A counted table** (move requirements, monster skills) starts with its number of records, so `build` takes a
  list of any length and writes the count.

## The console UI

`mubmd tui ~/mu/Data` opens the folder: the `.bmd` files on the left (`≡` a text table, `▤` a record table, dimmed
what this version does not read), the open table on the right, and the editor below. A record table also shows the
fields of the selected record between the two.

| Key | Action |
| --- | --- |
| `/` | Search every text of the rows |
| `f` | Filter the rows: all, used, changed, with findings |
| `enter` | On a text, edit it. On a record, go to its fields, and on a field, edit it. `enter` again keeps the edit, `escape` drops it and goes back one step. The editor counts the bytes and warns (`⚠`) of a value the client would misread |
| `ctrl+z`, `ctrl+y` | Undo, redo |
| `ctrl+s` | Show the changes and their warnings, and write them after `y` |
| `d` | Show the unsaved changes |
| `r` | Read the file again and keep the unsaved edits, after someone else wrote it |
| `t` | Switch between the dark and the light theme |
| `ctrl+p` | Every action, by name |
| `q` | Quit |

The editor takes a text as written and a number as a number. A field kept as `{"hex": "..."}` is not edited
there: use `mubmd set`. A file inside the reference folder opens read-only: it can be searched, not edited.

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

Stages 1 to 3 are done.

1. Design and skills.
2. Texts end to end.
3. Every table: items, skills, gates, filters, item sets, credits, the slide, move requirements, monster skills,
   dialogs and quests.
4. Models, with the K16 cipher.
5. A wireframe preview of a model in the console UI.
