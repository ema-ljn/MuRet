# mutools, stage 8: editing actions in the studio Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `mutools web --edit` opens the studio on a working copy and lets a person change an action by hand: pick a
bone, turn it or move it with three.js's TransformControls at the key on the ruler, copy or remove keys, copy a whole
action as a new one, undo and redo, and save the model with the checks every other write has. This is stage B of the
action editor; stage A ([`2026-11-27-mutools-actions.md`](2026-11-27-mutools-actions.md)) moves actions as files.

**Why now:** Emanuel, 2026-11-27: new movements for emotes and other things; at 22:30Z he chose TransformControls from
three r187 over rings of our own ("si es mejor"), vendored at a pinned version with its MIT license; at 23:21Z he said
to start stage B.

**What was measured before this stage:**

- NiceGUI 4.1.0 bundles three 0.187.0 (`nicegui-scene`, the same integrity hash as npm's `three-0.187.0.tgz`), with
  OrbitControls and no TransformControls. Its bundle carries every class TransformControls imports, and it imports
  none of them by the bare name `three`, so an import map entry `three` can serve them to the vendored file without a
  second copy of three.
- NiceGUI's socket accepts any origin (`cors_allowed_origins='*'`), and an event names its page by the page's client
  id, a random id written into the page itself. A page of another site cannot read that id, but the edit mode should
  not rest on that alone.
- A pose edit of `player/Player.bmd` that shares every list it does not change diffs against the opened file in a
  tenth of a millisecond (`diff_views` skips what is the same object), and checking the view with the codec takes 0.09
  s, so every drag can be checked before it enters the history.
- The studio keeps four opened models and drops the oldest: in edit mode that would lose unsaved changes.

**Architecture:** The pose math stays in Python, as the threejs skill asks: the browser sends a bone's new local
quaternion or position, and Python turns the quaternion into the file's angles (the inverse of `angle_quaternion`,
`0x5fcd20`). The key and action edits are pure functions in `core/formats/action_keys.py`; `app/pose_edits.py` applies
them to an `EditSession`, which already has undo and redo; the page's edit panel calls them and the viewport rebuilds
the clip of the edited action. Saving is `EditSession.save`, so the reference refusal, write-verify, the stale check
and the `.orig` copy hold.

**Tech Stack:** stage 6's, plus `TransformControls.js` of three 0.187.0, byte for byte.

## Global Constraints

- `Client/assets/Data` is never written: `mutools web --edit` on it is `REFERENCE_READ_ONLY`, and `roundtrip` still
  gives 1,991 identical and 3 skipped.
- Without `--edit` the studio writes nothing and looks as it does today.
- `core/` imports only the standard library; only `adapters/web` imports NiceGUI.
- Schema 1 holds: `web` gains `--edit` and its output a boolean `edit`; no new error code, exit code or finding.
- The PR touches only `Client/tools/`, `docs/`, `.claude/` and `*.md`.
- English, comments only when strictly necessary, one behavior per test, Angular commits with scope `tools`.

## Decisions taken in this plan

1. **The vendored file.** `adapters/web/studio/vendor/three/TransformControls.js` is three 0.187.0's
   `examples/jsm/controls/TransformControls.js`, byte for byte, with three's `LICENSE` beside it. A test pins its
   SHA-256. Its `import ... from 'three'` reaches the bundled three through a two-line module of ours registered as
   `three` in the import map, which re-exports those names from `nicegui-scene`; another test checks that the module
   exports every name the vendored file imports.
2. **Only a working copy is edited.** `--edit` on the reference is `REFERENCE_READ_ONLY`, as every other write there,
   with `workspace init` as the suggestion.
   In edit mode the header says "Working copy, editing".
3. **The page's own connection carries the edits.** No new route writes. The server refuses (403) every request whose
   `Origin` is not its own address, so a page of another site cannot open the socket or drive the page.
