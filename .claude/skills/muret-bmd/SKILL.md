---
name: muret-bmd
description: Use when opening, inspecting, searching, translating or editing a MU `.bmd` file (Text.bmd, item.bmd and, as stages land, skills, gates, dialogs, quests, models), when a task names `mubmd`, a game text by its number or a file under `Data/Local`, or when `mubmd` answers with an error code such as REFERENCE_READ_ONLY, TEST_FAILED or STALE_FILE.
---

# Editing .bmd files with mubmd

mubmd reads, checks and edits MU Online's .bmd files byte-exact (texts and items). Every command prints one
JSON object on stdout, {ok: true, schema, command, data, warnings}, or one on stderr when it fails, {ok: false, schema,
error: {code, message, suggestion, input, pointer}}, and exits 0, 1 (findings or differences), 2 (usage), 20 (not
found), 30 (precondition) or 70 (a bug in the tool). Places inside a file are JSON Pointers (/texts/500). The reference
folder Client/assets/Data is read-only: run `mubmd workspace init ~/mu/Data` once and edit the copy with `find`, `get`,
then `set` or `apply` (a JSON Patch of test and replace). A write is verified before the file is replaced, and its
result is what was written. `mubmd <command> --help` describes one command as JSON.

This version reads the text tables: `Local/Text.bmd` (cp1252, the one the client reads), `Local/Texttest.bmd` and
`Local/TKOU.bmd` (gb2312). Each holds 2,048 texts, `/texts/0` to `/texts/2047`, of up to 300 bytes in the file's
encoding. `Local/item.bmd`, `itemtest.bmd`, and `n380.bmd` have 8,192 item records. Their names are addressed as
`/records/<section * 512 + index>/name` and hold up to 29 encoded bytes. Other `.bmd` files answer `UNKNOWN_FORMAT`
until their stage lands. The tool is in
[`Client/tools/bmd`](../../../Client/tools/bmd/README.md) and its contract is the
[spec](../../../docs/superpowers/specs/2026-11-25-bmd-editor-design.md).

## Setup, once

```bash
mubmd --version || pip install -e Client/tools/bmd   # Python 3.12+, in a virtual environment
mubmd workspace init ~/mu/Data                        # from the repo: copies 370 MB and checks every file
```

If a working copy already exists, use it. Any `Data` folder with no `.sha256` manifest beside it is one, and
`workspace init` on one keeps it as it is: `changed` is `false` and `edited` counts the files that differ from the
reference. The client runs it with `Main --data ~/mu/Data`. People edit the same folder with `mubmd tui ~/mu/Data`;
agents use the commands.

## One job, one call

| Job | Call | What you need from `data` |
| --- | --- | --- |
| Change one text | `set FILE /texts/N 'new text'` | `changed`, `changes: [{pointer, old, new}]` |
| Change several, all or none | `apply FILE '[{"op": "test", ...}, {"op": "replace", ...}]'` | the same |
| Read one text | `get FILE /texts/N` | `value` |
| Read texts N to N+M-1 | `get FILE /texts --offset N --limit M` | `value`: item `i` is text `N + i`; `next_offset` |
| Find texts | `find FILE QUERY` | `items: [{index, pointer, text}]`, `total`, `next_offset` |
| Every place a query appears | `find FILE QUERY --fields pointer --limit 2048` | `items: [{pointer}]`, `total` |
| Check a file or a folder | `validate PATH` | `files: [{path, findings}]`, `checked`, `skipped` |
| What changed since the reference | `diff REFERENCE_FILE FILE` | `changes: [{pointer, old, new}]`, `total` |

The result of `set` and `apply` is the file as it is now. Do not read it back, and do not run the command with
`--dry-run` first: the write was verified, and `warnings` already carries any finding at the changed places. The `new`
of a change is as good as a `get` for a later `test`, and its `old` is the undo: `set` it back. `--dry-run` is for
showing someone a batch before it is written.

Every list pages the same way: 50 items by default, `--limit` and `--offset` to move, `total` for the whole count and
`next_offset`, which is `null` on the last page. That holds for `find`, `get` on a list, `diff` and the `changes` of
`build`. On `get`, `truncated` only says that `value` is a page and not the whole list: a text is never cut.

## Recipes

`$WORK` is the working copy, `$REF` is `Client/assets/Data` and `$TMP` is any scratch folder. A test runs each recipe
on its own fresh working copy, so the lines are the tool's real syntax and the values are the reference's: one recipe
does not see another's edits. `# exit 1` marks a call that ends with 1.

Read an item name and inspect its fields.

<!-- recipe: item-name -->
```bash
mubmd schema item
mubmd get $REF/Local/item.bmd /records/0/name
```

Rename the first item in a working copy after reading its current value with `get`. The example uses the reference's
name. If `get` returned a hex object for the name, pass that exact object as the `test` value. Saving a changed record
updates the checksum and keeps any trailing bytes.

```bash
mubmd apply $WORK/Local/item.bmd '[{"op":"test","path":"/records/0/name","value":"Kris"},{"op":"replace","path":"/records/0/name","value":"Training Kris"}]'
```

Change one text. A value for a text is taken as written; put `--` before one that starts with `-`.

<!-- recipe: change-one-text -->
```bash
mubmd set $WORK/Local/Text.bmd /texts/500 'El jugador salió del juego'
```

