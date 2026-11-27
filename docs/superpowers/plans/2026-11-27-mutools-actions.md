# mutools, stage 7: actions as files Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `mutools action export|import|remove`. An action of a model (an animation: its keys and every bone's pose in
each key) goes to a JSON file of its own, and comes back into that model, or into another one with the same skeleton,
as a new action at the end or over an existing one. This is stage A of the action editor; the studio's edit mode (B),
the Blender bridge (C) and our client's new emotes (D) all read and write this file.

**Why now:** Emanuel, 2026-11-27: "que el programa nos permita generar movimientos o crear movimientos nuevos así lo
podemos exportar para emotes u otras cosas". The plan of the four stages went to him as a Claude Doc ("mutools: editor
de acciones"); his answers to its four open questions, the same day: TransformControls from three r187 for stage B,
dedicated `mutools action` commands ("agregalos"), the new emotes in a data file of our client for stage D (he left
finding where to us), and pygltflib for stage C. Order A, B, C, D, each with its own PR and his yes.

**What was measured before this stage:**

- `player/Player.bmd` has 205 actions and 60 bones, and no meshes. 22 bones change their position inside some action
  (the root, `Bip01`, in 189 of them), so the file carries positions and rotations for every bone.
- Every bone name of `Player.bmd`, and most names of the other models, come as `{"text", "tail"}`: the exporter left
  bytes after the name's 0, and two copies of the same skeleton need not have the same tail.
- `player/playerTest.bmd` has the same skeleton as `Player.bmd` (names and parents, bone by bone), so an action moves
  between the two with this file.
- Opening `Player.bmd` takes 0.7 s and encoding it with write-verify 1.2 s, so a command that adds an action to it
  takes about 2.5 s.
- A new action of the player reaches the other clients with no change to the server: the GameServer forwards C1 18
  without checking the action, and the receiver (`0x4cb750`) plays any action outside the ranges its tables map as
  itself, if the model has it. With one byte for the action, the player has room for 51 more (205 to 255).
  (`Client/docs/research/04-protocolo.md`, "La acción: 18".)

**Architecture:** One pure module in `core/formats`, `action_file.py`: the file's format, how an action leaves a model
view and how it goes into one. One module of use cases in `app`, `actions.py`, which opens the model and saves it with
`save_view`, so the reference refusal, write-verify, the stale check and the `.orig` copy hold as they do for `set`. The
CLI gets an `action` group, as it has a `workspace` group.

**Tech Stack:** the tool's. No new dependency.

## Global Constraints

- `Client/assets/Data` is never written; `mutools roundtrip Client/assets/Data` still gives 1,991 identical and 3
  skipped.
- `core/` imports only the standard library (`tests/test_layers.py`).
- Schema 1 holds: three new commands, and no new error code, exit code or finding; no existing output changes.
- The PR touches only `Client/tools/`, `docs/`, `.claude/` and `*.md`, so CI runs the tool's workflow and not the
  client's compile suite.
- English, comments only when strictly necessary, one behavior per test, Angular commits with scope `tools`.

## Decisions taken in this plan

1. **The action file.** One JSON object:

   ```json
   {"format": "mutools-action", "format_version": 1, "keys": 2, "lock": 0,
    "bones": [{"name": "Bip01", "parent": -1,
               "positions": [[0.0, 1.5, 80.25], [0.0, 1.5, 80.5]],
               "rotations": [[1.570796, 0.0, 0.0], [1.570796, 0.0, 0.1]]},
              {"dummy": 1}]}
   ```

   `keys` and `lock` are the action's, and `positions` sits beside them only when `lock` is not 0, as in the model's
   view. `bones` has one entry per bone of the model, in its order: a real bone gives the text of its name (its tail
   dropped), its parent, and its position and rotation in each key; a dummy one is `{"dummy": n}`, as the view shows
   it. The numbers are the view's (the shortest decimal that keeps a float's bits, or `{"f32": "<hex>"}`), so an action
   exported and imported back gives the same bytes.
2. **The same skeleton** means the same list of bones: as many, the dummies at the same numbers, and every real bone
   with the same name text and the same parent. Otherwise the import is `INVALID_VALUE` at `/bones` (the counts) or at
   `/bones/N` (the first bone that differs), and the message names the bone in both. Names are compared by their text
   because tails differ between copies.
