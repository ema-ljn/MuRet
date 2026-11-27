# BMD editor: a Python tool to read, check and edit MU's `.bmd` files (design)

> **Working document, 2026-11-25.** Emanuel's request: "an interpreter or an editor of `.bmd` files, in Python,
> following Python's best practices in code and architecture, AI friendly, maybe with skills"; then "to edit
> `Text.bmd` and those files inside the client's assets"; then "a nice, popular console UI, friendly for AI, that
> also has a CLI usable from a SKILL.md"; and, on the look, "give it a nice format, surprise me". He picked Textual
> for the console UI, and a separate working copy for the edited files. Then: "it has to be AI friendly: its CLI
> optimized for AI use, together with SKILL.md files for its use".
>
> **Renamed `mutools` on 2026-11-27.** Emanuel: "rename mubmd to mutools", together with a better design and an
> editor that changes models, adds new ones and animates them. The folder is `Client/tools/mutools`, the package and
> the command `mutools`; the finished stages' plans keep the old name.

## What and why

`Client/assets/Data/` holds 1,994 `.bmd` files: 1,962 3D models and 32 tables (texts, items, skills, gates, dialogs,
quests and others). Today only the client's C++ readers open them, and they read just what the client needs. Nobody
can look inside one, change a text or an item, and get a file the client reads, without writing code.

- **Goal:** open any `.bmd` of a `Data` folder, see what it holds with the names the research gives each field,
  change it, and write a file that the client reads exactly as the original would.
- **Goal:** AI friendly first. The CLI is an API whose main caller is an agent (JSON by default, stable shapes and
  error codes, self-description, no prompts), and a project skill teaches an agent to use it. A person gets the same
  operations in a console UI.
- **Goal:** a codebase that shows Python done well: typed, layered (ports and adapters), small modules, tested.
- **Non-goal:** a 3D modeling tool. Geometry and animation are editable through JSON, not by hand in the UI.
- **Non-goal:** importing from or exporting to Blender, glTF or OBJ. It can be a later stage.
- **Non-goal:** touching the client (`Client/src`), its build or its tests. The tool stands alone under
  `Client/tools/mutools/`.
- **It would be a failure:** a file read and written without changes that is not byte-identical to the original; an
  edited file that the client reads differently from what the tool showed; or silently changing the reference data in
  `Client/assets/Data`.

## Constraints

- **Byte-exact.** Every byte read is kept: padding, the bytes after a name's first `0`, the 38 bytes at the end of
  each 64-byte triangle that the client never reads (28 it does not copy and 10 it copies and nothing uses), trailing
  bytes after the last part (`item.bmd` has 80), the `"BMD"` prefix, the encrypted block's size. An unedited document
  writes back identical bytes. This is the project's fidelity rule, and the round trip over every file of
  `Client/assets/Data` is its test.
- **The client's C++ model is not enough.** `Client::Data::Models::Model` drops what the client does not use
  (`Client/src/Data/Models/Bmd.cpp`), so the tool has its own lossless model. The formats themselves come from
  `Client/docs/research/05-formatos-de-datos.md`, the C++ readers in `Client/src/Data/`, and the research repo's
  pseudocode and ASM.
- **The reference data is read-only.** `Client/assets/README.md`: nothing inside `Data/` changes, and `Data.sha256`
  pins every file for the golden tests.
- **Repo rules.** Code, docs, commits and PR text in English; comments only when strictly necessary
  (`docs/CODING_RULES.md` §7 and §9); dependencies justified (§10); one behavior, one test (§13); Angular-style commits
  and PR titles (`AGENTS.md`).
- **Library rule (§10).** MU's formats, checksums and ciphers are MU's own and are written by hand. Generic needs
  (UI, CLI, testing, linting) use libraries.
- **Python.** 3.12 or newer (CI uses 3.13, the version of the research scripts).

## Approaches

Two ways to read and write the formats were tried in a throwaway spike that read and rewrote all 1,962 models:

| | A. Standard library (`struct`) | B. `construct` (declarative) |
| --- | --- | --- |
| Round trip of the 1,962 models | byte-identical, 7.5 s | byte-identical, 20 s |
| Dependencies of the core | none | `construct` (1.0k stars) |
| Types | frozen dataclasses, checked by mypy | untyped `Container` dicts |
| Pitfalls | reader and writer written separately | field names clash with dict methods (`keys` broke the spike) |
| MU's ciphers (XOR3, K16) and checksum | by hand | by hand anyway |

**Picked: A** (Emanuel, 2026-11-25). The round-trip tests over every real file catch any drift between reader and
writer, and §10 asks for MU's formats by hand. Kaitai Struct was dropped: it needs a separate compiler (JVM) to
generate the Python.

