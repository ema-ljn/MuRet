# BMD editor, stage 4: models Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `mubmd` reads, checks and edits the 1,962 models of `Client/assets/Data`, plain and encrypted with K16, from
the CLI, the console UI and the `muret-bmd` skill, byte-exact. The wireframe preview stays for stage 5.

**Architecture:** One new codec, `model`, behind the same `FormatCodec` port. A model is kept as the file's own blocks
(one `bytes` per list of vertices, normals, triangles and keys), so reading and writing are slicing, and its JSON view
is built from the blocks when a command needs it. A model is found by its first three bytes; tables keep being found by
name. The use cases stay blind to formats; what changes in them is scale, because a model's view holds up to 600,000
numbers: an edit copies only the path it touches, `get` stops at the lists inside what it shows, `dump` writes short
values on one line. The checks that need the folder (texture files) live in `app/`.

**Tech Stack:** unchanged from stage 3 (Python 3.12+, Textual 8.2.8, Typer 0.29.2, Rich 15.0.0, pydantic 2.14.4;
pytest 8.4.2, ruff 0.17.6, mypy 2.4.1). No new dependency.

**Spec:** [`docs/superpowers/specs/2026-11-25-bmd-editor-design.md`](../specs/2026-11-25-bmd-editor-design.md), stage 4
of "Stages (one PR each)". Previous plans: [`2026-11-25-bmd-editor.md`](2026-11-25-bmd-editor.md) (stages 1 and 2),
[`2026-11-26-bmd-editor-tables.md`](2026-11-26-bmd-editor-tables.md) (stage 3).

## Global Constraints

- Byte-exact: `mubmd roundtrip Client/assets/Data` ends with `checked: 1991, identical: 1991, differing: [], skipped: 3`
  (29 tables, 1,962 models, and stage 3's three files that share a table's name).
- `mubmd validate Client/assets/Data` lists stage 3's five findings and the five of "Findings" below, nothing else.
- `Client/assets/Data` is never written. Tests copy what they edit.
- `core/` imports only the standard library, and nothing from `app/` or `adapters/` (`tests/test_layers.py`).
- The CLI contract of schema 1 holds: the envelopes, the exit codes 0/1/2/20/30/70 and the closed `ErrorCode` enum. This
  stage adds no error code, no command and no flag. It adds four finding codes and the fields of decision 9.
- The view stays `format_version` 1. A table's view changes only in how a float prints (decision 4).
- A whole model never lands on stdout (spec, "Token economy").
- **The PR touches only `Client/tools/`, `.claude/`, `docs/` and `*.md`**, so CI does not compile C++ (Emanuel,
  2026-11-25). No workflow file, no root `.gitignore`. Before opening the PR: `git diff --name-only
  origin/main...HEAD` against the `case` list of `.github/workflows/ci.yml`. The PR opens when the stage is done and
  gets no docs-only push while Emanuel waits to merge.
- English in code, docs, commits and PR text; comments only when strictly necessary (CODING_RULES §7, §9); one
  behavior, one test (§13); Angular commits with scope `tools`; the PR targets `main`.
- The layout comes from research 05 ("Modelos .bmd") and 06, and from the client's reader
  (`Client/src/engine/Data/Models/Bmd.cpp`, `ModelTextures.cpp`, `Client/src/engine/Crypto/K16.h`). Every number in
  "The model file" and "Findings" was measured on the 1,962 real files on 2026-11-26 and 27 with a throwaway parser
  that reads and rewrites them all identical.

## Decisions taken in this plan

Each is a call that the spec left open or that the real files forced. Emanuel reads these before any code.

1. **A model's view follows the file** (example in "The view"): `meshes`, `actions` and `bones` in the file's order,
   a position as `[x, y, z]`, a UV as `[u, v]`. Counts are never written by hand: they are the lengths of the lists.
2. **Bytes the client does not use show only when they are not zero.** A triangle is 64 bytes and the client uses 26
   of them, so `raw_0x01` (1 byte) and `raw_0x1a` (38 bytes) hold what the exporter left: 374,806 of the 450,338 real
   triangles carry a `raw_0x1a`. The padding of vertices and normals (`raw_0x02`, `raw_0x12`) is zero in every real
   file and never shows. A part written without them gets zeros. The spec says "28 bytes" of a triangle: those are the
   ones the original does not even copy; it copies 10 more that nothing reads.
3. **Names use the tables' three shapes, in cp949** (string, `{"text", "tail"}`, `{"hex"}`). A bone's or a texture's
   name keeps one byte for its 0 (31 bytes of text). The model's own name is the exporter's source path
   (`Data2\Item\Sword\Sword02.smd`), nothing in the client reads it, and 450 of them fill the 32 bytes with no 0: it
   reads as a string when its bytes decode, takes up to 32 bytes, and has no findings. 31 are cut inside a Korean
   character and read as `{"hex"}`.
4. **A float prints as the shortest number that reads back to the same float32** (`0.1`, not `0.10000000149011612`),
   in every format. Among the tables only `slide.bmd`'s `speed` is a float, and it prints `2.5` either way.
5. **`dump` writes a list or an object on one line when it fits in 120 columns.** A vertex takes one line instead of
   nine. `Player.bmd`'s dump would be 25 MB today and is 9 MB with this layout and decision 4's floats (measured
   with a prototype). Table dumps get shorter too (a gate per line). `build` reads both layouts.
6. **`get` shows the value asked for and the lists directly inside it; a list of objects further in prints as
   `{"count": n}`.** `get <model> ''` gives the header and the first parts with their counts; `get <model>
   /meshes/0/vertices` pages the vertices. For tables one thing changes: a page of quest or slide records (`get
   /records`) no longer spells out each record's entries, conditions or groups; `get /records/0` does, as today. This
   closes stage 3's deferred "trimming record fields in `get` pages".
7. **`version` belongs to the header:** `set` and `apply` refuse to change it, and `build` writes what the view says
   (12 is K16, any other is plain, as the client reads them). Decrypting or encrypting a model is `dump`, change
   `version`, `build`. An edited model keeps its version and its cipher.
8. **Four new findings** (table in "Findings"): `OUT_OF_RANGE` now also covers a model's indices and its 200 bones;
   `PARENT_AFTER_CHILD`, `TEXTURE_NOT_FOUND`, `TEXTURE_ELSEWHERE` and `TEXTURE_ROLE_CHANGED` are warnings, so the
   write happens. A texture's file is looked for in the model's folder and then in the folders beside it, because 69
   real meshes use a texture that only another folder has (`player/Helper03.bmd` uses `Item/unicon.OZJ`).
9. **`info` and `formats` gain fields, and three of `info`'s become null for a model:** `info` gets `version`,
   `meshes`, `bones` and `actions` (null for a table); `records`, `used_records` and `read_by_client` are null for a
   model (the catalog does not know which models the client loads). `formats` gets `matches` (`"name"` for a table,
   `"content"` for the model, which has no file list and no size). The spec says no field is retyped within a schema
   version: this widens three fields for a kind of file that answered `UNKNOWN_FORMAT` until now, and nothing changes
   for the files schema 1 could already open.
10. **An edit copies only the path it touches.** Today every edit copies the whole view and the console UI keeps one
    copy per undo step; `Player.bmd`'s view takes 38 MB in memory. This also closes stage 3's deferred minor on undo
    memory.
11. **The console UI shows a model as a tree of its parts** (mock-up in Task 6): meshes, actions, and bones nested by
    parent, with the selected part's fields below. Names, a mesh's `texture` and a bone's `parent` are edited there;
    geometry and keys through JSON, as the spec says. Models get a glyph in the file tree. `/` and `f` stay for tables.
12. **A model with more than 65,536 real bones times actions is refused**, as MuRet's reader does
    (`MaxPosedBoneActions`): an action with no keys takes three bytes, so a small file could ask for a huge view.
    `CORRUPT_FILE` when reading, `INVALID_VALUE` when writing. The largest real one is `Player.bmd` with 10,250.

Out of scope, still deferred: the preview (stage 5); `find` over a folder (which models use a texture); search and the
row filter inside a model in the console UI; JSON Patch `add` and `remove`; findings for limits the research does not
give (vertex buffers, a polygon other than 3, an action with no keys); knowing which models the client loads; import
and export (a spec non-goal); the rest of stage 3's deferred list.

## Review Focus

1. A file that lies about itself: only `BMD`, a size word past the file, a negative count, a count the file cannot
   hold, thousands of empty actions times bones. Expected: `CORRUPT_FILE` naming the part, no traceback, no huge
   allocation; a folder `validate` reports it and goes on. Tests in Tasks 1 and 4.
