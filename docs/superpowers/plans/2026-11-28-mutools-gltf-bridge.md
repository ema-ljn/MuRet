# mutools, stage 9: the Blender bridge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `mutools gltf export` writes a model as a glTF binary file (`.glb`) with its meshes, textures, skeleton and
actions, so Blender opens it as an armature with one action per MU action. `mutools gltf import` reads an animation
from a `.glb` that Blender wrote and puts it into the model as an action, new or over another one. This is stage C of
the action editor; stages A ([`2026-11-27-mutools-actions.md`](2026-11-27-mutools-actions.md)) and B
([`2026-11-27-mutools-action-editing.md`](2026-11-27-mutools-action-editing.md)) are merged.

**Why now:** Emanuel, 2026-11-27: new movements for emotes and other things; at 22:30Z he chose pygltflib for this
stage, and on 2026-11-28 at 00:10Z he said to start stage C.

**What was measured before this stage:**

- pygltflib 1.16.5 (MIT) is pure Python, needs `dataclasses-json` and `deprecated` (8 packages in all), and has no
  `py.typed`. It writes `Player.bmd` with its 205 actions in 1.4 s, but reads Blender's export of it in 16 s: its
  `dataclasses-json` turns 130,000 objects into dataclasses. The standard library's `json` reads the same file in 0.05 s.
- Blender 5.3.0 installs as the `bpy` module for Python 3.13 (`pip install bpy`, about 1 GB) and runs its own glTF
  importer and exporter without a screen, so a round trip through Blender can be tested here.
- Of the 1,962 models, none has a vertex on a dummy bone, none has a bone with an empty name, and 10 repeat a bone name
  (`Monster/Monster28.bmd` has two `Mesh01`; Blood Castle's gates repeat `Box02`).
- `player/Player.bmd` has no meshes: its body comes from the parts (armor, pants, gloves, boots, helm) that borrow its
  skeleton, as the studio shows them.

**Architecture:** the math stays in Python and the standard library: `app/gltf_rig.py` turns a model into a neutral
description of a glTF scene (nodes, a skin, meshes, images, animations), and `app/gltf_animation.py` turns an
animation of such a scene back into an action file of stage A, which `with_action` puts into the model;
`core/affine.py` holds the matrices. `adapters/gltf/writer.py` writes that description as a `.glb` with pygltflib, the
only module that imports it, and `adapters/gltf/reader.py` reads a `.glb` with the standard library (see what was
measured). The CLI commands sit in `adapters/cli/commands.py` like the other groups.

**Tech Stack:** mutools as it is, plus pygltflib 1.16.5 as the optional extra `mutools[gltf]`.

## Global Constraints

- `Client/assets/Data` is never written, and `roundtrip` still gives 1,991 identical and 3 skipped.
- `core/` imports only the standard library; only `adapters/gltf/writer.py` imports pygltflib, and `tests/test_layers.py`
  says so.
- Schema 1 holds: two new commands with their output types; no new error code, exit code or finding.
- The PR touches only `Client/tools/`, `docs/`, `.claude/` and `*.md`.
- English, comments only when strictly necessary, one behavior per test, Angular commits with scope `tools`.

## Decisions taken in this plan

1. **The commands.** `mutools gltf export <model> --output <file.glb> [--part <model>]... [--action N]...` and
   `mutools gltf import <model> <file.glb> [--animation <name or N>] [--at N]`, both with `--dry-run`. Without the
   extra, export answers `USAGE` with `pip install 'mutools[gltf]'` as the suggestion, as `web` does without its own;
   import needs no extra.
2. **MU's space inside glTF's.** glTF is Y-up and MU is Z-up. The root bones carry the turn of -90 degrees about x
   and every other bone keeps MU's own numbers. (A root node `MU (Z up)` holding the turn, as this plan first said,
   made Blender turn the skeleton twice: it put the node's turn on the armature and the inverse bind matrices' on the
   bones.) Import takes each root bone through whatever nodes Blender writes above it, an `Armature` node, and back
   through the same turn.
3. **The rest pose** is key 0 of action 0, as the studio frames a model: the vertices are placed there, each joint's
   node holds that pose, and the inverse bind matrices come from it, so Blender shows the model standing and its bones
   where they belong.