For the interfaces, compared by popularity and fit:

| Library | Stars | License | Verdict |
| --- | ---: | --- | --- |
| Textual | 37.5k | MIT | **picked** for the console UI: widgets (tree, data table, inputs, command palette), CSS themes, headless tests with `run_test` and `Pilot`, SVG snapshots |
| Typer | 20.3k | MIT | **picked** for the CLI: commands declared with type hints, help generated, built on Click |
| Rich | 57.6k | MIT | comes with Textual and Typer; used for the CLI's human output |
| prompt_toolkit | 10.6k | BSD-3 | lower level, about twice the code for the same screens |
| urwid | 3.0k | LGPL-2.1 | older API, copyleft |

## The design

### Where it lives

```text
Client/tools/mutools/
  pyproject.toml            package `mutools`, console script `mutools`, pinned dependencies, ruff, mypy, pytest config
  README.md                 what it does, install, the commands, the JSON format
  src/mutools/
    core/                   no third-party import
      domain/               the documents: frozen dataclasses, one module per kind
      formats/              codecs: bytes <-> document, one module per format; ciphers and checksum
      catalog.py            which file is which format
      ports.py              the protocols the core needs (Codec, DocumentStore)
    app/                    use cases: open, inspect, validate, get, set, dump, build, diff, roundtrip
    adapters/
      storage.py            the file system: atomic writes, backups, the read-only reference folder
      json_codec.py         document <-> JSON
      cli/                  Typer
      tui/                  Textual
      web/                  NiceGUI and three.js
  tests/                    mirrors src/
```

A `src/` layout keeps the tests importing the installed package, not the folder. Changes under `Client/tools/` skip
the C++ build in CI, so a tool PR runs only its own checks.

### Layers (ports and adapters)

- **Domain** (`core/domain`): one frozen, slotted dataclass tree per kind of file. `ModelDocument` (meshes, actions,
  bones, with every raw byte kept), `TextTableDocument`, and `RecordTableDocument` for the fixed-record tables, plus the
  container facts every document carries (`version`, `encrypted`, `stored_checksum`, `trailing`). Edits are pure
  functions that return a new document (`dataclasses.replace`), so undo and redo are a stack of documents.
- **Formats** (`core/formats`): a `Codec[D]` per format with `decode(bytes) -> D` and `encode(D) -> bytes`. XOR3, K16
  and the table checksum are small modules of their own, ported from the client (`Core/Xor3`, `Data/Terrain/K16`,
  `Data/Tables/TableChecksum`) and from the research.
