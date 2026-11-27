# BMD editor, stage 5: the web viewer of models Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `mubmd web ~/mu/Data` opens a page in the browser, served from the same machine, that shows any of the
1,962 models with its textures, plays its actions, draws its skeleton, and lists its parts, facts and findings. It
views and does not edit. It takes the place of the braille wireframe the spec had as stage 5.

**Architecture:** A third adapter, `adapters/web`, beside the CLI and the console UI, over the same use cases. Three
new use cases in `app/` turn a model's view into what a renderer needs (corners, bones, poses as quaternions, the
texture of a mesh), and `core/` learns to read the two texture files as the client does. The page is NiceGUI (Python);
the model is drawn by three.js, which NiceGUI ships, through one JavaScript file of ours that builds a skinned mesh.
The browser asks the server for a scene, then for one action at a time.

**Tech Stack:** stage 4's, plus NiceGUI 4.1.0 as the optional extra `mubmd[web]` (it brings FastAPI, Starlette,
uvicorn and three.js r187) and Playwright 1.65.2 for the browser tests. No Node, no bundler, no JavaScript package.

**Spec:** [`docs/superpowers/specs/2026-11-25-bmd-editor-design.md`](../specs/2026-11-25-bmd-editor-design.md); this
stage replaces its stage 5 and Task 9 rewrites those paragraphs. The analysis Emanuel chose from on 2026-11-27 ("Web
local con three.js") is the Claude Doc "mubmd: editor visual de modelos". Previous plan:
[`2026-11-27-bmd-editor-models.md`](2026-11-27-bmd-editor-models.md) (stage 4).

## Global Constraints

- The web writes nothing: no file of any folder changes while `mubmd web` runs. `Client/assets/Data` is never written,
  and tests copy what they need or only read it.
- `mubmd roundtrip Client/assets/Data` and `mubmd validate Client/assets/Data` give what they gave after stage 4
  (1,991 identical and 3 skipped; the same eleven findings, in eight files).
- `core/` imports only the standard library. `app/` imports no adapter and no UI or web library. Only `adapters/web`
  imports `nicegui`, `fastapi`, `starlette` or `uvicorn`, and the CLI imports `adapters/web` only inside the `web`
  command, so every other command starts as fast as today (`tests/test_layers.py`).
- The CLI contract of schema 1 holds: the envelopes, the exit codes 0/1/2/20/30/70 and the closed `ErrorCode` enum.
  This stage adds one command, `web`, with its output type. It adds no error code, no exit code and no finding code.
- stdout of `mubmd web` holds nothing but its envelope.
- The server listens on 127.0.0.1 only. There is no flag to change that.
- Exact pins: `nicegui==4.1.0`, `playwright==1.65.2`. No third-party JavaScript file enters the repo: three.js comes
  inside NiceGUI's wheel, and `bmd_model.js` is ours.
- **The PR touches only `Client/tools/`, `docs/`, `*.md` and `.github/workflows/bmd-tool.yml`**, all of them in the
  list of `.github/workflows/ci.yml` that does not compile C++ (Emanuel, 2026-11-25). Before opening the PR: `git diff
  --name-only origin/main...HEAD` against that `case` list. The PR opens when the stage is done and gets no docs-only
  push while Emanuel waits to merge.
- English in code, docs, commits and PR text; comments only when strictly necessary (CODING_RULES §7, §9); one
  behavior, one test (§13); Angular commits with scope `tools`; the PR targets `main`.
- Every number in "What was measured" and in the tests was measured on the real files on 2026-11-27. A float of a
  model is a float32, so tests compare floats with `pytest.approx`.

## Decisions taken in this plan

Each is a call the analysis left open or that the real files and the prototype forced. Emanuel reads these before any
code.

1. **One tool, a third way in.** `mubmd web` lives in `adapters/web`, and NiceGUI is the optional extra `mubmd[web]`:
   installed, it adds 39 packages and about 89 MB, which the CLI and the console UI do not need. `[dev]` includes it.
2. **This stage views; it does not edit.** With no write, the reference folder can be opened as it is (`mubmd web
   Client/assets/Data`). Editing from the browser is the next stage and brings its own protection (a token, so that
   another page open in the browser cannot ask this server to write).
3. **The page's requests are private to the page.** They are not part of the CLI contract: no schema number, no
   envelope. Agents keep using the commands; `web` is for people, like `tui`, and its help says so. The `muret-bmd`
   skill is not edited.
4. **`mubmd web` under the CLI contract.** It prints one line on stderr when it is ready (`mubmd web:
   http://127.0.0.1:8765 (Ctrl+C stops it)`), opens the browser unless `--no-browser`, and when it stops prints the
   usual envelope and exits 0. Without the extra it fails with `USAGE` and the suggestion `pip install 'mubmd[web]'`,
   as `tui` does without a terminal. A port in use fails with `IO_ERROR` and the suggestion `mubmd web --port 0`
   before the server starts (uvicorn would exit 3 on its own, outside the contract). The default port is 8765, and
   `--port 0` takes a free one.
5. **Local only.** Besides listening on 127.0.0.1, the server refuses a request whose `Host` is not `127.0.0.1` or
   `localhost` (Starlette's `TrustedHostMiddleware`, which covers the websocket too). That stops a site on the
   internet from reaching the server through a DNS name that points at 127.0.0.1. A `file` that leaves the served
   folder is refused.
6. **The server sends what a renderer needs, not the view.** A scene has, per mesh, one position, one UV and one bone
   per triangle corner; the bones; the keys of each action. The poses of one action travel per request, already as
   quaternions. The prototype sent `Player.bmd` whole (205 actions, 4.8 MB of JSON, 2.5 s); now its scene is small and
   an action is asked for when it is played. Normals are not sent: nothing uses them (decision 7).
7. **Angles become quaternions in Python**, with Half-Life's formula, the rotation Rz · Ry · Rx
   (`Client/src/engine/Math/HalfLifeMath.h`, 0x5fcd20), so the math is tested without a browser. The browser only
   interpolates (three.js `AnimationMixer`) and skins: a vertex follows one bone and is stored in that bone's space, so
   a three.js `SkinnedMesh` with identity inverse matrices draws it as it is.
8. **It is a viewer, not the client's renderer.** Textures are drawn unlit, both sides, with alpha test. There is no
   lighting, none of the client's per-model blend rules and no x87 math. A switch draws the model additive, for the
   effects that are light on black.
9. **Textures are read as the client reads them.** An OZJ is a JPEG after 24 bytes, sent as it is for the browser to
   decode; one that is not a JPEG (seven in the Data are TGA) is not shown, as libjpeg rejects it. An OZT is read by a
   reader of ours in `core/`, with the client's rule (`Client/src/engine/Data/Textures/OztTexture.h`, 0x63bd00): 4
   bytes, width and height at 0x10 and 0x12, 32 bits at 0x14 or it is refused, BGRA pixels from 0x16, rows flipped, no
   side over 512. Its pixels travel as RGBA bytes to a `THREE.DataTexture`. Not Pillow (CODING_RULES §10): what is
   read is those few lines, Pillow would show `Monster/wolfa02.OZT` (24 bits), which the client refuses, and it would
   be one more dependency. The 512 limit is not checked on a JPEG, whose size only its decoder knows (one file in the
   Data is over it, `skill/null.OZJ`, and no model uses it).
10. **A texture that is not shown leaves its mesh in a flat color, and the mesh's facts say why.** `validate` does not
    learn to open texture files in this stage: no new finding.
11. **The file does not say how fast an action plays** (the client sets the speed in code, per model and action). The
    viewer plays 8 keys per second, with a control for 2, 4, 8 and 16. An action loops as the client's does
    (`Client/src/engine/Models/Animation.h`, 0x4ec200): after the last key comes the first, and an action with `lock`
    wraps one key earlier.
12. **Character parts can be shown on the player's skeleton.** In `player/`, 219 of the 227 other models (the
    helms, armors, pants, gloves and boots, each with one action of one key) hang their vertices from bones that
    `Player.bmd` has at the same index with the same name and parent. For those the page offers a switch, and with it
    on the part moves with `Player.bmd`'s 205 actions. The rule: every bone a vertex of the model uses, and each
    ancestor of it, has the same name and the same parent at that index in the file named `player.bmd` (any case) of
    the model's folder. The helpers, the wings and `playerTest.bmd` do not pass, and get no switch.
13. **Odd data is drawn without failing.** A triangle whose vertex names a bone outside the skeleton or a dummy is
    not drawn, and the mesh's facts count it (`object35/Object36.bmd`, mesh 1, bone -8888: its 10 triangles). A bone
    whose parent is outside the list or that never reaches a root hangs from the root, as in the console UI's tree. A
    parent listed after its child (`NPC/BloodCastle01.bmd`) needs nothing. A model with no meshes shows its skeleton
    (`Player.bmd`). A model with nothing (`Object3/Object25.bmd`) shows an empty scene and says so.
14. **Browser tests run in CI, in a second job.** The JavaScript (about 200 lines) runs only in a browser, so it gets
    tests with Playwright against headless Chromium: `pytest -m browser`. They run in a new job of `bmd-tool.yml`, in
    parallel with today's, so the wait stays what the first job takes (about 4.5 minutes; the new one an estimated
    3).
    `bmd-tool.yml` is in `ci.yml`'s list of files that do not compile. The other way, if Emanuel prefers it: the
    marker stays and only people and agents run it.