4. **What is edited.** The model's own actions only: on another model's skeleton (a helm on `Player.bmd`) the panel
   says to open that file. A bone is picked in the inspector's list of bones; dummy bones have no pose. The gizmo turns
   the bone in its own space (`Turn`) or moves it (`Move`); dragging pauses the action on the key under the playhead.
5. **The math.** The browser sends the bone's local quaternion after a turn, or its local position after a move, never
   both: what was not dragged keeps its floats. Python writes the angles of the quaternion (Rz Ry Rx, as
   `angle_quaternion`), rounded to float32 as the file stores them. A turn that comes back the same changes nothing.
6. **Keys.** `Copy key` puts a copy of the key after it (positions, rotations and, with a lock, the lock position);
   `Remove key` takes it out and is refused on an action's last key. `Copy action` adds a copy of the action after the
   last one, through stage A's `with_action`, and plays it.
7. **Undo.** Every edit is one step of the session's history; `Ctrl+Z` undoes, `Ctrl+Shift+Z` and `Ctrl+Y` redo,
   `Ctrl+S` saves. The session lives on the server, so a reload keeps unsaved changes, and in edit mode a model with
   changes is never dropped from the four kept open.
8. **A locked action** shows its first bone at the x and y of key 0, as the client does; the panel says so when that
   bone is picked, because a move of it at another key shows only in z.

## Files

- Create `src/mutools/core/formats/action_keys.py`: `with_pose`, `with_key_copied`, `without_key`.
- Modify `src/mutools/app/model_scene.py`: `quaternion_angles`.
- Create `src/mutools/app/pose_edits.py`: `turn_bone`, `move_bone`, `copy_key`, `remove_key`, `copy_action`.
- Modify `src/mutools/app/session.py`: `EditSession.change(view)`.
- Modify `src/mutools/adapters/web/`: `server.py` (`edit`, the origin guard), `models.py` (keep changed sessions,
  `save`), `page.py` and a new `editor.py` (the edit panel), `elements.py`, `viewport.js`, `shortcuts.js`,
  `theme.py`, `studio/rig.js` (rebuild one action's clip), a new `studio/gizmo.js` (the TransformControls around a
  bone), `three/index.js`, `studio/vendor/three/`.
- Modify `src/mutools/adapters/cli/commands.py`: `web --edit`.
- Tests beside each, the browser tests, the README, the spec and the threejs skill's rule on what the bundle lacks.

## Tasks

### Task 1: The pose math and the key edits

- [x] `quaternion_angles`, tested against `angle_quaternion` both ways.
- [x] `action_keys.py`: a pose set at one key, a key copied and removed, the lock positions kept in step, the last key
  refused, every list it does not change shared with the view it was given.
- [x] `EditSession.change` and `pose_edits.py`, with undo and redo.

### Task 2: The server in edit mode

- [x] `mutools web --edit`, refused on the reference; `serve(..., edit=True)`.
- [x] The origin guard; changed sessions kept; `Models.save`.

### Task 3: The editor in the page

- [x] The vendored TransformControls and the `three` module, with their two tests.
- [x] The viewport attaches the gizmo to the picked bone, pauses on the key, sends `posed`, and rebuilds the edited
  action's clip on `reposed`.
- [x] The edit panel under the actions: the bone and key, `Turn` and `Move`, `Copy key`, `Remove key`, `Copy action`,
  `Undo`, `Redo`, `Save` with the count of changes, the answer of the save and its warnings.
- [x] Page tests through the test user; browser tests: the gizmo is on the bone, a turn reaches the session, a save
  writes the file.

### Task 4: The README, the spec, the skill

- [x] README and spec: the edit mode; threejs skill: TransformControls is vendored, by Emanuel's decision.
- [x] `ruff check`, `ruff format --check`, `mypy src tests`, `pytest`, the browser tests, `roundtrip`.

## Later stages, not in this plan

- **C, the Blender bridge:** `mutools gltf export|import` of actions with pygltflib.
- **D, new emotes in our client:** our own data file for new emotes and `mutools emote add`.