- **Validation** (`core/domain/checks.py`): rules that report findings, never raise: indices outside their mesh, a
  parent after its child (the research's `BloodCastle01` exception), a text longer than its record, bytes that
  CP1252 cannot encode, a checksum that does not match, a count over the client's limits. Each finding has a code,
  a JSON Pointer into the document and a severity.
- **Use cases** (`app/`): plain functions that take ports and return result dataclasses. The CLI and the TUI call the
  same ones; neither has logic of its own.
- **Adapters**: the file system, JSON, the CLI, the TUI and the web viewer. Only adapters import Typer, Textual or
  Rich, and only `adapters/web` imports the web libraries (NiceGUI, FastAPI, Starlette, uvicorn); the CLI imports
  `adapters/web` only inside the `web` command, so every other command starts as fast as before. A test reads the
  imports of every module (`ast`) and fails if `core` imports `app` or `adapters`, or anything outside the standard
  library.

Patterns used where they fit, not for their own sake: Strategy (one codec per format behind one protocol), Registry
(the catalog), Command (edits as values, undoable), Facade (the use cases).

### Formats and the catalog

The catalog maps a file to its codec by its name (case-insensitive, as Windows and the client do), checked against
its size, or by the `"BMD"` prefix for models.

| Kind | Files in `Client/assets/Data` | Layout | Cipher | Checksum |
| --- | --- | --- | --- | --- |
| Model | 1,962 `*.bmd` starting with `BMD` | version 0x0C: u32 size + block; other versions: plain | K16 on 0x0C | — |
| Text | `Local/Text.bmd` (and `Texttest`; `TKOU` has the same layout in Chinese) | 2048 × 300 | XOR3 | — |
| Item | `Local/item.bmd` (`itemtest`, `n380`) | 8192 × 0x54 | XOR3 per record | 0xE2F1 |
| Skill | `Local/skill.bmd` (`Skilltest`) | 256 × 0x3C | XOR3 per record | 0x5A18 |
| Gate | `Gate.bmd` (`Gatetest`) | 256 × 9 | XOR3 per record | — |
| Filters | `Local/Filter.bmd`, `FilterName.bmd` | 1000 × 20 | XOR3 per record | 0x3E7D, 0x2BC1 |
| Item sets | `itemsettype.bmd`, `itemsetoption.bmd` (and `test` twins) | 8192 × 4, 64 × 0x4C | XOR3 per record | 0xE5F1, 0xA2F1 |
| Credits | `Local/credit.bmd` (`credittest`) | 400 × 33 | XOR3 of the login, whole file | — |
| Slide | `Local/slide.bmd` (`slidetest`) | period, speed, 5 × 0x2008 | XOR3 whole file | — |
| Move requirements | `Local/movereq.bmd` (`movereqtest`, `TKOUREQ`) | count + count × 0x50 (28 used of the 36 the file has room for) | XOR3 per record | — |
| Monster skills | `Local/MonsterSkill.bmd` (`test` twin) | count + count × 0x1C | XOR3 per record | — |
| Dialogs | `Local/Dialog.bmd` (`Dialogtest`) | 200 × 0x400 | XOR3 per record | — |
| Quests | `Local/quest.bmd` (`questtest`) | 200 × 0x2A8 | XOR3 per record | — |

Details (which XOR3 key, where the count goes, which parity the checksum uses) come from research 05 and the C++
readers named there. Every byte of a record belongs to exactly one field; a field the research has not named yet is
`raw_0xNN` (its offset) with its bytes, and a test checks that each schema covers its record size with no gap or
overlap. Files the client does not read (the `test` twins, `TKOU`, `n380`) are opened with the layout they match and
marked "not read by the client". Their encodings come from the files: the `test` twins of items, skills, item set
options, the slide, dialogs and quests are Korean (`cp949`); `movereqtest.bmd`, `TKOUREQ.bmd` and `n380.bmd` are
Chinese (`gb2312`); `Texttest.bmd` and `credittest.bmd` are `cp1252`. Files that
share a table's name and are something else (`Data/Item/item.bmd`, `Data/Item/Itemtest.bmd`, `Data/skill/skill.bmd`)
answer `UNKNOWN_FORMAT`, in any letter case, for reads and writes.

### Saving

1. Encode the document.
2. Decode the new bytes and compare with the document; a mismatch aborts (write-verify).
3. Check that the file on disk still has the SHA-256 it had when it was read; if someone changed it meanwhile (the TUI
   open while an agent ran `set`), stop with `STALE_FILE` instead of overwriting their change.
4. Write to a temporary file in the target folder and `os.replace` it, keeping the old file as `<name>.orig` the
   first time. It is not `<name>.bak` because the game's own folder already has `item.bmd.bak`, `skill.bmd.bak`,
   `Filter.bmd.bak` and `Movereq.bmd.bak`.

What the writer recomputes after an edit: counts (from list lengths), the encrypted block's size, and the checksum of
a table whose records changed. An unedited table keeps its stored checksum, even a wrong one (Q-C01: the lab's
`item.bmd` fails its own). The version, the cipher and the trailing bytes stay as read: `set` and `apply` refuse a
model's `version`, and only `build` writes the one its view says (12 is K16, any other is plain), which is how a model
is decrypted or encrypted.

**Edits go to a working copy; the reference folder is read-only** (Emanuel's pick, 2026-11-25, over editing
`Client/assets/Data` in place and over an overlay of only the changed files). A folder with a `Data.sha256` beside it
(`Client/assets/Data`) is never written: the tool opens it for viewing, and `set`, `apply`, `build` or a save into it
fail with an error that names the way out. Edits happen in a working copy, a full `Data` folder that the client can run
(`Main --data <copy>`, or the lab's `cliente-lab/Data`):

- `mutools workspace init <dest>` copies the reference folder (about 370 MB) to `<dest>` and checks it against
  `Data.sha256`; any existing `Data` folder works as a working copy too.
- Every command reads and writes the path it is given, with no overlay and no hidden state: two `set` calls on the
  same file build on each other.
- The TUI opens a folder; on the reference folder its header says "read-only" and "save as" offers a working copy.

### Texts

Records are decoded as CP1252, the client's western profile (`Client/docs/research/12-codificacion.md`). The catalog
gives each text file its encoding: `TKOU.bmd` decodes entirely as GB2312 (1,671 records, checked 2026-11-25), and
`--encoding` overrides it. A record that its encoding cannot decode, or that has bytes after its first `0`, is shown
and kept as hex. In the record tables such bytes are common (36 of `Dialog.bmd`'s 75 dialogs, 278 of `n380.bmd`'s 399
item names), so a text field with bytes after its first `0` reads as `{"text": "...", "tail": "<hex>"}`: the text
stays readable, searchable and editable, an untouched field keeps every byte, and writing a plain string drops the
tail, which the client never reads. There `{"hex": ...}` is left for a text with no `0` or that its encoding cannot
read. On edit, the tool checks that the text encodes in the file's encoding and fits the record's 300 bytes.
A text that fills all 300, with no `0` left, is a warning and not an error: record 7 of `Text.bmd` is like that
(Q-C07), and the client reads such a text into the next record. `printf` specifiers (`%d`, `%s`) are highlighted, and changing their number or order is a warning: the client
passes some texts to `sprintf`.

### The JSON format

`mutools dump` writes, and `mutools build` reads, one JSON document per file. It is the document's own view, the same one
`get` shows, so a JSON Pointer means the same thing in `get`, `set`, `apply` and the dumped file (`/texts/500` is record
500 in all of them):

```json
{
  "format_version": 1,
  "format": "text",
  "encoding": "cp1252",
  "texts": ["", "Atencao !!! Hack detectado, …", "…", "…"]
}
```

- Stable key order and layout, so a diff in git is readable: two spaces per level, and a list or an object on one
  line when it fits in 120 columns (a gate or a vertex per line; `Player.bmd`'s dump is 9 MB instead of 25).
  `build` reads any layout.
- Bytes as lowercase hex strings. Finite floats as JSON numbers, the shortest that reads back to the same float32
  (`0.1`, not `0.10000000149011612`); NaN and infinities as `{"f32": "<8 hex digits>"}`, keeping their bits.
- Bytes the client does not use are `raw_0xNN` members. In a model they show only when they are not zero, and a part
  written without them gets zeros.
- Dense: every record is there, also the empty ones, so an array index is a record index. A model's counts are the
  lengths of its lists.
- `format_version` versions this layout; a later change bumps it and `build` rejects versions it does not know.

### The CLI: an API for agents first

Emanuel, 2026-11-25: the project has to be AI friendly, which means a CLI optimized for agents, with a SKILL.md for
its use. The CLI is therefore designed as an API whose main caller is an agent; people use the TUI or `--human`. It
follows the core and recommended levels of the vendored `ai-native-cli` skill (see
[AI friendliness](#ai-friendliness)), adapted where noted.

```text
mutools                                     the self-description below
mutools formats                             the catalog: every format, its files, known by name or by content
mutools schema <format>                     the fields of a format: JSON pointer, offset, type, limits, research reference
mutools workspace init <dest>               copy the reference Data folder to a working copy and check it
mutools info <file>                         format, cipher, a table's records or a model's parts, findings count
mutools find <file> <query>                 records whose text or name matches; --field, --regex
mutools get <file> <pointer>                one value or subtree: /texts/500, /records/12/name, /meshes/0/texture_name
mutools set <file> <pointer> <value>        one edit, saved as in Saving
mutools apply <file> <patch.json>           several edits at once, all or none (JSON Patch: test and replace)
mutools validate <file|folder>              findings
mutools diff <a> <b>                        field by field, between two files or a file and a JSON
mutools dump <file> --output <out.json>     the whole document as JSON, to a file
mutools build <in.json> --output <file>     the reverse: a table to its own name, a model to any .bmd name
mutools roundtrip <folder>                  decode and encode every .bmd; reports any byte that differs
mutools tui [<folder|file>]                 the console UI
```

**Output.**

- JSON is the default; nothing has to be added for an agent. `--human` switches to Rich tables and colors.
- stdout carries exactly one JSON object and nothing else; logs, progress and warnings go to stderr.
- Every success has the same envelope: `{"ok": true, "schema": 1, "command": "set", "data": {...}, "warnings": [...]}`.
  `schema` versions the output: within one, no field is renamed, retyped or removed, and CI checks every command's
  output against its JSON Schema. Stage 4 widened three of `info`'s fields within schema 1: `records`, `used_records`
  and `read_by_client` are `null` for a model, a kind of file that answered `UNKNOWN_FORMAT` until then, and nothing
  changed for the files schema 1 could already open. `info` also gained `version`, `meshes`, `bones` and `actions`
  (`null` for a table), and `formats` gained `matches` (`"name"` for a table, `"content"` for the model).
- The same names, types and units everywhere: a record is always `index`, a place is always `pointer`, sizes are
  always bytes.
- Token economy: lists are paged (`--limit`, default 50, and `--offset`, with `total` and `next_offset` in the result),
  `--fields` keeps only the named keys, `get` on a large subtree cuts its arrays at `--limit` and says so
  (`"truncated": true`), `info` summarizes, and `dump` writes to a file and returns only its path and size. `get`
  shows the value asked for and the lists directly inside it; a list of objects further in prints as `{"count": n}`,
  so `get <model> ''` is the header and the first parts with their counts, and a page of quest records does not spell
  out each record's entries. An item of a page keeps its own short lists whole (a vertex's three numbers, a dialog's
  ten answers). The `changes` of `diff`, `set`, `apply` and `build` count lists the same way, and `set` and `apply`
  show their first 50 changes with the total. A whole model never lands on stdout. `validate` and `roundtrip` over a
  folder take `--ndjson` to stream one finding per line.

**Errors.**

- Errors are one JSON object on stderr: `{"ok": false, "schema": 1, "error": {"code": "REFERENCE_READ_ONLY",
  "message": "...", "suggestion": "mutools workspace init ~/mu/Data", "input": {...}, "pointer": "/texts/500"}}`. The
  `suggestion` is a command the agent can run next whenever there is one, and `input` echoes what the command got.
- The codes are a closed list, part of the contract, and never renamed within a version. Among them:
  `USAGE`, `FILE_NOT_FOUND`, `UNKNOWN_FORMAT`, `POINTER_NOT_FOUND`, `INVALID_VALUE`, `TEXT_TOO_LONG`,
  `NOT_ENCODABLE`, `CORRUPT_FILE`, `REFERENCE_READ_ONLY`, `STALE_FILE`, `TEST_FAILED`, `WRITE_VERIFY_FAILED`,
  `NOT_EMPTY`.
- Exit codes: 0 done; 1 done but with findings or differences (`validate`, `diff`, `roundtrip`); 2 bad usage; 20 a
  file, format or pointer that does not exist; 30 a precondition failed (read-only reference, a file too short for
  its format, a file that changed since it was read, a JSON Patch `test` that did not hold, write-verify).
- Nothing but `tui` is interactive: a missing argument is a `USAGE` error, never a prompt. Unknown flags are rejected.

**Input.**

- Addresses inside a document are JSON Pointers (RFC 6901), the same in `get`, `set`, `apply`, `schema`, findings and
  the TUI's status line, so a pointer copied from one place works in any other.
- Values are typed by the schema: `set` parses `<value>` as the field's type and rejects what does not fit, with the
  limit in the message (`TEXT_TOO_LONG`: "312 bytes, the record holds 300").
- `apply` takes a JSON Patch (RFC 6902) limited to `test` and `replace`. `test` is the agent's guard against editing
  something that changed: `[{"op": "test", "path": "/texts/500", "value": "El usuario salió del juego"}, {"op":
  "replace", "path": "/texts/500", "value": "..."}]`. Either every operation applies or none does.

**Safety.**

- Every command that writes takes `--dry-run`, which returns the diff (as JSON: pointer, old value, new value) and
  writes nothing.
- Writing is idempotent: setting a value it already has returns `"changed": false` and touches no file, and
  `workspace init` on a copy that already checks against `Data.sha256` does nothing.
- What is valid but unusual is a warning, not an error, so the agent keeps its work: changing a text's `printf`
  specifiers (`PRINTF_CHANGED`), a parent bone after its child (`PARENT_AFTER_CHILD`), a texture name whose file
  neither the model's folder nor a folder beside it has (`TEXTURE_NOT_FOUND`), one that only a folder beside it has
  (`TEXTURE_ELSEWHERE`), and a new texture name that changes what the client reads from it: the file's kind, the
  mesh's slot or its effect (`TEXTURE_ROLE_CHANGED`). A number that points outside its list is `OUT_OF_RANGE`, an
  error that is still written. A write's warnings are the findings at the places it changed and every finding the
  file did not have before it.
- The reference folder is never written (`REFERENCE_READ_ONLY`, with the `workspace init` suggestion).
- Writes go only to files the catalog knows (a table by its name, a model to any `.bmd` name), after write-verify,
  with the original kept as `.orig`.
- `workspace init` into a folder that is not empty needs `--yes`.

**Self-description.**

- `mutools` with no arguments and `mutools --help` return JSON: what the tool is, every command with its parameters
  (name, type, required, default, choices), an example, its exit codes and its error codes. It is generated from the
  Typer app, so it cannot drift from the code. `--human` gives the classic help.
- `mutools --brief` returns one paragraph, the same one the skill opens with.
- `mutools schema` returns the formats' fields, so an agent learns a format without the research, and each command's
  entry in the help carries its output's JSON Schema.
- No MCP server for now: the articles read for this design disagree on it, and a CLI with a skill costs an agent
  fewer tokens. The use cases are plain functions, so an MCP adapter can be added later without touching the core.

### The console UI

A Textual app with its own theme, **"Lorencia"**: near-black background, MU's old gold for borders and titles, a deep
red for errors and unsaved marks, parchment-colored text. A light variant exists for bright terminals.

```text
╭─ ◆ mutools ─ ~/mu/Data ───────────────────────────────────── working copy · 1 unsaved ─╮
│ ╭ Data ───────────────╮ ╭ Local/Text.bmd ─ text · XOR3 · 1645/2048 used ▰▰▰▰▰▰▰▱▱ ───╮ │
│ │ ▾ Local             │ │  #     text                                         bytes  │ │
│ │   ◆ Text.bmd     ●  │ │  100   Zen                                              3  │ │
│ │     item.bmd        │ │  500   El usuario salió del juego                      26  │ │
│ │     skill.bmd       │ │  577   Aumenta %d%% de Daño                            20  │ │
│ │ ▸ Monster           │ │                                                            │ │
│ │ ▸ player            │ ╰────────────────────────────────────────────────────────────╯ │
│ ╰─────────────────────╯ ╭ record 500 ────────────────────────────────────────────────╮ │
│                         │ El usuario salió del juego▏                   26/299 bytes │ │
│                         ╰────────────────────────────────────────────────────────────╯ │
│ / search  enter edit  ctrl+z undo  ctrl+s save  d diff  ctrl+p commands  q quit        │
╰────────────────────────────────────────────────────────────────────────────────────────╯
```

- **Left:** the folder as a tree with only `.bmd` files, an icon per kind and a dot on unsaved files.
- **Right:** the document. Text and record tables are data tables with search, filters (non-empty, changed, with
  findings) and an editor below that counts bytes as you type and flags characters CP1252 cannot hold. A record table
  lists the fields of the selected record under the table, and item rows show their section. Models show a tree of
  meshes, actions and bones with their counts and names, each bone under its parent, and the fields of the selected
  part below; names, texture names, a mesh's texture and a bone's parent are editable there, the rest through JSON.
  Search and the row filter are for tables.
- **A model is drawn in the browser**, not in the terminal: see [The web viewer](#the-web-viewer).
- **Saving** opens a diff of what changed and where it will be written; nothing is written before that confirmation.
- **The command palette** (`ctrl+p`) lists every action; the footer shows the keys.
- `textual serve` can show the same app in a browser, with nothing extra in the code.

### The web viewer

`mutools web ~/mu/Data` serves one page from the same machine and opens it in the browser. It shows any model with its
textures, plays its actions, draws its skeleton and lists its parts, facts and findings. Emanuel chose it on
2026-11-27 ("Web local con three.js") over the wireframe in braille characters that this design had as its surprise.
Its plan, with every decision and what was measured on the real files, is
[`2026-11-27-bmd-editor-web-viewer.md`](../plans/2026-11-27-bmd-editor-web-viewer.md).

```text
┌ ◆ mutools ─ ~/mu/Data ──────────────────────────────────────────────────────────────────────┐
│ ┌ filter ───────────┐ ┌ Monster/Monster01.bmd ───────────────────────┐ ┌ parts ────────────┐ │
│ │ ▸ Item (246)      │ │ ▶ ❙❙  key ───●────── 7 / 20   8 keys/s ▾     │ │ ▾ meshes (14)     │ │
│ │ ▾ Monster (115)   │ │ ☐ skeleton  ☐ additive                       │ │   0 helmat_01.jpg │ │
│ │   Monster01.bmd ● │ │                                              │ │ ▾ actions (7)     │ │
│ │   Monster02.bmd   │ │              (the model, three.js)           │ │   0 20 keys       │ │
│ │ ▸ NPC (57)        │ │                                              │ │ ▸ bones (45)      │ │
│ │ ▸ player (228)    │ │                                              │ ├ mesh 0 ───────────┤ │
│ └───────────────────┘ └──────────────────────────────────────────────┘ │ triangles 26 …    │ │
│                                                                        ├ findings (0) ─────┤ │
└────────────────────────────────────────────────────────────────────────┴───────────────────┘
```

- **Left:** the served folder's models, one node per folder that has any, with a filter. Tables are not listed.
- **Middle:** the scene, with orbit controls and a grid, and the camera placed so the model's box is in view. Above
  it: play and pause, the key slider, the speed (the file does not say how fast an action plays) and the switches for
  the skeleton and for additive drawing.
- **Right:** the parts tree the console UI shows, the facts and fields of the chosen part, read-only, and the model's
  findings. Choosing a mesh tints it in the scene, choosing an action plays it, and choosing a bone turns the skeleton
  on and marks that bone. Choosing a finding chooses its part.
- **It views; it does not edit.** With no write, the reference folder can be opened as it is. Editing from the browser
  is a later stage and brings its own protection (a token, so that another page open in the browser cannot ask this
  server to write).
- **Local only.** The server listens on 127.0.0.1, with no flag to change that, and refuses a request whose `Host` is
  not `127.0.0.1` or `localhost`, so a site on the internet cannot reach it through a DNS name that points at
  127.0.0.1. A model outside the served folder is refused, and a name that could leave it (`..`, a drive, a network
  share) is refused before the disk is touched. A mesh's texture is looked for where `validate` looks: in the model's
  folder and in the folders beside it.
- **It is a viewer, not the client's renderer.** Textures are read as the client reads them (an OZJ is a JPEG after 24
  bytes; an OZT is 32-bit BGRA rows stored bottom first, no side over 512) and drawn unlit, on both sides, with alpha
  test. There is no lighting, none of the client's blend rules per model and no x87 math. A texture that no folder
  has, or that the client refuses, leaves its mesh in a flat color, and the mesh's facts say why.
- **Character parts can be shown on the player's skeleton.** A model of `player/` whose vertices hang from bones that
  the `Player.bmd` of its folder has at the same index, with the same name and parent (219 of the other 227), gets a
  switch, and with it on the part moves with `Player.bmd`'s 205 actions.
- **The server sends what a renderer needs, not the view:** per mesh, one position, one UV and one bone per triangle
  corner; the bones; and the poses of one action per request, already as quaternions (Half-Life's angle order,
  0x5fcd20, computed in Python so that it is tested without a browser). The page is NiceGUI, and the model is a
  three.js `SkinnedMesh` built by one JavaScript file of ours. three.js comes inside NiceGUI's wheel: no third-party
  JavaScript enters the repo.
- **The page's requests are private to the page**, outside the CLI contract. `mutools web` itself is under it: one line
  on stderr when it is ready, the usual JSON object when `Ctrl+C` stops it, `USAGE` without the `web` extra and
  `IO_ERROR` for a port in use. NiceGUI is an optional extra, `mutools[web]`, that the CLI and the console UI do not
  need.

### The studio

Stage 6 turns the web viewer into a studio, on Emanuel's asks of 2026-11-27 ("improve its design, surprise me", "the
site looks dull", "light, but without losing render quality") and with his two corrections of the same day: the first
look was generic, and the page is dark. What the viewer section above says about safety, the server's routes and what
is drawn still holds; this replaces its layout. The plan, with every decision, is
[`2026-11-27-mutools-studio.md`](../plans/2026-11-27-mutools-studio.md).

- **A library with pictures:** one folder at a time, each model on a plate with its picture, drawn in the browser by
  one shared renderer and kept in IndexedDB by file and content, eight plates to a page so a folder of 246 models
  keeps the page its height. An index of folders and a search through every name.
- **The vitrine:** the model alone on a dark stage whose glow takes the hue of its textures, with `Textured`, `Lit`
  and `Wire`, the bones, additive drawing and the player's skeleton for character parts.
- **The actions at the client's speed:** the client's speed tables (the player's, the monsters', the NPCs') are
  copied into `core/client_speeds.py`, and a test reads the client's C++ so the copy cannot drift. The actions are laid
  end to end at their real lengths; one the client plays once plays once.
- **The inspector:** meshes with texture swatches, the parts tree, the findings.
- **Drawn on demand:** nothing is drawn while nothing changes; a playing action draws 30 frames a second at most, at
  a pixel ratio of 1 while the camera moves and 2 at rest; the loop stops when the page is hidden. A browser test
  counts the frames.
- **Two project skills** guide it: `frontend-design` (Anthropic's, with MuRet's decisions about the look) and
  `threejs` (five of CloudAI-X's, with MuRet's rules over their continuous loops).

### AI friendliness

Three parts: the CLI above, a skill that teaches it, and tests that keep both honest.

**The skill, `.claude/skills/muret-bmd/SKILL.md`.**

- Its description names when to use it (open, inspect, translate or edit a `.bmd`: `Text.bmd`, items, skills, gates,
  dialogs, quests, models), so it loads on those requests and not on C++ work in `Client/src/Data`.
- Its body, short enough to read whole: the one-paragraph brief (the same as `mutools --brief`); the workflow
  (working copy, `find`, `get`, then `set` or `apply` once, then `validate`: a write is verified and returns its
  changes and warnings, so `--dry-run` is for showing a batch to someone before it is written; Emanuel, 2026-11-25);
  recipes for the common jobs (change a text, translate several texts with one `apply`, change an item's field,
  rename a texture, check a working copy against the reference); the error codes with the way out of each; and the
  rules (never write the reference folder, `test` before `replace`, `--limit` and `--fields` to save tokens,
  `roundtrip` after changing a codec).
- Every command in its recipes is executable: a test runs each recipe against a fresh working copy and checks the
  result, so the skill cannot drift from the tool.
- It is written and checked as `superpowers:writing-skills` says: before it exists, a fresh agent gets a few of those
  jobs with only the tool installed, and what it gets wrong shapes the skill; with the skill, the same agent must
  finish each job in a few calls without reading the tool's source.
- `AGENTS.md` gets one line in "Dónde está cada cosa" saying when to use the tool and pointing to the skill.

**Skills for writing the tool**, vendored like the C++ ones (upstream copy in `references/`, a MuRet layer on top,
ranked below the repo's rules):

- `ai-native-cli`, from [sickn33/agentic-awesome-skills](https://github.com/sickn33/agentic-awesome-skills)
  (the source of the existing `clean-code` skill; content under CC BY 4.0): 98 rules for CLIs that agents call, in
  three levels. MuRet's layer adopts the core and recommended levels as the section above adapts them (JSON Pointer
  paths, the exit codes, `--human`), and skips the ecosystem level's `agent/` folder and issue system, which the skill
  and GitHub already cover.
- From [wshobson/agents](https://github.com/wshobson/agents) (MIT, 42.8k stars; the source of the existing
  `clean-architecture` skill): `python-design-patterns`, `python-code-style`, `python-project-structure`,
  `python-testing-patterns`, `python-type-safety` and `python-error-handling`. Hexagonal architecture is already
  covered by `clean-architecture`.

**The TUI** is tested headless with Textual's `Pilot`, so an agent can drive it and check it without a screen.

### Tests and CI

- pytest. Per codec: small vectors built in the test (one record, one tiny model) for each rule, and the round trip
  over every `.bmd` of `Client/assets/Data` (about 10 s; the data is in the repo, so CI runs it).
- Ciphers and checksum: vectors cross-checked with the C++ tests' expected values.
- The CLI through Typer's `CliRunner`: each command's JSON checked against its JSON Schema, each error code and exit
  code reached by a test, and stdout holding nothing but the JSON object.
- The skill's recipes, run as tests (see [AI friendliness](#ai-friendliness)).
- The TUI with `run_test` and `Pilot` (open, edit, undo, save to a temporary folder) and SVG snapshots
  (`pytest-textual-snapshot`) of the main screens.
- The web viewer's page and its requests through NiceGUI's test user, with no browser. Its JavaScript runs only in a
  browser, so a few tests marked `browser` drive a headless Chromium with Playwright against a real `mutools web`
  (`pytest -m browser`); `pytest` alone leaves them out.
- `ruff check`, `ruff format --check` and `mypy --strict` over `src` and `tests`.
- A workflow of its own, `.github/workflows/bmd-tool.yml`, on changes under `Client/tools/mutools/**`: Python 3.13,
  `pip install -e ".[dev]"` with exact pins, then the checks above. A second job, in parallel, installs Chromium and
  runs the browser tests.

### Stages (one PR each)

One PR per stage, as Emanuel prefers since 2026-11-11 (milestone-sized PRs instead of one concept per PR). Each PR
leaves its own checks green, and the workflow lands with the first code PR.

1. **Design and skills:** this document, the implementation plan, the seven vendored skills (`ai-native-cli` and six
   for Python).
2. **Texts end to end:** the package, XOR3, the catalog, the text codec, JSON, every CLI command for texts, the TUI
   with its theme and the text editor, the `muret-bmd` skill with its executable recipes and its agent check, the
   workflow. After this PR an agent or a person can edit `Text.bmd`.
3. **Every table:** K16-free formats first, then the checksummed ones; schemas with the research's field names. Its
   plan, with the decisions the real files forced, is
   [`2026-11-26-bmd-editor-tables.md`](../plans/2026-11-26-bmd-editor-tables.md).
4. **Models:** the lossless model codec with K16, validation, the model view in the TUI. Its plan, with the decisions
   the 1,962 real files forced, is [`2026-11-27-bmd-editor-models.md`](../plans/2026-11-27-bmd-editor-models.md).
5. **The web viewer of models:** `mutools web`, a page served from the same machine that shows a model with its
   textures, actions and skeleton. It took the place of the wireframe preview. Its plan is
   [`2026-11-27-bmd-editor-web-viewer.md`](../plans/2026-11-27-bmd-editor-web-viewer.md).
6. **The studio:** the web viewer's new look and what makes it light; see "The studio" above. The stages after it
   (editing in the browser, the Blender bridge, animation, new models, the character room) are listed in its plan and
   each waits for Emanuel's yes.

Each stage ends with the round trip over its files passing.

## Left to the builder

- Module and function names, beyond the layout above.
- How each record schema is written (a tuple of field specs per table is the expected shape).
- The exact palette values of the "Lorencia" theme.
