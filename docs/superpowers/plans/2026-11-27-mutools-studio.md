# mutools, stage 6: the studio Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `mutools web` becomes a studio that looks good and costs nothing while nothing moves: a library of models
with thumbnails, the model in a vitrine, its actions laid out below it, and an inspector of parts beside it. It still
views and does not edit: editing is stage 7.

**The look changed twice on 2026-11-27.** Emanuel found the first mockup (night blue and MU gold) generic, "slop like
the styles seen everywhere", and asked for a design skill: the second direction, "Vitrine", was made with Anthropic's
`frontend-design` skill (Design canvas artboard `Vitrine.dc.html`). It had a light page; he then asked for it dark ("no
me gusta en blanco, lo prefiero en negro o en dark mode"). Decision 8 is the result.

**Why now:** Emanuel, 2026-11-27: "rename it mutools, improve its design, add editing models, new ones, animating
them, surprise me"; "the site looks dull"; and on WebGL: "light, but without losing render quality: a balance that
looks good without the browser's resources going to every badly managed polygon".

**What was measured before this stage** (headless Chromium, SwiftShader, 1500x900, `Monster/Monster01.bmd`): the
stage 5 viewer draws the same while paused as while playing, because NiceGUI's `ui.scene` renders every frame (its
loop is `requestAnimationFrame` plus `setTimeout(1000 / fps)`, and it also runs its CSS2D and CSS3D renderers each
frame) and the model has a second loop of its own: about 830 draw calls a second and the main thread 45% busy, paused.
`player/Player.bmd`: about 110 draw calls a second and 35%.

**Architecture:** The page stays NiceGUI and the model stays three.js r187 from NiceGUI's own bundle (`nicegui-scene`
in the import map: no JavaScript package and no third-party file enters the repo). `ui.scene` goes away: a Vue
component of ours, `Viewport`, owns the renderer, the camera, the orbit controls and the frame loop, and builds the
model with the code `bmd_model.js` has today. The browser renders thumbnails with one shared renderer and keeps them in
IndexedDB. Two small use cases join `app/`: the client's speed of each action, and the listing of the library.

**Tech Stack:** stage 5's. No new Python or JavaScript dependency. Fonts: Alegreya and Alegreya Sans from Google Fonts
with system fallbacks, so the page works offline in system fonts.

## Global Constraints

- The web writes nothing; `Client/assets/Data` is never written; `mutools roundtrip Client/assets/Data` still gives
  1,991 identical and 3 skipped.
- `core/` imports only the standard library; only `adapters/web` imports NiceGUI or its stack (`tests/test_layers.py`).
- The CLI contract of schema 1 holds: no new command, error code, exit code or finding.
- The PR touches only `Client/tools/`, `docs/`, `.claude/`, `*.md` and `.github/workflows/bmd-tool.yml`.
- English, comments only when strictly necessary, one behavior per test, Angular commits with scope `tools`.

## Decisions taken in this plan

1. **Render on demand.** A frame is drawn only when something changed: the camera moved, a key changed, a switch
   flipped, the canvas was resized, a texture arrived. While an action plays the loop runs, capped at 30 frames a
   second; between keys the pose is interpolated, so 30 frames look smooth for actions of 2 to 16 keys a second.
   Paused, the page draws nothing. The loop also stops while the tab is hidden or the viewport is scrolled away
   (`visibilitychange`, `IntersectionObserver`).
2. **Quality where it is cheap.** Antialiasing on; pixel ratio up to 2 at rest and 1 while the camera moves, back to
   the full ratio one frame after it stops; textures with mipmaps and anisotropic filtering (up to 8, or what the GPU
   allows); `powerPreference: "low-power"`, which the integrated GPU of a laptop serves well for models of at most
   9,414 corners.
3. **Three ways to look at a model, and two switches.** `Textured` is today's view (unlit, as the client's textures
   look); `Lit` adds a hemisphere light and one directional light, no shadows, for the volume, with the file's normals
   (sent per corner by the server); `Wire` draws the triangles' edges. `Bones` and `Additive` are switches.
