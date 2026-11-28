# mutools, stage 10: new emotes in MuRet's client Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** a player of MuRet's client says a word and plays a movement that the original does not have. The movement is
an action added to `player/Player.bmd` after its 205 (stages A and C), a row of `Data/MuRet/Emotes.txt` makes it an
emote, `mutools emote add` writes that row, and the server gets the same packet as for the original's emotes. This is
stage D of the action editor; stages A ([`2026-11-27-mutools-actions.md`](2026-11-27-mutools-actions.md)), B
([`2026-11-27-mutools-action-editing.md`](2026-11-27-mutools-action-editing.md)) and C
([`2026-11-28-mutools-gltf-bridge.md`](2026-11-28-mutools-gltf-bridge.md)) are merged.

**Why now:** Emanuel, 2026-11-27: new movements "para emotes u otras cosas", with the emotes in a data file of our own
client; on 2026-11-28 at 01:23Z he said to start stage D, with the check in the VM at the end.

**What the original does, from the research:**

- 0x576eb0 looks for 95 triggers in what was said, in order, and the first one found decides one of 22 cases: texts
  270 to 348 and 783 of `Text.bmd` and 15 strings of `main.exe` (0x6c90e0..0x6c912c, in CP949). 0x576960 compares
  bytes and tells capitals apart; an empty text is in every message. Text 279 of the Spanish `Text.bmd` is `T`, so any
  message with a capital T plays an emote.
- Each case calls 0x576a60, which plays the animation only when the hero stands (actions 1 to 12 or 0x8a), gives an
  elf the next animation except for 0xb7 to 0xc0, and sends `C1 05 18 [direction] [action]`; then the case sends its
  own `C1 18`. A standing hero sends two identical packets (Q-C776).
- The receiver, 0x4cb750, handles 0x12, 0x78 and 0x79, 0x7a and 0x7b and 0x7e apart, takes 0x80 to 0x9b through a
  table with an elf's own animation, and plays any other action as itself. `SetAction` leaves an action the model does
  not have alone.
- 0x60dbe0 gives Player.bmd's actions their speeds (`Tables::PlayerActionSpeed`); the 57 it does not write stay at 0
  (Q-C87), so an added action needs a speed or it does not move. The original's emotes play 0.32 keys a step.

**Architecture:** `game/Tables/AddedEmotes` reads the file (pure, from bytes) and `LoadGameData` loads it from the Data
folder; `GameClient` hands the rows to `WorldRuntime::addedEmotes` and to `GameModels`, whose player skeleton takes
their speeds. The chat (`Scenes/GameChat`) asks `UI::FindTheEmote` first and `UI::FindTheAddedEmote` after it, and
`Actors::DoTheAddedEmote` plays and sends the action as a case of 0x576eb0 does. `Actors::ApplyActionReply` gives an
elf the row's elf action. In mutools, `core/formats/emote_table.py` reads and writes the file with the client's rules,
`core/checks/emote_checks.py` finds the triggers the original takes, `app/emotes.py` holds the use cases and the CLI's
`emote` group calls them.

**Tech Stack:** C++20 as the client has it; mutools as it is, standard library only in `core`.

## Global Constraints

- The original's data and behavior do not change: `Client/assets/Data` is never written, the 95 triggers keep their
  order and cases, and `roundtrip` still gives 1,991 identical and 3 skipped.
- The server is not touched: an added emote sends `C1 05 18` as an original one does.
- `check_layers.py` gives no new violation; the new files have their rows in `module-map.tsv`.
- Schema 1 of mutools holds: three new commands with their output types, and one new finding, `TRIGGER_TAKEN`; no new
  error code or exit code.
- English, comments only when strictly necessary, one behavior per test.

## Decisions taken in this plan

1. **Where the rows live.** `Data\MuRet\Emotes.txt`, a folder of MuRet's own inside Data, found ignoring case as the
   client finds every data file. No file means no added emotes; a file the client cannot read stops it at start-up,
   with the line in the log, as a broken table of the original does.
