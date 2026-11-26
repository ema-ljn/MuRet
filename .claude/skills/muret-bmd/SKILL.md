---
name: muret-bmd
description: Use when opening, inspecting, searching, translating or editing a MU `.bmd` table (Text.bmd, item.bmd, skill.bmd, Gate.bmd, Dialog.bmd, quest.bmd, movereq.bmd and the other tables under `Data` and `Data/Local`), when a task names `mubmd` or a game text, item, skill, gate, dialog or quest by its number or name, or when `mubmd` answers with an error code such as REFERENCE_READ_ONLY, TEST_FAILED, INVALID_VALUE or STALE_FILE.
---

# Editing .bmd files with mubmd

mubmd reads, checks and edits MU Online's .bmd files byte-exact (texts and tables). Every command prints one
JSON object on stdout, {ok: true, schema, command, data, warnings}, or one on stderr when it fails, {ok: false, schema,
error: {code, message, suggestion, input, pointer}}, and exits 0, 1 (findings or differences), 2 (usage), 20 (not
found), 30 (precondition) or 70 (a bug in the tool). Places inside a file are JSON Pointers (/texts/500,
/records/1/mana). The reference folder Client/assets/Data is read-only: run `mubmd workspace init ~/mu/Data` once and
edit the copy with `find`, `get`, then `set` or `apply` (a JSON Patch of test and replace). A write is verified before
the file is replaced, and its result is what was written. `mubmd <command> --help` describes one command as JSON.

