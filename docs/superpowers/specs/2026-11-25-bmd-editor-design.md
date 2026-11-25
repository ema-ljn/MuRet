# BMD editor: a Python tool to read, check and edit MU's `.bmd` files (design)

> **Working document, 2026-11-25.** Emanuel's request: "an interpreter or an editor of `.bmd` files, in Python,
> following Python's best practices in code and architecture, AI friendly, maybe with skills"; then "to edit
> `Text.bmd` and those files inside the client's assets"; then "a nice, popular console UI, friendly for AI, that
> also has a CLI usable from a SKILL.md"; and, on the look, "give it a nice format, surprise me". He picked Textual
> for the console UI.

## What and why

`Client/assets/Data/` holds 1,994 `.bmd` files: 1,962 3D models and 32 tables (texts, items, skills, gates, dialogs,
quests and others). Today only the client's C++ readers open them, and they read just what the client needs. Nobody
can look inside one, change a text or an item, and get a file the client reads, without writing code.

- **Goal:** open any `.bmd` of a `Data` folder, see what it holds with the names the research gives each field,
  change it, and write a file that the client reads exactly as the original would.
- **Goal:** the same operations from a console UI for a person and from a CLI with JSON output for an agent, with a
  project skill that tells an agent how to use it.
- **Goal:** a codebase that shows Python done well: typed, layered (ports and adapters), small modules, tested.
- **Non-goal:** a 3D modeling tool. Geometry and animation are editable through JSON, not by hand in the UI.
- **Non-goal:** importing from or exporting to Blender, glTF or OBJ. It can be a later stage.
- **Non-goal:** touching the client (`Client/src`), its build or its tests. The tool stands alone under
  `Client/tools/bmd/`.
- **It would be a failure:** a file read and written without changes that is not byte-identical to the original; an
  edited file that the client reads differently from what the tool showed; or silently changing the reference data in
  `Client/assets/Data`.

## Constraints

- **Byte-exact.** Every byte read is kept: padding, the bytes after a name's first `0`, the 28 bytes of each 64-byte
  triangle that the client ignores, trailing bytes after the last part (`item.bmd` has 80), the `"BMD"` prefix, the
  encrypted block's size. An unedited document writes back identical bytes. This is the project's fidelity rule, and the
  round trip over every file of `Client/assets/Data` is its test.
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
Client/tools/bmd/
  pyproject.toml            package `mubmd`, console script `mubmd`, pinned dependencies, ruff, mypy, pytest config
  README.md                 what it does, install, the commands, the JSON format
  src/mubmd/
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
  a path inside the document and a severity.
- **Use cases** (`app/`): plain functions that take ports and return result dataclasses. The CLI and the TUI call the
  same ones; neither has logic of its own.
- **Adapters**: the file system, JSON, the CLI and the TUI. Only adapters import Typer, Textual or Rich. A test reads
  the imports of every module (`ast`) and fails if `core` imports `app` or `adapters`, or anything outside the
  standard library.

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
marked "not read by the client". Files that match no layout (`Data/Item/item.bmd`, `Data/Item/Itemtest.bmd`,
`Data/skill/skill.bmd`) open read-only as raw bytes.

### Saving

1. Encode the document.
2. Decode the new bytes and compare with the document; a mismatch aborts (write-verify).
3. Write to a temporary file in the target folder and `os.replace` it, keeping the old file as `<name>.bak` the first
   time.

What the writer recomputes after an edit: counts (from list lengths), the encrypted block's size, and the checksum of
a table whose records changed. An unedited table keeps its stored checksum, even a wrong one (Q-C01: the lab's
`item.bmd` fails its own). The version, the cipher and the trailing bytes stay as read.