4. **Time.** One key is one frame at 24 frames a second, Blender's default, so frame k is key k. Import counts the
   keys from the smallest step between the times it reads, at most a frame at 24 frames a second: Blender leaves out
   the frames that change nothing, and wrote only the first and last of action 0's three keys. It samples each channel
   at every key (step, linear with rotations slerped, or cubic spline).
5. **Names.** A joint is named after its bone; a name that repeats gets ` (N)` after it, N being the bone's number, and
   import names the model's bones the same way. Dummy bones have no node. Animations are named `NNN Name`, the
   action's number and the studio's name for it, so Blender lists them in order.
6. **What import checks.** Every real bone of the model needs a joint of its name under the same parent, or the file
   is for another skeleton (`INVALID_VALUE`). A bone with no channel in the animation keeps its node's pose at every
   key. Over an action (`--at N`), a key whose pose comes back as the stored one, within what float32 and Blender's
   matrices change, keeps the stored floats: an action exported and imported over itself without changes leaves the
   file as it was. The noise grows with the model: 0.0006 units and 0.00013 rad at most on `Player.bmd`, whose places
   reach 376, and 0.019 units on `Monster/Monster33.bmd`, whose places reach 2,581. A place is kept within 0.01 units
   or 1/50,000 of the model's largest stored place, whichever is more (0.05 on `Monster33.bmd`, 1.6 on
   `Logo/logo01.bmd`, whose places reach 77,860), and a turn within 0.001 rad. The new action takes the lock of the
   one it replaces (0 when it is added); a locked one keeps its lock positions when the keys match, and else repeats
   its first, which the client never reads.
7. **Parts.** `--part` adds the meshes of a model that borrows the skeleton (the studio's rule), so `Player.bmd` goes
   to Blender with a body. Textures are embedded: an OZJ as its JPEG, an OZT as PNG; one that is not found or not read
   leaves its mesh untextured and is listed in the output's `untextured`.
8. **The Blender test.** A test marked `blender` runs Blender's own importer and exporter through `bpy` on an exported
   file and imports the result back; like the browser tests it is left out of a plain `pytest`, and CI does not install
   Blender.

## Files

- Create `src/mutools/core/affine.py`: turns, scales and places as matrices.
- Create `src/mutools/app/gltf_rig.py`: the neutral scene, `joint_names`, `model_rig`.
- Create `src/mutools/app/gltf_animation.py`: `sampled`, `key_times`, `chosen_animation`, `animation_action`.
- Create `src/mutools/app/gltf_actions.py`: `export_gltf`, `import_gltf` (the use cases, given the writer or the reader).
- Create `src/mutools/adapters/gltf/writer.py` (`write_glb`, pygltflib) and `reader.py` (`read_glb`, standard library).
- Modify `src/mutools/adapters/cli/commands.py`: the `gltf` group; `describe.py`: a repeatable option is a list;
  `pyproject.toml`: the extra, the `blender` marker and mypy's override.
- Tests beside each, the Blender test, the README, the spec and the `muret-bmd` skill's recipe.

## Tasks

### Task 1: The scene in Python

- [x] `model_rig`: joints, rest pose, inverse bind matrices, meshes with parts, animations; named joints.
- [x] `animation_action`: keys from times, sampling, the root through its ancestors, kept floats, lock.

### Task 2: The file and the commands

- [x] `write_glb` with pygltflib and `read_glb` with the standard library; an export read back gives the same nodes,
  joints and animations.
- [x] `mutools gltf export|import`, their output types and examples; the extra and its `USAGE`.

### Task 3: Proof

- [x] Export, import over the same action: identical bytes, for every action of `Player.bmd`, and for all 3,099
  actions of the 1,962 models.
- [x] Through Blender 5.3 (`bpy`): export, Blender import and export, `mutools gltf import`: the same action back (all
  205 of `Player.bmd` by hand, three in the test), and a turn made in Blender comes back on its key and bone only.
  By hand also the models with the largest places (`Logo/logo01.bmd`, `Monster/Monster33.bmd`, `Logo/Logo05.bmd`,
  `Object1/HouseEtc02.bmd`) and five others: every action came back as it was.
- [x] README, spec, skill recipe; `ruff`, `mypy`, `pytest`, the browser tests, `roundtrip`.

## Later stages, not in this plan

- **D, new emotes in our client:** our own data file for new emotes and `mutools emote add`.