15. **The "Lorencia" palette is shared.** Its values move to `adapters/palette.py`, read by the console UI and by the
    web. The web has the dark variant only.
16. **The braille wireframe is dropped.** The spec's stage 5 becomes this viewer (Task 9 has the paragraphs).

## What was measured

On the 1,962 models of `Client/assets/Data` and on the prototype (NiceGUI 4.1.0, headless Chromium with software
WebGL), 2026-11-27. The prototype was 213 lines of Python and 128 of JavaScript, outside the repo.

| | |
| --- | --- |
| Biggest model by corners | `Monster/monster65.bmd`: 9,414 corners in 10 meshes |
| Biggest action | `Logo/NewFace01.bmd`, action 1: 76 keys of 112 bones |
| Models with no meshes / no actions / no bones | 9 / 4 / 4 |
| Vertices on a bone outside the skeleton | one mesh: `object35/Object36.bmd`, mesh 1 |
| Vertices on a dummy bone | none |
| Polygons that are not triangles | none |
| The 3,421 meshes' textures | 3,351 in the model's folder, 69 in a folder beside it, 1 nowhere (`Item/waterring.bmd`) |
| Their files | 2,926 OZJ that are JPEG, 494 OZT of 32 bits; none of them is one the client refuses |
| OZT in the Data | 664: 663 of 32 bits, `Monster/wolfa02.OZT` of 24; all uncompressed, 663 with 26 bytes after the pixels |
| Listing the folder's models | 0.17 s |
| Opening a model and building its view | `Item/Sword01.bmd` 3 ms, `Monster/Monster01.bmd` 48 ms, `player/Player.bmd` 0.6 s |
| Prototype, `Monster01` ready in the browser | 0.5 s (request 60 to 120 ms, building the meshes 6 to 11 ms) |