**The reference folder is read-only.** A folder with a `Data.sha256` beside it (`Client/assets/Data`) is never
written. Opening it works as usual; saving writes to a mirror under `out/bmd/Data/` (already git-ignored) with the same
relative path, and the tool says where. Any other `Data` folder (the lab's `cliente-lab/Data`, a copy) is saved in
place with its backup.

### Texts

Records are decoded as CP1252, the client's western profile (`Client/docs/research/12-codificacion.md`). The catalog
gives each text file its encoding: `TKOU.bmd` decodes entirely as GB2312 (1,671 records, checked 2026-11-25), and
`--encoding` overrides it. A record that its encoding cannot decode is shown and kept as hex. On edit, the tool checks that the text encodes in CP1252 and fits
the record with its `0`; record 7 of `Text.bmd`, which fills its 300 bytes without one (Q-C07), stays as it is unless
edited. `printf` specifiers (`%d`, `%s`) are highlighted, and changing their number or order is a warning: the client
passes some texts to `sprintf`.

### The JSON format

`mubmd dump` writes, and `mubmd build` reads, one JSON document per file:

```json
{
  "mubmd": 1,
  "format": "text",
  "container": {"cipher": "xor3"},
  "body": {"texts": [{"index": 500, "text": "El usuario salió del juego"}]}
}
```

- Stable key order and indentation, so a diff in git is readable.
- Bytes as lowercase hex strings. Finite floats as JSON numbers (a float32 goes to float64 and back exactly); NaN and
  infinities as `{"f32": "<8 hex digits>"}`, keeping their bits.
- Sparse where it helps: empty text records and all-zero table records are left out and come back as zeros.
- `"mubmd": 1` is the format version; a later change bumps it and `build` rejects versions it does not know.

### The CLI (for agents and scripts)

```text
mubmd formats                         the catalog; --json
mubmd schema <format>                 the fields of a format, with offsets, types and research references
mubmd info <file>                     format, version, cipher, counts, checksum state
mubmd validate <file|folder>          findings; exit code 1 when there is an error
mubmd get <file> <path>               one value: texts/500, records/12/name, meshes/0/texture_name
mubmd set <file> <path> <value>       one edit, saved as above; --dry-run prints the diff and writes nothing
mubmd dump <file> [-o out.json]
mubmd build <in.json> -o <file>
mubmd diff <a> <b>                    field by field, between two files or a file and a JSON
mubmd roundtrip <folder>              decode and encode every .bmd; reports any byte that differs
mubmd tui [<folder|file>]             the console UI
```

Every command takes `--json` and then prints one JSON object (results or `{"error": {"code", "message", "path"}}`)
and nothing else on stdout. No command asks a question; anything that writes accepts `--dry-run`. Exit codes: 0 done,
1 findings or differences, 2 bad usage, 3 a file that cannot be read.

### The console UI

A Textual app with its own theme, **"Lorencia"**: near-black background, MU's old gold for borders and titles, a deep
red for errors and unsaved marks, parchment-colored text. A light variant exists for bright terminals.

```text
╭─ ◆ mubmd ─ Client/assets/Data ─────────────────────── read-only · saves to out/bmd ─╮
│ ╭ Data ───────────────╮ ╭ Local/Text.bmd ─ text · XOR3 · 1645/2048 used ▰▰▰▰▰▰▰▱▱ ╮ │
│ │ ▾ Local             │ │  #     text                                       bytes │ │
│ │   ◆ Text.bmd     ●  │ │  100   Zen                                          3  │ │
│ │     item.bmd        │ │  500   El usuario salió del juego                  26  │ │
│ │     skill.bmd       │ │  577   Aumenta %d%% de Daño                         20  │ │
│ │ ▸ Monster           │ │                                                        │ │
│ │ ▸ player            │ ╰────────────────────────────────────────────────────────╯ │
│ ╰─────────────────────╯ ╭ record 500 ────────────────────────────────────────────╮ │
│                         │ El usuario salió del juego▏               26/299 bytes │ │
│                         ╰────────────────────────────────────────────────────────╯ │
│ / search  enter edit  ctrl+z undo  ctrl+s save  d diff  ctrl+p commands  q quit      │
╰──────────────────────────────────────────────────────────────────────────────────────╯
```

- **Left:** the folder as a tree with only `.bmd` files, an icon per kind and a dot on unsaved files.
- **Right:** the document. Text and record tables are data tables with search, filters (non-empty, changed, with
  findings) and an editor below that counts bytes as you type and flags characters CP1252 cannot hold. Item records are
  grouped in their 16 sections. Models show a tree of meshes, actions and bones with their counts and names; names,
  texture names, a mesh's texture and a bone's parent are editable there, the rest through JSON.
- **The surprise:** a model's wireframe in the terminal, drawn with braille characters and slowly turning, posed with
  the first key of its first action (Half-Life's angle-to-quaternion order, as research 05 describes). Character parts
  borrow `player/Player.bmd`'s skeleton. It is a preview in float64, not the client's x87 math.
- **Saving** opens a diff of what changed and where it will be written; nothing is written before that confirmation.
- **The command palette** (`ctrl+p`) lists every action; the footer shows the keys.
- `textual serve` can show the same app in a browser, with nothing extra in the code.

### AI friendliness

- The CLI as above: JSON only on stdout, stable shapes, no prompts, `--dry-run`, meaningful exit codes, complete
  `--help`.
- `mubmd schema` lets an agent learn a format without reading the research.
- A project skill, `.claude/skills/muret-bmd/SKILL.md`: when to use the tool, the commands, the JSON format, and the
  safety rules (dry-run first, the reference folder is read-only, run `roundtrip` after a codec change).
- The Python skills that guide the tool's own code, vendored like the C++ ones (upstream copy in `references/`, a
  MuRet layer on top, ranked below the repo's rules): from [wshobson/agents](https://github.com/wshobson/agents)
  (MIT, 42.8k stars; the source of the existing `clean-architecture` skill), `python-design-patterns`,
  `python-code-style`, `python-project-structure`, `python-testing-patterns`, `python-type-safety` and
  `python-error-handling`. Hexagonal architecture is already covered by `clean-architecture`.
- The TUI is tested headless, so an agent can drive and check it without a screen.

### Tests and CI

- pytest. Per codec: small vectors built in the test (one record, one tiny model) for each rule, and the round trip
  over every `.bmd` of `Client/assets/Data` (about 10 s; the data is in the repo, so CI runs it).
- Ciphers and checksum: vectors cross-checked with the C++ tests' expected values.
- The CLI through Typer's `CliRunner`, checking the JSON output.
- The TUI with `run_test` and `Pilot` (open, edit, undo, save to a temporary folder) and SVG snapshots
  (`pytest-textual-snapshot`) of the main screens.
- `ruff check`, `ruff format --check` and `mypy --strict` over `src` and `tests`.
- A workflow of its own, `.github/workflows/bmd-tool.yml`, on changes under `Client/tools/bmd/**`: Python 3.13,
  `pip install -e ".[dev]"` with exact pins, then the checks above.

### Stages (one PR each)

1. **Design and skills:** this document, the implementation plan, the six vendored Python skills.
2. **Texts end to end:** the package, XOR3, the catalog, the text codec, JSON, every CLI command for texts, the TUI
   with its theme and the text editor, the `muret-bmd` skill, the workflow. After this PR `Text.bmd` can be edited.
3. **Every table:** K16-free formats first, then the checksummed ones; schemas with the research's field names.
4. **Models:** the lossless model codec with K16, validation, the model view in the TUI.
5. **The wireframe preview.**

Each stage ends with the round trip over its files passing.

## Left to the builder

- Module and function names, beyond the layout above.
- How each record schema is written (a tuple of field specs per table is the expected shape).
- How the sparse JSON marks a left-out record, as long as `build` restores it byte-exact.
- The exact palette values of the "Lorencia" theme and the braille renderer's details.