2. **The format.** Text, so a person can read it: the first line is `MuRet emotes 1`, and each row has tab-separated
   columns: the action (205 to 255: after Player.bmd's 205, and the packet carries one byte), the elf's action (0 to
   255), the keys per step (above 0, at most 10, read with `std::from_chars` as the nearest float, so `0.32` is the
   original's 0x3EA3D70A), `loop` or `once`, and one or more triggers, none empty. `\r\n` ends a line as `\n` does, an
   empty line is skipped, an action comes once, and the file is at most 64 KiB.
3. **The order.** The original's 95 triggers first, then the rows in the file's order. An added emote never takes a
   message an original one takes.
4. **Playing and sending.** An added emote goes the way of a case: the standing check of 0x576a60, the action set on
   the hero (an elf's `elfAction`), and two `C1 18` with the row's action while standing, one otherwise. No elf `+1`:
   the row names the elf's action itself.
5. **Receiving.** An added action that another player of MuRet's client sends plays as itself, as 0x4cb750 plays any
   action it does not list, and an elf takes the row's elf action.
6. **Speeds.** `MakePlayerSkeleton` gives each row's action its keys per step and its loop or once (a once action
   holds its last key as the original's do). A row's elf action takes the row's speed too when it is an added action
   (205 or more) with no row of its own; an original action keeps the original's speed.
7. **mutools checks what the client checks.** `emote add` refuses a row the client would not read (`INVALID_VALUE`
   with a pointer), an action or an elf action `Player.bmd` does not have (with `action import` as the suggestion), a
   trigger the encoding cannot write (`NOT_ENCODABLE`) and a file over 64 KiB. Reading is as strict as the client's:
   `+0.32`, ` 0.32` and `0_32`, which Python's `float` takes and `from_chars` does not, are `CORRUPT_FILE`.
8. **A trigger the original takes** is a warning, not an error: `TRIGGER_TAKEN`, on `emote add` and `emote list`, for
   each trigger that holds one of the original's 95 (the folder's `Local/Text.bmd` and the 15 strings), and for every
   trigger when one of those texts is empty. The row is written all the same.
9. **The commands.** `mutools emote add <data> <N> --trigger <word>... [--elf-action N] [--keys-per-step K] [--once]
   [--replace] [--encoding E] [--dry-run]`, `mutools emote list <data>` and `mutools emote remove <data> <N>`. They
   take the Data folder, not the file, because add reads `player/Player.bmd` and `Local/Text.bmd` beside it. Adding the
   row an action already has writes nothing (`changed: false`); changing it needs `--replace`. The triggers are CP1252
   by default, the chat's bytes in the western profile.

## Files

- Create `Client/src/game/Tables/AddedEmotes.h` and `.cpp` (`ReadAddedEmotes`, `FindAddedEmote`); modify
  `GameData` (`LoadAddedEmotes`, the field), `Kernel/WorldRuntime.h`, `App/GameClient.cpp`, `Assets/GameModels`,
  `Assets/PlayerSkeleton`, `UI/Emotes` (`FindTheAddedEmote`), `Actors/Emote` (`DoTheAddedEmote`),
  `Actors/ActionReply` and `Scenes/GameChat.cpp`; two rows in `module-map.tsv`.
- Tests: `tests/data/test_added_emotes.cpp` and new cases in the tests of game data, the skeleton, the emotes, the
  emote, the action reply and the game chat.
- Create `src/mutools/core/formats/emote_table.py`, `core/checks/emote_checks.py` and `app/emotes.py`; modify
  `adapters/cli/commands.py` (the `emote` group), `describe.py` (a number option), the `FileStore` port and
  `LocalFileStore` (`make_folder`); tests beside each, the README, the spec and the `muret-bmd` skill with its recipe.

## Tasks

### Task 1: The client reads the file

- [x] `ReadAddedEmotes` with every rule of decision 2, each broken rule an error that names its line.
- [x] `LoadGameData` loads it, missing or not; `GameClient` hands it to the runtime and the models.

### Task 2: The client plays them

- [x] The chat: the original's first, then the added rows; the hero plays and sends the action.
- [x] The receiver: an added action plays as itself, an elf's as its row says.
- [x] The skeleton: the rows' speeds and repeats.

### Task 3: mutools writes them

- [x] `emote add`, `list` and `remove`, their output types and examples; `TRIGGER_TAKEN`.
- [x] README, spec, skill recipe; `ruff`, `mypy`, `pytest`, `roundtrip`.

### Task 4: The VM (Emanuel)

- [ ] Our client sends action 205 to GS 1.02: does the server pass it on to the players around?
- [ ] An original client with the new `Player.bmd` sees it: does it play, and at what speed (its speed table has no
  entry for 205)?
- [ ] An original client with the old `Player.bmd` sees it: it should do nothing, as `SetAction` leaves an action the
  model does not have alone.