This version reads every table of the game: the 29 files under [The tables](#the-tables). The other `.bmd` files are
3D models and answer `UNKNOWN_FORMAT` until their stage lands, and so do `Data/Item/item.bmd`,
`Data/Item/Itemtest.bmd` and `Data/skill/skill.bmd`, which are named like a table and are not one (inside a folder
called `Item` or `skill`, a file with the table's name is the table only if it is as large as one). The tool is in
[`Client/tools/bmd`](../../../Client/tools/bmd/README.md) and its contract is the
[spec](../../../docs/superpowers/specs/2026-11-25-bmd-editor-design.md).

## Setup, once

```bash
mubmd --version || pip install -e Client/tools/bmd   # Python 3.12+, in a virtual environment
mubmd workspace init ~/mu/Data                        # from the repo: copies 370 MB and checks every file
```

If a working copy already exists, use it. Any `Data` folder with no `.sha256` manifest beside it is a working copy,
and `workspace init` on one keeps it as it is: `changed` is `false` and `edited` counts the files that differ from the
reference. The client runs it with `Main --data ~/mu/Data`. People edit the same folder with `mubmd tui ~/mu/Data`;
agents use the commands.

## The tables

A table is a list of records, and a record's number is the game's own number, from 0: text 500 is `/texts/500`, skill
1 (Poison) is `/records/1`, dialog 53 is `/records/53`. The files are in `Data/Local`, except the gates, which are in
`Data`.

| Format | Files | A record is | One pointer |
| --- | --- | --- | --- |
| `text` | `Text.bmd`, `Texttest.bmd`, `TKOU.bmd` | a text of up to 300 bytes; 2,048 | `/texts/500` |
| `item` | `item.bmd`, `itemtest.bmd`, `n380.bmd` | an item, at `section * 512 + index`; 8,192 | `/records/0/durability` |
| `skill` | `skill.bmd`, `Skilltest.bmd` | a skill; 256 | `/records/1/mana` |
| `gate` | `Data/Gate.bmd`, `Data/Gatetest.bmd` | a gate between maps; 256 | `/records/18/level` |
| `filter` | `Filter.bmd` | a forbidden word; 1,000 | `/records/0/word` |
| `filter_name` | `FilterName.bmd` | a word a character's name may not hold; 1,000 | `/records/0/word` |
| `item_set_type` | `itemsettype.bmd`, `itemsettypetest.bmd` | the sets of the item with that number; 8,192 | `/records/0/sets/0` |
| `item_set_option` | `itemsetoption.bmd`, `itemsetoptiontest.bmd` | a set: its name and options; 64 | `/records/1/name` |
| `credit` | `credit.bmd`, `credittest.bmd` | a line of the credits: `kind` and `text`; 400 | `/records/0/text` |
| `slide` | `slide.bmd`, `slidetest.bmd` | the whole file, record 0: the notices by level | `/records/0/groups/0/texts/0` |
| `move_requirement` | `movereq.bmd`, `movereqtest.bmd`, `TKOUREQ.bmd` | a `/move` destination; as many as the file counts (28) | `/records/0/level` |
| `monster_skill` | `MonsterSkill.bmd`, `MonsterSkilltest.bmd` | a monster type and its 5 skills; counted (8) | `/records/0/skills/0` |
| `dialog` | `Dialog.bmd`, `Dialogtest.bmd` | an NPC's line and its answers; 200 | `/records/53/answers/0` |
| `quest` | `quest.bmd`, `questtest.bmd` | a quest: name, 16 entries, 16 conditions; 200 | `/records/0/name` |

- **The fields of a record:** `get FILE /records/N` shows one record whole, with every field's name. `mubmd schema
  FORMAT` adds each field's type and size, and "the client handles at most 10" where the client has a limit of its own.
- **The client reads the first file of each row.** The others (`*test.bmd`, `TKOU*.bmd`, `n380.bmd`) are other
  editions, most of them in Korean (cp949) or Chinese (gb2312): change them only when the task names them. `mubmd
  formats` lists every file with its encoding.
- **A `raw_0xNN` field** is bytes the research has not named yet. Leave it as it is.
- **The filters end at their first empty record.** The client reads the words of `Filter.bmd` and `FilterName.bmd`
  from record 0 up to the first empty one, and nothing after it, whatever the file holds there: add a word in that
  first empty record, and never leave a gap.
- **Gates.** `flag` 1 is an entrance, 2 an exit and 0 a `/move` destination. An entrance's `target` is the record of
  the exit it leads to, and its `level` is the level it asks for. `map` is a map number: 0 Lorencia, 1 Dungeon, 2
  Devias, 3 Noria. For another map, `find` its name in `movereq.bmd` and read the `map` of that record's `gate`.
- **The data spells names its own way** ("Davias", "Lighting"). Search a fragment (`vias`) before deciding a name is
  not there.

## One job, one call

| Job | Call | What you need from `data` |
| --- | --- | --- |
| Change one text | `set FILE /texts/N 'new text'` | `changed`, `changes: [{pointer, old, new}]` |
| Change one number | `set FILE /records/N/FIELD 45` | the same |
| Change several, all or none | `apply FILE '[{"op": "test", ...}, {"op": "replace", ...}]'` | the same |
| Read one text | `get FILE /texts/N` | `value` |
| Read one record whole | `get FILE /records/N` | `value`: every field |
| Read records N to N+M-1 | `get FILE /records --offset N --limit M` (`/texts` in a text table) | `value`: item `i` is record `N + i`; `next_offset` |
| Find a record by a text it holds | `find FILE QUERY` | `items: [{index, pointer, text}]`: `index` is the record |
| Find in one field only | `find FILE QUERY --field name` | the same |
| Every place a query appears | `find FILE QUERY --fields pointer --limit 8192` | `items: [{pointer}]`, `total` |
| Pick records by a number | `dump FILE --output OUT.json`, then a script | the file's `records` |
| Check a file or a folder | `validate PATH` | `files: [{path, findings}]`, `checked`, `skipped` |
| What changed since the reference | `diff REFERENCE_FILE FILE` | `changes: [{pointer, old, new}]`, `total` |

On `set`, a value for a text is taken as written, with no quotes of its own, and any other value is JSON: `45`,
`-1`, `2.5`, `{"hex": "00"}`. In a patch every value is JSON: `"Veneno"` for a text, `45` for a number.

The result of `set` and `apply` is the file as it is now. Do not read it back, and do not run the command with
`--dry-run` first: the write was verified, and `warnings` already carries any finding at the changed places. The `new`
of a change is as good as a `get` for a later `test`, and its `old` is the undo: `set` it back. `--dry-run` is for
showing someone a batch before it is written.

`find` looks in every text of a record (a name, a dialog's line and its answers) and never in its numbers. Every list
pages the same way: 50 items by default, `--limit` and `--offset` to move, `total` for the whole count and
`next_offset`, which is `null` on the last page. That holds for `find`, `get` on a list, `diff` and the `changes` of
`build`. On `get`, `truncated` only says that `value` is a page and not the whole list: a text is never cut.

## Recipes

`$WORK` is the working copy, `$REF` is `Client/assets/Data` and `$TMP` is any scratch folder. A test runs each recipe
on its own fresh working copy, so the lines are the tool's real syntax and the values are the reference's: one recipe
does not see another's edits. `# exit 1` marks a call that ends with 1.

Change one text. Put `--` before a value that starts with `-`.

<!-- recipe: change-one-text -->
```bash
mubmd set $WORK/Local/Text.bmd /texts/500 'El jugador salió del juego'
```

Change one number of a record. Saving a table that has a checksum (items, skills, filters, item sets) writes the new
checksum too.

<!-- recipe: change-an-item-field -->
```bash
mubmd set $WORK/Local/item.bmd /records/0/durability 22
```

Find a record by its name and change one of its fields. `find` gives the record in `index`, and a `get` of the
record shows every field, so no `schema` call is needed to learn their names. When the task already gives the
record's number, the `apply` alone is enough: its `test` on the name proves the record is the one you mean.

<!-- recipe: find-a-skill-and-change-it -->
```bash
mubmd find $WORK/Local/skill.bmd poison --field name
mubmd get $WORK/Local/skill.bmd /records/1
mubmd apply $WORK/Local/skill.bmd '[{"op":"test","path":"/records/1/name","value":"Poison"},{"op":"replace","path":"/records/1/mana","value":45}]'
```

Rename something with a guard: `test` the name the tool last showed you, then `replace` it. If the name is no longer
that value, nothing is written and the call ends with `TEST_FAILED` (exit 30).

<!-- recipe: rename-an-item -->
```bash
mubmd apply $WORK/Local/item.bmd '[{"op":"test","path":"/records/0/name","value":"Kris"},{"op":"replace","path":"/records/0/name","value":"Training Kris"}]'
```

<!-- recipe: rename-a-skill -->
```bash
mubmd apply $WORK/Local/skill.bmd '[{"op":"test","path":"/records/1/name","value":"Poison"},{"op":"replace","path":"/records/1/name","value":"Veneno"}]'
```

Translate or rewrite several texts in one call. `test` each text against the value the tool last showed you (a `get`,
a `find`, or the `new` of your own earlier write; read only the ones you have not seen), then `replace` it. The patch
is the argument itself, a file, or `-` for stdin.

<!-- recipe: translate-several-texts -->
```bash
mubmd get $WORK/Local/Text.bmd /texts --offset 500 --limit 2
mubmd apply $WORK/Local/Text.bmd '[{"op": "test", "path": "/texts/500", "value": "El usuario salió del juego"}, {"op": "test", "path": "/texts/501", "value": "El usuario ya está en otra party"}, {"op": "replace", "path": "/texts/500", "value": "The user left the game"}, {"op": "replace", "path": "/texts/501", "value": "The user is already in another party"}]'
```

Translate a dialog's answers: one `get` of the record, one `apply`. A patch that holds an apostrophe goes on stdin,
through a quoted heredoc, because it cannot sit inside single quotes. An answer holds 63 bytes and the line 299.
The line of this record comes as `{"text", "tail"}`: see the rule on the three shapes of a text.

<!-- recipe: translate-a-dialog -->
```bash
mubmd get $WORK/Local/Dialog.bmd /records/53
mubmd apply $WORK/Local/Dialog.bmd - <<'PATCH'
[{"op": "test", "path": "/records/53/answers/0", "value": "Please repeat the story."},
 {"op": "test", "path": "/records/53/answers/1", "value": "I will go find it."},
 {"op": "test", "path": "/records/53/answers/2", "value": "I don't care."},
 {"op": "replace", "path": "/records/53/answers/0", "value": "Contame la historia de nuevo."},
 {"op": "replace", "path": "/records/53/answers/1", "value": "Voy a buscarlo."},
 {"op": "replace", "path": "/records/53/answers/2", "value": "No me interesa."}]
PATCH
```

Find texts. `find` ignores case and returns 50 items by default. `total` counts every match whatever the `--limit`,
and `next_offset` is `null` when the items you got are all of them. To match case exactly, or a pattern, add
`--regex`: the query is then a Python regular expression, so escape `.`, `(`, `?` and the like in a literal one.

<!-- recipe: find-texts -->
```bash
mubmd find $WORK/Local/Text.bmd usuario --limit 5
mubmd find $WORK/Local/Text.bmd '^Zen' --regex
mubmd find $WORK/Local/Dialog.bmd 'find it' --field answers
```

List every place a query appears, in one call, without the texts. `--limit` has no maximum, so with one as large as
the table `next_offset` comes back `null` and the list is complete. The match ignores case (`%d` also finds `%D`): use
`--regex` in that same call only when the task tells the two apart.

<!-- recipe: list-every-pointer -->
```bash
mubmd find $WORK/Local/Text.bmd %d --fields pointer --limit 2048
```

List the names a table uses: the empty records have no text, so `.` as a regular expression matches the used ones.

<!-- recipe: list-the-used-names -->
```bash
mubmd find $REF/Local/skill.bmd . --regex --fields pointer,text --limit 256
```

Read a gate and where it leads: gate 18 is the entrance from Lorencia to Devias, and its `target`, 19, is the exit.

<!-- recipe: read-a-gate -->
```bash
mubmd get $REF/Gate.bmd /records --offset 18 --limit 2
```

Pick records by a number. `find` does not look at numbers, so write the table to a file and filter it with a script;
do not print it (a gate table is 43 KB of JSON, an item table 8 MB).

<!-- recipe: pick-records-by-a-number -->
```bash
mubmd dump $REF/Gate.bmd --output $TMP/gate.json
```

```bash
python3 -c 'import json, sys
for number, gate in enumerate(json.load(open(sys.argv[1]))["records"]):
    if gate["flag"] == 1 and gate["map"] == 0:
        print(number, gate)' $TMP/gate.json
```

Check a file or a whole folder. Exit 1 means a finding is a warning or an error, not that the call failed; `info`
findings alone exit 0. The reference's own findings are known, so a working copy starts with them: `TEXT_FILLS_RECORD`
at text 7 of `Text.bmd` and texts 7 and 714 of `Texttest.bmd`, `CHECKSUM_MISMATCH` in `item.bmd` and `n380.bmd` (the
first write to the file corrects it) and `TEXT_HAS_TAIL` at text 1829 of `TKOU.bmd` (info).

<!-- recipe: check-a-working-copy -->
```bash
mubmd info $WORK/Local/Text.bmd
mubmd validate $WORK/Local/skill.bmd
mubmd validate $WORK/Local/Text.bmd   # exit 1
mubmd validate $WORK                  # exit 1
```

See what a working copy changed. Exit 1 means the files differ.

<!-- recipe: compare-with-the-reference -->
```bash
mubmd diff $REF/Local/Text.bmd $WORK/Local/Text.bmd
mubmd set $WORK/Local/Text.bmd /texts/100 Zen!
mubmd diff $REF/Local/Text.bmd $WORK/Local/Text.bmd   # exit 1
```

Edit a whole table as JSON, for a change too large for a patch. `dump` writes the view (66 KB for a text table: edit
it with a script, do not print it), `diff` shows what the edited JSON would change, and `build` writes it.

<!-- recipe: edit-through-json -->
```bash
mubmd dump $WORK/Local/Text.bmd --output $TMP/text.json
mubmd diff $WORK/Local/Text.bmd $TMP/text.json
mubmd build $TMP/text.json --output $WORK/Local/Text.bmd --dry-run
```

Make a working copy from somewhere that is not the repo, and check that a change to the tool keeps every file
byte-exact.

<!-- recipe: working-copy-and-roundtrip -->
```bash
mubmd workspace init $TMP/mu/Data --from $REF --dry-run
mubmd roundtrip $WORK
```

## Rules

- **Never write the reference folder.** `Client/assets/Data` is the original, and the tool refuses
  (`REFERENCE_READ_ONLY`). Do not copy a file over it by hand either.
- **`test` before `replace`.** A value you read a moment ago may have changed; the `test` is what makes the edit safe.
  `set` has no guard: use it when you do not depend on the old value.
- **Keep the `printf` specifiers.** The client passes many texts to `sprintf`, so `%d` and `%s` stay, in the same
  order. A write that changes them succeeds with a `PRINTF_CHANGED` warning: read `warnings` and fix the text.
- **A text has a size in bytes, in the file's encoding**, and only characters that encoding has: 300 for a text of
  `Text.bmd`, and for a field what `schema` says, less one for the 0 that ends it (a 32-byte name holds 31). In
  cp1252 an accented letter is one byte, and there are no emoji or CJK characters.
- **A number has two limits: its field and the client.** One outside its field (70000 in two bytes) is refused with
  `INVALID_VALUE`. One that fits the field and is more than the client handles (an `answer_count` of 11) is
  written, with an `OUT_OF_RANGE` error in `warnings`, and `validate` exits 1 until it is fixed: read `warnings`
  after every write.
- **A text can come in three shapes.** A string is the usual one. `{"text": "...", "tail": "<hex>"}` is a text with
  leftover bytes after the 0 that ends it, which the client never shows: `test` it with that exact object, and
  `replace` it with a plain string, which clears the leftover. `{"hex": "..."}` is a text the file's encoding could
  not read: leave it alone, or `replace` it with a string to make it a text again. Its value is JSON, so on `set` a
  string needs its quotes: `set FILE /texts/N '"new text"'`.
- **Save tokens.** `--fields` keeps only the keys you name, `--limit` and `--offset` page, and `validate` on a folder
  lists only the files with findings. `mubmd --help` with no command is 21 KB: ask for one command's help.
- **Several writers can share a working copy.** They take turns on the folder's `.mubmd.lock`, and the one whose file
  changed under it writes nothing and gets `STALE_FILE`. Leave `.mubmd.lock` and `<name>.orig` (the file as it was
  before its first write) where they are; the `.bak` files are the game's own.
- **After changing a codec**, run `mubmd roundtrip Client/assets/Data`: `differing` must be empty.

## Errors and the way out

An error has no `data`, and nothing was written. Read `error.code` and `error.message`; `error.suggestion`, when it
is not `null`, is the next command to run or the flag to use.

| Code | Exit | The way out |
| --- | ---: | --- |
| `USAGE` | 2 | `mubmd <command> --help` |
| `INVALID_VALUE` | 2 | The value does not fit: a text takes a string, a number field a number inside its size, a hex field exactly its bytes, a patch is a list, `--field` a name `schema` lists |
| `TEXT_TOO_LONG` | 2 | Shorten the text; the message gives its size and the field's in bytes |
| `NOT_ENCODABLE` | 2 | Replace the character the file's encoding does not have |
| `FILE_NOT_FOUND` | 20 | Check the path; names keep the case they have on disk, and the gates are in `Data`, not `Data/Local` |
| `UNKNOWN_FORMAT` | 20 | The file is a model or not a table: see [The tables](#the-tables) |
| `POINTER_NOT_FOUND` | 20 | `get FILE /records/N` shows the fields a record has; `mubmd schema FORMAT` lists them all |
| `REFERENCE_READ_ONLY` | 30 | Use the working copy: `mubmd workspace init ~/mu/Data` |
| `TEST_FAILED` | 30 | `get` the pointer in `error.pointer`, decide again with its real value, send the patch again |
| `STALE_FILE` | 30 | Someone wrote the file while the command ran: run it again |
| `NOT_EMPTY` | 30 | The destination of `workspace init` is not a working copy: choose another folder |
| `CORRUPT_FILE` | 30 | The file is not a table of its format; copy it again from the reference |
| `WRITE_VERIFY_FAILED` | 30 | A bug in the tool: stop and report the command |
| `IO_ERROR` | 30 | The disk, its permissions, or another writer holding the folder for 10 s; the message says which |
| `INTERNAL_ERROR` | 70 | A bug in the tool: stop and report the command and the message |

## What an agent without this skill got wrong

| It did | Do this |
| --- | --- |
| Read `mubmd --help` whole, or ran `formats` and `schema`, to learn the tool | Use the tables above; ask for one command's help when a flag is missing |
| Ran every write with `--dry-run` first | Write once: the result is the dry run's, plus the write |
| Read a value back after writing it | `changes` and `warnings` are the proof |
| Read two neighbours with two `get` calls | `get FILE /records --offset N --limit M` |
| Took the first 50 matches for all of them | `next_offset: null` means you have them all; if not, raise `--limit` |
| Sent a patch with a wrong `test` to see the guard work | The guard is tested in the tool; trust `TEST_FAILED` |
| Looked for `Gate.bmd` in `Data/Local` | The gates are in `Data` |
| Printed all 256 gates to pick a few by their numbers | `dump --output`, then a script |
| Searched "Devias" and found nothing | The data says "Davias": search a fragment |