3. **`import` adds the action at the end** by default; `--at N` puts it over action N, which must exist
   (`POINTER_NOT_FOUND` at `/actions/N` otherwise, with the advice to leave `--at` out). The result gives the action's
   number and how many actions the model has after the write.
4. **`remove` takes the last action with no flag; any other needs `--yes`**, because every later action moves down one
   number, and the client and the server name a player's actions by their number (C1 18). Without it the answer is
   `USAGE`, with `--yes` as the suggestion.
5. **Only models have actions.** A table gets `USAGE`.
6. **The limits stay the view's:** keys up to 32,767, and bones times actions up to 65,536, the client's pose buffer.
   C1 18's one byte (256 actions) matters only to the player, and it is stage D's: the tool lets a model hold more, as
   `build` does today.
7. **No angle conversion in this stage.** Rotations are stored as angles, so moving an action copies them as they are.
   The quaternion to angles conversion (the inverse of `0x5fcd20`) waits for its first user, the studio's rotation
   rings in stage B.
8. **The export is the dump's twin:** it writes to `--output` (never a `.bmd`), takes `--dry-run`, and answers with the
   path, the size and the action's keys.

## Files

- Create `src/mutools/core/formats/action_file.py`: `ACTION_FILE_FORMAT`, `action_file(view, index)`,
  `read_action_file(value)`, `with_action(view, action, at)`, `without_action(view, index)`.
- Create `src/mutools/app/actions.py`: `export_action`, `import_action`, `remove_action` and their results.
- Modify `src/mutools/adapters/cli/commands.py`: the `action` group, its three commands, their output types and
  examples.
- Create `tests/core/test_action_file.py`, `tests/app/test_actions.py`, `tests/adapters/cli/test_actions.py`.
- Modify `tests/adapters/cli/test_help.py` (the three commands are described).
- Modify `.claude/skills/muret-bmd/SKILL.md` (a recipe, two rows of "One job, one call", the rule on numbers),
  `Client/tools/mutools/README.md` and the spec.

## Tasks

### Task 1: The action file

- [ ] Tests first, on `skill/Rider01.bmd` (4 actions, the third with a lock, 24 bones of which 10, 17 and 21 are
  dummies): an exported action has the action's keys, lock and lock positions, and every bone's pose in it; a dummy
  stays a dummy; an action read back into its own model at its own number changes nothing; added at the end it is the
  last action and every real bone has its pose; another skeleton, another bone count, a dummy where a bone was, another
  name or another parent are refused at the bone; another tail is accepted; a file that is not an action file, of
  another version, or whose lists do not have `keys` items is refused at its own pointer; removing an action takes it
  out of the actions and out of every bone.
- [ ] `action_file.py`.

### Task 2: The use cases

- [ ] Tests first, with the fake store: export writes the file and refuses a `.bmd` output; a number out of range is
  `POINTER_NOT_FOUND`; a table is `USAGE`; import appends and replaces, and its dry run writes nothing; an action
  exported and imported over itself writes nothing (`changed: false`); the reference is never written; removing the
  last action needs no flag and any other needs `yes`.
- [ ] `actions.py`.

### Task 3: The commands

- [ ] `mutools action export FILE N --output OUT.json [--dry-run]`, `mutools action import FILE ACTION.json [--at N]
  [--dry-run]`, `mutools action remove FILE N [--yes] [--dry-run]`.
- [ ] CLI tests: the envelope, each output against its JSON Schema, the exit codes of the errors above; the help
  describes the three commands.

### Task 4: The skill, the README, the spec

- [ ] A recipe that exports an action of `NPC/BloodCastle01.bmd`, imports it as a new one and removes it (the recipe
  test runs it).
- [ ] README: the three commands; spec: the commands and stage 7.
- [ ] `ruff check`, `ruff format --check`, `mypy src tests`, `pytest`, the browser tests, `mutools roundtrip
  Client/assets/Data`.

## Later stages, not in this plan

- **B, the studio edits actions:** `mutools web --edit` on a working copy only, TransformControls from three r187
  (vendored at a pinned version with its MIT license) to rotate and move bones, keys added and removed, undo, and a
  save through this stage's use cases.
- **C, the Blender bridge:** `mutools gltf export|import` of actions with pygltflib (Z up in MU, Y up in glTF; one
  frame is one key).
- **D, new emotes in our client:** a data file of our own for the new emotes (trigger text, action, speed), read by
  our client, and `mutools emote add`; the original client's check in the VM comes last.