Checked on the prototype and on throwaway probes: `ui.run` called from a function leaves stdout clean with
`show_welcome_message=False` (the probe also saw it return after Ctrl+C, but its process ignored the signal: on a real
Ctrl+C it ends in a `KeyboardInterrupt`, which Task 8's test found and `serve` now takes); `TrustedHostMiddleware`
answers 400 to another `Host`; NiceGUI's test user reaches the routes and the page without a browser, and a tree
selection can be triggered; `mypy --strict` passes over code that imports NiceGUI; `pip` resolves NiceGUI beside stage
4's pins; Playwright for Python drives the cloud container's Chromium when given its path.

**Not checked:** Windows (Emanuel's machine: the first thing to try), a real GPU, and WebGL in the Chromium of
GitHub's runner (the browser job's first run says; the tests launch it with `--enable-unsafe-swiftshader`, which is
harmless where it is not needed).

## Review Focus

What the tests of a happy path would not exercise, most likely first. Each has its test in the task named.

1. **A working copy edited while the page is open** (the console UI or an agent saves the model): the next scene
   shows the file as it is now, not the view that was open. Task 5, `test_a_changed_file_is_read_again`.
2. **A request for a file outside the served folder** (`../`, an absolute path, a table): refused, with nothing read.
   Task 5, `test_a_file_outside_the_folder_is_not_found` and `test_a_table_is_not_a_scene`.
3. **A texture that is missing or that the client refuses**: the model still loads, the mesh is flat and says why.
   Tasks 1 and 2 for the reasons, Task 6 for the facts, Task 8 `test_a_missing_texture_leaves_the_mesh_flat`.
4. **A model with an odd skeleton or with nothing**: no meshes, no actions, no bones, a bone outside the list, a
   parent that loops. Tasks 3 and 4, and Task 8 `test_odd_models_load_without_errors`.
5. **The server started where it cannot or should not answer**: the port taken, the extra missing, a `Host` from
   elsewhere, two pages open with different models. Task 7 (`test_a_taken_port_is_an_error_with_a_way_out`,
   `test_web_without_the_extra_says_how_to_install_it`, `test_another_host_is_refused`) and Task 6
   (`test_two_pages_keep_their_own_model`).

## The scene

What `GET /api/scene?file=player/HelmMale01.bmd&on=donor` answers, with its lists cut to their first items. `on` is
`own` (the default) or `donor`.

```json
{
  "file": "player/HelmMale01.bmd",
  "skeleton": "player/Player.bmd",
  "donor": "player/Player.bmd",
  "bones": [{"name": "Bip01", "parent": -1}, {"name": "Bip01 Footsteps", "parent": 0}],
  "actions": [{"keys": 3, "lock": true}, {"keys": 6, "lock": false}],
  "meshes": [{"positions": [-8.1, 2.5, 3.0], "uvs": [0.37, 0.82], "bones": [20], "skipped": 0}],
  "bounds": {"low": [-13.141, -16.272, 160.149], "high": [25.936, 34.578, 192.624]}
}
```

- `skeleton` is the file whose bones and actions these are: `file` itself, or the donor when `on=donor`. `donor` is
  the file whose skeleton the model can borrow, or `null`. Paths are relative to the served folder, with `/`.
- A dummy bone has the name `""` and the parent -1, and keeps its index.
- A mesh has three numbers per corner in `positions` (in its bone's space), two in `uvs` (the file's, whose v grows
  downwards; the textures are sent top row first to match) and one in `bones`. `skipped` counts its triangles that are
  not drawn.
- `bounds` is the box of the model at the first key of the first action, or `null` when there is nothing to measure.

`GET /api/poses?file=player/Player.bmd&action=0` answers `{"keys": 3, "bones": [{"positions": [9 numbers],
"quaternions": [12 numbers]}, null, ...]}`: per bone, three numbers per key and four per key (x, y, z, w), or `null`
for a dummy or a bone without that action.

`GET /api/texture?file=Item/Sword01.bmd&mesh=0` answers the JPEG (`image/jpeg`), or an OZT's pixels
(`application/octet-stream`, RGBA, top row first, with the headers `X-Width` and `X-Height`).

A request that fails answers `{"code": "FILE_NOT_FOUND", "message": "..."}` with 404 for `FILE_NOT_FOUND`,
`POINTER_NOT_FOUND` and `UNKNOWN_FORMAT`, and 400 for any other code. Every answer carries `Cache-Control: no-store`.

## The page

One page, `/`, with `?file=<model>` and `&on=donor`. Dark "Lorencia" colors.

```text
┌ ◆ mubmd ─ ~/mu/Data ────────────────────────────────────────────────────────────────────────┐
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
  Choosing a model goes to `/?file=<it>`.
- **Middle:** the scene, with orbit controls and a grid, the camera placed so the model's box is in view. Above it:
  play and pause, the key slider, the speed, the switches for the skeleton and for additive drawing, and, when the
  model has a donor, the switch "On Player.bmd's skeleton" (it reloads the page with `on=donor`).
- **Right:** the parts tree of `model_parts`, the facts and fields of the chosen part (`part_facts`, `part_fields`,
  read-only), and the model's findings. Choosing a mesh tints it in the scene; choosing an action plays it; choosing a
  bone turns the skeleton on and marks that bone. Choosing a finding chooses its part.
- With no `file`, the middle says to pick a model. With a `file` that fails, it shows the error's message.

## Files

All under `Client/tools/bmd/` unless a path starts elsewhere.

| File | What it holds |
| --- | --- |
| `src/mubmd/core/formats/texture_images.py` (new) | `read_ozt`, `read_ozj`, `RgbaImage` |
| `src/mubmd/app/checks.py` | `TextureFiles.find` |
| `src/mubmd/app/textures.py` (new) | `mesh_texture`, `MeshTexture` |
| `src/mubmd/app/model_parts.py` | `tree_parents`, taken out of `_bone_parts` |
| `src/mubmd/app/model_scene.py` (new) | `angle_quaternion`, `model_scene`, `action_poses`, `rest_bounds`, `borrows_skeleton`, `donor_of` |
| `src/mubmd/adapters/palette.py` (new) | `Palette`, `LORENCIA`, `LORENCIA_LIGHT` |
| `src/mubmd/adapters/tui/theme.py` | its two themes built from the palette |
| `src/mubmd/adapters/web/models.py` (new) | `Models`: the served folder, its models, the open sessions |
| `src/mubmd/adapters/web/api.py` (new) | `add_api`: the three routes and the error answer |
| `src/mubmd/adapters/web/scene.py`, `bmd_model.js` (new) | `BmdModel`, the scene object, and its JavaScript |
| `src/mubmd/adapters/web/page.py` (new) | `add_page`: the page |
| `src/mubmd/adapters/web/server.py` (new) | `free_port`, `guard`, `build`, `serve` |
| `src/mubmd/adapters/cli/commands.py` | the `web` command, `WebData` |
| `pyproject.toml` | the `web` extra, `[dev]`, the `browser` marker, NiceGUI's test plugin |
| `tests/…` | one test module per module above, `tests/adapters/web/`, `tests/adapters/cli/test_web.py`, `tests/test_layers.py` |
| `.github/workflows/bmd-tool.yml` | the job `mubmd web (Chromium)` |
| `README.md`, `docs/superpowers/specs/2026-11-25-bmd-editor-design.md` | the viewer, the stages |

`bmd_model.js` ships in the wheel beside `scene.py`, as `mubmd.tcss` does beside the console UI.

---

### Task 1: Texture files as the client reads them

**Files:**
- Create: `src/mubmd/core/formats/texture_images.py`, `tests/core/test_texture_images.py`

**Interfaces:**
- Produces:

```python
OZJ_PREFIX: Final = 24
OZT_PREFIX: Final = 4
MAX_SIDE: Final = 512

@dataclass(frozen=True, slots=True)
class RgbaImage:
    width: int
    height: int
    pixels: bytes  # 4 bytes per pixel, red first, the image's top row first

def read_ozt(data: bytes) -> RgbaImage
def read_ozj(data: bytes) -> bytes  # the JPEG that follows the prefix
```

  Both raise `MubmdError(ErrorCode.CORRUPT_FILE, ...)` with a message that names the reason. `read_ozt` reads width
  and height as signed 16-bit numbers at 0x10 and 0x12 and the bits per pixel at 0x14, takes `width * height * 4`
  bytes from 0x16 as BGRA rows stored bottom first, and ignores the rest of the header and whatever follows the
  pixels. `read_ozj` checks only that the bytes after the prefix start with `FF D8`.

- [ ] **Step 1: Write the failing tests**, on the reference's files, read and never written:
  - `test_an_ozt_gives_its_pixels_top_row_first`: `read_ozt` of `Item/unicon01.OZT` (4,144 bytes) is 32 × 32, has
    4,096 bytes of pixels, its first four are `(226, 226, 226, 3)`, the first four of its last row are `(202, 124, 56,
    247)`, and `hashlib.sha256(pixels).hexdigest()` starts with `bce5a0cba06402e6`.
  - `test_an_ozt_that_is_not_square`: `NPC/lnn01.OZT` is 64 × 128 and its first pixel is `(193, 193, 193, 83)`.
  - `test_an_ozt_of_24_bits_is_refused`: `Monster/wolfa02.OZT` raises `CORRUPT_FILE`, and the message has `24`.
  - `test_an_ozt_cut_short_is_refused`, parametrized: the first 10 bytes of `unicon01.OZT`, and the file without its
    last 2,000 bytes.
  - `test_an_ozt_with_a_side_over_512_is_refused` and `test_an_ozt_with_no_pixels_is_refused`: `unicon01.OZT` with its
    width rewritten to 513, and to 0.
  - `test_an_ozj_gives_its_jpeg`: `read_ozj` of `Item/sword02.OZJ` (6,316 bytes) has 6,292 bytes and starts with `FF D8
    FF E0`.
  - `test_an_ozj_that_is_a_tga_is_refused`: `object35/ma_001.OZJ` raises `CORRUPT_FILE`, and the message says it is not
    a JPEG.
  - `test_an_ozj_shorter_than_its_prefix_is_refused`: 24 bytes.
- [ ] **Step 2:** Run `pytest tests/core/test_texture_images.py -q`. Expected: FAIL, the module does not exist.
- [ ] **Step 3:** Implement with `struct`. **Step 4:** Run it again, then `pytest tests/test_layers.py -q`. Expected:
  PASS.
- [ ] **Step 5: Commit** `feat(tools): read OZT and OZJ textures as the client does`

### Task 2: The texture of a mesh

**Files:**
- Create: `src/mubmd/app/textures.py`, `tests/app/test_textures.py`
- Modify: `src/mubmd/app/checks.py`, `tests/app/test_checks.py`

**Interfaces:**
- Consumes: `read_ozt`, `read_ozj`, `RgbaImage` (Task 1); `texture_use`, `leaf_text`, `TextureFiles`.
- Produces:

```python
# app/checks.py, in TextureFiles
def find(self, model: Path, file: str) -> Path | None
    # the file of that name, ignoring case, in the model's folder, or else in the first folder beside it that has it

# app/textures.py
@dataclass(frozen=True, slots=True)
class MeshTexture:
    file: Path
    content: bytes | RgbaImage  # an OZJ's JPEG, or an OZT's pixels

def mesh_texture(store: FileStore, model: Path, view: JsonObject, mesh: int) -> MeshTexture
```

  `place` keeps its three answers and is written over `find`. `mesh_texture` makes its own `TextureFiles`, so a file
  added to the folder is seen by the next request. It raises `POINTER_NOT_FOUND` (pointer `/meshes/<n>`) for a mesh
  the view does not have, `FILE_NOT_FOUND` when the name is not text or no folder has the file (the message is the
  `TEXTURE_NOT_FOUND` finding's), and lets Task 1's `CORRUPT_FILE` through.

- [ ] **Step 1: Write the failing tests** (`reference_data`, and `view_of` from `tests/models.py`):
  - `test_find_gives_the_file_with_its_own_case`: for `Item/Sword01.bmd`, `find(model, "SWORD02.ozj")` is the
    reference's `Item/sword02.OZJ`.
  - `test_find_looks_in_the_folders_beside`: for `player/Helper03.bmd`, `find(model, "unicon.OZJ")` is
    `Item/unicon.OZJ`; for `Item/waterring.bmd`, `find(model, "waterring.OZJ")` is `None`.
  - `test_a_jpeg_texture`: `mesh_texture` of `Item/Sword01.bmd`, mesh 0: `file` is `Item/sword02.OZJ` and `content`
    is 6,292 bytes.
  - `test_a_tga_texture`: of `player/Helper03.bmd`, mesh 1 (`unicon01.tga`, which only `Item/` has): `file` is
    `Item/unicon01.OZT` and `content` is an `RgbaImage` of 32 × 32.
  - `test_a_texture_no_folder_has`: `Item/waterring.bmd`, mesh 1 (`waterring.JPG`), raises `FILE_NOT_FOUND`, and the
    message has `waterring.OZJ`.
  - `test_a_mesh_the_model_does_not_have`: `Item/Sword01.bmd`, mesh 1, raises `POINTER_NOT_FOUND`.
  - `test_a_texture_the_client_refuses`: in a copy of `Item/` (Sword01 and its texture) whose `sword02.OZJ` is
    replaced by `object35/ma_001.OZJ`'s bytes, it raises `CORRUPT_FILE`.
- [ ] **Step 2:** Run them. Expected: FAIL. **Step 3:** Implement. **Step 4:** `pytest tests/app -q`. Expected: PASS,
  with stage 4's tests of `place` unchanged.
- [ ] **Step 5: Commit** `feat(tools): find and read the texture of a model's mesh`

### Task 3: The scene and the poses

**Files:**
- Create: `src/mubmd/app/model_scene.py`, `tests/app/test_model_scene.py`
- Modify: `src/mubmd/app/model_parts.py`, `tests/app/test_model_parts.py`

**Interfaces:**
- Produces:

```python
# app/model_parts.py
def tree_parents(bones: Sequence[JsonValue]) -> list[int]
    # per bone, the bone the tree hangs it from: its own parent when that parent is in the list and reaches a
    # root, else NO_PARENT (a dummy too). `_bone_parts` uses it; its tree does not change.

# app/model_scene.py
type Quaternion = tuple[float, float, float, float]  # x, y, z, w

@dataclass(frozen=True, slots=True)
class SceneBone:
    name: str
    parent: int

@dataclass(frozen=True, slots=True)
class SceneAction:
    keys: int
    lock: bool

@dataclass(frozen=True, slots=True)
class SceneMesh:
    positions: tuple[float, ...]
    uvs: tuple[float, ...]
    bones: tuple[int, ...]
    skipped: int

@dataclass(frozen=True, slots=True)
class Scene:
    bones: tuple[SceneBone, ...]
    actions: tuple[SceneAction, ...]
    meshes: tuple[SceneMesh, ...]

@dataclass(frozen=True, slots=True)
class BonePose:
    positions: tuple[float, ...]
    quaternions: tuple[float, ...]

@dataclass(frozen=True, slots=True)
class ActionPoses:
    keys: int
    bones: tuple[BonePose | None, ...]

def angle_quaternion(x: float, y: float, z: float) -> Quaternion
def model_scene(view: JsonObject, skeleton: JsonObject | None = None) -> Scene
def action_poses(view: JsonObject, action: int) -> ActionPoses
```

  - `angle_quaternion` is Half-Life's, from radians, with the half angles: `x = sr·cp·cy − cr·sp·sy`, `y = cr·sp·cy +
    sr·cp·sy`, `z = cr·cp·sy − sr·sp·cy`, `w = cr·cp·cy + sr·sp·sy`, where r, p and y are the halves of x, y and z.
  - `model_scene`: the bones and actions are `skeleton`'s when given, else the view's; the meshes are always the
    view's. A bone's parent is `tree_parents`'. A triangle gives its corners 0, 1, 2, and also 0, 2, 3 when its
    `polygon` is 4 or more; with a `polygon` under 3 it is skipped. A triangle is also skipped when a corner's vertex
    or UV index is outside its list, or when a corner's vertex names a bone that the scene's bones do not have or that
    is a dummy there.
  - `action_poses` raises `POINTER_NOT_FOUND` (pointer `/actions/<n>`) for an action the view does not have.

- [ ] **Step 1: Write the failing tests**:
  - `test_the_angles_turn_in_half_lifes_order`, parametrized: `(0, 0, 0)` gives `(0, 0, 0, 1)`; `(π/2, 0, 0)` gives
    `(√½, 0, 0, √½)`; `(0, π/2, 0)` gives `(0, √½, 0, √½)`; `(0, 0, π/2)` gives `(0, 0, √½, √½)`; `(π/2, 0, π/2)`
    gives `(0.5, 0.5, 0.5, 0.5)`, which the opposite order would give as `(0.5, -0.5, 0.5, 0.5)`.
  - `test_a_scene_has_a_corner_per_triangle_corner`: `Item/Sword01.bmd` has one bone (`Box01`, parent -1), one action
    of 1 key, one mesh with 204 bones, 612 positions, 408 uvs and `skipped` 0; its first corner is at `(-1.417384,
    -1.0287832, -10.2511)` with the UV `(0.9868, 0.4574)` on bone 0.
  - `test_a_scene_of_a_monster`: `Monster/Monster01.bmd` has 45 bones, 14 meshes whose first four have 78, 18, 114
    and 210 corners, actions of 20, 30, 7, 9, 9, 13 and 9 keys, bone 3 named `Bip01 L Thigh` under bone 2, and bones
    9, 16, 21, 32 and 41 with the name `""` and the parent -1.
  - `test_a_bone_outside_the_skeleton_skips_its_triangles` (Review Focus 4): `object35/Object36.bmd` has mesh 0 with
    336 corners and `skipped` 0, and mesh 1 with no corners and `skipped` 10.
  - `test_a_quad_gives_two_triangles` and `test_an_index_outside_its_list_skips_the_triangle`: Sword01's view with its
    first triangle's `polygon` put to 4 and the fourth index of its `vertices` and of its `uvs` put to 3 gives 207
    corners (in the file those fourth indices are -12851, what the exporter left); with only its first `uvs` index put
    to 9999, 201 corners and `skipped` 1.
  - `test_a_vertex_on_a_dummy_bone_is_skipped`: a view of two bones, the second a dummy, and one triangle on it.
  - `test_a_parent_that_loops_hangs_from_the_root` (Review Focus 4): Monster01's view with bone 2's parent put to 3
    gives bones 2 and 3 the parent -1 in the scene, and `tree_parents` says the same.
  - `test_a_model_with_nothing_is_an_empty_scene`: `Object3/Object25.bmd` gives no bones, no actions and no meshes.
  - `test_another_skeleton_gives_its_bones_and_actions`: `model_scene(HelmMale01's view, Player's view)` has 60 bones,
    205 actions, HelmMale01's 2 meshes (186 and 18 corners), and every corner on bone 20.
  - `test_the_poses_of_an_action`: `action_poses(Sword01, 0)` has 1 key and one bone with the positions `(0.049219,
    0.0, -0.078563)` and the quaternion `(0.649683, 0.279126, -0.279126, 0.649684)`; `action_poses(Monster01, 1)` has
    30 keys, 90 positions and 120 quaternion numbers in bone 0, the first position `(-0.225072, 3.207344,
    101.761055)` and the first quaternion `(0, 0, -0.707106, 0.707107)`, and `None` for bone 9.
  - `test_an_action_the_model_does_not_have`: `action_poses(Sword01, 1)` raises `POINTER_NOT_FOUND`.
  - `tests/app/test_model_parts.py`: `test_tree_parents_of_a_real_model`: Monster01's are its bones' own parents,
    with -1 for its five dummies.
- [ ] **Step 2:** Run them. Expected: FAIL. **Step 3:** Implement. **Step 4:** `pytest tests/app -q`. Expected: PASS,
  with stage 4's tests of the bone tree unchanged.
- [ ] **Step 5: Commit** `feat(tools): turn a model's view into a scene and its poses`

### Task 4: The box at rest and the player's skeleton

**Files:**
- Modify: `src/mubmd/app/model_scene.py`, `tests/app/test_model_scene.py`

**Interfaces:**
- Consumes: Task 3's functions; `FileStore.files_in`.
- Produces:

```python
DONOR_NAME: Final = "player.bmd"

@dataclass(frozen=True, slots=True)
class Bounds:
    low: tuple[float, float, float]
    high: tuple[float, float, float]

def rest_bounds(view: JsonObject, skeleton: JsonObject | None = None) -> Bounds | None
def borrows_skeleton(view: JsonObject, donor: JsonObject) -> bool
def donor_of(store: FileStore, model: Path) -> Path | None
```

  - `rest_bounds` poses the bones of `skeleton or view` with the first key of the first action: a bone's place is its
    parent's place and turn applied to its own position, and its turn is its parent's times its own. A bone with no
    pose stays at the origin, unturned. The box holds every drawn corner; with no drawn corner, the origin of every
    bone that is not a dummy; with neither, the answer is `None`.
  - `borrows_skeleton` is decision 12's rule, and is false for a model with no drawn corner.
  - `donor_of` gives the file named `DONOR_NAME`, ignoring case, in the model's folder, when it is not the model
    itself. It does not open it.

- [ ] **Step 1: Write the failing tests** (floats within 0.01):
  - `test_the_box_of_a_model_at_rest`, parametrized: Sword01 `(-1.673, -64.725, -9.254)` to `(1.771, 10.318, 8.245)`;
    Monster01 `(-79.39, -112.313, -1.25)` to `(84.139, 51.869, 214.797)`; `NPC/BloodCastle01.bmd`, whose bones 43 and
    44 come before their parent, `(-211.746, -128.875, -12.23)` to `(64.793, 276.016, 139.654)`.
  - `test_the_box_leaves_out_what_is_not_drawn`: Object36 is `(-161.949, -145.952, -18.27)` to `(193.877, 196.27,
    186.079)`.
  - `test_a_model_with_no_meshes_is_boxed_by_its_bones`: `player/Player.bmd` is `(-20.701, -23.303, -6.908)` to
    `(23.044, 24.656, 168.342)`.
  - `test_a_model_with_nothing_has_no_box`: `Object3/Object25.bmd` gives `None`.
  - `test_two_bones_in_a_chain`: a view built in the test, bone 0 at `(0, 0, 10)` turned `(0, 0, π/2)`, bone 1 under
    it at `(5, 0, 0)` unturned, one triangle on bone 1 with the corners `(1, 0, 0)`, `(0, 1, 0)` and `(0, 0, 1)`: the
    box is `(-1, 5, 10)` to `(0, 6, 11)`.
  - `test_a_part_on_the_players_skeleton`: `rest_bounds(HelmMale01, Player)` is `(-13.141, -16.272, 160.149)` to
    `(25.936, 34.578, 192.624)`, and on its own skeleton `(-16.199, -19.041, 150.363)` to `(13.603, 30.907, 182.822)`.
  - `test_the_parts_that_borrow_the_players_skeleton`: over the reference's `player/` folder, `borrows_skeleton` is
    true for 219 models and false for `Helper01` to `Helper04`, `Wing02` to `Wing04` and `playerTest.bmd`.
  - `test_the_donor_is_the_folders_player`: `donor_of` of `player/HelmMale01.bmd` is `player/Player.bmd`; of
    `player/Player.bmd` and of `Item/Sword01.bmd`, `None`.
- [ ] **Step 2:** Run them. Expected: FAIL. **Step 3:** Implement. **Step 4:** `pytest tests/app -q`. Expected: PASS.
- [ ] **Step 5: Commit** `feat(tools): box a model at rest and find the parts that borrow the player's skeleton`

### Task 5: The web extra, the layers and the page's requests

**Files:**
- Create: `src/mubmd/adapters/web/__init__.py`, `models.py`, `api.py`; `tests/adapters/web/__init__.py`,
  `conftest.py`, `test_api.py`
- Modify: `pyproject.toml`, `tests/conftest.py`, `tests/test_layers.py`

**Interfaces:**
- Consumes: Tasks 2 to 4; `EditSession.open`, `is_model`, `format_name_of`, `FileStore.list_bmd`, `FileStore.sha256`.
- Produces:

```python
# adapters/web/models.py
KEPT_SESSIONS: Final = 4

class Models:
    def __init__(self, store: FileStore, root: Path) -> None
    root: Path
    def listed(self) -> tuple[str, ...]          # the folder's models, relative, with "/", in the store's order
    def path(self, file: str) -> Path            # FILE_NOT_FOUND when empty, outside the folder, or not a file
    def session(self, file: str) -> EditSession  # UNKNOWN_FORMAT when the file is not a model
    def donor(self, file: str) -> str | None     # the relative path of the donor when the model borrows from it

# adapters/web/api.py
def add_api(app: FastAPI, models: Models) -> None  # "The scene" above
```

  `session` gives the same `EditSession` while the file's SHA-256 is the one it was opened with, opens it again when
  it is not, and keeps the `KEPT_SESSIONS` used last. `path` resolves the file and refuses one that is not under the
  resolved root.

  `pyproject.toml`: `web = ["nicegui==4.1.0"]`; `[dev]` gains `"mubmd[web]"` and `"playwright==1.65.2"`; pytest gains
  the marker `browser`, `addopts = "-m 'not browser'"` and `main_file = ""`. `tests/conftest.py` registers
  `nicegui.testing.user_plugin`. `tests/adapters/web/conftest.py` has `web_copy` (the `model_copy` folder with the
  real `Item/sword02.OZJ`, `Item/unicon.OZJ`, `Item/unicon01.OZT` and `player/Player.bmd` copied over) and `viewer`
  (NiceGUI's `user` after `add_api(app, Models(LocalFileStore(), web_copy))`; Task 6 makes it call `build`).

- [ ] **Step 1: Write the failing tests**:
  - `tests/test_layers.py`: the web libraries join the ones `app/` may not import;
    `test_only_the_web_adapter_imports_the_web_libraries` (no module of `adapters/` outside `adapters/web` imports
    `nicegui`, `fastapi`, `starlette` or `uvicorn`); `test_the_web_adapter_does_not_import_the_other_adapters` (nothing
    in `adapters/web` imports `mubmd.adapters.cli`, `mubmd.adapters.tui`, `typer` or `textual`).
  - `tests/adapters/web/test_api.py`, through `viewer.http_client`:
    - `test_the_scene_of_a_model`: `/api/scene?file=Item/Sword01.bmd` answers 200 with `Cache-Control: no-store`,
      `file` and `skeleton` `Item/Sword01.bmd`, `donor` null, one bone, `actions` `[{"keys": 1, "lock": false}]`, a
      mesh of 204 `bones`, and `bounds` as in Task 4.
    - `test_the_scene_on_the_donor`: `/api/scene?file=player/HelmMale01.bmd` has `donor` `player/Player.bmd` and 56
      bones; with `&on=donor`, `skeleton` is `player/Player.bmd`, with 60 bones and 205 actions.
      `/api/scene?file=player/Helper03.bmd&on=donor` answers 400.
    - `test_the_poses_of_an_action`: `/api/poses?file=Item/Sword01.bmd&action=0` has `keys` 1 and one bone with 3
      positions and 4 quaternion numbers; `action=1` answers 404 with the code `POINTER_NOT_FOUND`.
    - `test_a_jpeg_texture`: `/api/texture?file=Item/Sword01.bmd&mesh=0` is `image/jpeg`, 6,292 bytes.
    - `test_a_tga_texture`: `/api/texture?file=player/Helper03.bmd&mesh=1` is `application/octet-stream`, 4,096
      bytes, `X-Width` 32, `X-Height` 32.
    - `test_a_texture_that_is_not_there`: `Item/waterring.bmd`, mesh 1, answers 404 with the code `FILE_NOT_FOUND`.
    - `test_a_file_outside_the_folder_is_not_found` (Review Focus 2), parametrized over `../outside.bmd`, the
      absolute path of the reference's `Item/Sword01.bmd`, `Item/../../outside.bmd` and `""`, with a model copied
      beside `web_copy` as `outside.bmd`: 404 with the code `FILE_NOT_FOUND` on the three routes.
    - `test_a_table_is_not_a_scene` (Review Focus 2): `/api/scene?file=Local/Text.bmd` answers 404 with the code
      `UNKNOWN_FORMAT`.
    - `test_a_changed_file_is_read_again` (Review Focus 1): after the scene of `Item/Sword01.bmd` is asked for, the
      file is overwritten with `player/HelmMale01.bmd`'s bytes, and the next scene has 56 bones.
    - `test_the_models_of_the_folder`: `Models.listed()` has the 12 of `MODEL_FILES` plus `player/Player.bmd` and
      none of `TABLE_FILES`.
- [ ] **Step 2:** `pip install -e ".[dev]"`, then run them. Expected: FAIL. **Step 3:** Implement. **Step 4:** `pytest
  -q`, `mypy src tests`. Expected: PASS; `mubmd --version` still imports nothing of the web (the layers test).
- [ ] **Step 5: Commit** `feat(tools): serve a model's scene, poses and textures to the page`

### Task 6: The page and the 3D object

**Files:**
- Create: `src/mubmd/adapters/palette.py`, `src/mubmd/adapters/web/scene.py`, `bmd_model.js`, `page.py`, `server.py`
  (only `build`); `tests/adapters/web/test_page.py`
- Modify: `src/mubmd/adapters/tui/theme.py`, `tests/adapters/web/conftest.py`

**Interfaces:**
- Consumes: `Models` and the routes (Task 5); `model_parts`, `part_of`, `part_title`, `part_facts`, `part_fields`,
  `counted`; `EditSession.findings`; `rest_bounds`.
- Produces:

```python
# adapters/palette.py
@dataclass(frozen=True, slots=True)
class Palette:  # the fields of today's two Textual themes: primary, secondary, accent, warning, error, success,
    ...         # foreground, background, surface, panel (hex strings) and dark (bool)
LORENCIA: Final[Palette]
LORENCIA_LIGHT: Final[Palette]

# adapters/web/scene.py
class BmdModel(Object3D, component="bmd_model.js"):
    def __init__(self, file: str, on: str) -> None
    def play(self, action: int) -> None
    def pause(self) -> AwaitableResponse  # the key it stopped at
    def resume(self) -> None
    def seek(self, key: float) -> None
    def speed(self, keys_per_second: float) -> None
    def skeleton(self, shown: bool) -> None
    def additive(self, on: bool) -> None
    def highlight(self, mesh: int | None) -> None
    def mark_bone(self, bone: int | None) -> None

# adapters/web/page.py
DEFAULT_SPEED: Final = 8
SPEEDS: Final = (2, 4, 8, 16)
def add_page(models: Models) -> None  # the page "/" of "The page" above

# adapters/web/server.py
def build(store: FileStore, root: Path) -> None  # add_api and add_page on NiceGUI's app
```

  `bmd_model.js` exports the class NiceGUI asks for, with the methods above and:
  - `async create_mesh(file, on)`: asks for the scene; makes one `THREE.Bone` per bone, each under its parent or under
    the group, and one `THREE.Skeleton` over them with identity inverses; per mesh, a `BufferGeometry` with
    `position`, `uv`, `skinIndex` (`bone, 0, 0, 0`) and `skinWeight` (`1, 0, 0, 0`), a `MeshBasicMaterial` (both
    sides, `alphaTest` 0.25) and a `SkinnedMesh` bound with an identity matrix and never culled; its texture, from
    `/api/texture`, as an image when the answer is `image/jpeg` and as a `DataTexture` (RGBA, `X-Width`, `X-Height`)
    when it is `application/octet-stream`, both with `flipY` false, sRGB and repeat wrapping; on any other answer the
    mesh keeps a flat color. It then asks for action 0's poses and poses the model at key 0. A scene with no drawn
    corner shows its skeleton.
  - An action's clip has a position track and a quaternion track per posed bone, with key k at time k and one more
    time, `keys`, that repeats key 0; with `lock`, that last time is left out. The mixer's time scale is the speed.
    Poses are asked for once per action and kept.
  - `window.mubmdViewer`, for the browser tests and for looking from the console: `ready`, `file`, `skeleton`,
    `bones`, `meshes`, `corners`, `textures` (`shown`, `flat`), `action`, `keys`, `key`, `playing`, `bounds` (`low`,
    `high`: the box three.js computes for the skinned meshes at action 0, key 0, or `null`).

- [ ] **Step 1: Write the failing tests** (`viewer` now calls `build`; a tree's nodes are read from its props, and a
  choice is `viewer.find(...).trigger("update:selected", <id>)`):
  - `test_the_folder_lists_its_models`: on `/`, the files tree has a node `Item` whose children are `Sword01.bmd`
    and `waterring.bmd` with the ids `Item/Sword01.bmd` and `Item/waterring.bmd`; no node is `Local`; the page says to
    pick a model.
  - `test_a_model_opens_with_its_parts`: on `/?file=Item/Sword01.bmd`, the parts tree's three nodes have the labels
    of `model_parts(view).children`, and the page shows `version` and the model's facts.
  - `test_choosing_a_mesh_shows_its_facts`: after choosing `/meshes/0`, the page shows `mesh 0`, `triangles`, `68`
    and `sword02.OZJ`.
  - `test_choosing_an_action_names_it`: on `NPC/BloodCastle01.bmd`, after choosing `/actions/0` the toolbar shows
    `action 0 · 12 keys`.
  - `test_a_model_lists_its_findings` and `test_a_mesh_says_what_is_not_drawn` (Review Focus 3 and 4): on
    `object35/Object36.bmd` the page shows `OUT_OF_RANGE`, and after choosing `/meshes/1`, `10 triangles not drawn`.
  - `test_a_mesh_says_why_its_texture_is_not_shown` (Review Focus 3): on `Item/waterring.bmd`, after choosing
    `/meshes/1`, the page shows `waterring.OZJ` and `not shown`.
  - `test_a_part_of_the_player_offers_its_skeleton`: `/?file=player/HelmMale01.bmd` shows `On Player.bmd's skeleton`;
    `/?file=Item/Sword01.bmd` does not.
  - `test_a_file_that_fails_shows_its_message`: `/?file=../outside.bmd` shows the `FILE_NOT_FOUND` message and no
    parts tree.
  - `test_two_pages_keep_their_own_model` (Review Focus 5): with a second user from `create_user`, one page on
    Sword01 and the other on Object36: after choosing `/meshes/1` on the second, the first still shows Sword01's name
    and no `not drawn`.
  - Stage 4's snapshot tests of the console UI pass unchanged over the shared palette.
- [ ] **Step 2:** Run them. Expected: FAIL. **Step 3:** Implement the palette, the object, the page and `build`.
  **Step 4:** `pytest -q`, `mypy src tests`, `ruff check .`. Expected: PASS.
- [ ] **Step 5: Commit** `feat(tools): show a model, its parts and its findings on a page`

### Task 7: `mubmd web`

**Files:**
- Modify: `src/mubmd/adapters/web/server.py`, `src/mubmd/adapters/cli/commands.py`
- Create: `tests/adapters/web/test_server.py`, `tests/adapters/cli/test_web.py`

**Interfaces:**
- Consumes: `build` (Task 6).
- Produces:

```python
# adapters/web/server.py
HOST: Final = "127.0.0.1"
ALLOWED_HOSTS: Final = ("127.0.0.1", "localhost")
DEFAULT_PORT: Final = 8765
def free_port(port: int) -> int  # the port when it is free on HOST, a free one for 0; IO_ERROR when it is taken
def guard() -> None              # TrustedHostMiddleware with ALLOWED_HOSTS on NiceGUI's app
def serve(store: FileStore, target: Path, *, port: int, open_browser: bool) -> str  # the address it served

# adapters/cli/commands.py
@dataclass(frozen=True, slots=True)
class WebData:
    path: Path
    url: str

def load_web() -> Callable[..., str]  # imports adapters/web and gives `serve`; the one place the CLI imports it
```

  `mubmd web [PATH] [--port N] [--no-browser]`: `PATH` is a Data folder or a model, by default the current folder;
  `--port` goes from 0 to 65535 and defaults to `DEFAULT_PORT`. A path that does not exist is `FILE_NOT_FOUND`, before
  anything of the web is imported. `serve` on a model serves its folder and opens the page on that model. It calls
  `free_port`, `build` and `guard`, prints decision 4's line on stderr when the server is up, runs NiceGUI with
  `reload=False`, `show_welcome_message=False`, the title `mubmd` and the dark theme, and returns when the server
  stops. The command's help says it is for people and runs until Ctrl+C; `EXAMPLES` and `OUTPUT_TYPES` get `web`
  (`mubmd web ~/mu/Data`, `WebData`).

- [ ] **Step 1: Write the failing tests**:
  - `tests/adapters/web/test_server.py`:
    - `test_a_taken_port_is_an_error_with_a_way_out` (Review Focus 5): with a socket bound and listening on
      127.0.0.1, `free_port` of its port raises `IO_ERROR` with the suggestion `mubmd web --port 0`.
    - `test_port_zero_takes_a_free_one`: `free_port(0)` is over 0 and can be bound.
    - `test_another_host_is_refused` (Review Focus 5): after `build` and `guard`, `/api/scene?file=Item/Sword01.bmd`
      with the header `Host: evil.example` answers 400, and with `Host: 127.0.0.1:8765` and `Host: localhost:8765`,
      200.
  - `tests/adapters/cli/test_web.py`:
    - `test_web_without_the_extra_says_how_to_install_it` (Review Focus 5): with `load_web` patched to raise
      `ModuleNotFoundError(name="nicegui")`, `mubmd web <working copy>` exits 2 with the code `USAGE` and the
      suggestion `pip install 'mubmd[web]'`.
    - `test_web_on_a_path_that_does_not_exist`: exits 20 with `FILE_NOT_FOUND`, and `load_web` is not called.
    - `test_web_prints_its_envelope_when_the_server_stops`: with `load_web` patched to give a function that returns
      `http://127.0.0.1:8765`, stdout is the envelope with `command` `web` and `data` `{"path": ..., "url":
      "http://127.0.0.1:8765"}`, and it was called with the port 8765 and `open_browser` true; with `--port 0
      --no-browser`, with 0 and false.
    - Stage 2's `test_every_command_is_fully_described` covers `web`'s entry.
- [ ] **Step 2:** Run them. Expected: FAIL. **Step 3:** Implement. **Step 4:** `pytest -q`, `mypy src tests`.
  Expected: PASS.
- [ ] **Step 5:** Run `mubmd web ../../assets/Data --no-browser` and open the address: Monster01 plays, and Ctrl+C
  prints the envelope. **Commit** `feat(tools): add mubmd web`

### Task 8: The browser tests and their job

**Files:**
- Create: `tests/adapters/web/test_browser.py`
- Modify: `tests/adapters/web/conftest.py`, `.github/workflows/bmd-tool.yml`

**Interfaces:**
- Consumes: the `mubmd` console script; `window.mubmdViewer` (Task 6).
- Produces: the fixtures `served` and `page`. `served` (session scope) starts `mubmd web <folder> --port 0
  --no-browser` as a process on a folder with `Item/Sword01.bmd`, `Item/waterring.bmd`, `Monster/Monster01.bmd`,
  `object35/Object36.bmd`, `Object3/Object25.bmd`, `player/HelmMale01.bmd`, `player/Helper03.bmd`,
  `player/Player.bmd` and their real texture files, copied from the reference; reads the address from stderr; and at
  the end sends it SIGINT. `page` is a Playwright page of a headless Chromium launched with
  `--enable-unsafe-swiftshader` and, when the variable `MUBMD_CHROMIUM` is set, with that executable (in the cloud
  container, `/opt/pw-browsers/chromium`); it collects the page's console errors, its page errors and its answers of
  400 or more. Every test in the module is marked `browser`.

- [ ] **Step 1: Write the tests** (each waits for `window.mubmdViewer.ready`):
  - `test_a_model_is_drawn_with_its_textures`: on Monster01, `meshes` is 14, `bones` 45, `textures` is 14 shown and
    0 flat, and nothing was collected.
  - `test_the_browser_poses_the_model_where_python_does`: Monster01's `bounds` are within 0.01 of Task 4's box. This
    ties three.js's skinning to `rest_bounds`.
  - `test_a_tga_texture_is_drawn`: on Helper03, no texture is flat.
  - `test_a_missing_texture_leaves_the_mesh_flat` (Review Focus 3): on `Item/waterring.bmd`, `textures` is 1 shown
    and 1 flat, the only answer collected is the 404 of mesh 1's texture, and there is no page error.
  - `test_an_action_plays_and_pauses`: on Monster01, after clicking action 1 in the parts tree, `action` is 1,
    `keys` 30 and `playing` true; after 500 ms `key` has grown; after clicking pause it stops growing and the slider
    shows it.
  - `test_a_part_moves_with_the_players_actions`: on HelmMale01, after turning on the donor switch, `skeleton` is
    `player/Player.bmd`, `bones` 60, and the `bounds` are Task 4's for the part on the player's skeleton.
  - `test_odd_models_load_without_errors` (Review Focus 4), parametrized: Object36 (`corners` 336), Player (`meshes`
    0, `bones` 60, the skeleton shown) and Object25 (`ready`, nothing drawn): nothing collected.
  - `test_the_server_stops_with_its_envelope`: a second `mubmd web` process, after SIGINT, exits 0 and its stdout is
    one line, the envelope of `web` with its address.
- [ ] **Step 2:** Run `MUBMD_CHROMIUM=/opt/pw-browsers/chromium pytest -m browser -q`. Expected: PASS; a failure here
  is a defect of Task 6 or 7, fixed there with its own test where Python can reach it.
- [ ] **Step 3:** Add to `bmd-tool.yml` the job `browser`, named `mubmd web (Chromium)`: the same checkout, Python
  and `pip install -e ".[dev]"` as `check`, then `python -m playwright install --with-deps chromium`, then `pytest -m
  browser -v`. `check` stays as it is.
- [ ] **Step 4: Commit** `test(tools): drive the web viewer in a browser`

### Task 9: The README, the spec and the finish

**Files:**
- Modify: `README.md`, `docs/superpowers/specs/2026-11-25-bmd-editor-design.md`

- [ ] **Step 1: `README.md`:** "For people" names both the console UI and the web viewer; "Install" gains `pip install
  -e "Client/tools/bmd[web]"` for the viewer; the commands table gains `mubmd web [<folder or model>] [--port N]
  [--no-browser]`; a section "The web viewer" after "The console UI" says what it shows, that it writes nothing and
  listens on 127.0.0.1 only, and how to stop it; "Development" names `adapters/web`, the `browser` marker and
  `MUBMD_CHROMIUM`; "Stages" says stages 1 to 5 are done and names stage 5 as the web viewer.
- [ ] **Step 2: The spec:** in "Where it lives", `adapters/web/` (NiceGUI and three.js) joins the tree. In "Layers",
  the adapters are the file system, the CLI, the console UI and the web viewer, and only `adapters/web` imports the
  web libraries. In "The console UI", "The surprise" is replaced by one line that points to a new section, "The web
  viewer", written from this plan's "The page", "The scene" and decisions 2, 5, 8 and 12. "Tests and CI" gains
  NiceGUI's test user, the browser tests and the second job. Stage 5 becomes "**The web viewer of models**", with a
  link to this plan, and "Left to the builder" loses the braille renderer.
- [ ] **Step 3: The whole check**, from `Client/tools/bmd`: `ruff check . && ruff format --check . && mypy src tests
  && pytest`, then `MUBMD_CHROMIUM=/opt/pw-browsers/chromium pytest -m browser`, then `mubmd roundtrip
  ../../assets/Data` (1,991 identical, 3 skipped) and `mubmd validate ../../assets/Data` (the eleven findings).
- [ ] **Step 4:** `git diff --name-only origin/main...HEAD` against `ci.yml`'s `case` list: every path is under
  `Client/tools/`, `docs/`, is a `*.md`, or is `.github/workflows/bmd-tool.yml`.
- [ ] **Step 5: Commit** `docs(tools): describe the web viewer`, push, and open the PR `feat(tools): view models in
  the browser with mubmd web` against `main`, with screenshots of the page taken with Playwright and the list of this
  plan's decisions. Emanuel merges.

## Left to the builder

- The page's exact layout, sizes and wording beyond the texts the tests name, and the camera's distance and angle,
  as long as the model's box is in view when it opens.
- How `bmd_model.js` is split into functions, and the flat color of a mesh without texture.
- How the files tree filters.

## Later stages, not in this plan

- **Editing from the browser** (stage 6): the fields the console UI edits, then moving bones and vertices. It needs
  the token of decision 2 and, for a gizmo, three.js's `TransformControls`, which NiceGUI's bundle does not carry.
- **The bridge to Blender** (stage 7): export and import through SMD, the format the models were made from.
- A native window (`pywebview`), the light palette, picking a part by clicking on the model.