2. An edit that changes how long a list is, or lists that must agree and do not (an action's `keys` against each
   bone's lists, `lock` against `positions`). Expected: counts are rewritten from the lists; a disagreement is
   `INVALID_VALUE` at the list and nothing is written; an index left pointing outside is `OUT_OF_RANGE` and the write
   happens. Tests in Tasks 2 and 3.
3. Floats that JSON lets through or prints badly: `NaN`, the infinities, `-0.0`, denormals, `1e39`, an integer,
   `true`. Expected: a finite number float32 holds is taken, an integer too; the rest is `INVALID_VALUE`; no float
   changes a bit by being read and written. Tests in Task 2.
4. Names: a Korean name cut inside a character, 32 bytes with no 0, bytes after the 0, a rename that is too long, not
   encodable, has no dot, or turns a script letter or a slot on or off. Expected: an untouched name keeps every byte; a
   rename that changes what the client does with the mesh warns and is written. Tests in Tasks 2, 3 and 4.
5. The biggest model (`player/Player.bmd`: 2.4 MB, 205 actions) through `info`, `get`, `set`, `dump`, `build` and the
   console UI. Expected: no command prints a whole model, and an edit session does not hold one copy of the model per
   edit. Tests in Tasks 4, 5 and 6.

---

## The model file

```text
"BMD" · version u8
  version 0x0C:  size u32 · a block of `size` bytes, K16 from its first byte · (bytes after the block)
  any other:     the content, from offset 4
content:
  name 32 · meshes i16 · bones i16 · actions i16
  per mesh:    vertices i16 · normals i16 · uvs i16 · triangles i16 · texture i16
               vertices × 16:   bone i16 · raw 2 · position f32 ×3
               normals × 20:    bone i16 · raw 2 · normal f32 ×3 · vertex i16 · raw 2
               uvs × 8:         u f32 · v f32
               triangles × 64:  polygon u8 · raw 1 · vertices i16 ×4 · normals i16 ×4 · uvs i16 ×4 · raw 38
               texture_name 32
  per action:  keys i16 · lock u8 · when lock is not 0: positions, keys × 12 (f32 ×3)
  per bone:    dummy u8 · when it is 0: name 32 · parent i16 · per action: positions keys × 12, rotations keys × 12
  (bytes after the last bone)
```

All little endian. **K16:** `plain[i] = (key[i % 16] ^ cipher[i]) - previous` modulo 256; `previous` is `0x5E` for the
first byte and `cipher[i - 1] + 0x3D` after it; the key is `D1 73 52 F6 D2 9A CB 27 3E AF 59 31 37 B3 E7 A2`.

**A texture's name** (0x60d5f0 and 0x459940, as `ModelTextures.cpp`): the file the client opens is the name up to its
first dot plus `.OZT` when the letter after that dot is `t` or `T`, and `.OZJ` otherwise (no dot, or a dot at the end:
`.OZJ`). A name starting with `ski` or `level` sends the mesh to the skin slot, `hid` to the hidden one and `hair` to
the hair one (lowercase, byte by byte: `Hair` does not count). The script is the up to four characters between the
first `_` and the first dot: uppercase `R` (blend mesh), `H` (hidden), `S` (scrolling UV), `N` (no shine); any other
character among them and the name has no script (`Monster81_R2.jpg`).

What the real files hold (the tests' expected values):

- 1,962 models: 816 of version 0x0C, 1,142 of 0x0A and 4 of 0x00 (`Object3/Object25.bmd` and three more: 42 bytes, no
  parts). 3,421 meshes, 450,338 triangles, 301,367 normals, 3,099 actions (186 locked), 25,526 real bones and 2,720
  dummy ones. The largest are `player/Player.bmd` and `player/playerTest.bmd`, 2,414,277 bytes.
- In every file: the size word is the file's size minus 8, nothing follows the block or the last bone, the padding of
  vertices and normals is zero, `lock` and `dummy` are 0 or 1, a mesh's `texture` is its own index, every polygon is 3,
  no count is negative, every bone and texture name has its 0.
- Most per model: 14 meshes, 148 bones, 205 actions, 76 keys in an action; per mesh 1,035 vertices and 1,934 triangles.
- 142 meshes have a script (135 `R`, 6 `H`, 1 `S`), the client's golden count.
- A simple K16 loop in Python decrypts the 816 encrypted files (33 MB) in 2.3 s; reading and checking every model
  takes about 7 s, and building every view 6 s more.

## The view

```json
{
  "format_version": 1, "format": "model", "encoding": "cp949",
  "version": 10,
  "name": "Data2\\Item\\Sword\\Sword02.smd",
  "meshes": [{
    "texture": 0,
    "texture_name": "sword02.jpg",
    "vertices": [{"bone": 0, "position": [-1.417384, -1.0287832, -10.2511]}],
    "normals": [{"bone": 0, "normal": [0.0, 4.3312576e-07, -1.0], "vertex": 0}],
    "uvs": [[0.9868, 0.45740002]],
    "triangles": [{"polygon": 3, "vertices": [0, 1, 2, -12851], "normals": [0, 0, 0, -12851],
                   "uvs": [0, 1, 2, -12851], "raw_0x01": {"hex": "cd"}, "raw_0x1a": {"hex": "cdcd…"}}]
  }],
  "actions": [{"keys": 1, "lock": 0}],
  "bones": [
    {"name": {"text": "Box01", "tail": "cdcd…"}, "parent": -1,
     "actions": [{"positions": [[0.049219, 0.0, -0.078563]], "rotations": [[1.570796, 0.811578, 0.0]]}]},
    {"dummy": 1}
  ],
  "trailing": ""
}
```

(`Item/Sword01.bmd`, each list cut to its first item; the `{"dummy": 1}` bone is from another model.)

| Place | Read | Written from (anything else is `INVALID_VALUE` at that pointer) |
| --- | --- | --- |
| `/version` | the byte | a number 0 to 255 (`set` and `apply` refuse it: decision 7) |
| `/name` | decision 3 | a string of at most 32 encoded bytes, or the other two text shapes |
| `texture_name`, a bone's `name` | the tables' three text shapes | as a table's text of 32 bytes (`TEXT_TOO_LONG`, `NOT_ENCODABLE`) |
| `bone`, `vertex`, `texture`, `parent`, a triangle's indices | a number | an `int` that is not a `bool`, inside int16 |
| `polygon`, `lock` | a number | 0 to 255 |
| `position`, `normal`, a UV, a key | lists of 3, 3, 2 and 3 floats (decision 4) | a list of exactly that many floats, each as a table's `f32` |
| `raw_0x…` | `{"hex": …}`, only when not all zero | that object with exactly its bytes; absent means zeros |
| an action | `{"keys", "lock"}`, and `positions` when `lock` is not 0 | `positions` present exactly when `lock` is not 0, with `keys` items |
| a bone | `{"name", "parent", "actions"}`, or `{"dummy": n}` with the byte | `actions` with one `{"positions", "rotations"}` per action of the model, each list with that action's `keys` items; `dummy` 1 to 255 |
| `/trailing` | hex, as in a table | hex |
| `/after_block` | hex, only when not empty | hex; with a `version` other than 12 it must be absent or empty |

A list holds at most 32,767 items (its count is an int16). An object takes exactly its members: one missing or one
extra is `INVALID_VALUE` at the object.

## Findings

| Code | Severity | When | Pointer |
| --- | --- | --- | --- |
| `OUT_OF_RANGE` | error | a vertex's or a normal's `bone` outside the model's bones; a normal's `vertex` outside its mesh's vertices; a triangle's vertex, normal or UV index outside its mesh's lists (the first `polygon` of the four, at most 4); a mesh's `texture` outside the meshes. One finding per mesh and kind, with how many. | the first one |
| `OUT_OF_RANGE` | error | a bone's `parent` outside -1 to the last bone | `/bones/N/parent` |
| `OUT_OF_RANGE` | error | more than 200 bones (the client's pose buffer, Q-C401) | `/bones` |
| `PARENT_AFTER_CHILD` | warning | a bone whose `parent` is itself or a later bone (Q-C108) | `/bones/N/parent` |
| `TEXTURE_NOT_FOUND` | warning | no file for a mesh's texture in the model's folder or in the folders beside it | `/meshes/N/texture_name` |
| `TEXTURE_ELSEWHERE` | warning | an edit left a mesh with a texture that only a folder beside the model's has | `/meshes/N/texture_name` |
| `TEXTURE_ROLE_CHANGED` | warning | an edit changed a texture name's loader (OZJ or OZT), slot or script | `/meshes/N/texture_name` |

The last two need the file as it was, so `validate` never reports them. File names are compared without case. A
texture name that is `{"hex"}` gets no texture finding.

The reference's five: `object35/Object36.bmd`, `OUT_OF_RANGE` at `/meshes/1/vertices/0/bone` and
`/meshes/1/normals/0/bone` (bone -8888, Q-C347); `NPC/BloodCastle01.bmd`, `PARENT_AFTER_CHILD` at `/bones/43/parent`
and `/bones/44/parent` (both hang from 45); `Item/waterring.bmd`, `TEXTURE_NOT_FOUND` at `/meshes/1/texture_name`
(`waterring.JPG`, no `waterring.OZJ` anywhere).

## Files

```text
Client/tools/bmd/src/mubmd/
  core/ciphers/k16.py                  NEW  K16
  core/documents/model.py              NEW  Model, Mesh, Action, Bone: the file's blocks
  core/formats/model_format.py         NEW  the codec: bytes <-> blocks, and the port's methods
  core/formats/model_view.py           NEW  blocks <-> JSON, and the fields `schema` lists
  core/formats/model_textures.py       NEW  what a texture's name says: file, slot, script
  core/checks/model_checks.py          NEW  a model's findings
  core/formats/record_fields.py        MOD  the text and float leaves become public; the shortest float
  core/json_text.py                    NEW  the layout of `dump`
  core/leaves.py, core/patch.py        MOD  outline; edits that share what they do not touch
  core/catalog.py, core/ports.py       MOD  the model format, a model's Summary, three FileStore methods (Tasks 4, 6)
  app/checks.py                        NEW  the codec's findings plus the texture files'
  app/model_parts.py                   NEW  what the console UI shows of a model: the tree, a part's fields
  app/documents.py, app/edits.py, app/queries.py, app/folders.py, app/session.py   MOD
  adapters/storage.py                  MOD  head, files_in, folders_in
  adapters/cli/commands.py, describe.py, human.py   MOD  examples, the brief, a model's `info`
  adapters/tui/widgets/model_tree.py   NEW  a model's parts
  adapters/tui/widgets/bmd_tree.py, adapters/tui/app.py, adapters/tui/mubmd.tcss   MOD
Client/tools/bmd/tests/                one test file per new module; tests/core/test_models.py for the real files
.claude/skills/muret-bmd/SKILL.md, Client/tools/bmd/README.md, AGENTS.md, the spec   MOD  Task 7
```

Commands run from `Client/tools/bmd`. The whole check is `ruff check . && ruff format --check . && mypy src tests &&
pytest -q` (568 tests pass on `main` at 229ec50a).

Test files, in `tests/conftest.py`:

```python
MODEL_FILES: tuple[str, ...] = (
    "Object3/Object25.bmd", "object35/Object70.bmd", "Object12/Shine01.bmd", "skill/WaveForce.bmd",
    "skill/m_waves.bmd", "Item/Sword01.bmd", "Item/waterring.bmd", "player/Helper03.bmd",
    "player/HelmMale01.bmd", "object35/Object36.bmd", "NPC/BloodCastle01.bmd", "player/MaskHelmMale09.bmd",
)
TEXTURE_FILES: tuple[str, ...] = (
    "object35/crroof.OZJ", "Object12/light2_R.OZJ", "skill/GroundEff.OZJ", "skill/combo3.OZJ", "Item/sword02.OZJ",
    "Item/etcring.OZJ", "Item/unicon.OZJ", "Item/unicon01.OZT", "player/head_02.OZJ", "player/hide.OZJ",
    "object35/cr1_branch06.OZT", "object35/cr1_tree03.OZJ", "NPC/ng01.OZJ", "NPC/skin_barbarian_01.OZJ",
    "NPC/lnn01.OZT", "NPC/ju01_R.OZJ", "player/level_man02.OZJ", "player/hair_R.OZJ", "player/head_10.OZJ",
)
```

The fixture `model_copy` is `working_copy` plus `MODEL_FILES` and one empty file per name of `TEXTURE_FILES`: the
textures those models name, where the reference has them (it has no `waterring.OZJ`). `working_copy` stays as it is,
so no earlier test or snapshot changes. The smallest real models are the tests' vectors, as the real tables are for
stage 3's tests: the smallest is 42 bytes, and the reference is in the repo.

---

### Task 1: K16 and the model's blocks

**Files:**
- Create: `src/mubmd/core/ciphers/k16.py`, `src/mubmd/core/documents/model.py`,
  `src/mubmd/core/formats/model_format.py`; `tests/core/test_k16.py`, `tests/core/test_model_format.py`,
  `tests/core/test_models.py`
- Modify: `tests/conftest.py` (`MODEL_FILES`)

**Interfaces:**
- Produces:

```python
# core/ciphers/k16.py
def decrypt_k16(data: bytes) -> bytes
def encrypt_k16(data: bytes) -> bytes

# core/documents/model.py
MODEL_MAGIC: Final = b"BMD"
ENCRYPTED_VERSION: Final = 0x0C
NAME_SIZE: Final = 32
VERTEX_SIZE: Final = 16
NORMAL_SIZE: Final = 20
UV_SIZE: Final = 8
TRIANGLE_SIZE: Final = 64
KEY_SIZE: Final = 12                   # one position or one rotation
MAX_POSED_BONE_ACTIONS: Final = 1 << 16
MAX_BONES: Final = 200

@dataclass(frozen=True, slots=True)
class Mesh:
    texture: int
    vertices: bytes                    # whole elements, as in the file
    normals: bytes
    uvs: bytes
    triangles: bytes
    texture_name: bytes                # 32 bytes

@dataclass(frozen=True, slots=True)
class Action:
    keys: int
    lock: int
    positions: bytes = b""             # keys * KEY_SIZE bytes when lock is not 0

@dataclass(frozen=True, slots=True)
class Bone:
    dummy: int                         # the byte as read; 0 is a real bone
    name: bytes = b""                  # 32 bytes for a real bone
    parent: int = -1
    keys: bytes = b""                  # for each action in order: its positions, then its rotations

@dataclass(frozen=True, slots=True)
class Model:
    version: int
    name: bytes
    meshes: tuple[Mesh, ...]
    actions: tuple[Action, ...]
    bones: tuple[Bone, ...]
    trailing: bytes                    # after the last bone
    after_block: bytes                 # version 0x0C: after the encrypted block
    encoding: str

# core/formats/model_format.py
MODEL_ENCODING: Final = "cp949"
class ModelFormat:
    name = "model"
    def decode(self, data: bytes, file: CatalogFile) -> Model     # encoding: file.encoding or MODEL_ENCODING
    def encode(self, document: Model) -> bytes
```

  - `decode` raises `CORRUPT_FILE`, with a message that names the part and what was left ("mesh 2 asks for 1,035
    vertices and 312 bytes are left"), when the data does not start with `BMD` and a version byte; when a version
    0x0C file has no size word or one larger than what follows it; when a count is negative; when a block does not
    fit; and when real bones times actions pass `MAX_POSED_BONE_ACTIONS`. It checks a count against the bytes left
    before it slices or builds anything.
  - `encode` writes the counts from the blocks' lengths and, for version 0x0C, the size word from the block.

- [ ] **Step 1: Write the failing tests**:
  - `tests/core/test_k16.py`: `test_k16_decrypts_the_clients_vectors`: `decrypt_k16(bytes(2)) ==
    bytes.fromhex("7336")`, `decrypt_k16(bytes(17))[16] == 0x94`, `decrypt_k16(bytes.fromhex("8be52492")) ==
    bytes.fromhex("fcce5403")`. `test_k16_encrypts_what_it_decrypts`: `decrypt_k16(encrypt_k16(data)) == data` and
    `encrypt_k16(decrypt_k16(data)) == data` for `bytes(range(256)) * 3`.
  - `tests/core/test_model_format.py`, on the real small files:
    - `test_a_plain_model_reads_its_parts`: `Object12/Shine01.bmd` (390 bytes) is version 10 with one mesh (`texture`
      0, 64 bytes of vertices, 20 of normals, 32 of uvs, 128 of triangles, a `texture_name` starting
      `b"light2_R.jpg\0"`), one action (`keys` 1, `lock` 0, no positions) and one bone (`dummy` 0, a name starting
      `b"Box01\0"`, `parent` -1, 24 bytes of keys); `trailing` and `after_block` are empty.
    - `test_an_encrypted_model_reads_its_parts`: `object35/Object70.bmd` (306 bytes, size word 298) is version 12,
      its name is `b"Data2\\Object35\\cr_cmcenter02tr.S"` (32 bytes, no 0), and its mesh has 3 vertices and 1 triangle.
    - `test_a_model_with_no_parts`: `Object3/Object25.bmd` (42 bytes) is version 0 with no meshes, actions or bones.
    - `test_a_locked_action_keeps_its_positions`: `skill/WaveForce.bmd` has one action with `keys` 12, `lock` 1 and
      144 bytes of positions, and two bones with 288 bytes of keys each.
    - `test_a_dummy_bone_is_one_byte`: `player/HelmMale01.bmd` has 56 bones, and the dummy ones are 9, 16, 21, 24, 32,
      41, 52, 53, 54 and 55.
    - `test_a_model_encodes_to_its_bytes`, parametrized over `MODEL_FILES`.
    - `test_bytes_after_the_last_bone_are_kept`: Shine01 plus `b"\xaa\xbb"` has that `trailing` and encodes back.
      `test_bytes_after_the_encrypted_block_are_kept`: Object70 plus `b"\x01\x02"` has that `after_block`, an empty
      `trailing`, and encodes back.
    - `test_a_file_that_lies_is_corrupt` (Review Focus 1), parametrized: `b"BMD"`; `b"BMD\x0c\x01"`; Object70 with
      its size word one higher; Shine01 cut to 389, 50 and 40 bytes; Shine01 with its mesh count at `ff ff`; Shine01
      with its vertex count at `ff 7f`; and a plain model built in the test with 3 real bones and 30,000 actions of
      no keys (90,000 pairs). Each raises `CORRUPT_FILE`.
  - `tests/core/test_models.py`, over the reference (`pytest.mark.parametrize` is not used here: one test walks the
    files): `test_every_model_round_trips`: 1,962 files start with `BMD`; by version 816, 1,142 and 4;
    `encode(decode(data)) == data` for each; and the totals of "The model file" (3,421 meshes, 450,338 triangles,
    301,367 normals, 3,099 actions of which 186 locked, 25,526 real bones, 2,720 dummy).
- [ ] **Step 2:** `pytest tests/core/test_k16.py tests/core/test_model_format.py tests/core/test_models.py -q`.
  Expected: FAIL on import.
- [ ] **Step 3:** Implement the three modules. K16 is the plain loop of "The model file".
- [ ] **Step 4:** `pytest -q`. Expected: PASS, the 568 earlier tests included; `mypy src tests` clean.
- [ ] **Step 5: Commit** `feat(tools): read and write a model's blocks, with the K16 cipher`

### Task 2: The view

**Files:**
- Create: `src/mubmd/core/formats/model_view.py`, `tests/core/test_model_view.py`
- Modify: `src/mubmd/core/formats/record_fields.py`, `src/mubmd/core/formats/model_format.py`,
  `src/mubmd/core/ports.py` (`Summary`), `tests/core/test_record_fields.py`, `tests/core/test_models.py`

**Interfaces:**
- Consumes: Task 1; `decode_text`, `encode_fixed_text` (`core/documents/text_table.py`); `FieldInfo`.
- Produces:

```python
# core/formats/record_fields.py: today's private leaf helpers, public and taking a size instead of a RecordField
def float_view(data: bytes) -> JsonValue      # the shortest number that packs back to `data`; {"f32"} when not finite
def float_bytes(value: JsonValue, pointer: str) -> bytes
def text_view(data: bytes, encoding: str) -> JsonValue
def text_bytes(value: JsonValue, size: int, encoding: str, pointer: str) -> bytes

# core/formats/model_view.py
def model_view(model: Model) -> JsonObject
def model_from_view(view: JsonObject) -> Model
MODEL_FIELDS: Final[tuple[FieldInfo, ...]]

# core/ports.py
@dataclass(frozen=True, slots=True)
class Summary:
    cipher: str
    records: int | None                # None for a model
    used_records: int | None
    section_size: int | None = None
    version: int | None = None         # these four: None for a table
    meshes: int | None = None
    bones: int | None = None
    actions: int | None = None

# ModelFormat gains
def to_view(self, document: Model) -> JsonObject
def from_view(self, view: JsonObject) -> Model
def describe_fields(self) -> tuple[FieldInfo, ...]
def summarize(self, document: Model) -> Summary      # cipher "K16" for version 0x0C, "none" otherwise
```

  - `float_view` tries 1 to 9 significant digits and keeps the first number that packs to the same four bytes; if
    none does, the float32's exact value. `float_bytes` takes an `int` that is not a `bool` as that float.
  - `model_view` and `model_from_view` follow the table of "The view". `model_from_view` checks every list length
    and every value, names the pointer of the first one that is wrong, and refuses more than
    `MAX_POSED_BONE_ACTIONS` real bones times actions (`INVALID_VALUE` at `/bones`).
  - `MODEL_FIELDS` has one entry per kind of leaf, with `<N>` for an index into a list of any length and
    `<0..2>` for a fixed one: `/name`, `/version`, `/meshes/<N>/texture`, `/meshes/<N>/texture_name`,
    `/meshes/<N>/vertices/<N>/bone`, `/meshes/<N>/vertices/<N>/position/<0..2>`, and so on through the triangles, the
    actions and the bones. `type` is `text`, `int`, `uint`, `f32` or `bytes`; `limit` as a table's; `offset` is the
    leaf's offset inside its element; `reference` is `research 05, "Modelos .bmd"`.

- [ ] **Step 1: Write the failing tests**:
  - `tests/core/test_record_fields.py`: `test_a_float_prints_as_the_shortest_number_that_keeps_its_bits`,
    parametrized with the bytes and the number: `cdcccc3d` is `0.1`, `00002040` is `2.5`, `00000080` is `-0.0` (with
    its sign), `01000000` is `1e-45`, `ffff7f7f` is `3.4028235e+38`; each number packs back to its bytes.
    `test_no_finite_float_changes_a_bit` (Review Focus 3): 20,000 bit patterns from `random.Random(4)`; every finite
    one satisfies `float_bytes(float_view(data), "") == data`.
  - `tests/core/test_model_view.py`:
    - `test_a_model_reads_as_the_file_says`: the view of `Item/Sword01.bmd` has `version` 10, `name`
      `"Data2\\Item\\Sword\\Sword02.smd"`, one mesh with `texture_name` `"sword02.jpg"`, 42 vertices, 68 normals, 42
      uvs and 68 triangles whose first items are the ones of "The view" (the triangle's `raw_0x1a` is `{"hex": "cd" *
      38}`, the bone's tail `"cd" * 26`), `actions == [{"keys": 1, "lock": 0}]`, `trailing == ""` and no
      `after_block`.
    - `test_zero_raw_bytes_do_not_show`: triangle 0 of mesh 0 of `object35/Object36.bmd` is `{"polygon": 3,
      "vertices": [0, 1, 2, 0], "normals": [0, 1, 2, 0], "uvs": [0, 1, 2, 0], "raw_0x01": {"hex": "01"}}`.
    - `test_a_locked_action_shows_its_positions`: action 0 of `skill/WaveForce.bmd` has `keys` 12, `lock` 1 and 12
      `positions`, the first `[0.0, 0.0, 0.0]`; bone 1 has `parent` 0.
    - `test_a_dummy_bone_shows_its_byte`: bone 9 of `player/HelmMale01.bmd` is `{"dummy": 1}`.
    - `test_a_models_name_reads_by_its_bytes` (Review Focus 4): Object70's is `"Data2\\Object35\\cr_cmcenter02tr.S"`
      (32 bytes, no 0); Shine01's is `"Data2\\Object12\\햇빛2.smd"`; `skill/m_waves.bmd`'s is `{"text":
      "Z:\\m_waves.smd", "tail": "00752050726f6a6563745c746d705c6d5f"}`; `player/Helper03.bmd`'s is `{"hex":
      "44617461325c4974656d5c48656c7065725cc0afb4cfb8aebec65fbec6c0ccc5"}`.
    - `test_a_view_builds_the_same_model`, parametrized over `MODEL_FILES`: `model_from_view(json.loads(json.dumps(
      model_view(model)))) == model`.
    - `test_counts_follow_the_lists` (Review Focus 2): Sword01's view with its first vertex appended to the vertices
      encodes to a file 16 bytes longer that decodes with 43 vertices.
    - `test_lists_that_disagree_are_refused` (Review Focus 2), parametrized on WaveForce's view, each
      `INVALID_VALUE` with the pointer given: `keys` 11 (`/actions/0/positions`); bone 0 with two `actions`
      (`/bones/0/actions`); bone 1's `rotations` cut to 11 (`/bones/1/actions/0/rotations`); a position `[0.0, 0.0]`
      (`/actions/0/positions/0`); `lock` 0 keeping `positions` (`/actions/0/positions`); `lock` 1 on an action with
      no `positions` (`/actions/0`); a vertex with a member `"x"` (`/meshes/0/vertices/0`); `after_block` `"00"` on a
      version 10 view (`/after_block`); 3 real bones and 30,000 actions of no keys (`/bones`).
    - `test_a_float_refuses_what_json_lets_through` (Review Focus 3), at `/meshes/0/vertices/0/position/0` of
      Sword01: `True`, `"1"`, `float("nan")`, `float("inf")` and `1e39` are `INVALID_VALUE` at that pointer; `1` is
      taken and reads back `1.0`; `{"f32": "0000c07f"}` is taken and encodes those bytes.
    - `test_a_written_name_fits_its_bytes` (Review Focus 4): over Shine01's `texture_name`, `"light2.jpg"` writes the
      text and 22 zeros (the tail goes); 32 `a` are `TEXT_TOO_LONG` and 31 are taken; `"😀.jpg"` is `NOT_ENCODABLE`.
      Over `/name`, 32 `a` are taken with no 0 and 33 are `TEXT_TOO_LONG`.
    - `test_the_fields_cover_every_leaf`: every leaf of Sword01's and WaveForce's views, with its indices replaced
      as `MODEL_FIELDS` writes them, is one of its pointers; `/meshes/<N>/texture_name` is a `text` and
      `/meshes/<N>/vertices/<N>/position/<0..2>` an `f32` at offset 4.
    - `test_a_summary_counts_the_parts`: `player/Player.bmd` gives cipher `"K16"`, version 12, 0 meshes, 60 bones,
      205 actions and `records` `None`; Sword01 gives cipher `"none"`.
  - `tests/core/test_models.py`: `test_every_model_view_builds_the_same_model`, one test over the 1,962 (about 20 s).
- [ ] **Step 2:** `pytest tests/core/test_model_view.py tests/core/test_record_fields.py -q`. Expected: FAIL.
- [ ] **Step 3:** Implement. Pack and unpack a block with one `struct` call per block, not per element.
- [ ] **Step 4:** `pytest -q`. Expected: PASS (the slide's `speed` still reads `2.5`); `mypy src tests` clean.
- [ ] **Step 5: Commit** `feat(tools): show a model as JSON and build it back`

### Task 3: A model's findings

**Files:**
- Create: `src/mubmd/core/formats/model_textures.py`, `src/mubmd/core/checks/model_checks.py`;
  `tests/core/test_model_textures.py`, `tests/core/test_model_checks.py`
- Modify: `src/mubmd/core/formats/model_format.py`, `tests/core/test_models.py`

**Interfaces:**
- Consumes: Tasks 1 and 2; `Finding`, `Severity`, `ValueCheck`.
- Produces:

```python
# core/formats/model_textures.py
@dataclass(frozen=True, slots=True)
class TextureUse:
    file: str                # what the client opens: the name up to its first dot, then ".OZT" or ".OZJ"
    slot: str | None         # "skin", "hidden", "hair"
    script: str              # the letters in force, in the order "RHSN"; "" with no script
def texture_use(name: str) -> TextureUse
def texture_text(name: bytes, encoding: str) -> str | None    # the name up to its 0; None with no 0 or not decodable

# core/checks/model_checks.py
def check_model(model: Model, original: Model | None) -> tuple[Finding, ...]

# ModelFormat gains
def check(self, document: Model, original: Model | None) -> tuple[Finding, ...]
def check_value(self, pointer: str, value: JsonValue, original: Model) -> ValueCheck
```

  - `check_model` gives the findings of "Findings" that need no folder: `OUT_OF_RANGE`, `PARENT_AFTER_CHILD`, and
    `TEXTURE_ROLE_CHANGED` for a mesh that `original` also has under the same index, whose name's `TextureUse` differs
    in the file's extension, the slot or the script. The message says what changed (`sword02.OZJ` to `sword02.OZT`).
  - `check_value` checks one leaf without building the model: it raises what writing that value would raise, and
    returns the bytes a text may hold (32 for `/name`, 31 for the other names, `None` for other leaves) and the
    findings of that value alone (`TEXTURE_ROLE_CHANGED`, `PARENT_AFTER_CHILD`, `OUT_OF_RANGE` of a parent or of a
    mesh's `texture`). A pointer that is not a leaf of this model (a list, a mesh it lacks, a dummy bone's name) is
    `POINTER_NOT_FOUND`.

- [ ] **Step 1: Write the failing tests**:
  - `tests/core/test_model_textures.py`, `test_a_texture_name_says_its_file_slot_and_script`, parametrized as name,
    file, slot, script: `sword02.jpg`, `sword02.OZJ`, none, none · `lnn01.tga`, `lnn01.OZT` · `Hair_R.TGA`,
    `Hair_R.OZT`, no slot, `R` · `hair_R.jpg`, `hair_R.OZJ`, `hair`, `R` · `hair_r.tga`, `hair_r.OZT`, `hair`, no
    script · `level_man02.jpg`, `skin` · `skin_barbarian_01.jpg`, `skin`, no script · `hide.jpg`, `hidden` ·
    `bossmap1_R.jpg.jpg`, `bossmap1_R.OZJ`, `R` · `x.tga.jpg`, `x.OZT` · `x.jpg.tga`, `x.OZJ` · `ataque`,
    `ataque.OZJ` · `ataque.`, `ataque.OZJ` · the empty name, `.OZJ` · `x_NSHR.jpg`, `RHSN` · `x_RRRRzz.jpg`, `R` ·
    `Monster81_R2.jpg`, no script · `Object01_H`, `Object01_H.OZJ`, `H`.
  - `tests/core/test_model_checks.py`, on views changed in the test and built with `model_from_view`:
    - `test_an_index_outside_its_list_is_out_of_range` (Review Focus 2), parametrized on Sword01 (1 bone, 42
      vertices, 68 normals, 42 uvs, 1 mesh): vertex 0's `bone` 1 (`/meshes/0/vertices/0/bone`); normal 0's `bone` -1;
      normal 0's `vertex` 42; triangle 0's `vertices` `[42, 1, 2, 0]` (`/meshes/0/triangles/0/vertices/0`); its
      `normals` `[0, 68, 0, 0]` (`…/normals/1`); its `uvs` `[0, 1, -1, 0]` (`…/uvs/2`); the mesh's `texture` 1
      (`/meshes/0/texture`). Each gives one error finding at that pointer.
    - `test_many_bad_indices_of_a_mesh_are_one_finding`: every triangle of Sword01 with its first vertex index at
      9999 gives one `OUT_OF_RANGE` at `/meshes/0/triangles/0/vertices/0` whose message has "68".
    - `test_the_fourth_index_counts_only_for_a_polygon_of_four`: Sword01 untouched (fourth indices at -12851) has no
      finding; triangle 0 with `polygon` 4 has one at `…/vertices/3`.
    - `test_a_parent_outside_the_bones_is_out_of_range`: `parent` 56 and `parent` -2 on bone 0 of HelmMale01.
    - `test_a_parent_after_its_child_is_a_warning`: bone 1 of HelmMale01 with `parent` 5, and with `parent` 1, gives
      one `PARENT_AFTER_CHILD` warning at `/bones/1/parent`.
    - `test_more_than_200_bones_is_out_of_range`: a model with 201 dummy bones has one finding at `/bones`; with 200,
      none.
    - `test_a_rename_that_changes_what_the_client_does_warns` (Review Focus 4), with Sword01 as the original,
      parametrized: `sword02.tga`, `sword02_R.jpg` and `hair.jpg` each give one `TEXTURE_ROLE_CHANGED` at
      `/meshes/0/texture_name`; `blade.jpg` gives none; with `original=None`, none.
    - `test_check_value_checks_one_leaf`, with HelmMale01 as the original: `("/meshes/0/texture_name", "a" * 32)`
      raises `TEXT_TOO_LONG`; `("/meshes/0/texture_name", "head_02.tga")` returns limit 31 and one
      `TEXTURE_ROLE_CHANGED`; `("/name", "a" * 32)` returns limit 32 and no finding; `("/bones/1/parent", 5)` returns
      limit `None` and one `PARENT_AFTER_CHILD`; `("/bones/1/parent", 99)` one `OUT_OF_RANGE`;
      `("/meshes/0/vertices/0/position/0", "x")` raises `INVALID_VALUE`; `/meshes/9/texture_name`, `/bones/9/name` and
      `/meshes/0/vertices` raise `POINTER_NOT_FOUND`.
  - `tests/core/test_models.py`: `test_the_real_models_have_only_their_known_findings`: over the 1,962,
    `check_model(model, None)` gives exactly the four findings of Object36 and BloodCastle01 in "Findings".
    `test_the_real_models_have_the_scripts_the_client_counts`: `texture_use` over every mesh gives 135 scripts with
    `R`, 6 with `H`, 1 with `S` and none with `N`, each with one letter.
- [ ] **Step 2:** Run the three files. Expected: FAIL on import. **Step 3:** Implement. **Step 4:** `pytest -q`.
  Expected: PASS.
- [ ] **Step 5: Commit** `feat(tools): check a model's indices, bones and texture names`

### Task 4: The catalog, the use cases and the CLI

**Files:**
- Create: `src/mubmd/app/checks.py`, `tests/app/test_checks.py`, `tests/adapters/cli/test_models.py`
- Modify: `src/mubmd/core/catalog.py`, `src/mubmd/core/ports.py`, `src/mubmd/adapters/storage.py`,
  `src/mubmd/app/documents.py`, `app/edits.py`, `app/queries.py`, `app/folders.py`, `app/session.py`,
  `src/mubmd/adapters/cli/commands.py`, `describe.py`, `human.py`, `src/mubmd/adapters/tui/app.py` (only what the
  new `Summary` forces in the title); `tests/conftest.py` (`model_copy`),
  `tests/app/fake_store.py`, `tests/core/test_catalog.py`, `tests/adapters/test_storage.py`,
  `tests/adapters/cli/test_tables.py`

**Interfaces:**
- Consumes: Tasks 1 to 3.
- Produces:

```python
# core/ports.py
class CatalogFile:  read_by_client: bool | None      # None for a model
class FileStore(Protocol):                           # two more methods
    def files_in(self, folder: Path) -> tuple[Path, ...]: ...      # directly inside; () when it is not a folder
    def folders_in(self, folder: Path) -> tuple[Path, ...]: ...

# core/catalog.py
def identify(file_name: str | Path, data: bytes) -> Identified
    # a table by its name, as today; otherwise a model when `data` starts with MODEL_MAGIC, with the file's own
    # name, MODEL_ENCODING and read_by_client None
def target_of(output: Path, spec: FormatSpec) -> Identified
    # what `build` writes: a table's file by its name, as today; a model view to any other *.bmd
# all_formats() ends with FormatSpec("model", None, (), ModelFormat())

# app/documents.py
HEADER_POINTERS                                      # gains "/version"
@dataclass(frozen=True, slots=True)
class DecodedDocument:  path: Path; identified: Identified; sha256: str; size: int; document: Any; read_only: bool
def decode_document(store: FileStore, path: Path, *, encoding: str | None = None) -> DecodedDocument   # no view

# app/checks.py
class TextureFiles:                                  # one listing per folder, kept for a whole command
    def __init__(self, store: FileStore) -> None
    def place(self, model: Path, file: str) -> str | None
        # "" when the model's folder has `file`, the name of a folder beside it that has it, None when none does
def findings_of(store: FileStore, path: Path, identified: Identified, document: Any, original: Any | None,
                textures: TextureFiles | None = None) -> tuple[Finding, ...]
    # the codec's findings and, for a model, TEXTURE_NOT_FOUND and TEXTURE_ELSEWHERE

# app/queries.py
class InfoResult:  records: int | None; used_records: int | None; read_by_client: bool | None
                   version: int | None; meshes: int | None; bones: int | None; actions: int | None
class FormatInfo:  matches: str                       # "name" or "content"
```

  - `open_document` is built on `decode_document`. `info`, `validate` and `roundtrip` use `decode_document`, so they
    build no view; a folder `validate` shares one `TextureFiles`.
  - Every place that calls `codec.check` today (`info`, `validate`, `save_view`, `build`, `EditSession`) calls
    `findings_of`.
  - `TEXTURE_ELSEWHERE` is reported for a mesh whose texture name differs from `original`'s mesh of the same index,
    or that `original` lacks; never with `original=None`.
  - `build` finds its target with `target_of`. `find`, `get`, `set`, `apply`, `diff`, `dump` and `schema` need no
    change to open a model.
  - CLI: `--human info` prints a model's parts; `schema` and the help gain one model example (`mubmd get
    ~/mu/Data/Item/Sword01.bmd /meshes/0/texture_name`). The console UI's file tree does not change yet: it knows
    files by name, so a model stays dimmed until Task 6.

- [ ] **Step 1: Write the failing tests**:
  - `tests/core/test_catalog.py`: `test_a_model_is_known_by_its_first_bytes`: `identify("Data/Item/Sword01.bmd",
    b"BMD\x0a")` is `model` with encoding `cp949` and `read_by_client` `None`; with `b""` it is `UNKNOWN_FORMAT`.
    `test_a_tables_name_wins_over_the_bytes`: `identify("Data/Local/Text.bmd", b"BMD\x0a")` is `text`. Stage 3's
    `test_a_table_name_inside_a_model_folder_is_unknown` passes unchanged. `test_build_targets`: a model view to
    `x.bmd` is `model`; a model view to `Gate.bmd` and a gate view to `x.bmd` fail as today's mismatch does.
  - `tests/adapters/test_storage.py`: `files_in` and `folders_in` list one level, and give `()` for a missing
    folder.
  - `tests/app/test_checks.py` (on `FakeStore`): `test_a_texture_in_the_models_folder_is_found`,
    `test_a_texture_in_a_folder_beside_is_found_without_case` (`ITEM/UNICON.ozj` for `player/Helper03.bmd`),
    `test_a_texture_nobody_has_is_a_warning`, `test_only_an_edit_reports_a_texture_elsewhere`,
    `test_a_folder_is_listed_once` (a counting store: validating two models of one folder calls `files_in` on it
    once).
  - `tests/adapters/cli/test_models.py`, on `model_copy`:
    - `test_info_counts_a_models_parts`: `info Item/Sword01.bmd` has `format` `model`, `encoding` `cp949`, `cipher`
      `none`, `size` 6866, `version` 10, `meshes` 1, `bones` 1, `actions` 1, `findings` 0, and `records`,
      `used_records` and `read_by_client` null.
    - `test_info_of_the_biggest_model` (Review Focus 5): `info <reference>/player/Player.bmd` has `cipher` `K16`,
      `version` 12, `meshes` 0, `bones` 60, `actions` 205, `read_only` true.
    - `test_set_renames_a_texture`: `set Item/Sword01.bmd /meshes/0/texture_name etcring.jpg` exits 0 with no
      warnings, and the file differs from the reference only inside bytes 6772 to 6803.
    - `test_an_encrypted_model_stays_encrypted`: `set skill/m_waves.bmd /bones/0/name Root` exits 0; the file is
      still 3,030 bytes starting `BMD\x0c`, and `diff` against the reference has one change, at `/bones/0/name`, from
      `{"text": "Box01", "tail": "30310031"}` to `"Root"`.
    - `test_set_refuses_the_version`: `set Item/Sword01.bmd /version 12` exits 2 with `INVALID_VALUE` at `/version`
      and the file unchanged.
    - `test_build_changes_the_version`: Sword01 dumped, `version` set to 12 in the JSON and built over the file
      gives 6,870 bytes starting `BMD\x0c` and the size word 6862; built again with 10 gives the reference's bytes.
    - `test_build_writes_a_model_under_a_new_name`: the dump built to `Item/Sword99.bmd` equals `Sword01.bmd`.
    - `test_validate_names_a_texture_nobody_has`: `validate Item/waterring.bmd` exits 1 with one `TEXTURE_NOT_FOUND`
      warning at `/meshes/1/texture_name` naming `waterring.OZJ`.
    - `test_a_texture_in_a_folder_beside_is_no_finding`: `validate player/Helper03.bmd` exits 0.
    - `test_a_rename_warns_and_is_written` (Review Focus 4), parametrized on Sword01's `/meshes/0/texture_name`:
      `GroundEff.jpg` warns `TEXTURE_ELSEWHERE` naming `skill`; `nothing.jpg` warns `TEXTURE_NOT_FOUND`;
      `etcring_R.tga` has a `TEXTURE_ROLE_CHANGED` among its warnings. Each exits 0 and `get` reads the new name.
    - `test_find_reads_a_models_names`: `find player/HelmMale01.bmd Thigh` has `/bones/3/name`; `find … hide --field
      texture_name` has one item, `/meshes/1/texture_name`.
    - `test_formats_lists_the_model`: the last format is `model` with `size` null, no `files` and `matches`
      `content`; `text` has `matches` `name`. `test_schema_describes_a_model`: `schema model` lists
      `/meshes/<N>/texture_name`.
    - `test_a_corrupt_model_is_an_error_and_a_finding` (Review Focus 1): Sword01 cut to 100 bytes: `info` exits 30
      with `CORRUPT_FILE`; `validate` on the folder lists it as that file's finding and still checks the others.
  - `tests/adapters/cli/test_tables.py`: stage 3's two reference tests become `test_roundtrip_reads_every_file`
    (`checked 1991, identical 1991, differing [], skipped 3`) and `test_the_reference_has_only_its_known_findings`
    (stage 3's five and the five of "Findings"; `checked 1991`, `skipped 3`).
- [ ] **Step 2:** Run them. Expected: FAIL. **Step 3:** Implement. **Step 4:** `pytest -q`. Expected: PASS; no stage
  2 or 3 snapshot changes.
- [ ] **Step 5: Commit** `feat(tools): open models from the CLI`

### Task 5: Scale: `get` by levels, edits that copy one path, short dumps

**Files:**
- Create: `src/mubmd/core/json_text.py`, `tests/core/test_json_text.py`, `tests/app/test_scale.py`
- Modify: `src/mubmd/core/leaves.py`, `src/mubmd/core/patch.py`, `src/mubmd/app/queries.py` (`get`),
  `src/mubmd/app/folders.py` (`dump`); `tests/core/test_leaves.py`, `tests/core/test_patch.py`,
  `tests/adapters/cli/test_models.py`, `tests/adapters/cli/test_tables.py`

**Interfaces:**
- Produces:

```python
# core/leaves.py
def outline(value: JsonValue) -> tuple[JsonValue, bool]
    # `value` with every list of objects or of lists inside it replaced by {"count": n}; lists of leaves stay.
    # The bool says whether anything was replaced.

# core/json_text.py
LINE_WIDTH: Final = 120
def view_text(view: JsonValue) -> str
    # JSON indented by 2, not ASCII-escaped, ending in "\n"; a list or an object goes on one line when, with its
    # indentation and its key, it fits in LINE_WIDTH

# core/patch.py: same signatures
def apply_patch(view: JsonObject, ops: Sequence[PatchOp]) -> JsonObject
    # the given view is never changed; the result shares with it every part that no operation is at or under
```

  - `get`: a list at the pointer is paged as today, and each item shown goes through `outline`. An object at the
    pointer keeps its leaves and its lists of leaves; a member that is a list of objects or of lists shows its first
    `--limit` items, each through `outline`; a member that is an object is treated as the object at the pointer.
    Lists of leaves are cut at `--limit` as today. `truncated` is true when anything was cut or counted.
  - `diff_views` skips two parts that are the same object, so the changes of an edited view cost what the edit
    touched.
  - `dump` writes `view_text(view)`.

- [ ] **Step 1: Write the failing tests**:
  - `tests/core/test_leaves.py`: `test_outline_counts_the_lists_of_objects`: `outline({"name": "a", "classes": [1,
    0], "entries": [{"kind": 1}], "uvs": [[0.5, 0.25]], "at": {"hex": "00"}})` is `({"name": "a", "classes": [1, 0],
    "entries": {"count": 1}, "uvs": {"count": 1}, "at": {"hex": "00"}}, True)`; a value of leaves and lists of leaves
    comes back equal, with `False`.
  - `tests/adapters/cli/test_models.py`:
    - `test_get_of_a_model_shows_its_parts_and_their_counts` (Review Focus 5): `get <reference>/player/Player.bmd ''
      --limit 2` prints under 2,000 bytes; its value has `version` 12, `meshes` `[]`, `actions` `[{"keys": 3, "lock":
      1, "positions": {"count": 3}}, {"keys": 6, "lock": 0}]` and two bones, the first with `parent` -1 and `actions`
      `{"count": 205}`; `truncated` is true.
    - `test_get_pages_a_list_inside_a_model`: `get Item/Sword01.bmd /meshes/0/vertices --offset 41 --limit 5` has
      `total` 42, `next_offset` null and one item, `{"bone": 0, "position": [-0.2545926, 0.24154522, 64.7253]}`.
    - `test_get_of_a_mesh_shows_its_first_items`: `get Item/Sword01.bmd /meshes/0 --limit 2` has two `vertices`, two
      `triangles`, and `truncated` true.
    - `test_dump_then_build_gives_the_same_model`, parametrized over `MODEL_FILES`: built to a new name, the bytes
      equal the source's.
    - `test_the_biggest_model_dumps_and_builds` (Review Focus 5): `dump <reference>/player/Player.bmd` writes under
      12,000,000 bytes and prints only the path and the size; `build` of that file gives the reference's bytes.
  - `tests/adapters/cli/test_tables.py`: `test_get_of_one_quest_shows_its_entries` (`get Local/quest.bmd /records/0`
    has 16 `entries`, as in stage 3) and `test_a_page_of_quests_counts_their_entries` (`get Local/quest.bmd /records
    --limit 2`: each record's `entries` and `conditions` are `{"count": 16}`). Update any stage 3 test that pins a
    page of quest or slide records.
  - `tests/core/test_patch.py`: `test_apply_shares_what_it_does_not_touch`: on `{"a": [{"x": 1}, {"x": 2}], "b":
    [3]}`, replacing `/a/1/x` with 5 gives a result whose `["b"]` and `["a"][0]` are the view's own objects, whose
    `["a"][1]["x"]` is 5, and leaves the view's at 2. `test_a_later_operation_sees_an_earlier_one`: `replace /a/1/x 5`
    then `test /a/1/x 5` passes. `test_the_result_does_not_share_the_patch_value`: changing the operation's value
    after `apply_patch` does not change the result.
  - `tests/app/test_scale.py` (Review Focus 5), on a copy of `player/Player.bmd`:
    `test_an_edit_shares_the_rest_of_the_model`: after `EditSession.set("/bones/0/name", "Root")`,
    `session.view["bones"][1] is session.original["bones"][1]` and `session.view["actions"] is
    session.original["actions"]`; `session.changes` is one change; after `undo`, none.
    `test_saving_the_biggest_model_changes_only_its_edit`: after `save`, the file is still 2,414,277 bytes and decodes
    to a model equal to the reference's except bone 0's name.
  - `tests/core/test_json_text.py`: `test_a_value_that_fits_stays_on_one_line`: `view_text({"position": [1.5, 2.0,
    -0.0]})` is `'{"position": [1.5, 2.0, -0.0]}\n'`. `test_a_value_that_does_not_fit_gets_a_line_per_member`:
    `view_text({"texts": ["a" * 70, "b" * 70]})` is the object and its list each opened, one string per line at four
    spaces, written out in the test. `test_the_text_reads_back_as_the_view`, parametrized: Sword01's view,
    `Text.bmd`'s, `{}`, `{"a": [], "b": {}}`, `{"q": "\"é\\"}`: `json.loads(view_text(view)) == view`, and `é` is not
    escaped. `test_no_line_passes_the_width_but_a_single_value`: in Sword01's text, every line over 120 columns
    holds one leaf.
- [ ] **Step 2:** Run them. Expected: FAIL. **Step 3:** Implement. **Step 4:** `pytest -q`. Expected: PASS.
- [ ] **Step 5: Commit** `feat(tools): keep get, edits and dumps small on big models`

### Task 6: The console UI for a model

```text
╭─ ◆ mubmd ─ ~/mu/Data ──────────────────────────────────────── working copy · 1 unsaved ─╮
│ ╭ Data ───────────────╮ ╭ player/HelmMale01.bmd ─ model · version 10 · 2 meshes · 56 … ╮ │
│ │ ▸ Item              │ │ ▾ meshes (2)                                                │ │
│ │ ▾ player            │ │     0  head_02.jpg · 40 vertices · 62 triangles             │ │
│ │   ◆ HelmMale01.bmd ●│ │     1  hide.jpg · 8 vertices · 6 triangles · hidden         │ │
│ │   △ Helper03.bmd    │ │ ▾ actions (1)                                               │ │
│ │   △ Player.bmd      │ │     0  1 key                                                │ │
│ │ ▤ Gate.bmd          │ │ ▾ bones (56)                                                │ │
│ ╰─────────────────────╯ │   ▾ 0  Bip01                                                │ │
│                         │       1  Bip01 Footsteps                                    │ │
│                         │     ▾ 2  Bip01 Pelvis                                       │ │
│                         │         3● Left thigh                                       │ │
│                         │     9  (dummy)                                              │ │
│                         ╰─────────────────────────────────────────────────────────────╯ │
│                         ╭ bone 3 ─────────────────────────────────────────────────────╮ │
│                         │ name        ● Left thigh                                    │ │
│                         │ parent        2                                             │ │
│                         │ actions       1                                             │ │
│                         ╰─────────────────────────────────────────────────────────────╯ │
│                         ╭ /bones/3/name ──────────────────────────────────────────────╮ │
│                         │ Left thigh▏                                    10/31 bytes  │ │
│                         ╰─────────────────────────────────────────────────────────────╯ │
│ enter edit  ctrl+z undo  ctrl+s save  d diff  ctrl+p commands  q quit                   │
╰─────────────────────────────────────────────────────────────────────────────────────────╯
```

**Files:**
- Create: `src/mubmd/app/model_parts.py`, `tests/app/test_model_parts.py`,
  `src/mubmd/adapters/tui/widgets/model_tree.py`, `tests/adapters/tui/test_models.py`
- Modify: `src/mubmd/adapters/tui/widgets/bmd_tree.py`, `field_list.py`, `src/mubmd/adapters/tui/app.py`,
  `mubmd.tcss`, `src/mubmd/core/ports.py`, `src/mubmd/adapters/storage.py`, `src/mubmd/app/documents.py`;
  `tests/adapters/tui/test_snapshots.py`, `tests/adapters/test_storage.py`, `tests/app/fake_store.py`

**Interfaces:**
- Consumes: `EditSession` (`parse`, `measure`, `set`, `findings`, `summary`), `identify` (Task 4), `texture_use`
  and `texture_text` (Task 3).
- Produces, for the file tree's glyphs (it reads three bytes of each file, not the file):

```python
# core/ports.py, FileStore
def head(self, path: Path, count: int) -> bytes: ...               # the first `count` bytes
# app/documents.py
def format_name_of(store: FileStore, path: Path) -> str | None     # by the name and the head; None when unknown
```

- Produces, in `app/model_parts.py` (no Textual import):

```python
@dataclass(frozen=True, slots=True)
class Part:
    pointer: str                       # "", "/meshes", "/meshes/0", "/actions/3", "/bones/12"
    label: str
    children: tuple["Part", ...] = ()

def is_model(view: JsonObject) -> bool
def model_parts(view: JsonObject) -> Part
    # the model, with its groups `meshes (n)`, `actions (n)` and `bones (n)` under it
def part_fields(view: JsonObject, pointer: str) -> JsonObject
    # what the UI edits there: {"name"} for the model, {"texture_name", "texture"} for a mesh, {"name", "parent"}
    # for a real bone, {} for anything else
def part_facts(view: JsonObject, pointer: str) -> tuple[tuple[str, str], ...]
    # read-only lines: a mesh's counts, file, slot and script; an action's keys and lock; a bone's actions
```

  - Labels: a mesh is `0  sword02.jpg · 42 vertices · 68 triangles`, plus its slot and its script when it has them; an
    action `3  12 keys · locked`; a bone `3  Bip01 L Thigh`; a dummy bone `9  (dummy)`.
  - A real bone hangs from its parent when the parent is a real bone of the model and the chain of parents from it
    reaches -1 without passing the bone again; any other bone sits directly under `bones`. Siblings keep the file's
    order.
- What the UI does:
  1. A model opens as a tree where a table has its rows. The title gives the version, the cipher when it is K16, and
     the counts.
  2. Moving through the tree shows the part's fields below: the editable ones, then the facts, dimmed. `enter` on a
     part goes to the fields; `enter` on an editable field loads it in the editor, whose title is the full pointer;
     `escape` goes back one step. A part with nothing to edit keeps the focus on the tree and says so.
  3. The editor works as for a table: a text as written with `n/limit bytes`, a number as a number, the warnings of
     `measure` under it (`TEXTURE_ROLE_CHANGED`, `PARENT_AFTER_CHILD`, `OUT_OF_RANGE`).
  4. After an edit the tree is rebuilt (a renamed bone, a bone moved under its new parent) with the cursor on the same
     pointer, and a changed part carries `●`. Undo, redo, the diff screen, save and reload are the table's.
  5. In a model, `/` and `f` say in the status line that they work in tables.
  6. The file tree marks a model with `△`.

- [ ] **Step 1: Write the failing tests**:
  - `tests/app/test_model_parts.py`: `test_a_model_has_three_groups` (HelmMale01: `meshes (2)`, `actions (1)`,
    `bones (56)`); `test_a_bone_hangs_from_its_parent` (bone 3, `Bip01 L Thigh`, is under bone 2, which is under bone
    0); `test_dummy_bones_sit_under_the_group` (the ten, labeled `9  (dummy)` and so on);
    `test_a_parent_after_its_child_still_nests` (BloodCastle01: bones 43 and 44 are under 45);
    `test_bones_in_a_cycle_sit_under_the_group` (bones 1 and 2 as each other's parent, and a bone with `parent` 99);
    `test_a_part_has_the_fields_the_ui_edits` (`/meshes/0` of Sword01: `{"texture_name": "sword02.jpg", "texture":
    0}`; `/bones/9` of HelmMale01: `{}`; `""`: the name); `test_a_meshs_facts_say_what_its_name_does` (mesh 1 of
    `player/MaskHelmMale09.bmd` has `("file", "hair_R.OZJ")`, `("slot", "hair")` and `("script", "R")`).
  - `tests/adapters/tui/test_models.py` (`run_test` and `Pilot`, size 120×40, on `model_copy`):
    - `test_opening_a_model_shows_its_tree`: HelmMale01 shows the three groups and the title has `2 meshes · 56
      bones · 1 action`.
    - `test_renaming_a_texture_saves_only_its_name`: Sword01's mesh 0 `texture_name` becomes `etcring.jpg`; after
      save the file differs from the reference only inside bytes 6772 to 6803.
    - `test_moving_a_bone_moves_it_in_the_tree`: bone 3 of HelmMale01 with `parent` 0 is under bone 0 and carries
      `●`; after `ctrl+z` it is back under bone 2.
    - `test_a_parent_after_the_bone_warns_and_is_taken`: `parent` 5 on bone 1 shows the warning under the editor and
      `enter` keeps it.
    - `test_a_dummy_bone_has_nothing_to_edit`: `enter` on bone 9 leaves the focus on the tree.
    - `test_search_and_filter_stay_for_tables`: `/` on a model shows no search box and a status message.
    - `test_the_file_tree_marks_models`: `Sword01.bmd` has `△`, `Text.bmd` `≡`, `Gate.bmd` `▤`. In
      `tests/adapters/test_storage.py`: `head` gives the first bytes, the whole file when it is shorter, and
      `FILE_NOT_FOUND` for a missing file.
    - `test_the_biggest_model_opens_and_saves` (Review Focus 5): a copy of `Player.bmd` shows 205 actions and 60
      bones; bone 0 renamed and saved decodes with that name.
    - A snapshot `test_a_model`: HelmMale01 with bone 3 selected.
- [ ] **Step 2:** `pytest tests/adapters/tui tests/app/test_model_parts.py -q`. Expected: FAIL.
- [ ] **Step 3:** Implement. **Step 4:** `pytest -q`. Expected: PASS; the earlier snapshots unchanged. Look at the
  new snapshot in `snapshot_report.html` before accepting it with `--snapshot-update`.
- [ ] **Step 5:** Save screenshots of a model's tree, a mesh's fields and the file tree (dark theme) to
  `/mnt/project-files/bmd-editor/`.
- [ ] **Step 6: Commit** `feat(tools): show and edit a model's parts in the console UI`

### Task 7: The skill, the README and the spec

**Files:**
- Modify: `.claude/skills/muret-bmd/SKILL.md`, `src/mubmd/adapters/cli/describe.py` (`BRIEF`),
  `tests/skill/test_recipes.py` (its working copy becomes `model_copy`), `Client/tools/bmd/README.md`, `AGENTS.md`
  (the tool's line), `docs/superpowers/specs/2026-11-25-bmd-editor-design.md`,
  `docs/superpowers/plans/2026-11-25-bmd-editor.md` (stage 4's line links here)

**Interfaces:**
- Consumes: the finished CLI. `tests/skill/test_recipes.py` runs every recipe; `BRIEF` equals the skill's first
  paragraph.

- [ ] **Step 1: Baseline, before touching the skill** (superpowers:writing-skills): one fresh agent with the tool
  installed and today's skill gets three jobs on a working copy and reports its calls: make `Item/Sword01.bmd` use
  the texture `etcring.jpg` and say whether the client will find the file; say which bone `Bip01 L Thigh` hangs from
  in `player/HelmMale01.bmd` and how many bones the model has; turn `skill/m_waves.bmd` into a plain model without
  changing anything else. Keep what it gets wrong.
- [ ] **Step 2: Write the failing tests**: the new recipes, with their `<!-- recipe: ... -->` marks:
  `read-a-models-parts` (`get $REF/player/HelmMale01.bmd '' --limit 3`), `rename-a-texture` (`get` the name, then one
  `apply` with `test` and `replace`), `move-a-bone` (`apply` on `/bones/3/parent`), `dump-and-build-a-model` (`dump`
  to `$TMP`, `build` to a new name; the text beside it says that changing `version` in the JSON between the two
  decrypts or encrypts the model, which a recipe cannot do because recipes run only `mubmd`).
- [ ] **Step 3:** Rewrite the skill: the first paragraph and `BRIEF` say "texts, tables and models"; the description
  names models, textures and bones; a section on models with the view in ten lines, the pointers, how `get` counts
  what it does not show, the texture name's three rules as a small table, the findings and what to do about each,
  the version rule, and the reference's five findings.
- [ ] **Step 4:** `pytest tests/skill -q`. Expected: PASS. Run the three jobs again with a fresh agent and the new
  skill: each finishes in a few calls without reading the tool's source. Fix what it still gets wrong and note both
  call counts for the PR body.
- [ ] **Step 5:** README: models in the formats table, a model's view, the UI's tree, stage 4 done. Spec: the places
  that would now contradict the tool: the triangle's bytes (decision 2), floats (4), the dump's layout (5), `get`
  (6), the findings' names (8), `info` and `formats` (9), and "Models" in "Stages".
- [ ] **Step 6: Commit** `docs(tools): teach the muret-bmd skill the models`

### Task 8: Finish stage 4

- [ ] **Step 1:** From `Client/tools/bmd`, on Python 3.13 and 3.12: `ruff check .`, `ruff format --check .`,
  `mypy src tests`, `pytest -q`. Expected: all pass.
- [ ] **Step 2:** `mubmd roundtrip ../../assets/Data` and `mubmd validate ../../assets/Data`. Expected: the two
  results of "Global Constraints". Note how long each takes, and `info`, `set`, `dump` and `build` on a copy of
  `Player.bmd`, with the dump's size, for the PR body.
- [ ] **Step 3:** `git diff --name-only origin/main...HEAD`: every path under `Client/tools/`, `.claude/`, `docs/`, or
  a `*.md`.
- [ ] **Step 4:** One fresh reviewer reads the whole branch against this plan, its Review Focus and the spec; fix
  what it finds that is critical or important, each with a test that failed first.
- [ ] **Step 5:** Push and open **PR 4**, `feat(tools): edit models with mubmd`, with the decisions taken and what
  stays deferred in its body; assign `ema-ljn`, request his review, subscribe to its activity.
