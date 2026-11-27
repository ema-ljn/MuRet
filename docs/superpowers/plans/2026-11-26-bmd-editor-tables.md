# BMD editor, stage 3: every table Implementation Plan

> **Renamed.** Since 2026-11-27 the tool is `mutools`, in `Client/tools/mutools`, with the package `mutools`. This
> plan is the record of a finished stage and keeps the names it had: read `mubmd` as `mutools` and `Client/tools/bmd` as
> `Client/tools/mutools`.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `mubmd` reads, checks and edits every `.bmd` table of `Client/assets/Data` (29 files in 14 formats), from the
CLI, the console UI and the `muret-bmd` skill, byte-exact. Models stay for stage 4.

**Architecture:** Stage 2's ports and adapters, and the `RecordTableFormat` that PR #470 added for items: one codec
driven by a schema per table. This stage makes the schema able to describe every table (signed numbers, a float,
lists, nested structs, a count in front of the records, no checksum) and writes one schema per table. The use cases,
the CLI and the console UI stop knowing format names: they walk the view.

**Tech Stack:** unchanged from stage 2 (Python 3.12+, Textual 8.2.8, Typer 0.29.2, Rich 15.0.0, pydantic 2.14.4;
pytest 8.4.2, ruff 0.17.6, mypy 2.4.1). No new dependency.

**Spec:** [`docs/superpowers/specs/2026-11-25-bmd-editor-design.md`](../specs/2026-11-25-bmd-editor-design.md), stage 3
of "Stages (one PR each)". Previous plan: [`2026-11-25-bmd-editor.md`](2026-11-25-bmd-editor.md).

## Global Constraints

- Byte-exact: `mubmd roundtrip Client/assets/Data` ends with `checked: 29, identical: 29, differing: [], skipped: 1965`
  (1,962 models and the three files of "Not tables" below).
- `Client/assets/Data` is never written. Tests copy what they edit.
- `core/` imports only the standard library, and nothing from `app/` or `adapters/` (`tests/test_layers.py`).
- The CLI contract of schema 1 holds: the envelopes, the exit codes 0/1/2/20/30/70 and the closed `ErrorCode` enum. This
  stage adds no error code and one finding code, `OUT_OF_RANGE`.
- The view stays `format_version` 1. The text format's view does not change.
- **The PR touches only `Client/tools/`, `.claude/`, `docs/` and `*.md`**, so CI does not compile C++ (Emanuel,
  2026-11-25). No workflow file, no root `.gitignore`. Before opening the PR: `git diff --name-only
  origin/main...HEAD` against the `case` list of `.github/workflows/ci.yml`. The PR opens when the stage is done (a
  push to the branch before that runs no CI) and gets no docs-only push while Emanuel waits to merge.
- English in code, docs, commits and PR text; comments only when strictly necessary (CODING_RULES §7, §9); one
  behavior, one test (§13); Angular commits with scope `tools`; the PR targets `main`.
- Layouts come from research 05, the client's loaders under `Client/src/game/Tables/` and the research repo's loader
  notes; every schema below was run against the real files on 2026-11-26 (all 26 new files decode and encode
  identical).

## Decisions taken in this plan

Each is a call that the spec left open or that the real files forced. Emanuel reads these before any code.

1. **A text with bytes after its first 0 reads as `{"text": "...", "tail": "<hex>"}`** in record tables, not as hex.
   The spec's rule (hex) was written for `Text.bmd`, where one record has such bytes. In the tables they are common:
   36 of `Dialog.bmd`'s 75 dialogs, 32 of `slide.bmd`'s 70 texts, 278 of `n380.bmd`'s 399 item names. With the tail
   kept apart the text is readable, searchable and editable, and an untouched field keeps every byte. Writing a plain
   string drops the tail, which the client never reads. `{"hex": ...}` stays for a text with no 0 or that its
   encoding cannot read. This changes how seven names of `item.bmd` read since #470 (hex before).
2. **The first write keeps the original as `<name>.orig`**, not `<name>.bak`. The reference already has
   `item.bmd.bak`, `skill.bmd.bak`, `Filter.bmd.bak` and `Movereq.bmd.bak`, so in a working copy the first write to
   `item.bmd` keeps no backup today, and the other three tables would get none either. A working copy from stage 2
   keeps its `Text.bmd.bak`; its next write adds a `Text.bmd.orig` with the file as it is then.
3. **Item records get every field the research names** (31 fields, four of them still `raw`). #470's `raw_0x22`
   becomes `equip_slot` and `raw_0x23`, and `raw_0x26` becomes `damage_min` to `raw_0x51`. Pointers to `name`,
   `two_handed`, `level`, `width` and `height` do not change.
4. **`slide.bmd` is a table of one record** (`/records/0/groups/2/texts/5`), so it needs no codec of its own.
5. **A table with a count in front** (`movereq.bmd`, `MonsterSkill.bmd`): the count is the length of `/records`.
   Records are added or removed by replacing `/records` (`apply`) or through `dump` and `build`; bytes after the
   counted records stay as read (`movereq.bmd` has 640). A count that is negative or larger than the file holds is
   `CORRUPT_FILE`.
6. **Not tables:** `Data/Item/item.bmd`, `Data/Item/Itemtest.bmd` and `Data/skill/skill.bmd` share a table's name and
   are something else. They answer `UNKNOWN_FORMAT`, as #470 decided for the first; the spec's "open read-only as raw
   bytes" is dropped.
7. **Test twins' encodings**, read from the files: the `test` twins are Korean (`cp949`: `Skilltest`, `Dialogtest`,
   `questtest`, `slidetest`, `itemsetoptiontest`), `movereqtest.bmd` and `TKOUREQ.bmd` are Chinese (`gb2312`),
   `credittest.bmd` is `cp1252`. None is read by the client.
8. **One new finding, `OUT_OF_RANGE` (error):** a count or index the client would use past its buffers
   (a dialog with more than 10 answers, a slide group with more than 32 texts, a quest with more than 16 entries or
   conditions, a monster type outside 0..0xFFFF). It is a finding, so the write still happens. No real file has one.
9. **The console UI shows a record's fields in a list under the table** (mock-up in Task 7), with a row filter on `f`
   (all, used, changed, with findings) and a glyph per kind of file in the tree. The text table keeps its stage 2 look.

Out of scope, still deferred: models (stage 4), the preview (stage 5), JSON Patch `add` and `remove`, trimming record
fields in `get` pages, paging a single file's `validate`, a schema snapshot test.

