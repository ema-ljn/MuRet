---
name: muret-bmd
description: Use when opening, inspecting, searching, translating or editing a MU `.bmd` file - a table (Text.bmd, item.bmd, skill.bmd, Gate.bmd, Dialog.bmd, quest.bmd, movereq.bmd and the other tables under `Data` and `Data/Local`) or a 3D model (its meshes, texture names, bones and actions, in `Data/Item`, `Data/player`, `Data/skill`, `Data/NPC` and the `Object` folders) - when a task names `mutools`, a game text, item, skill, gate, dialog or quest by its number or name, or a model's texture, bone, action (an animation, to copy, add or remove) or version (K16), or when `mutools` answers with an error code such as REFERENCE_READ_ONLY, TEST_FAILED, INVALID_VALUE or STALE_FILE or a finding such as TEXTURE_NOT_FOUND or OUT_OF_RANGE.
---

# Editing .bmd files with mutools

mutools reads, checks and edits MU Online's .bmd files byte-exact (texts, tables and models). Every command prints one
JSON object on stdout, {ok: true, schema, command, data, warnings}, or one on stderr when it fails, {ok: false, schema,
error: {code, message, suggestion, input, pointer}}, and exits 0, 1 (findings or differences), 2 (usage), 20 (not
found), 30 (precondition) or 70 (a bug in the tool). Places inside a file are JSON Pointers (/texts/500,
/records/1/mana, /meshes/0/texture_name). The reference folder Client/assets/Data is read-only: run `mutools workspace
init ~/mu/Data` once and edit the copy with `find`, `get`, then `set` or `apply` (a JSON Patch of test and replace). A
write is verified before the file is replaced, and its result is what was written. `mutools <command> --help` describes
one command as JSON.