4. **One renderer for every thumbnail.** Browsers allow about 16 WebGL contexts, so the library never opens one per
   card: a hidden renderer draws each visible card's model once, in the first pose of its first action, and the PNG is
   kept in IndexedDB under the file's name and the first 16 hex digits of its SHA-256 (in the card's HTML), so a
   changed file gets a new thumbnail. A browser without IndexedDB draws them again on each visit. The average color of
   the model's textures, weighted by triangles, tints its card and the vitrine's glow.
5. **The client's speed of each action.** The file does not say how fast an action plays; the client does, in keys
   per 40 ms step (`Client/src/game/Tables/PlayerActions.h`, 0x60dbe0; `Client/src/game/Assets/MonsterModelTable.h`,
   0x612e5a). The studio plays at that speed when it knows it (`Player.bmd`'s actions with a speed, every
   `Monster/MonsterNN.bmd`, the 35 NPC models the client loads at 0.25 keys a step), and the speed line says where the
   speed came from. Any other model plays at 8 keys a second and says so. Half and double speed scale it. Actions the
   client plays once (a monster's `die`, four of the player's) play once and stay on their last key.
6. **Names of actions where the client gives them.** Monster actions 0 to 6 are the seven the client sets speeds for;
   they are shown as `stop 1`, `stop 2`, `walk`, `attack 1`, `attack 2`, `shock` and `die`, the order of those
   speeds. Other models show the action's number only.