## Review Focus

1. JSON lets through values a field cannot hold: `true`, `1.5`, `NaN`, `Infinity`, a number wider than its field.
   Expected: `INVALID_VALUE` naming the pointer, nothing written. Tests in Tasks 2 and 6.
2. A text with bytes after its first 0: untouched, the file keeps every byte; edited as a text, only that field
   changes and its tail goes. Tests in Tasks 2, 6 and 7.
3. A whole record or list replaced through `apply` or `build` with the wrong shape (a field missing or extra, a short
   list). Expected: `INVALID_VALUE` naming the place, nothing written. Tests in Tasks 2 and 6.
4. A counted table whose count word is negative or larger than the file holds (a truncated `movereq.bmd`). Expected:
   `CORRUPT_FILE`, no crash and no huge allocation. Test in Task 3.
5. A file with a table's name that is not that table (`Data/skill/skill.bmd`, `data/ITEM/Item.bmd`). Expected:
   `UNKNOWN_FORMAT` for reads and writes, in any letter case. Test in Task 4.

---

## The tables

Every table is XOR3 per record (key `FC CF AB`, restarting at each record). "Key" is the checksum key; all use the
parity of `Item.bmd` (`xor_odd_words=True`) except `skill`. ✓ marks the file the client reads. Offsets are from the
start of a record; `u`/`i` are unsigned/signed little endian with their size in bytes, `×n` is a list.

| Format | Files (encoding) | Records | Key |
| --- | --- | --- | --- |
| `item` | `Local/item.bmd` ✓ (cp1252), `itemtest.bmd` (cp949), `n380.bmd` (gb2312) | 8192 × 0x54 | 0xE2F1 |
| `skill` | `Local/skill.bmd` ✓ (cp1252), `Skilltest.bmd` (cp949) | 256 × 0x3C | 0x5A18, even words |
| `gate` | `Gate.bmd` ✓, `Gatetest.bmd` | 256 × 9 | none |
| `filter` | `Local/Filter.bmd` ✓ (cp1252) | 1000 × 20 | 0x3E7D |
| `filter_name` | `Local/FilterName.bmd` ✓ (cp1252) | 1000 × 20 | 0x2BC1 |
| `item_set_type` | `Local/itemsettype.bmd` ✓, `itemsettypetest.bmd` | 8192 × 4 | 0xE5F1 |
| `item_set_option` | `Local/itemsetoption.bmd` ✓ (cp1252), `itemsetoptiontest.bmd` (cp949) | 64 × 0x4C | 0xA2F1 |
| `credit` | `Local/credit.bmd` ✓ (cp1252), `credittest.bmd` (cp1252) | 400 × 33 | none |
| `slide` | `Local/slide.bmd` ✓ (cp1252), `slidetest.bmd` (cp949) | 1 × 0xA030 | none |
| `move_requirement` | `Local/movereq.bmd` ✓ (cp1252), `movereqtest.bmd`, `TKOUREQ.bmd` (gb2312) | count × 0x50 | none |
| `monster_skill` | `Local/MonsterSkill.bmd` ✓, `MonsterSkilltest.bmd` | count × 0x1C | none |
| `dialog` | `Local/Dialog.bmd` ✓ (cp1252), `Dialogtest.bmd` (cp949) | 200 × 0x400 | none |
| `quest` | `Local/quest.bmd` ✓ (cp1252), `questtest.bmd` (cp949) | 200 × 0x2A8 | none |

`item` and `item_set_type` have `section_size=512` (a record is `section * 512 + index`). `item` and `skill` skip a
same-named file inside `Data/Item` and `Data/skill`.

Fields, in order (a `raw_0xNN` field is `bytes`, named after its offset in its record or struct):

- **`item`**: `name` text 30 · `two_handed` u1 · `raw_0x1f` 1 · `level` u2 · `equip_slot` u1 · `raw_0x23` 1 · `width`
  u1 · `height` u1 · `damage_min` u1 · `damage_max` u1 · `defense_rate` u1 · `defense` u1 · `magic_defense` u1 ·
  `attack_speed` u1 · `walk_speed` u1 · `durability` u1 · `magic_durability` u1 · `magic_power` u1 ·
  `required_strength` u2 · `required_agility` u2 · `required_energy` u2 · `required_vitality` u2 ·
  `required_leadership` u2 · `required_level` u2 · `value` u1 · `raw_0x3d` 3 · `price` i4 · `stat_kind` u1 ·
  `classes` u1 ×5 · `resistances` u1 ×7 · `raw_0x51` 3.
- **`skill`**: `name` text 32 · `level` u1 · `damage` u1 · `mana` u2 · `stamina` u2 · `distance` u1 · `raw_0x27` 1 ·
  `delay` i4 · `energy` u1 · `raw_0x2d` 1 · `leadership` u2 · `element` u1 · `kind` u1 · `linked_skill` u1 ·
  `parameter` u1 · `evolutions` u1 ×3 · `classes` u1 ×5.
- **`gate`**: `flag` · `map` · `x1` · `y1` · `x2` · `y2` · `target` · `direction` · `level`, each u1.
- **`filter`**, **`filter_name`**: `word` text 20.
- **`item_set_type`**: `sets` u1 ×3 · `raw_0x03` 1.
- **`item_set_option`**: `name` text 32 · `options` u1 ×12 · `values` u1 ×12 · `extra_options` u1 ×2 · `extra_values`
  u1 ×2 · `full_set_pieces` u1 · `full_options` u1 ×5 · `full_values` u1 ×5 · `classes` u1 ×5.
- **`credit`**: `kind` u1 · `text` text 32.
- **`slide`**: `period` i4 · `speed` f32 · `groups` struct ×5 of { `max_level` i4 · `count` i4 (at most 32) · `texts`
  text 0x100 ×32 }.
- **`move_requirement`**: `number` i4 · `name` text 32 · `search_name` text 32 · `level` i4 · `zen` u4 · `gate` i4.
- **`monster_skill`**: `type` i4 (0 to 0xFFFF) · `skills` i4 ×5 · `raw_0x18` 4.
- **`dialog`**: `text` text 0x12C · `answer_count` i4 (at most 10) · `next` i4 ×10 · `actions` i4 ×10 · `answers` text
  0x40 ×10.