This version reads every `.bmd` of the game: the 29 table files under [The tables](#the-tables) and the 1,962 3D
models under [The models](#the-models). Three files answer `UNKNOWN_FORMAT`: `Data/Item/item.bmd`,
`Data/Item/Itemtest.bmd` and `Data/skill/skill.bmd`, which are named like a table and are neither a table nor a model
(inside a folder called `Item` or `skill`, a file with the table's name is the table only if it is as large as one).
The tool is in
[`Client/tools/mutools`](../../../Client/tools/mutools/README.md) and its contract is the
[spec](../../../docs/superpowers/specs/2026-11-25-bmd-editor-design.md).

## Setup, once

```bash
mutools --version || pip install -e Client/tools/mutools   # Python 3.12+, in a virtual environment
mutools workspace init ~/mu/Data                        # from the repo: copies 370 MB and checks every file
```

If a working copy already exists, use it. Any `Data` folder with no `.sha256` manifest beside it is a working copy,
and `workspace init` on one keeps it as it is: `changed` is `false` and `edited` counts the files that differ from the
reference. The client runs it with `Main --data ~/mu/Data`. People edit the same folder with `mutools tui ~/mu/Data`;
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

- **The fields of a record:** `get FILE /records/N` shows one record whole, with every field's name. `mutools schema
  FORMAT` adds each field's type and size, and "the client handles at most 10" where the client has a limit of its own.
- **The client reads the first file of each row.** The others (`*test.bmd`, `TKOU*.bmd`, `n380.bmd`) are other
  editions, most of them in Korean (cp949) or Chinese (gb2312): change them only when the task names them. `mutools
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

## The models

Every other `.bmd` is a 3D model: 1,962 files in `Data/Item`, `Data/player`, `Data/skill`, `Data/NPC`, `Data/Monster`
and the `Object` folders. A model is known by its first bytes (`BMD` and a version), not by its name, so the same
commands work on any of them. `info FILE` gives its `version`, its `cipher` (`K16` when the version is 12, `none`
otherwise), its `size` in bytes and how many `meshes`, `bones` and `actions` it has; `bones` counts every entry of
`/bones`, the dummy ones too, and `records`, `used_records` and `read_by_client` are `null` in a model. A model may
have no meshes: `player/Player.bmd` is the characters' skeleton and its 205 actions. This is the whole view, with each
list cut to one item:

```json
{"format_version": 1, "format": "model", "encoding": "cp949", "version": 10,
 "name": "Data2\\Item\\Sword\\Sword02.smd",
 "meshes": [{"texture": 0, "texture_name": "sword02.jpg",
             "vertices": [{"bone": 0, "position": [-1.417384, -1.0287832, -10.2511]}],
             "normals": [{"bone": 0, "normal": [0.0, 4.3312576e-07, -1.0], "vertex": 0}],
             "uvs": [[0.9868, 0.45740002]],
             "triangles": [{"polygon": 3, "vertices": [0, 1, 2, -12851], "normals": [0, 0, 0, -12851],
                            "uvs": [0, 1, 2, -12851], "raw_0x01": {"hex": "cd"}, "raw_0x1a": {"hex": "cdcd..."}}]}],
 "actions": [{"keys": 1, "lock": 0}],
 "bones": [{"name": "Box01", "parent": -1,
            "actions": [{"positions": [[0.049219, 0.0, -0.078563]], "rotations": [[1.570796, 0.811578, 0.0]]}]},
           {"dummy": 1}],
 "trailing": ""}
```

| Part | One pointer | It is |
| --- | --- | --- |
| A mesh's texture | `/meshes/0/texture_name` | a text of up to 31 bytes: see the three rules below |
| A vertex, a normal | `/meshes/0/vertices/41`, `/meshes/0/normals/0` | its `bone` (a bone's number) and three floats |
| A triangle | `/meshes/0/triangles/0` | `vertices`, `normals` and `uvs`: four places in its own mesh's lists each, of which the client uses the first three |
| An action (an animation) | `/actions/0` | `keys`, how many poses it has, and `lock` |
| A bone | `/bones/3` | `name`, `parent` (a bone's number, from 0, or -1 for a root) and its pose in every action |
| A bone's pose | `/bones/3/actions/0/rotations/0` | `[x, y, z]`; `positions` beside it, one of each per key of action 0 |
| A bone that is only a place | `/bones/9` | `{"dummy": 1}`: no name, no parent, nothing to edit; it still has its number |

- **`get` counts what it does not show.** It gives the value at the pointer and the lists directly inside it, `--limit`
  items of each; a list of objects one level further in comes as `{"count": n}`. So `get FILE '' --limit 3` is a
  model's header and its first three meshes, actions and bones (under 2 KB even for `Player.bmd`), and `get FILE
  /bones --limit 200` is every bone with its name and its parent, with `total` the number of bones. Point at a counted
  list to read it: `get FILE /meshes/0/vertices --offset 40 --limit 3`. `truncated` is `true` whenever something was
  counted or cut, so it is `true` for that list of bones too: `next_offset: null` is what says every bone is there.
- **A page gives each of its items whole**, but for the lists it counts: a vertex keeps its three numbers and a
  triangle its four whatever the `--limit`, as a dialog keeps its ten answers. `--limit` cuts the list at the pointer
  and, when the pointer is one part and not a list, the lists directly inside that part.
- **Names are texts like a table's**, in cp949: a string, `{"text", "tail"}` or `{"hex"}` (the rule on the three
  shapes of a text holds). Most bone names and some texture names come as `{"text": "Bip01 Pelvis", "tail":
  "cdcd..."}`, because the exporter left bytes after the name's 0: read `text`, and `test` the whole object, as `get`
  gave it. `find FILE QUERY` looks in the texture names and the bone names, and `--field
  texture_name` or `--field name` narrows it to one of the two. The model's own `/name` is the path its exporter read:
  the client ignores it, and `find` does not look there.
- **Counts are the lengths of the lists.** There is no count to edit: to add or remove a vertex, a key or a bone,
  `replace` the whole list, or edit a `dump`. Lists that must agree are checked on every write (`INVALID_VALUE` at the
  list, nothing written): an action with `lock` 0 has no `positions`, and every bone has one entry per action, each
  with that action's `keys` positions and rotations.
- **An action moves as a file of its own.** `action export FILE N --output A.json` writes action N to an action file:
  its `keys`, its `lock` (and `positions` when the lock is not 0), and per bone its name, its parent and one
  `positions` and one `rotations` entry, `[x, y, z]`, per key (rotations in radians). `action import FILE A.json` puts
  it after the last action of a model with the same skeleton (as many bones, the same names and parents), or over
  action N with `--at N`; `data.action` is its number. `action remove FILE N` takes out the last action with no flag,
  and any other needs `--yes`, because every later action moves down one number and the client and the server name
  a character's actions by their number. A new action of `player/Player.bmd` is 205; numbers up to 255 reach the
  other players, because the action travels in one byte.
- **`raw_0x...` are bytes the client does not use.** They show only when they are not zero. Leave them as they are.
- **The version is the cipher, and only `build` changes it.** 12 is K16 (encrypted) and any other is plain: write 10,
  as 1,142 of the game's 1,146 plain models do. An edited model keeps its version. `set` and `apply` refuse
  `/version`: to decrypt or encrypt a model, `dump` it, change `version` in the JSON and `build` it (the recipe
  below). A K16 model is 4 bytes longer than the same model plain: after the version it stores the length of what
  follows. `info` gives a file's `size`.

**What the client reads from a texture's name.** The name is not only a file name:

| It decides | Rule | Examples |
| --- | --- | --- |
| The file the client opens, in the model's folder | the name up to its first dot, plus `.OZT` when the letter after that dot is `t` or `T`, and `.OZJ` otherwise | `sword02.jpg` opens `sword02.OZJ`; `lnn01.tga` opens `lnn01.OZT` |
| The mesh's slot on a character | a name that starts with `ski` or `level` is the skin, `hid` is hidden (never drawn), `hair` is the hair; lowercase only | `hide.jpg`, `level_man02.jpg` |
| The mesh's effect | up to four capitals between the first `_` and the dot: `R` blend, `H` hidden, `S` scrolling, `N` no shine; any other character there and there is no effect | `hair_R.jpg` has `R`; `Monster81_R2.jpg` has none |

**A model's findings**, in `warnings` after a write and in `validate`:

| Finding | It means | Do |
| --- | --- | --- |
| `TEXTURE_NOT_FOUND` (warning) | neither the model's folder nor a folder beside it has the file the name opens | fix the name, or put the `.OZJ` or `.OZT` in the model's folder |
| `TEXTURE_ELSEWHERE` (warning, on a write) | the model's folder does not have the file and a folder beside it does; the message names it | answer "not in the model's folder", and copy the file there: that is always right |
| `TEXTURE_ROLE_CHANGED` (warning, on a write) | the new name changes the file's kind (OZJ or OZT), the slot or the effect; the message says which | keep the old name's prefix, its `_LETTERS` and the first letter of its extension, unless that change is the task |
| `PARENT_AFTER_CHILD` (warning) | a bone hangs from itself or from a later bone, and the client poses a bone after its parent | choose an earlier bone, or -1 |
| `OUT_OF_RANGE` (error) | a number points outside its list: a vertex's or a normal's `bone`, a normal's `vertex`, a triangle's corner, a mesh's `texture`, a bone's `parent`; or the model has more than 200 bones | fix the number: the client reads outside its buffers |

A warning or an error in `warnings` does not stop the write: the file is written as you asked, so read `warnings`
after every write. They hold every finding the write caused, wherever it is: a shorter list of vertices warns at the
first normal and the first triangle that now point outside it. Every write looks for the texture files, so no warning
on a new texture name means the model's own
folder has its file: there is nothing to check with `validate`, `ls` or a `--dry-run`. The tool does not know which
folder the client opens a given model's textures from. It is the model's own for nearly all of them, and 42 of the
game's models name a file that only another folder has (`player/Helper03.bmd` names `Item/unicon.OZJ`): `validate`
reports none of those, and `TEXTURE_ELSEWHERE` is only for a name you write.

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
| What a model has | `info FILE` | `version`, `cipher`, `size`, `meshes`, `bones`, `actions` |
| A model's parts and their sizes | `get FILE '' --limit 3` | `value`: the header and the first parts, `{"count": n}` for the rest |
| Every bone with its parent | `get FILE /bones --limit 200` | `value`: item `i` is bone `i`, and `{"dummy": 1}` is a dummy one; `total` |
| Change a texture name or a bone | `apply FILE '[{"op": "test", ...}, {"op": "replace", ...}]'` | `changes`, and `warnings`: the texture's findings |
| Decrypt or encrypt a model | `dump`, change `version` in the JSON, `build` | `changes: [{pointer: "/version", ...}]` |
| Copy an action to a file | `action export FILE N --output A.json` | `output`, `keys` |
| Add an action from a file | `action import FILE A.json --limit 1` | `action`: the new action's number; `actions` |
| Take out the last action | `action remove FILE N --limit 1` | `actions`: how many are left |
| Check a file or a folder | `validate PATH` | `files: [{path, findings}]`, `checked`, `skipped` |
| What changed since the reference | `diff REFERENCE_FILE FILE` | `changes: [{pointer, old, new}]`, `total` |

On `set`, a value for a text is taken as written, with no quotes of its own, and any other value is JSON: `45`,
`-1`, `2.5`, `{"hex": "00"}`. In a patch every value is JSON: `"Veneno"` for a text, `45` for a number.

The result of `set` and `apply` is the file as it is now. Do not read it back, and do not run the command with
`--dry-run` first: the write was verified, and `warnings` already carries every finding the write caused. The `new`
of a change is as good as a `get` for a later `test`, and its `old` is the undo: `set` it back. `changes` holds the
first 50 and `total` counts them all, and a list of objects, or one whose length changed, shows in `old` and `new` as
`{"count": n}`, as in `get`. `--dry-run` is for showing someone a batch before it is written.

`find` looks in every text of a record (a name, a dialog's line and its answers) and never in its numbers. Every list
pages the same way: 50 items by default, `--limit` and `--offset` to move, `total` for the whole count and
`next_offset`, which is `null` on the last page. That holds for `find`, `get` on a list, `diff` and the `changes` of
`build`. On `get`, `truncated` says that `value` is not all there is at the pointer: it is one page of a list, or a
list inside it was cut at `--limit` or shown as `{"count": n}`. A text is never cut.

## Recipes

`$WORK` is the working copy, `$REF` is `Client/assets/Data` and `$TMP` is any scratch folder. A test runs each recipe
on its own fresh working copy, so the lines are the tool's real syntax and the values are the reference's: one recipe
does not see another's edits. `# exit 1` marks a call that ends with 1.

Change one text. Put `--` before a value that starts with `-`.

<!-- recipe: change-one-text -->
```bash
mutools set $WORK/Local/Text.bmd /texts/500 'El jugador salió del juego'
```

Change one number of a record. Saving a table that has a checksum (items, skills, filters, item sets) writes the new
checksum too.

<!-- recipe: change-an-item-field -->
```bash
mutools set $WORK/Local/item.bmd /records/0/durability 22
```

Find a record by its name and change one of its fields. `find` gives the record in `index`, and a `get` of the
record shows every field, so no `schema` call is needed to learn their names. When the task already gives the
record's number, the `apply` alone is enough: its `test` on the name proves the record is the one you mean.

<!-- recipe: find-a-skill-and-change-it -->
```bash
mutools find $WORK/Local/skill.bmd poison --field name
mutools get $WORK/Local/skill.bmd /records/1
mutools apply $WORK/Local/skill.bmd '[{"op":"test","path":"/records/1/name","value":"Poison"},{"op":"replace","path":"/records/1/mana","value":45}]'
```

Rename something with a guard: `test` the name the tool last showed you, then `replace` it. If the name is no longer
that value, nothing is written and the call ends with `TEST_FAILED` (exit 30).

<!-- recipe: rename-an-item -->
```bash
mutools apply $WORK/Local/item.bmd '[{"op":"test","path":"/records/0/name","value":"Kris"},{"op":"replace","path":"/records/0/name","value":"Training Kris"}]'
```

<!-- recipe: rename-a-skill -->
```bash
mutools apply $WORK/Local/skill.bmd '[{"op":"test","path":"/records/1/name","value":"Poison"},{"op":"replace","path":"/records/1/name","value":"Veneno"}]'
```

Translate or rewrite several texts in one call. `test` each text against the value the tool last showed you (a `get`,
a `find`, or the `new` of your own earlier write; read only the ones you have not seen), then `replace` it. The patch
is the argument itself, a file, or `-` for stdin.

<!-- recipe: translate-several-texts -->
```bash
mutools get $WORK/Local/Text.bmd /texts --offset 500 --limit 2
mutools apply $WORK/Local/Text.bmd '[{"op": "test", "path": "/texts/500", "value": "El usuario salió del juego"}, {"op": "test", "path": "/texts/501", "value": "El usuario ya está en otra party"}, {"op": "replace", "path": "/texts/500", "value": "The user left the game"}, {"op": "replace", "path": "/texts/501", "value": "The user is already in another party"}]'
```

Translate a dialog's answers: one `get` of the record, one `apply`. A patch that holds an apostrophe goes on stdin,
through a quoted heredoc, because it cannot sit inside single quotes. An answer holds 63 bytes and the line 299.
The line of this record comes as `{"text", "tail"}`: see the rule on the three shapes of a text.

<!-- recipe: translate-a-dialog -->
```bash
mutools get $WORK/Local/Dialog.bmd /records/53
mutools apply $WORK/Local/Dialog.bmd - <<'PATCH'
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
mutools find $WORK/Local/Text.bmd usuario --limit 5
mutools find $WORK/Local/Text.bmd '^Zen' --regex
mutools find $WORK/Local/Dialog.bmd 'find it' --field answers
```

List every place a query appears, in one call, without the texts. `--limit` has no maximum, so with one as large as
the table `next_offset` comes back `null` and the list is complete. The match ignores case (`%d` also finds `%D`): use
`--regex` in that same call only when the task tells the two apart.

<!-- recipe: list-every-pointer -->
```bash
mutools find $WORK/Local/Text.bmd %d --fields pointer --limit 2048
```

List the names a table uses: the empty records have no text, so `.` as a regular expression matches the used ones.

<!-- recipe: list-the-used-names -->
```bash
mutools find $REF/Local/skill.bmd . --regex --fields pointer,text --limit 256
```

Read a gate and where it leads: gate 18 is the entrance from Lorencia to Devias, and its `target`, 19, is the exit.

<!-- recipe: read-a-gate -->
```bash
mutools get $REF/Gate.bmd /records --offset 18 --limit 2
```

Pick records by a number. `find` does not look at numbers, so write the table to a file and filter it with a script;
do not print it (a gate table is 28 KB of JSON, an item table 7 MB).

<!-- recipe: pick-records-by-a-number -->
```bash
mutools dump $REF/Gate.bmd --output $TMP/gate.json
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
first write to the file corrects it) and `TEXT_HAS_TAIL` at text 1829 of `TKOU.bmd` (info); and in the models,
`OUT_OF_RANGE` at `/meshes/1/vertices/0/bone` and `/meshes/1/normals/0/bone` of `object35/Object36.bmd`,
`PARENT_AFTER_CHILD` at bones 43 and 44 of `NPC/BloodCastle01.bmd` and `TEXTURE_NOT_FOUND` at mesh 1 of
`Item/waterring.bmd`. A whole `Data` folder takes about 4 seconds.

<!-- recipe: check-a-working-copy -->
```bash
mutools info $WORK/Local/Text.bmd
mutools validate $WORK/Local/skill.bmd
mutools validate $WORK/Local/Text.bmd   # exit 1
mutools validate $WORK                  # exit 1
```

See what a working copy changed. Exit 1 means the files differ.

<!-- recipe: compare-with-the-reference -->
```bash
mutools diff $REF/Local/Text.bmd $WORK/Local/Text.bmd
mutools set $WORK/Local/Text.bmd /texts/100 Zen!
mutools diff $REF/Local/Text.bmd $WORK/Local/Text.bmd   # exit 1
```

Edit a whole table as JSON, for a change too large for a patch. `dump` writes the view (66 KB for a text table: edit
it with a script, do not print it), `diff` shows what the edited JSON would change, and `build` writes it.

<!-- recipe: edit-through-json -->
```bash
mutools dump $WORK/Local/Text.bmd --output $TMP/text.json
mutools diff $WORK/Local/Text.bmd $TMP/text.json
mutools build $TMP/text.json --output $WORK/Local/Text.bmd --dry-run
```

Read a model: what it has, its first parts with the sizes of the rest, every bone with its parent, and two vertices.
A bone's number is its place in `/bones`, and `find` gives it as `index`.

<!-- recipe: read-a-models-parts -->
```bash
mutools info $REF/player/HelmMale01.bmd
mutools get $REF/player/HelmMale01.bmd '' --limit 3
mutools get $REF/player/HelmMale01.bmd /bones --limit 200
mutools find $REF/player/HelmMale01.bmd 'L Thigh' --field name
mutools get $REF/Item/Sword01.bmd /meshes/0/vertices --offset 40 --limit 3
```

Make a mesh use another texture: read the name, then `test` it and `replace` it. The answer's `warnings` say whether
the client will find the file: none means the model's folder has it (`Item/etcring.OZJ` here), and the findings above
name the other cases. Every mesh has its own texture name: `info` counts the meshes, and `find FILE . --regex --field
texture_name` lists every name with its pointer.

<!-- recipe: rename-a-texture -->
```bash
mutools get $WORK/Item/Sword01.bmd /meshes/0/texture_name
mutools apply $WORK/Item/Sword01.bmd '[{"op":"test","path":"/meshes/0/texture_name","value":"sword02.jpg"},{"op":"replace","path":"/meshes/0/texture_name","value":"etcring.jpg"}]'
```

Hang a bone from another one. Bone 3 of this model, `Bip01 L Thigh`, hangs from bone 2; this moves it under bone 0.

<!-- recipe: move-a-bone -->
```bash
mutools apply $WORK/player/HelmMale01.bmd '[{"op":"test","path":"/bones/3/parent","value":2},{"op":"replace","path":"/bones/3/parent","value":0}]'
```

Write a model as JSON and build it back, under a new name or over itself: `build` needs no flag to replace a file,
and keeps the old one as `<name>.orig`. A model's dump is large (9 MB for `Player.bmd`, 14 KB for this one): change it
with a script, do not print it.

<!-- recipe: dump-and-build-a-model -->
```bash
mutools dump $WORK/skill/m_waves.bmd --output $TMP/m_waves.json
mutools build $TMP/m_waves.json --output $WORK/skill/m_waves2.bmd
```

Decrypt a model (12 to 10) or encrypt it (10 to 12): the same `dump`, a script that changes `version` in the JSON,
and `build` over the model itself. `build` answers with one change, at `/version`, with the old and the new number,
and that answer is the proof that nothing else in the model changed:

```bash
mutools dump $WORK/skill/m_waves.bmd --output $TMP/m_waves.json
python3 -c 'import json, sys
view = json.load(open(sys.argv[1], encoding="utf-8"))
view["version"] = 10
json.dump(view, open(sys.argv[1], "w", encoding="utf-8"), ensure_ascii=False)' $TMP/m_waves.json
mutools build $TMP/m_waves.json --output $WORK/skill/m_waves.bmd
```

Add an action to a model, and take it out again. The action file is JSON: change its poses with a script when the
task asks for a new movement, never by printing it (31 KB for a player's action of 7 keys). `--limit 1` keeps the answer
short, and its `action` is the new action's number.

<!-- recipe: add-and-remove-an-action -->
```bash
mutools action export $WORK/NPC/BloodCastle01.bmd 0 --output $TMP/stand.json
mutools action import $WORK/NPC/BloodCastle01.bmd $TMP/stand.json --limit 1
mutools action remove $WORK/NPC/BloodCastle01.bmd 1 --limit 1
```

Make a working copy from somewhere that is not the repo, and check that a change to the tool keeps every file
byte-exact.

<!-- recipe: working-copy-and-roundtrip -->
```bash
mutools workspace init $TMP/mu/Data --from $REF --dry-run
mutools roundtrip $WORK
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
- **Save tokens.** `--fields`, which `find` and `info` have and `get` does not, keeps the keys you name; `--limit` and
  `--offset` page, and `validate` on a folder lists only the files with findings. `mutools --help` with no command is
  22 KB: ask for one command's help. Never print a model's dump.
- **Several writers can share a working copy.** They take turns on the folder's `.mutools.lock`, and the one whose file
  changed under it writes nothing and gets `STALE_FILE`. Leave `.mutools.lock` and `<name>.orig` (the file as it was
  before its first write) where they are; the `.bak` files are the game's own.
- **After changing a codec**, run `mutools roundtrip Client/assets/Data`: `differing` must be empty.

## Errors and the way out

An error has no `data`, and nothing was written. Read `error.code` and `error.message`; `error.suggestion`, when it
is not `null`, is the next command to run or the flag to use.

| Code | Exit | The way out |
| --- | ---: | --- |
| `USAGE` | 2 | `mutools <command> --help`; on `action remove`, an action that is not the last needs `--yes` |
| `INVALID_VALUE` | 2 | The value does not fit: a text takes a string, a number field a number inside its size, a hex field exactly its bytes, a patch is a list, `--field` a name `schema` lists; in a model, two lists that must agree do not, or the pointer is `/version` (only `build` changes it); on `action import`, the action file is for another skeleton (`error.pointer` is its first bone that differs) |
| `TEXT_TOO_LONG` | 2 | Shorten the text; the message gives its size and the field's in bytes |
| `NOT_ENCODABLE` | 2 | Replace the character the file's encoding does not have |
| `FILE_NOT_FOUND` | 20 | Check the path; names keep the case they have on disk, and the gates are in `Data`, not `Data/Local` |
| `UNKNOWN_FORMAT` | 20 | The file is not a table and does not start like a model: one of the three files named at the top, or not a `.bmd` of the game |
| `POINTER_NOT_FOUND` | 20 | `get FILE /records/N` shows the fields a record has; `mutools schema FORMAT` lists them all |
| `REFERENCE_READ_ONLY` | 30 | Use the working copy: `mutools workspace init ~/mu/Data` |
| `TEST_FAILED` | 30 | `get` the pointer in `error.pointer`, decide again with its real value, send the patch again |
| `STALE_FILE` | 30 | Someone wrote the file while the command ran: run it again |
| `NOT_EMPTY` | 30 | The destination of `workspace init` is not a working copy: choose another folder |
| `CORRUPT_FILE` | 30 | The file is cut short or is not what its format says (the message names the part); copy it again from the reference |
| `WRITE_VERIFY_FAILED` | 30 | A bug in the tool: stop and report the command |
| `IO_ERROR` | 30 | The disk, its permissions, or another writer holding the folder for 10 s; the message says which |
| `INTERNAL_ERROR` | 70 | A bug in the tool: stop and report the command and the message |

## What an agent without this skill got wrong

| It did | Do this |
| --- | --- |
| Read `mutools --help` whole, or ran `formats` and `schema`, to learn the tool | Use the tables above; ask for one command's help when a flag is missing |
| Ran every write with `--dry-run` first | Write once: the result is the dry run's, plus the write |
| Read a value back after writing it | `changes` and `warnings` are the proof |
| Read two neighbours with two `get` calls | `get FILE /records --offset N --limit M` |
| Took the first 50 matches for all of them | `next_offset: null` means you have them all; if not, raise `--limit` |
| Sent a patch with a wrong `test` to see the guard work | The guard is tested in the tool; trust `TEST_FAILED` |
| Looked for `Gate.bmd` in `Data/Local` | The gates are in `Data` |
| Printed all 256 gates to pick a few by their numbers | `dump --output`, then a script |
| Searched "Devias" and found nothing | The data says "Davias": search a fragment |
| Ran `schema model` to learn a model's pointers | They are in [The models](#the-models) |
| After renaming a texture, ran `validate`, listed the folder and sent a made-up name with `--dry-run` to learn whether the tool looks for the file | It does, on every write: an empty `warnings` is the answer |
| Passed `--fields` to `get` | `find` and `info` have it, `get` does not; `get FILE /bones --limit 200` already leaves out each bone's poses |
| Answered a question about one bone with a `find`, two `get`, an `info` and a `dump` | `get FILE /bones --limit 200`: every bone, its name and its parent |
| Guessed the version of a plain model | 10 |