Translate or rewrite several texts in one call. `test` each text against the value the tool last showed you (a `get`,
a `find`, or the `new` of your own earlier write; read only the ones you have not seen), then `replace` it: if any
text is no longer that value, nothing is written and the call ends with `TEST_FAILED` (exit 30). The patch is the
argument itself, a file, or `-` for stdin.

<!-- recipe: translate-several-texts -->
```bash
mubmd get $WORK/Local/Text.bmd /texts --offset 500 --limit 2
mubmd apply $WORK/Local/Text.bmd '[{"op": "test", "path": "/texts/500", "value": "El usuario salió del juego"}, {"op": "test", "path": "/texts/501", "value": "El usuario ya está en otra party"}, {"op": "replace", "path": "/texts/500", "value": "The user left the game"}, {"op": "replace", "path": "/texts/501", "value": "The user is already in another party"}]'
```

Find texts. `find` ignores case and returns 50 items by default. `total` counts every match whatever the `--limit`,
and `next_offset` is `null` when the items you got are all of them. To match case exactly, or a pattern, add
`--regex`: the query is then a Python regular expression, so escape `.`, `(`, `?` and the like in a literal one.

<!-- recipe: find-texts -->
```bash
mubmd find $WORK/Local/Text.bmd usuario --limit 5
mubmd find $WORK/Local/Text.bmd '^Zen' --regex
```

List every place a query appears, in one call, without the texts. `--limit` has no maximum and a table has 2,048
texts, so `next_offset` comes back `null` and the list is complete. The match ignores case (`%d` also finds `%D`): use
`--regex` in that same call only when the task tells the two apart.

<!-- recipe: list-every-pointer -->
```bash
mubmd find $WORK/Local/Text.bmd %d --fields pointer --limit 2048
```

Check a file or a whole folder. Exit 1 means a finding is a warning or an error, not that the call failed; `info`
findings alone exit 0. The reference's own findings are known: text 7 fills its record (`TEXT_FILLS_RECORD`, a
warning) and `TKOU.bmd` text 1829 has bytes after its first 0 (`TEXT_HAS_TAIL`, info).

<!-- recipe: check-a-working-copy -->
```bash
mubmd info $WORK/Local/Text.bmd
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
- **`test` before `replace`.** A text you read a moment ago may have changed; the `test` is what makes the edit safe.
  `set` has no guard: use it when you do not depend on the old value.
- **Keep the `printf` specifiers.** The client passes many texts to `sprintf`, so `%d` and `%s` stay, in the same
  order. A write that changes them succeeds with a `PRINTF_CHANGED` warning: read `warnings` and fix the text.
- **A text is at most 300 bytes in the file's encoding**, and only characters that encoding has. In cp1252 an
  accented letter is one byte, and there are no emoji or CJK characters.
- **Save tokens.** `--fields` keeps only the keys you name, `--limit` and `--offset` page, and `validate` on a folder
  lists only the files with findings. `mubmd --help` with no command is 19 KB: ask for one command's help.
- **A record shown as `{"hex": "..."}`** could not be read in the file's encoding, or has bytes after its first 0.
  Leave it alone. Its value is JSON, not a text taken as written: `set FILE /texts/N '{"hex": "..."}'` with 600 hex
  digits keeps every byte, and `set FILE /texts/N '"new text"'` makes it a text and drops the bytes after the 0.
- **Several writers can share a working copy.** They take turns on the folder's `.mubmd.lock`, and the one whose file
  changed under it writes nothing and gets `STALE_FILE`. Leave `.mubmd.lock` and `<name>.bak` (the file as it was
  before its first write) where they are.
- **After changing a codec**, run `mubmd roundtrip Client/assets/Data`: `differing` must be empty.

## Errors and the way out

An error has no `data`, and nothing was written. Read `error.code` and `error.message`; `error.suggestion`, when it
is not `null`, is the next command to run or the flag to use.

| Code | Exit | The way out |
| --- | ---: | --- |
| `USAGE` | 2 | `mubmd <command> --help` |
| `INVALID_VALUE` | 2 | The value does not fit: a text takes a string, a hex record 600 hex digits, a patch is a list |
| `TEXT_TOO_LONG` | 2 | Shorten the text; the message gives its size in bytes |
| `NOT_ENCODABLE` | 2 | Replace the character the file's encoding does not have |
| `FILE_NOT_FOUND` | 20 | Check the path; names keep the case they have on disk (`item.bmd`, `Text.bmd`) |
| `UNKNOWN_FORMAT` | 20 | `mubmd formats` lists what this version reads |
| `POINTER_NOT_FOUND` | 20 | `mubmd schema text` gives the pointers of a format |
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
| Read `mubmd --help` whole to learn the tool | Use the table above; ask for one command's help when a flag is missing |
| Ran every write with `--dry-run` first | Write once: the result is the dry run's, plus the write |
| Read a text back after writing it | `changes` and `warnings` are the proof |
| Read two neighbours with two `get` calls | `get FILE /texts --offset N --limit M` |
| Took the first 50 matches for all of them | `next_offset: null` means you have them all; if not, raise `--limit` |
| Sent a patch with a wrong `test` to see the guard work | The guard is tested in the tool; trust `TEST_FAILED` |