- **`quest`**: `entry_count` i2 (at most 16) · `condition_count` i2 (at most 16) · `npc_type` u1 · `name` text 32 ·
  `raw_0x25` 1 · `entries` struct ×16 of { `raw_0x00` 1 · `kind` u1 · `item_group` u1 · `item_index` u1 · `item_level`
  u1 · `item_count` u1 · `condition` u1 · `classes` u1 ×5 · `dialogs` i2 ×4 } · `raw_0x166` 2 · `conditions` struct
  ×16 of { `raw_0x00` 1 · `link` u1 · `need_quest` i2 · `min_level` u2 · `max_level` u2 · `raw_0x08` 4 · `zen` u4 ·
  `fail_dialog` i2 · `raw_0x12` 2 }.

What the real files hold (the tests' expected values): used records `skill` 73, `gate` 125, `filter` 0, `filter_name`
217, `item_set_type` 8192, `item_set_option` 64, `credit` 244, `dialog` 75, `quest` 4; `movereq.bmd` 28 records and
640 bytes after them, its twins 30 and 480; `MonsterSkill.bmd` 8. The eight checksums the client checks match.
`mubmd validate Client/assets/Data` keeps exactly today's findings: `TEXT_FILLS_RECORD` (Text 1, Texttest 2),
`TEXT_HAS_TAIL` (TKOU 1), `CHECKSUM_MISMATCH` (`item.bmd`, `n380.bmd`).

## Files

```text
Client/tools/bmd/src/mubmd/
  core/formats/record_fields.py        NEW  RecordField and the value codec (bytes <-> JSON) of fields, lists, structs
  core/formats/record_table_format.py  MOD  the table around the records: XOR3, count, checksum, view, findings
  core/formats/item_format.py          MOD  the full item schema
  core/formats/{skill,gate,filter,item_set,credit}_format.py                          NEW  Task 4
  core/formats/{dialog,quest,slide,move_requirement,monster_skill}_format.py          NEW  Task 5
  core/formats/text_format.py, core/checks/text_checks.py   MOD  check_value, check_text_record
  core/leaves.py                       NEW  walking a view: leaves, a leaf's text, blank values
  core/catalog.py, core/ports.py       MOD
  app/documents.py, app/edits.py, app/queries.py, app/session.py   MOD  no format names or sizes
  app/rows.py                          NEW  what the console UI shows of a row: label, search, filters
  adapters/storage.py                  MOD  the backup's name
  adapters/cli/commands.py, adapters/cli/describe.py   MOD  find --field, examples, the brief
  adapters/tui/widgets/field_list.py   NEW  the selected record's fields
  adapters/tui/widgets/{record_table_view,record_editor,bmd_tree}.py, adapters/tui/app.py, adapters/tui/mubmd.tcss  MOD
Client/tools/bmd/tests/                one test file per new module, tests/core/test_tables.py for the real files
.claude/skills/muret-bmd/SKILL.md, Client/tools/bmd/README.md, AGENTS.md, the spec   MOD  Task 8
```

Commands run from `Client/tools/bmd`. The whole check is `ruff check . && ruff format --check . && mypy src tests &&
pytest -q` (257 tests pass on `main` at 60e97346).

---

### Task 1: Keep the original as `<name>.orig`

**Files:**
- Modify: `src/mubmd/adapters/storage.py` (`_back_up_once`), `tests/adapters/test_storage.py`

**Interfaces:**
- Produces: `BACKUP_SUFFIX: Final = ".orig"` in `adapters/storage.py`; the first write of `<name>` copies it to
  `<name>.orig` unless that exists. Nothing else about `LocalFileStore.write` changes.

- [ ] **Step 1: Write the failing test** `test_a_bak_from_the_reference_does_not_stop_the_backup`: a folder with
  `skill.bmd` (`b"original"`) and `skill.bmd.bak` (`b"theirs"`); after `write(skill.bmd, b"new", expected_sha256=...)`:
  `skill.bmd.orig` holds `b"original"` and `skill.bmd.bak` still holds `b"theirs"`. Change the three existing
  assertions on `Text.bmd.bak` and `new.bmd.bak` to `.orig`.
- [ ] **Step 2:** `pytest tests/adapters/test_storage.py -q`. Expected: FAIL, `skill.bmd.orig` does not exist.
- [ ] **Step 3:** Implement. **Step 4:** Run again. Expected: PASS.
- [ ] **Step 5: Commit** `fix(tools): keep a table's original as .orig beside the reference's .bak files`

### Task 2: Record fields: numbers, lists, structs and texts with a tail

**Files:**
- Create: `src/mubmd/core/formats/record_fields.py`, `tests/core/test_record_fields.py`, `src/mubmd/core/leaves.py`,
  `tests/core/test_leaves.py`
- Modify: `src/mubmd/core/formats/record_table_format.py` (its field code moves out; it keeps re-exporting
  `RecordField` so `item_format.py` does not change yet), `src/mubmd/core/ports.py` (`FieldInfo.offset`)

**Interfaces:**
- Consumes: `encode_fixed_text`, `decode_text`, `is_text_encoding` (`core/documents/text_table.py`), `Finding`.
- Produces, in `record_fields.py`:

```python
Kind = Literal["text", "bytes", "uint", "int", "f32", "struct"]

@dataclass(frozen=True, slots=True)
class RecordField:
    name: str
    size: int                                # bytes of one element; a struct's is the span of its fields
    kind: Kind
    count: int | None = None                 # a JSON list of this many elements
    fields: tuple["RecordField", ...] = ()   # the members of a struct
    lowest: int | None = None                # what the client handles; outside it is an OUT_OF_RANGE finding
    highest: int | None = None

def span(field: RecordField) -> int          # size * (count or 1)
def struct(name: str, fields: tuple[RecordField, ...], count: int | None = None) -> RecordField
def fields_view(fields: tuple[RecordField, ...], data: bytes, encoding: str) -> JsonObject
def fields_bytes(fields: tuple[RecordField, ...], value: JsonValue, encoding: str, pointer: str) -> bytes
def leaf_at(fields: tuple[RecordField, ...], tokens: Sequence[str], pointer: str) -> RecordField
def leaf_bytes(field: RecordField, value: JsonValue, encoding: str, pointer: str) -> bytes
def describe(fields: tuple[RecordField, ...], prefix: str, reference: str) -> tuple[FieldInfo, ...]
def out_of_range(fields: tuple[RecordField, ...], value: JsonObject, pointer: str) -> Iterator[Finding]
```

  `leaf_at` raises `POINTER_NOT_FOUND` when the tokens name no leaf. `FieldInfo` gains `offset: int` (bytes from the
  start of the record; a list's is its first element's; 0 for `/texts/N`). `describe` writes a list as
  `<0..count-1>`: `/records/<0..199>/entries/<0..15>/dialogs/<0..3>`.

- The JSON of a value, and what a write accepts (anything else is `INVALID_VALUE` at the value's pointer):

| Kind | Read | Written from |
| --- | --- | --- |
| `uint`, `int` | a number | an `int` that is not a `bool`, inside the field's width |
| `f32` | a finite number; otherwise `{"f32": "<8 hex digits, file order>"}` | a finite number float32 holds, or that object |
| `text` | a string, when it has a 0, decodes, and only zeros follow | a string of at most `size - 1` encoded bytes (`TEXT_TOO_LONG`, `NOT_ENCODABLE` as today) |
| | `{"text": s, "tail": "<hex>"}`, when it decodes up to its first 0 and other bytes follow; `tail` is those bytes without the zeros at the end | that object, when text, 0 and tail fit (`TEXT_TOO_LONG`) |
| | `{"hex": ...}` otherwise | `{"hex": ...}` of exactly `size` bytes |
| `bytes` | `{"hex": ...}` | the same, exactly `size` bytes |
| `struct` | an object | an object with exactly the struct's fields |
| a list | a list | a list of exactly `count` |

- Produces, in `core/leaves.py`:

```python
type Tokens = tuple[str | int, ...]
def is_leaf(value: JsonValue) -> bool          # not a list, and not an object other than {hex}, {f32}, {text, tail}
def leaf_text(value: JsonValue) -> str | None  # a string leaf's text, also of {text, tail}; None for other leaves
def leaves(value: JsonValue, tokens: Tokens = ()) -> Iterator[tuple[Tokens, JsonValue]]
def is_blank(value: JsonValue) -> bool         # "", 0, 0.0, an all-zero {hex}, or a list or object of blanks
```

- [ ] **Step 1: Write the failing tests**, one per behavior:
  - `test_a_signed_field_reads_minus_one`: `RecordField("next", 4, "int")` over `ff ff ff ff` is `{"next": -1}`, and
    back.
  - `test_a_list_field_is_a_json_list`: `RecordField("classes", 1, "uint", count=5)` over `01 00 01 00 00` is
    `[1, 0, 1, 0, 0]`.
  - `test_a_struct_list_names_the_place_of_a_bad_value`: `struct("entries", (RecordField("kind", 1, "uint"),),
    count=2)` with `{"entries": [{"kind": 1}, {"kind": 256}]}` raises `INVALID_VALUE` with pointer
    `/records/3/entries/1/kind` (prefix `/records/3`).
  - `test_a_float_keeps_its_bits`: `00 00 20 40` is `2.5`; `00 00 c0 7f` is `{"f32": "0000c07f"}`; both encode back.
  - `test_a_number_field_refuses_what_json_lets_through` (Review Focus 1), parametrized: `True`, `1.5` and `"7"` for
    a `uint`; `256` for a 1-byte `uint`; `-1` for a `uint`; `32768` for a 2-byte `int`; `float("nan")`,
    `float("inf")`, `1e39` and `True` for an `f32`. Each is `INVALID_VALUE` with the field's pointer.
  - `test_a_text_with_bytes_after_its_zero_keeps_them` (Review Focus 2): a 32-byte `text` holding
    `b"Atlans\0\xd9\xcb\xb9"` and zeros is `{"text": "Atlans", "tail": "d9cbb9"}` and encodes to the same 32 bytes.
  - `test_a_string_over_a_text_with_a_tail_drops_the_tail`: writing `"Atlans 1"` gives `b"Atlans 1"` and 24 zeros.
  - `test_a_text_without_a_zero_stays_hex`: 32 bytes `ff` are `{"hex": "ff" * 32}`.
  - `test_a_text_and_tail_that_do_not_fit_are_refused`: `{"text": "a" * 30, "tail": "0102"}` in 32 bytes is
    `TEXT_TOO_LONG`.
  - `test_a_struct_refuses_missing_and_extra_fields` and `test_a_list_refuses_another_length` (Review Focus 3):
    `INVALID_VALUE` at the struct's or the list's pointer.
  - `test_describe_gives_each_leaf_its_pointer_and_offset`: for a struct list at offset 0x26 with `dialogs` at +0xC,
    the pointer above and `offset == 0x32`.
  - `test_out_of_range_reports_the_pointer`: `RecordField("answer_count", 4, "int", highest=10)` with 11 yields one
    `Finding("OUT_OF_RANGE", Severity.ERROR, "/records/0/answer_count", ...)`; with 10, none.
  - `tests/core/test_leaves.py`: `leaves` over `{"a": [1, {"hex": "00"}], "b": {"text": "x", "tail": "01"}}` yields
    `(("a", 0), 1)`, `(("a", 1), {"hex": "00"})`, `(("b",), {"text": "x", "tail": "01"})`; `leaf_text` is `"x"` for
    the last and `None` for the first two; `is_blank` is true for `""`, `0`, `{"hex": "0000"}`, `[0, ""]` and false
    for `{"text": "", "tail": "65"}` and `{"hex": "ff"}`.
- [ ] **Step 2:** `pytest tests/core/test_record_fields.py tests/core/test_leaves.py -q`. Expected: FAIL on import.
- [ ] **Step 3:** Implement both modules; `record_table_format.py` calls `fields_view` and `fields_bytes`.
- [ ] **Step 4:** `pytest -q`. Expected: PASS, the 257 earlier tests included (none pins a hex name for a text with a
  tail).
- [ ] **Step 5: Commit** `feat(tools): describe record fields as numbers, lists, structs and texts with a tail`

### Task 3: Tables with a count, tables with no checksum, and their findings

**Files:**
- Modify: `src/mubmd/core/formats/record_table_format.py`, `src/mubmd/core/ports.py`,
  `src/mubmd/core/formats/text_format.py`, `src/mubmd/core/checks/text_checks.py`
- Test: `tests/core/test_record_table_format.py` (new), `tests/core/test_text_format.py`

**Interfaces:**
- Consumes: Task 2.
- Produces:

```python
@dataclass(frozen=True, slots=True)
class RecordTableSchema:
    name: str
    count: int | None              # None: the file starts with a plain int32 count of its records
    record_size: int
    fields: tuple[RecordField, ...]
    checksum_key: int | None       # None: no checksum word follows the records
    reference: str
    xor_odd_words: bool = True
    section_size: int | None = None

class RecordTableFormat:
    file_size: int | None          # count * record_size, plus 4 with a checksum; None for a counted table

@dataclass(frozen=True, slots=True)
class ValueCheck:                  # core/ports.py
    limit: int | None              # the encoded bytes a text at that pointer may hold; None for other leaves
    warnings: tuple[Finding, ...]

class FormatCodec[D](Protocol):    # one more method
    def check_value(self, pointer: str, value: JsonValue, original: D) -> ValueCheck: ...

@dataclass(frozen=True, slots=True)
class Summary:                     # one more field
    section_size: int | None = None

def check_text_record(index: int, record: bytes, before: bytes | None, encoding: str) -> tuple[Finding, ...]
```

  - A counted table decodes `count` records after the count word and keeps the rest as `trailing`; it encodes
    `len(records)` as the count. A count below 0, or of more records than the file holds, is `CORRUPT_FILE` whose
    message gives the count and how many records fit. Its view accepts a `/records` list of any length.
  - A table with no checksum has no `records_sha256` and no `stored_checksum` in its view; every byte after its
    records is `trailing`.
  - `check` adds `out_of_range` of every record to `CHECKSUM_MISMATCH`.
  - `check_value` checks one leaf without rebuilding the table (an item table has 250,000 leaves): it raises what
    `set` would raise for that value, and returns the text limit (`size - 1`; 300 for `/texts/N`) and the findings
    of that value alone (`OUT_OF_RANGE`; for a text record, `check_text_record`'s). A pointer that is not a leaf of
    the format, or a record the table does not have, is `POINTER_NOT_FOUND`.
  - `check_text_table` keeps its results and is built on `check_text_record`.

- [ ] **Step 1: Write the failing tests** on a schema of two fields (`name` text 4, `value` int 4, `highest=10`), once
  with `count=2` and once counted:
  - `test_a_counted_table_reads_as_many_records_as_its_count`: count 2 and three records' worth of bytes give two
    records and 8 bytes of `trailing`.
  - `test_a_counted_table_writes_the_length_of_its_list`: a view with three records encodes `03 00 00 00` first and
    the same `trailing` last.
  - `test_a_count_the_file_cannot_hold_is_corrupt` (Review Focus 4), parametrized with `-1`, `3` over two records'
    bytes, and `0x7FFFFFFF`: `CORRUPT_FILE`.
  - `test_a_table_without_a_checksum_has_none_in_its_view`: keys are `format_version`, `format`, `encoding`,
    `records`, `trailing`.
  - `test_check_reports_a_value_the_client_cannot_hold`: `value` 11 gives `OUT_OF_RANGE` at `/records/1/value`.
  - `test_check_value_checks_one_leaf`: `("/records/1/name", "abcd")` raises `TEXT_TOO_LONG`; `"abc"` returns
    `ValueCheck(3, ())`; `("/records/1/value", 11)` returns `limit None` and one `OUT_OF_RANGE`; `/records/9/name` and
    `/records/1/nope` raise `POINTER_NOT_FOUND`.
  - `test_text_check_value_warns_of_a_text_that_fills_its_record` (`tests/core/test_text_format.py`): 300 bytes at
    `/texts/5` return `limit 300` and one `TEXT_FILLS_RECORD`.
- [ ] **Step 2:** `pytest tests/core -q`. Expected: FAIL. **Step 3:** Implement. **Step 4:** `pytest -q`. Expected:
  PASS; `mypy src tests` clean.
- [ ] **Step 5: Commit** `feat(tools): read tables with a count or no checksum, and check one value at a time`

### Task 4: Items in full, skills, gates, filters, item sets and credits

**Files:**
- Create: `src/mubmd/core/formats/skill_format.py`, `gate_format.py`, `filter_format.py` (two formats),
  `item_set_format.py` (two formats), `credit_format.py`; `tests/core/test_tables.py`,
  `tests/adapters/cli/test_tables.py`
- Modify: `src/mubmd/core/formats/item_format.py`, `src/mubmd/core/catalog.py`, `tests/conftest.py`,
  `tests/core/test_catalog.py`, `tests/adapters/cli/test_items.py`

**Interfaces:**
- Consumes: Tasks 2 and 3; the layouts of "The tables".
- Produces: one `<NAME>_FORMAT: Final = RecordTableFormat(RecordTableSchema(...))` per format; in the catalog,
  `FormatSpec(name, size: int | None, files, codec, model_folder: str | None = None)` with `size` from
  `codec.file_size`; `identify` skips a spec when the file's two parent folders are `data/<model_folder>` without
  case (#470's rule for `item`, now also `skill`). `all_formats()` order: `text`, `item`, `skill`, `gate`, `filter`,
  `filter_name`, `item_set_type`, `item_set_option`, `credit`, then Task 5's. In `tests/conftest.py`:
  `TABLE_FILES: tuple[str, ...]`, every catalog file relative to `Data`, and `working_copy` copies them all.

- [ ] **Step 1: Write the failing tests** in `tests/core/test_tables.py`, parametrized over this task's files:
  - `test_a_table_round_trips`: `encode(decode(data)) == data` and `from_view(to_view(document)) == document`.
  - `test_a_table_has_the_records_the_research_counts`: the used records of "The tables".
  - `test_the_checksums_the_client_checks_match`: `check(document, None) == ()` for `skill.bmd`, `Skilltest.bmd`,
    `Filter.bmd`, `FilterName.bmd` and the four item set files.
  - `test_known_records_read_as_the_research_says`, one case each: `item.bmd` record 0 (`name` "Kris", `level` 6,
    `width` 1, `height` 2, `damage_min` 6, `damage_max` 11, `attack_speed` 50, `durability` 20, `required_strength`
    40, `required_agility` 40, `classes` `[1, 1, 1, 1, 1]`); `skill.bmd` record 1 (`name` "Poison", `level` 30,
    `damage` 12, `mana` 42, `distance` 6, `energy` 100, `classes` `[1, 0, 0, 1, 0]`); `Skilltest.bmd` record 1 `name`
    "포이즌"; `Gate.bmd` record 1 (`flag` 1, `map` 0, `x1` 121, `y1` 232, `x2` 123, `y2` 233, `target` 2, `level`
    20); `FilterName.bmd` record 0 `word` "ADMIN" and record 6 `word` `{"text": "", "tail": "65627a656e"}`;
    `itemsettype.bmd` record 4096 `sets` `[3, 0, 0]`; `itemsetoption.bmd` record 3 (`name` with text "Hyperion",
    `full_set_pieces` 4, `full_options` `[19, 12, 255, 255, 255]`) and record 0 `name` `{"hex": "ff" * 32}`;
    `credit.bmd` record 0 `{"kind": 1, "text": "STAFF"}` and record 244 `kind` 0.
  - `test_raw_fields_are_named_after_their_offset`: in every schema of the catalog, a `bytes` field's name is
    `raw_0x` and its offset inside its record or struct, two hex digits at least.
  - `tests/core/test_catalog.py`: `test_a_table_name_inside_a_model_folder_is_unknown` (Review Focus 5),
    parametrized with `Data/Item/item.bmd`, `data/ITEM/Item.bmd`, `Data/skill/skill.bmd`, `DATA/Skill/SKILL.BMD`:
    `UNKNOWN_FORMAT`; `Data/Local/skill.bmd` is `skill`. Update the two tests that pin the format list and that
    `Skill.bmd` is unknown.
  - `tests/adapters/cli/test_tables.py`: `test_editing_a_skill_updates_its_checksum`: `set Local/skill.bmd
    /records/1/mana 45` exits 0, `validate` on the file has no findings, and the bytes outside record 1 and the
    checksum word are the reference's.
  - `tests/adapters/cli/test_items.py`: any assertion on `raw_0x22` or `raw_0x26` moves to the named fields.
- [ ] **Step 2:** `pytest tests/core/test_tables.py tests/core/test_catalog.py -q`. Expected: FAIL (unknown formats).
- [ ] **Step 3:** Write the six schema modules and the catalog entries.
- [ ] **Step 4:** `pytest -q`. Expected: PASS. `mubmd roundtrip ../../assets/Data` reports `checked: 18, differing: []`.
- [ ] **Step 5: Commit** `feat(tools): read items in full, skills, gates, filters, item sets and credits`

### Task 5: Dialogs, quests, the slide, move requirements and monster skills

**Files:**
- Create: `src/mubmd/core/formats/dialog_format.py`, `quest_format.py`, `slide_format.py`,
  `move_requirement_format.py`, `monster_skill_format.py`
- Modify: `src/mubmd/core/catalog.py`, `tests/conftest.py` (`TABLE_FILES`), `tests/core/test_tables.py`,
  `tests/adapters/cli/test_tables.py`

**Interfaces:**
- Consumes: Tasks 2 to 4.
- Produces: the five formats, after `credit` in `all_formats()`: `slide`, `move_requirement`, `monster_skill`,
  `dialog`, `quest`. `FormatSpec.size` is `None` for the two counted ones.

- [ ] **Step 1: Write the failing tests**: this task's files join the parametrized tests of Task 4, and:
  - `test_known_records_read_as_the_research_says` gains: `Dialog.bmd` record 53 (`answer_count` 3, `next` `[52, 54,
    53, -1, 0, 0, 0, 0, 0, 0]`, `actions` `[-1, 1, 2, 0, 0, 0, 0, 0, 0, 0]`, `answers[0]` "Please repeat the
    story.") and record 0 `text` `{"text": "Aew! Precisa de alguma coisa?", "tail": "643f"}`; `quest.bmd` record 0
    (`entry_count` 2, `condition_count` 3, `npc_type` 235, `entries[0]` with `item_group` 14, `item_index` 23,
    `dialogs` `[52, 54, 55, 56]`; `conditions[0]` with `link` 1, `fail_dialog` 73); `questtest.bmd` record 0 `name`
    "'제왕의 서'를 찾아라!"; `slide.bmd` record 0 (`period` 60, `speed` 2.5, `groups[0]` with `max_level` 15,
    `count` 28, `texts[0]` "Precisa esta no level 15 para entrar em Devias"; `groups[3].count` -1); `movereq.bmd`
    (28 records, 640 bytes of `trailing`, record 0 `{"number": 1, "name": "Arena", "search_name": "Arena", "level":
    50, "zen": 2000, "gate": 200}`, record 10 `name` `{"text": "Atlans", "tail": "d9cbb9"}`); `movereqtest.bmd`
    record 0 `name` "竞技场"; `MonsterSkill.bmd` (8 records, record 0 `type` 304, `skills` `[4, 1, -1, -1, -1]`).
  - `tests/adapters/cli/test_tables.py`: `test_roundtrip_reads_every_table`: `roundtrip` on the reference gives
    `checked 29, identical 29, differing [], skipped 1965`. `test_the_reference_has_only_its_known_findings`:
    `validate` on the reference lists exactly the findings of "The tables".
    `test_adding_a_move_requirement_grows_the_file`: `apply` replacing `/records` with its 28 records and one more
    exits 0; the file starts with `1d 00 00 00`, is 0x50 bytes longer and ends with the same 640 bytes.
- [ ] **Step 2:** Run those tests. Expected: FAIL (unknown formats). **Step 3:** Write the five schema modules and
  the catalog entries. **Step 4:** `pytest -q`. Expected: PASS.
- [ ] **Step 5: Commit** `feat(tools): read dialogs, quests, the slide, move requirements and monster skills`

### Task 6: The use cases and the CLI for any table

**Files:**
- Modify: `src/mubmd/app/documents.py`, `app/edits.py`, `app/queries.py`, `app/session.py`,
  `src/mubmd/adapters/cli/commands.py`; `tests/app/test_edits.py`, `test_queries.py`, `test_session.py`,
  `tests/app/conftest.py`, `tests/adapters/cli/test_tables.py`

**Interfaces:**
- Consumes: `leaves`, `leaf_text` (Task 2), `FormatCodec.check_value`, `Summary.section_size` (Task 3).
- Produces:
  - `app/documents.py`: `parse_value(raw: str, current: JsonValue, pointer: str) -> JsonValue`, moved from `edits.py`:
    `raw` is taken as written when `leaf_text(current)` is not `None` (a string, or a text with a tail), and is JSON
    otherwise (`INVALID_VALUE`).
  - `app/queries.py`: `find(..., field: str | None = None)` searches `leaf_text` of every leaf under the view's
    lists, reports a text with a tail at its own pointer with its text, never searches hex, and with `field` keeps
    the leaves whose last name in the pointer is `field`. `FormatInfo.size: int | None`.
  - `app/session.py`: `Measure(size: int | None, limit: int | None, error: MubmdError | None, warnings)`;
    `EditSession.parse(pointer: str, raw: str) -> JsonValue`; `EditSession.measure(pointer: str, value: JsonValue)
    -> Measure`, through `check_value` (no table rebuilt, no format name, no size constant in this module);
    `EditSession.findings -> tuple[Finding, ...]`, every finding of the current view.
  - CLI: `find` gets `--field NAME`; the examples of `schema` and `get` stay texts.

- [ ] **Step 1: Write the failing tests**:
  - `test_set_takes_a_text_with_a_tail_as_written` (Review Focus 2): `set Local/movereq.bmd /records/10/name
    'Atlans 1'` changes `{"text": "Atlans", "tail": "d9cbb9"}` to `"Atlans 1"`, and only those 32 bytes differ from
    the reference.
  - `test_set_parses_a_number`: `set Gate.bmd /records/1/level 25` exits 0 with `new: 25`.
  - `test_set_refuses_a_value_its_field_cannot_hold` (Review Focus 1), parametrized: `300` and `true` at
    `Gate.bmd /records/1/level`, `NaN` and `Infinity` at `Local/slide.bmd /records/0/speed`: exit 2, `INVALID_VALUE`
    with the pointer, the file's bytes unchanged.
  - `test_a_whole_record_must_keep_its_fields` (Review Focus 3): `apply Gate.bmd` replacing `/records/1` with
    `{"flag": 1}` is `INVALID_VALUE` at `/records/1`; replacing `/records/1/price` of `Local/skill.bmd` (no such
    field) is `POINTER_NOT_FOUND`; nothing is written.
  - `test_find_reads_a_text_with_a_tail`: `find Local/Dialog.bmd Aew` gives one item, pointer `/records/0/text`,
    text "Aew! Precisa de alguma coisa?".
  - `test_find_field_keeps_one_field`: `find Local/movereq.bmd Arena` gives two items (`name`, `search_name`) and
    with `--field name` one, `/records/0/name`.
  - `test_find_never_matches_hex`: `find Local/itemsetoption.bmd ffff` has `total 0`.
  - `tests/app/test_session.py`: `test_measure_of_a_number_has_no_byte_limit` (gate `level` 25: `size None`, `limit
    None`, no error); `test_measure_of_a_bad_number_is_an_error` (`"abc"` through `parse` raises `INVALID_VALUE`);
    #470's `test_item_name_measure_reserves_one_byte_for_the_terminator` and stage 2's two `measure` tests pass
    unchanged in what they assert.
- [ ] **Step 2:** Run them. Expected: FAIL. **Step 3:** Implement. **Step 4:** `pytest -q`. Expected: PASS.
- [ ] **Step 5: Commit** `feat(tools): find, set and measure values in any table`

### Task 7: The console UI for any table

```text
╭─ ◆ mubmd ─ ~/mu/Data ──────────────────────────────────────── working copy · 1 unsaved ─╮
│ ╭ Data ───────────────╮ ╭ Local/Dialog.bmd ─ dialog · XOR3 per record · 75/200 used ──╮ │
│ │ ▾ Local             │ │    #  text                                                  │ │
│ │   ▤ credit.bmd      │ │   52  O 'Scroll of Emperor', que fala de Muren, o primeiro… │ │
│ │   ◆ Dialog.bmd   ●  │ │   53● O 'Scroll of Emperor' possui uma forca poderosa. Qua… │ │
│ │   ▤ item.bmd        │ │   54  Va a procura do 'Scroll of Emperor'. Voce sera conce… │ │
│ │   ≡ Text.bmd        │ ╰──────────────────────────────────────────── used · 75 rows ─╯ │
│ │ ▸ Monster           │ ╭ record 53 ──────────────────────────────────────────────────╮ │
│ │ ▤ Gate.bmd          │ │ text            O 'Scroll of Emperor' possui uma forca pod… │ │
│ ╰─────────────────────╯ │ answer_count    3                                           │ │
│                         │ next/0          52                                          │ │
│                         │ actions/0       -1                                          │ │
│                         │ answers/0     ● Conte a historia de novo.                   │ │
│                         ╰─────────────────────────────────────────────────────────────╯ │
│                         ╭ /records/53/answers/0 ──────────────────────────────────────╮ │
│                         │ Conte a historia de novo.▏                     25/63 bytes  │ │
│                         ╰─────────────────────────────────────────────────────────────╯ │
│ / search  f filter  enter edit  ctrl+z undo  ctrl+s save  d diff  ctrl+p commands  q    │
╰─────────────────────────────────────────────────────────────────────────────────────────╯
```

**Files:**
- Create: `src/mubmd/app/rows.py`, `tests/app/test_rows.py`, `src/mubmd/adapters/tui/widgets/field_list.py`,
  `tests/adapters/tui/test_tables.py`
- Modify: `src/mubmd/adapters/tui/widgets/record_table_view.py`, `record_editor.py`, `bmd_tree.py`,
  `src/mubmd/adapters/tui/app.py`, `mubmd.tcss`; `tests/adapters/tui/test_item.py`, `test_snapshots.py`

**Interfaces:**
- Consumes: `EditSession.parse`, `measure`, `findings`, `summary` (Task 6); `leaves`, `leaf_text`, `is_blank`
  (Task 2); `identify` for the tree's glyphs.
- Produces, in `app/rows.py` (no Textual import):

```python
class RowFilter(StrEnum): ALL = "all"; USED = "used"; CHANGED = "changed"; FINDINGS = "findings"
def rows_key(view: JsonObject) -> str                     # "records" or "texts"
def row_label(value: JsonValue) -> str                    # a leaf's text; a record's first text, or "k v · k v"
def row_matches(value: JsonValue, query: str) -> bool     # any text leaf contains the query, without case
def row_shown(pointer: str, value: JsonValue, original: JsonValue, findings: tuple[Finding, ...],
              which: RowFilter) -> bool            # findings at or under the row's pointer count for FINDINGS
```

- What the UI does:
  1. One row per record: `#`, `section` when the format has sections, the label, and `bytes` only when rows are
     leaves (texts). The text table looks and behaves as in stage 2: its tests and snapshot pass unchanged.
  2. For a record table, a field list under the table shows every leaf of the selected record: its pointer inside
     the record (`answers/0`), its value, and `●` when it changed. `enter` on a row moves to the list; `enter` on a
     field loads it in the editor, whose title is the full pointer; `escape` goes back one step.
  3. The editor takes a text leaf as written and shows `n/limit bytes`; a text with a tail loads its text; a number
     leaf takes a number, and anything else marks the editor invalid and keeps nothing. A `{"hex"}` or `{"f32"}`
     leaf is not edited here: the status line says to use `mubmd set`.
  4. `f` cycles the row filter (all, used, changed, findings); the table's lower border names it with the row count.
     The palette gets "Filter rows". `/` searches any text leaf of a row.
  5. The tree marks text tables and record tables with a glyph each and dims the files this version does not read.

- [ ] **Step 1: Write the failing tests** (`run_test` and `Pilot`, size 120×40):
  - `tests/app/test_rows.py`: `row_label` of a dialog record is its text, of a gate record starts with `flag 1 · map
    0`, of `{"text": "Atlans", "tail": "d9cbb9"}` is "Atlans"; `row_matches` finds "story" in dialog 53's answers;
    `row_shown` with `USED` hides a blank record and shows `{"sets": [255, 255, 255], ...}`.
  - `test_opening_a_dialog_table_lists_its_records`: `Dialog.bmd` shows 200 rows; after `f`, 75, and the lower border
    says `used · 75 rows`.
  - `test_a_record_shows_its_fields`: with row 53 selected the field list has `answers/0` with "Please repeat the
    story.".
  - `test_editing_a_field_saves_only_that_field`: `answers/0` of record 53 becomes "Conte de novo."; after save, the
    record's bytes at 0x180..0x1C0 decode to it and every other byte of the file equals the reference's.
  - `test_a_number_field_takes_a_number`: `level` of gate 1 set to 25 and saved: byte 8 of record 1 is 25 after
    XOR3. `test_a_number_field_refuses_letters`: `abc` marks the editor `-invalid`, `enter` keeps the focus and the
    session has no change.
  - `test_a_text_with_a_tail_is_edited_as_its_text` (Review Focus 2): `name` of move requirement 10 loads "Atlans";
    saving "Atlans 1" shows one change in the diff screen, from the tailed object to the string.
  - `test_search_looks_in_every_text_of_a_row`: `/` then "Arena" on `movereq.bmd` leaves one row.
  - `test_item_rows_keep_their_section`: `item.bmd` row 7168 has `section` 14; #470's item tests reach the name
    through the field list (their `edit_name` helper changes, their assertions do not).
  - `test_the_tree_dims_what_it_cannot_open`: a folder with `Text.bmd`, `Gate.bmd` and a model shows two glyphs and
    one dimmed label.
  - A snapshot `test_a_record_table`: `Dialog.bmd` with row 53 selected.
- [ ] **Step 2:** `pytest tests/adapters/tui tests/app/test_rows.py -q`. Expected: FAIL.
- [ ] **Step 3:** Implement. **Step 4:** `pytest -q`. Expected: PASS; the stage 2 snapshot unchanged. Look at the new
  snapshot in `snapshot_report.html` before accepting it with `--snapshot-update`.
- [ ] **Step 5:** Save screenshots of the dialog table, an item's fields and the filter (dark theme) to
  `/mnt/project-files/bmd-editor/`.
- [ ] **Step 6: Commit** `feat(tools): show and edit any table's records in the console UI`

### Task 8: The skill, the README and the spec

**Files:**
- Modify: `.claude/skills/muret-bmd/SKILL.md`, `src/mubmd/adapters/cli/describe.py` (`BRIEF`),
  `Client/tools/bmd/README.md`, `AGENTS.md` (the tool's line), `docs/superpowers/specs/2026-11-25-bmd-editor-design.md`,
  `docs/superpowers/plans/2026-11-25-bmd-editor.md` (stage 3's line links here)

**Interfaces:**
- Consumes: the finished CLI. `tests/skill/test_recipes.py` runs every recipe; `BRIEF` equals the skill's first
  paragraph.

- [ ] **Step 1: Baseline, before touching the skill** (superpowers:writing-skills): one fresh agent with the tool
  installed and today's skill gets three jobs on a working copy and reports its calls: raise Poison's mana to 45 in
  `skill.bmd` and say whether the file still validates; translate the three answers of dialog 53 to Spanish in one
  write; say which gate leads from Lorencia to Devias and its level. Keep what it gets wrong.
- [ ] **Step 2: Write the failing tests**: the new recipes, with their `<!-- recipe: ... -->` marks, so
  `test_every_recipe_runs` fails on the ones the old text cannot satisfy: `change-an-item-field` (`set
  $WORK/Local/item.bmd /records/0/durability 22`), `rename-a-skill` (`apply` with `test` and `replace` on
  `/records/1/name`), `read-a-gate` (`get $REF/Gate.bmd /records/18`), `translate-a-dialog` (`get` record 53, then
  one `apply`), `list-the-used-names` (`find $REF/Local/skill.bmd . --regex --fields pointer,text --limit 256`), and
  #470's item rename gets its mark.
- [ ] **Step 3:** Rewrite the skill: the first paragraph and `BRIEF` say "texts and tables"; a table of the 14
  formats (file, what a record is, one pointer); the jobs table gains "change a number" and "find a record by a
  name, then read it whole"; the rule for `{"text", "tail"}` beside the one for `{"hex"}`; `.orig`; the reference's
  known findings; `INVALID_VALUE` covers a number outside its field; `--field`.
- [ ] **Step 4:** `pytest tests/skill -q`. Expected: PASS. Run the three jobs again with a fresh agent and the new
  skill: each finishes in a few calls without reading the tool's source. Fix what it still gets wrong and note both
  call counts for the PR body.
- [ ] **Step 5:** README: the formats table, the record view with the three shapes of a text, the UI's new keys,
  `.orig`, stage 3 done. Spec: the four places that would now contradict the tool (decisions 1, 2, 6 and 7).
- [ ] **Step 6: Commit** `docs(tools): teach the muret-bmd skill every table`

### Task 9: Finish stage 3

- [ ] **Step 1:** From `Client/tools/bmd`, on Python 3.13 and 3.12: `ruff check .`, `ruff format --check .`,
  `mypy src tests`, `pytest -q`. Expected: all pass.
- [ ] **Step 2:** `mubmd roundtrip ../../assets/Data`. Expected: `checked 29, identical 29, differing [], skipped
  1965`.
- [ ] **Step 3:** `git diff --name-only origin/main...HEAD`: every path under `Client/tools/`, `.claude/`, `docs/`, or
  a `*.md`.
- [ ] **Step 4:** One fresh reviewer reads the whole branch against this plan, its Review Focus and the spec; fix
  what it finds that is critical or important, each with a test that failed first.
- [ ] **Step 5:** Push and open **PR 3**, `feat(tools): edit every table with mubmd`, with the decisions taken
  and what stays deferred in its body; assign `ema-ljn`, request his review, subscribe to its activity.