7. **The library is a grid of plates with thumbnails, one folder at a time.** The plates are built in Python (no
   `/api/library`): the folders come from an index with counts and dot leaders (the world objects' map folders apart),
   and the search looks through every model's name (60 at most). Choosing a model no longer reloads the page, and a
   model from another folder brings its folder.
8. **Look: the "Vitrine", dark.** A dark page (`#111214`, raised `#191A1D`, wells `#1F2023`, text `#E7E5DE`), one
   accent, ultramarine (`#9AA6FF` for lines and text, `#3A47B0` for filled controls), madder for findings; Alegreya for
   names, Alegreya Sans for the interface. The model sits in a vitrine whose glow takes its textures' hue; the floor is
   concentric rings; the actions are a strip whose widths are their lengths at the client's speed; the key scrubber is
   a ruler; meshes show texture swatches. No monospace, no all-caps labels, no middle dots, no stat cards. The console
   UI keeps `LORENCIA`.
9. **Command palette.** `Ctrl+K` opens a search over every model's name and the studio's commands (frame, play or
   pause, bones, additive, the views, the speeds, the findings); `Enter` runs the first. `Space` plays or pauses and
   `F` frames the model, outside text fields.
10. **The render state tells the truth.** The vitrine's corner says whether it is drawing and at how many frames a
    second; the footer gives the triangles and draws of the last frame (`renderer.info`).
11. **Two skills.** `threejs`, a MuRet layer over five skills of
    [CloudAI-X/threejs-skills](https://github.com/CloudAI-X/threejs-skills) (MIT): fundamentals, animation,
    performance, interaction and loaders, with the rules above (render on demand, the pixel ratio, one context for
    thumbnails, disposal) ranked above the upstream examples, which run a continuous loop. And `frontend-design`,
    Anthropic's skill (Apache 2.0) with what MuRet decided about its look.

## Files

- `adapters/web/elements.py`: the Vue components' Python side (`Viewport`, `Transport`, `RenderState`, `Thumbnail`,
  `Shortcuts`); `viewport.js`, `transport.js`, `render_state.js`, `thumbnail.js`, `shortcuts.js`.
- `adapters/web/studio/`: ES modules served as `mutools-studio`: `index.js`, `bus.js` (events between the
  components), `frame_loop.js`, `rig.js` (what `bmd_model.js` did, without NiceGUI's `Object3D`), `stage.js`,
  `thumbnails.js`, `tint.js`.
- `adapters/web/page.py`: the studio; `library.py`, `timeline.py`, `inspector.py`, `finder.py`, `opened.py`,
  `widgets.py` and `theme.py` beside it.
- `adapters/web/api.py`: `/api/swatch` (a mesh's texture as a picture, TGA as PNG); `adapters/web/models.py`: the
  library's entries and folders.
- `app/action_speeds.py`, `core/client_speeds.py`: the client's tables; `core/formats/texture_images.py`: `png_of`.
- `adapters/palette.py`: `STUDIO`.
- Removed: `adapters/web/scene.py`, `adapters/web/bmd_model.js`.
- `.claude/skills/threejs/` and `.claude/skills/frontend-design/`: `SKILL.md`, `LICENSE`, `references/`.
- Tests beside each, and the browser tests.

## Tasks

### Task 1: The client's action speeds

- [x] `core/client_speeds.py`: the 181 writes of `PlayerActionSpeeds()` in their order (later ones win), the seven
  common monster speeds, the monster factors, the early single-action speeds and `NpcActionSpeed` (0.25), copied from
  `PlayerActions.cpp` and `MonsterModelTable.cpp`.
- [x] `app/action_speeds.py`: `action_speeds(relative_path, actions) -> tuple[ActionSpeed, ...]` with the speed in keys
  per second (speed x 25) and its source (`player`, `monster N`, `npc`, `unknown`).
- [x] Tests: `Player.bmd` action 0, an action no write touches (unknown), Monster01 action 0 (6.25 keys a second), a
  monster with a factor (number 3: x1.2), an NPC; and one test that reads `PlayerActions.cpp` and checks every number
  of the table, so the copy cannot drift from the client.

### Task 2: The library and the scene's extras

- [x] The library's entries (folder, name, meshes, actions, stamp), kept until the file changes, and the folders with
  their counts (`Models.entry`, `Models.folders`), read by the page in Python instead of an `/api/library`.
- [x] The speeds and names stay in Python (`OpenedModel`): the page sends the viewport the speed of the action it
  plays, so `/api/scene` does not change.
- [x] `/api/swatch` for the inspector's texture swatches.

### Task 3: The viewport

- [x] `studio/frame_loop.js`: `request()`, the 30 fps cap while playing, the stops (hidden, offscreen), the adaptive
  pixel ratio, and the counters for the status bar.
- [x] `studio/rig.js`: today's model building, plus `dispose()` (geometries, materials, textures, the mixer), the four
  view modes, and anisotropic filtering.
- [x] `viewport.js` and `viewport.py`: the component; its methods `open(file, on)`, `play`, `pause`, `seek`, `speed`,
  `mode`, `additive`, `highlight`, `mark_bone`, `frame`; it emits `ready`, `key` and `stats`.
- [x] Browser test: after the model is ready and paused, no draw call in one second; while playing, at most 31 frames a
  second.

### Task 4: The studio's page

- [x] Header (brand, breadcrumb, reference or working copy, `Ctrl+K`), library, viewport with its overlays (name and
  facts, view modes, frame, additive, axes, render state), timeline (action chips with names, keys and lock; transport;
  key scrubber with ticks; speed and its source), inspector (tabs Parts, Model, Findings; meshes with texture
  swatches; the chosen part's card), footer.
- [x] Choosing a model or an action changes the URL (`?file=…&action=…`) without reloading, so a link opens the same
  view.
- [x] Page tests through the test user; the existing browser tests move to the new selectors.

### Task 5: Thumbnails

- [x] `studio/thumbnails.js`: one hidden renderer; a queue fed by an `IntersectionObserver` on the cards; IndexedDB
  keyed by file and SHA-256.
- [x] Browser test: a card in view gets an image, and only one WebGL context exists besides the viewport's.

### Task 6: The skill, the README, the spec

- [x] `.claude/skills/threejs/` as decision 11 says.
- [x] README: the studio, with a screenshot path; spec: "The web viewer" becomes "The studio", stages 6 to 12 listed.
- [x] Checks: `ruff check .`, `ruff format --check .`, `mypy src tests`, `pytest`, `pytest -m browser`, `mutools
  roundtrip ../../assets/Data`; the measurement of "What was measured" again, with its numbers in the PR.

## Later stages, not in this plan

7. Editing models in the browser: the write path with its token, undo and redo, a gizmo, textures from PNG (Pillow).
8. The Blender bridge through glTF (pygltflib): export and import.
9. Animation: posing, adding and copying actions, importing them from glTF. Emanuel asked for it again on 2026-11-27
   (own animations, emotes): the original `main.exe` can only get an existing action redone (its 22 emotes are fixed
   cases, at fixed speeds); new emotes need our client.
10. Creating models: templates, primitives, bones.
11. The character room: `Player.bmd` dressed, weapons and wings where the client attaches them, a render closer to
    the client's.
12. Maps, if Emanuel wants them.
