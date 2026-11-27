# MU's music: findings and plan for one home

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** every music track of MU is named once, the engine plays music without knowing which tracks MU has, and each
piece of code that chooses a track has one owner that a rule can tell.

**Architecture:** in `main.exe` music has three layers. A tiny player (`0x4acfd0` play, `0x4acf60` stop, `0x4ad060`
"at the end", three globals) wraps `wzAudio.dll`. One table of 22 track paths (`0x6d5dcc..0x6d5e20`) holds every
track. Seven functions choose tracks from that table: the map music inline in the scene loop, Crywolf's, the siege's,
the login's, the credits' (two) and the sweep that stops all of them. MuRet copies the player well but mixes MU's
content into it: the engine's `Audio::Music` holds the 22 paths and the map music, and the other users spell the
paths again as private literals. The plan keeps the player in the engine behind its own port (the `wzAudio.dll`
equivalent), gives MU's music content a game module of its own, `game/Music` (layer 8, beside `game/Sound`), and
leaves each other choice with the module whose key the original dispatches on.

**Tech Stack:** C++20, GoogleTest, the behavior golden (`Client/tests/characterization`), `check_layers.py`,
`check_named_values.py`, `module-map.tsv`, `modules.tsv`.

**Spec:** the refactor design
[`2026-11-20-client-architecture-design.md`](../specs/2026-11-20-client-architecture-design.md), "The modules, one by
one" (criteria 1, 3 and 5); [`CODING_RULES.md`](../../CODING_RULES.md) §5 (the layer table), §7
"Types and aliases" (rule 5, ids) and "Named values" (rules 1, 2 and 7); the named-values plan
[`2026-11-23-named-values.md`](2026-11-23-named-values.md) (Tasks 3 and 8); the AR1 plan
[`2026-11-22-ar1-engine-apart.md`](2026-11-22-ar1-engine-apart.md) (Task 5b); the Crywolf plan
[`2026-11-24-crywolf-homes.md`](2026-11-24-crywolf-homes.md) (Task 2, PR 447); CL10's research
[`2026-11-18-cl10-audio.md`](../../../Client/docs/research/2026-11-18-cl10-audio.md) and
[ADR 0021](../../adr/0021-audio-sdl3.md).

Measured on `main` 57e08e8 (2026-11-25), with the map names of PRs 456 and 457 (open, `Maps/MapId.h`). The original
was read in the research repository's ASM and pseudocode of `main.exe` c32bbd5f
(`client-research/mainexe-vmxp-c32bbd5f/decompiled/functions/`): every caller of `0x4acfd0`, `0x4acf60`, `0x4ad060`
and `0x622b00`, every reader and writer of the player's three globals, and every play or stop of the three sound ids
the events loop as music.

## The answer in short

- **Yes, music deserves a home of its own, but on the game's side.** The engine already has its piece apart
  (`Audio::Music`, the player). What has no home is MU's content: 22 paths spelled 56 times in 4 files (45 of them
  in the engine's `Audio/Music.cpp`), and a map music chain that lives in the engine with bare map numbers.
  CODING_RULES §7 rule 7 says the engine names no game value, and this is the one engine site of AR1 Task 5b that
  PR 439 did not move.
- **One table in the original, one home in MuRet.** `main.exe` keeps the 22 paths in one pointer table at `0x6d5dcc`
  and every caller loads its track from a slot. The home is `game/Music/Tracks.h`: the slots as an `enum class Track`
  in the table's order and the paths in one array.
- **`game/Music`, layer 8, beside `game/Sound`.** It holds the tracks, the map music (the block `0x6388dd..0x639413`
  of `0x637700`) and the sweep (`0x622b00`). Nothing below needs it, and everything that plays music is above it:
  `Events/Crywolf` (9), `CastleSiege` (9), `FrontEnd` (9), `Session` (10), `Scenes` (11). `game/Maps` cannot hold
  it: layer 6 holds data without behavior (spec criterion 3, CODING_RULES §5), and the map music is behavior.
- **The other choices stay with their owners.** Crywolf's `0x430250` is in Crywolf's source unit (done in PR 447),
  the siege's `0x42b540` in the siege's, the login's and the credits' in the front end, the sweep's two callers in
  the `F1 02` handler. They name their tracks from `game/Music`.
- **The engine's player gets its own port.** `Audio::IPlayback` mixes the music stream (3 methods, `wzAudio.dll` in the
  original) with the effects (6 methods, DirectSound). Nine of the 10 test doubles stub one half (Crywolf's records
  both, since `0x430250` also requests a sound). `Audio::IMusicStream` takes the music half, and the player holds its
  stream instead of receiving it in every call.
- **Not a `Music.dll` for now.** One SDL3_mixer instance serves both halves, and the player is about 60 lines. After
  Task 4 the player depends only on its port, so ADR 0028 can still split it into a library of its own in one PR.
- **The checks learn to see it.** `check_named_values.py` blanks string literals today, so a path written twice is
  invisible, and the three sounds the events loop as music (`0x8b`, `0x99`, `0x9e`) escape the `sound` space because
  their names end in `Music`. Task 8 closes both.

## What `main.exe` does

### The player

| Piece | Address | What it does |
| --- | --- | --- |
| play | `0x4acfd0` (138 B) | if not blocked and (music on or forced), and the path differs from the current one byte for byte: `wzAudioPlay(path, 1)` and copy the path into the current one |
| stop | `0x4acf60` (103 B) | if (music on or forced) and a track is current and it is this path: `wzAudioStop()` and clear the current one |
| at the end | `0x4ad060` (12 B) | `wzAudioGetStreamOffsetRange() == 100` |
| current path | `0x5763204` | written only by `0x4acfd0` and `0x4acf60` |
| music on | `0x576333c` | `MusicOnOff` from the registry (`0x4b90a0`, 0 without the key); `0x4b9b80` calls `wzAudioCreate` and `wzAudioOption` only with it on |
| blocked | `0x5763568` | written only at `0x4b32be`, in `WM_DESTROY` of the window procedure `0x4af4e0`; read by `0x4acfd0` and at `0x6381ac` in `0x637700`, which then skips the draw, the ambience, the map music and Crywolf's (jump to `0x63942a`) |

No caller forces: the second argument is 0 at all 53 calls and in the sweep. The `1` passed to `wzAudioPlay` does not
make the DLL loop the track (ADR 0021).

### The table

One array of 22 pointers to `data\music\...` from `0x6d5dcc` to `0x6d5e20` (the sweep stops at `0x6d5e24`):

| Slot | Address | Track | Played by |
| ---: | --- | --- | --- |
| 0 | `0x6d5dcc` | `Pub.mp3` | map music |
| 1 | `0x6d5dd0` | `Mutheme.mp3` | credits |
| 2 | `0x6d5dd4` | `Church.mp3` | map music |
| 3 | `0x6d5dd8` | `Devias.mp3` | map music |
| 4 | `0x6d5ddc` | `Noria.mp3` | map music |
| 5 | `0x6d5de0` | `Dungeon.mp3` | map music |
| 6 | `0x6d5de4` | `atlans.mp3` | map music |
| 7 | `0x6d5de8` | `icarus.mp3` | map music |
| 8 | `0x6d5dec` | `tarkan.mp3` | map music |
| 9 | `0x6d5df0` | `lost_tower_a.mp3` | map music |
| 10 | `0x6d5df4` | `lost_tower_b.mp3` | nobody: only the sweep reads its slot |
| 11 | `0x6d5df8` | `kalima.mp3` | map music |
| 12 | `0x6d5dfc` | `castle.mp3` | the siege |
| 13 | `0x6d5e00` | `charge.mp3` | the siege |
| 14 | `0x6d5e04` | `lastend.mp3` | nobody: only the sweep reads its slot |
| 15 | `0x6d5e08` | `huntingground.mp3` | map music |
| 16 | `0x6d5e0c` | `Aida.mp3` | map music |
| 17 | `0x6d5e10` | `crywolf1st.mp3` | Crywolf |
| 18 | `0x6d5e14` | `crywolf_ready-02.ogg` | Crywolf |
| 19 | `0x6d5e18` | `crywolf_before-01.ogg` | Crywolf |
| 20 | `0x6d5e1c` | `crywolf_back-03.ogg` | Crywolf |
| 21 | `0x6d5e20` | `main_theme.mp3` | login, credits, map music |

MuRet's `WorldTracks` (`Audio/Music.cpp:14-35`) is this table in this order, without saying so.

### Who chooses a track

| Function | Calls | What it chooses | Its source unit | MuRet today | Target |
| --- | ---: | --- | --- | --- | --- |
| `0x637700`, block `0x6388dd..0x639413` | 26 | the map's track, in 11 groups in a fixed order; Lorencia and Devias only in the safe zone, Lorencia's pub by the cell layer, Devias's church by the hero's cell (`0x638ae4..0x6391e4`, which draws `rand()`) | the scene loop, inline | `Audio::Music::StepWorld` (`Audio/Music.cpp:61-135`, engine), called from `Scenes/WinMainLoop.cpp:39` | `game/Music`, `Music::StepTheMapMusic` |
| `0x430250` | 18 (4 "at the end") | Crywolf's stage track, called at `0x639422` right after the map music | Crywolf's map unit | `Scenes/CrywolfMusic.cpp` (PR 447), its four paths private | stays in `game/Events/Crywolf`; names from `game/Music` |
| `0x42b540` | 4 | `castle` or `charge` by the siege flag, every 1,800 s (`0x7239f0`) | the siege's unit, third link of `0x6061c0` | a lambda in `Scenes/GameScene.cpp:584-592` passed as `CastleSiegePrelude::updateMusic` | the choice moves into `StepCastleSiegePrelude` (`game/CastleSiege`) |
| `0x6349a0` | 1 (`0x634c31`) | `main_theme` at the login's start | the login scene's start | `Scenes/LoginScene.cpp:502` | stays; name from `game/Music` |
| `0x6411b0` | 2 (`0x64140d`, `0x64141b`) | opening the credits: `main_theme` off, `Mutheme` on | the login scene's credits button | `Scenes/LoginScene.cpp:1310-1311` | stays; names from `game/Music` |
| `0x63f9a0` | 2 (`0x63fbbd`, `0x63fbca`) | closing the credits: `Mutheme` off, `main_theme` on | the credits' close | `Scenes/LoginScene.cpp:1343-1344` | stays; names from `game/Music` |
| `0x622b00` | 1, over the 22 slots | stops whatever track of the table is current; called at `0x4c276a` (`F1 02` with 2, in the game scene) and `0x4c3626` (`F1 02` with 1) | a helper of its own (32 B) | `Audio::Music::StopWorld`, from `Scenes/ReturnToSelection.cpp:133` | `game/Music`, `Music::StopTheWorldMusic` |

That is CL10's count: 53 calls plus the sweep, in seven functions. Nothing else in `main.exe` calls the player.

### Music played as a sound

Blood Castle and Chaos Castle have no track in the table. The events loop a WAV of the sound catalog through the
effects API (`0x41ed10` with loop 1) and stop it with a rewind (`0x41ee00`):

| Id | File | Played | Stopped |
| --- | --- | --- | --- |
| `0x8b` | `sound/iBloodCastle.wav` | `0x40c178` (Blood Castle state 0) | `0x40c213` (state 2); `0x4c49e7` (world entry outside 11..17) |
| `0x99` | `sound/iChaosCastle.wav` | `0x404e99` (Chaos Castle state 5) | `0x404ed9` (state 7); `0x4c4a05`, `0x4c4ef5`, `0x4c7b86` (outside 18..23) |
| `0x9e` | `sound/aChaos.wav` | `0x404f01` (state 7, inside 18..23); `0x4c7b12` (the `1C` warp, from or to 18..23) | `0x404e8b` (state 5); `0x4c4a11`, `0x4c4f01`, `0x4c7b92` |

Fourteen sites in five functions (`0x40c140`, `0x404e20`, `0x4c3940`, `0x4c4b80`, `0x4c6e10`). They are sound ids:
their home is `game/Sound` (named-values Task 8), not `game/Music`. MuRet names them three ways:
`BloodCastleMusic`, `ChaosCastleMusic` and `ChaosCastleWaitMusic` (`Scenes/EventPackets.cpp:161,177-178`),
`OutsideBloodCastleSound`, `FirstOutsideChaosCastleSound` and `SecondOutsideChaosCastleSound`
(`Scenes/WorldEntry.cpp:52-54`), and bare `0x99` and `0x9e` (`Scenes/WorldTransitions.cpp:87,103-105,212-214`).

## What MuRet has today

| Piece | File | Lines | Module map | What is wrong |
| --- | --- | ---: | --- | --- |
| the player and MU's content | `Audio/Music.{h,cpp}` | 39 + 136 | `engine/Audio` | the 22 paths and the map music in the engine (§7 rule 7); 45 path literals for 22 tracks, because `StepWorld` spells again what `WorldTracks` lists; map ids as bare literals, so `check_named_values.py` does not see them; no `0x` citation of the player, the table, the sweep or the block |
| the port | `Audio/Playback.h` | 28 | `engine/Audio` | `IPlayback` has the music stream and the effects in one interface; 7 of the 10 test doubles write empty `PlayMusic` and `StopMusic`, and 2 of the other 3 write empty effects |
| Crywolf | `Scenes/CrywolfMusic.cpp` | 79 | `game/Events/Crywolf` | its four paths private (the lines Emanuel saw); `TrackEnd = 100` re-derives `0x4ad060` |
| login and credits | `Scenes/LoginScene.cpp` | 5 lines | `game/Scenes` | literals |
| siege | `Scenes/GameScene.cpp:584-592` | 9 | `game/Scenes` | the siege's choice lives in the game scene as a lambda, one more `std::function` in `CastleSiegePrelude` |
| sweep | `Scenes/ReturnToSelection.cpp:133` | 1 | `game/Session` | through the engine |

Also:

- **`force` and `loop` are dead.** `Music::Play` and `Music::Stop` take `force`, which no call passes, as no call in
  the original does; `IPlayback::PlayMusic` takes `loop`, always `false`.
- **The music row of AR1 Task 5b is still open.** PR 439 did the three comparison sites and left `Audio/Music.cpp`
  (its log shows only PRs 365 and 447). The done criterion missed it because the check reads only numbers, and this
  code has bare literals and strings.
- **The behavior golden does not hear audio.** Its harness leaves the audio device out, so no golden pins the order
  of the music calls across functions in a frame (login, world, event map, return).

## Where each piece goes

| Piece | Home | Why |
| --- | --- | --- |
| the player (`0x4acfd0`, `0x4acf60`, `0x4ad060`, the three globals) | `engine/Audio`, `Audio::MusicPlayer` | generic: it knows paths, not MU's tracks |
| the stream (`wzAudioPlay`, `wzAudioStop`, `wzAudioGetStreamOffsetRange`) | `engine/Audio`, the port `Audio::IMusicStream`; `engine/Platform` implements it in `SdlAudio` | the engine's edge (CODING_RULES §5, "Interfaces"), and the tests need it |
| the table (`0x6d5dcc`) | `game/Music`, `Music/Tracks.h` | a closed set fixed by the game: `enum class` (§7 "Types", rule 5); one home for the space (§7 "Named values", rule 1) |
| the map music (`0x6388dd..0x639413`) | `game/Music`, `Music/MapMusic.{h,cpp}` | one block of the original with an order between groups; it reads the map, the hero's safe flag and cell layer and a church test, nothing of the scene |
| the sweep (`0x622b00`) | `game/Music`, `Music/MapMusic.{h,cpp}` | it walks the table |
| Crywolf (`0x430250`) | `game/Events/Crywolf` (already) | Crywolf's source unit, its key is the stage |
| the siege (`0x42b540`) | `game/CastleSiege` | the siege's source unit, its key is the siege flag |
| login and credits | where their scenes go (`game/Scenes`, `game/FrontEnd`) | the scene is the key |
| the event loops (`0x8b`, `0x99`, `0x9e`) | `game/Sound` (named-values Task 8) | sound ids, played by the effects API |
| the siege's clock (`0x7239f0`) | `game/CastleSiege` state, through AR3.1's `globals.tsv` | written only by `0x4294c0`, `0x42a760` and `0x42b540`, all the siege's |
| the Devias church test (`0x638ae4..0x6391e4`) | stays with its caller until AR4 | it reads the hero's protected cells (`game/Actors`, layer 9), which layer 8 cannot see; the map music receives it as a lazy test, as today |

### Why not split the map music into map scripts

Spec criterion 5 gives "what a per-map chain reaches with the map as its gate" to `game/MapScripts/<Map>`. That reads
the chains of calls (`0x606660`, `0x6061c0`...), whose links are functions in each map's source unit: the siege's
`0x42b540` is one, Crywolf's `0x430250` another. The map music is not such a chain. It is inline code of the scene
loop, written in one place by the original's authors, and its order matters: a play replaces the shared stream at
once, so Lorencia's stops must run before Dungeon's play. Eleven map scripts called in a fixed order would write the
same chain again, spread over eleven files.

### The options for MU's content

| Option | For | Against |
| --- | --- | --- |
| **`game/Music`, layer 8 (chosen 2026-11-25)** | one home for one family; its own key (paths, one stream) and its own engine port; Emanuel asked to split "as much as possible, starting with sound" (2026-11-21); criterion 1, direction, holds | one more row in every list of modules (`modules.tsv`, CODING_RULES §5, the spec, ADR 0023, the `muret-module` skill) |
| `game/Sound`, layer 8 | one module for everything audible, no new row | two families with different keys (ids and paths), engine APIs and state in one module |
| `game/Maps`, layer 6 (the spec today) | no new row | layer 6 holds data without behavior (criterion 3, CODING_RULES §5); the login's, the credits' and the siege's tracks are not map data |

### The engine library

Emanuel named a `Music.dll` on 2026-11-22. The original has exactly that boundary: `wzAudio.dll` plays the music and
DirectSound the effects. In MuRet one SDL3_mixer instance serves both (`SdlAudio` creates one mixer and a music track
on it), and the player is about 60 lines. The recommendation for ADR 0028 is to keep it in `Audio`, with the boundary
as an interface: after Task 4, `MusicPlayer` and `IMusicStream` depend on nothing else in `Audio`, so a library of
their own is one PR away if he wants it.

[ADR 0028](../../adr/0028-shared-library-modules.md) (2026-11-25) gives a library, `Audio.dll`, to the part that plays:
the SDL3_mixer backend. The player stays where this plan puts it, in the engine behind its port.

## Found on the way

Not part of the refactor tasks. Each needs its own yes.

- **The frame after `WM_DESTROY`.** MuRet never calls `Music::SetBlocked`. The original sets `0x5763568` in
  `WM_DESTROY` (`0x4b32be`), and the rest of that frame skips the draw, the ambience, the map music and Crywolf's
  (`0x6381ac` → `0x63942a`). MuRet sets `destroyRequested` and stops at the loop's next check
  (`main.cpp:842-846`), so the rest of that turn still draws and runs `StepTheGameMusic`, which can draw `rand()` in
  Devias and request the ambience. A port fix that covers the draw, not
  only music; until then the plan keeps `SetBlocked` and its test.
- **The spec also puts the ambience in `game/Maps`.** `StepWorldAmbience` (`0x63869c..0x6388a8`) is behavior too (it
  loops and stops sounds and draws `rand()` in Noria and Icarus). By the same criterion it belongs in `game/Sound`;
  named-values Task 8 or AR4.1 decides it. This plan only removes the music from the `game/Maps` row.

## Decisions

1. **Where MU's music content lives.** Decided by Emanuel on 2026-11-25: a new module, `game/Music`, over
   `game/Sound` and `game/Maps`. Tasks 5 to 8 use it.
2. **No `Music.dll` in AR1.** Recommended; ADR 0028 decides libraries.
3. **The tracks are an `enum class Track : std::uint8_t`** in the table's slot order, with the paths in one
   `std::array` (§7 "Types", rule 5). The engine keeps taking a path: the game converts at its edge (§7 "Types",
   rule 5, last point). A file that gives a track a role names it with the home's value (`SecondNoticeTrack =
   Track::CrywolfBefore`, §7 "Named values", rule 2).
4. **`force` and `loop` leave the engine's signatures** (Task 4): no caller of the original or of MuRet uses them.
5. **Tests keep their path literals.** A test pins the file a track opens, so it writes the path, as the Crywolf test
   does today; the check of Task 8 reads `Client/src` only.

## Global Constraints

- Fidelity first. No task changes the order of the plays and stops, of the `rand()` draws, of the sound requests or of
  the calls between the map music and Crywolf's.
- Every task is a refactor: no golden and no test expectation changes. A test that moves keeps its assertions.
- With no device, nothing changes, as today: the player with no stream ignores `Play` and `Stop` and keeps its
  current track (a test pins it).
- A comparison keeps its shape (§7 "Named values", rule 4): `map == 1 || map == 5` stays two comparisons with names;
  `IsKalimaMap` replaces `(map >= 24 && map <= 29) || map == 36` because `0x63935e..0x639385` computes exactly that
  flag, as the same inlined test does at `0x638385`.
- Every address cited in a block that moves is cited where it lands; the PR gives the count of `0x` citations in the
  files it touches, before and after. Moved code gets the citations it lacks (`0x4acfd0`, `0x4acf60`, `0x4ad060`,
  `0x6d5dcc`, `0x622b00`, `0x6388dd..0x639413`).
- Code that lands in a new file is cleaned there: named values, comments only if strictly necessary and in English,
  its own test.
- New files of a module whose folder exists go in that folder, as `Maps/` did; their rows go in
  `Client/docs/research/coverage/module-map.tsv` in the same PR, and `check_layers.py` prints `0 new`.
- A file renamed keeps its content in a rename-only PR first (as PR 453 did).
- PRs target `main`. One task per PR, and Emanuel says yes before the next.
- GitHub CI is the build and the final word.

## Review Focus

- **The order between groups.** Leaving Lorencia for the Dungeon stops `Pub` and `main_theme`, then plays `Dungeon`;
  the test of Task 5 pins `stop` before `play` (it exists today:
  `MusicTest.FollowsNativeMapGroupOrderWhenSwitchingBetweenPubAndDungeon`).
- **The church test is lazy.** It runs only in Devias's safe zone, and each run draws `rand()`; a refactor that
  computes it before the call changes the CRT stream everywhere else. Task 5 pins how many times it is called per map.
- **Map 0 outside the safe zone keeps its track.** The first group neither plays nor stops then.
- **The two tracks nobody plays.** `lost_tower_b` and `lastend` stay in the table: the sweep reads their slots.
- **Exact names.** `0x4acfd0` compares bytes, so `Music\noria.mp3` is not `Music\Noria.mp3` (a test pins it).
- **Crywolf after the map music.** `0x639422` calls `0x430250` after the last group; `StepTheGameMusic` keeps that
  order.
- **The no-device client.** `runtime.audio` is null in most tests; after Task 4 the player's stream is null there.

---

## Tasks

Task 1 is this document. Tasks 2 and 3 depend on nothing. Task 4 needs Task 3. Task 5 needs Task 4 (PRs 456 and
457 are merged). Task 6 needs Task 5, Task 7 needs Task 6, Task 8 needs Task 6.

AR1.2 moved the engine to `Client/src/engine` (PR 466) before Tasks 3 to 5 began, and AR4.1 has been moving the game
modules to `Client/src/game` since. The paths in the tasks below are from before those moves: the player is now in
`engine/Audio`, and each task takes its files from `main` when it starts. Task 5 lands next to AR4.1's work on
`game/Sound` (layer 8, as `game/Music` will be).

| Task | What | Needs |
| ---: | --- | --- |
| 1 | this plan | |
| 2 | the behavior golden hears the audio | |
| 3 | rename the music files to `MusicPlayer` | |
| 4 | the music player gets its own port | 3 |
| 5 | MU's music leaves the engine: `game/Music` | 4, PRs 456 and 457 |
| 6 | every track named from `game/Music` | 5 |
| 7 | the siege chooses its music in its own step | 6 |
| 8 | the checks see the tracks and the event loops | 6 |

### Task 1: the plan

This PR. Title: `docs(refactor): plan one home for MU's music`.

**Files:**
- Create: `docs/superpowers/plans/2026-11-25-music-home.md`
- Modify: `docs/superpowers/specs/2026-11-20-client-architecture-design.md` (the tree, the module count and the rows
  of `game/Maps` and `game/Music`), `docs/superpowers/plans/2026-11-23-named-values.md` (Task 3's music row, Task 8),
  `docs/superpowers/plans/2026-11-22-ar1-engine-apart.md` (Task 5b's music row)

- [x] **Step 1:** write the findings and the tasks.
- [x] **Step 2:** point the spec, the named-values plan and the AR1 plan at this document.

### Task 2: the behavior golden hears the audio

Title: `test(characterization): let the behavior golden hear the music and the sounds`, branch
`test/characterization-audio`. Skills: `superpowers:test-driven-development`.

**Files:**
- Modify: `Client/tests/characterization/test_characterization.cpp`, `Client/tests/characterization/README.md`
- Create: `Client/tests/characterization/golden/<Scene>.audio.tsv` for the five scenes (`IdleKeepalive` came after
  this plan)

**Interfaces:**
- A recording device in the harness implements `Audio::IPlayback` and notes one row per call of the interface:
  `frame`, `event` (`play-music`, `stop-music`, `play-sound`, `stop-sound`, `volume`, `listener`, `register`,
  `release`), `value` (the path, or the id in hex with its flags). `runtime.audio` points at it.
- The five existing goldens must not change: with a device the client takes the branches that call it, and none of
  them draws, sends, calls `rand()` or changes the world summary. The PR shows the five comparisons passing before the
  new files are written; a difference stops the task and is reported.
- `MU_CHARACTERIZATION_WRITE=1` also writes the audio files; `MU_CHARACTERIZATION_TRACE=1` prints the audio rows.

- [x] **Step 1:** add the device and the comparison, with the new golden files missing; run and see the test fail
  on the missing file.
- [x] **Step 2:** write the files with `MU_CHARACTERIZATION_WRITE=1`; check by hand that `LoginToLorencia` has
  `main_theme` at the login and that `EventEntry` stops it on map 9 (`map != 0` stops `Pub` and `main_theme`).
- [x] **Step 3:** run `test_characterization` (CI runs the rest); the five `.tsv` digests unchanged.

### Task 3: rename the music files to `MusicPlayer`

Title: `refactor(AR1.1): rename the music files to MusicPlayer`, branch `refactor/ar1.1-music-player-files`.
Skills: `muret-move`.

**Files:**
- Rename: `Client/src/Audio/Music.{h,cpp}` → `Client/src/Audio/MusicPlayer.{h,cpp}`;
  `Client/tests/audio/test_music.cpp` → `Client/tests/audio/test_music_player.cpp`
- Modify: the include lines that name them (`Scenes/WorldRuntime.h`, `Scenes/LoginScene.cpp`,
  `Scenes/CrywolfMusic.cpp`, the tests), the map's file column

- [ ] **Step 1:** `git mv` and the include lines only; the class keeps its name until Task 4.
- [ ] **Step 2:** `check_pure_move.py` and `check_layers.py` pass; the build and the audio tests pass.

### Task 4: the music player gets its own port

Title: `refactor(AR1.1): give the music player its own port`, branch `refactor/ar1.1-music-stream`.
Skills: `refactoring` (Extract Interface, Remove Parameter), `cpp-coding-standards` (I.25, C.121),
`superpowers:test-driven-development`.

**Files:**
- Create: `Client/src/Audio/IMusicStream.h`
- Modify: `Client/src/Audio/MusicPlayer.{h,cpp}`, `Client/src/Audio/Playback.h`, `Client/src/Platform/SdlAudio.{h,cpp}`,
  `Client/src/Scenes/WorldRuntime.h`, `Client/src/Scenes/CrywolfMusic.cpp`, `Client/src/Scenes/LoginScene.cpp`,
  `Client/src/Scenes/GameScene.cpp`, `Client/src/Scenes/ReturnToSelection.cpp`, `Client/src/Scenes/WinMainLoop.cpp`,
  `Client/src/main.cpp`, the 10 test doubles, `Client/tests/characterization/test_characterization.cpp`,
  `Client/docs/research/coverage/module-map.tsv`

**Interfaces:**

```cpp
namespace Client::Audio
{
// What the client asks of wzAudio.dll while it runs: wzAudioPlay, wzAudioStop and wzAudioGetStreamOffsetRange.
class IMusicStream
{
public:
    virtual ~IMusicStream() = default;
    virtual void Play(std::string_view relativePath) = 0;
    virtual void Stop() = 0;
    [[nodiscard]] virtual int ProgressPercent() const = 0;
};

class MusicPlayer
{
public:
    void Attach(IMusicStream* stream);
    // 0x4acfd0
    void Play(std::string_view relativePath);
    // 0x4acf60
    void Stop(std::string_view relativePath);
    // 0x4ad060
    [[nodiscard]] bool ReachedTheEnd() const;
    void SetEnabled(bool enabled);
    void SetBlocked(bool blocked);
    [[nodiscard]] std::string_view Current() const;
    // StepWorld and StopWorld unchanged in body until Task 5.
};
} // namespace Client::Audio
```

- `IPlayback` loses `PlayMusic`, `StopMusic` and `MusicProgressPercent`; `SdlAudio` implements both interfaces.
- `main.cpp` attaches the stream where it sets `runtime.audio` today; `WorldRuntime::music` becomes
  `Audio::MusicPlayer`. Crywolf's `PlayToItsEnd` asks `ReachedTheEnd()` and its `TrackEnd` goes.
- With no stream, `Play` and `Stop` change nothing and `ReachedTheEnd()` is false, as today with a null playback.
- If thin-main Task 3 (`App::GameClient`) lands first, the attach goes where it composes the audio.

- [ ] **Step 1:** write the failing tests: the player with a recording stream (the cases of today's test, without
  `force`), `ReachedTheEnd()` true only at 100, nothing recorded with no stream.
- [ ] **Step 2:** extract the port, rename the class, drop `force` and `loop`; move each caller.
- [ ] **Step 3:** the 7 effect-only doubles lose their empty music methods; the 3 music doubles implement the stream.
- [ ] **Step 4:** the whole suite and the four goldens, and Task 2's audio files, unchanged.

### Task 5: MU's music leaves the engine

Title: `refactor(AR1.1): move MU's music out of the engine`, branch `refactor/ar1.1-music-module`.
Skills: `muret-module`, `refactoring` (Move Function), `clean-architecture`, `superpowers:test-driven-development`.

**Files:**
- Create: `Client/src/Music/Tracks.h`, `Client/src/Music/MapMusic.h`, `Client/src/Music/MapMusic.cpp`,
  `Client/tests/music/CMakeLists.txt`, `Client/tests/music/test_tracks.cpp`, `Client/tests/music/test_map_music.cpp`
- Modify: `Client/src/Audio/MusicPlayer.{h,cpp}` (loses `WorldTracks`, `StepWorld`, `StopWorld`),
  `Client/src/Scenes/WinMainLoop.cpp`, `Client/src/Scenes/ReturnToSelection.cpp`, `Client/tests/CMakeLists.txt`,
  `Client/tests/audio/test_music_player.cpp` (loses the map cases), `Client/modules.tsv` (row `game/Music`, layer 8,
  folder `Music/**`, `may_use` `*layer<8`), `Client/docs/research/coverage/module-map.tsv`, `docs/CODING_RULES.md` §5
  (layer 8 and the count), `docs/adr/0023-client-module-architecture.md`, `.claude/skills/muret-module/SKILL.md`
  (the count), the AR1 plan and the named-values plan (the music row done)

**Interfaces:**

```cpp
namespace Client::Music
{
// The slots of the original's track table, 0x6d5dcc..0x6d5e20, in its order.
enum class Track : std::uint8_t
{
    Pub, MuTheme, Church, Devias, Noria, Dungeon, Atlans, Icarus, Tarkan, LostTowerA, LostTowerB, Kalima, Castle,
    Charge, LastEnd, HuntingGround, Aida, Crywolf1st, CrywolfReady, CrywolfBefore, CrywolfBack, MainTheme
};
inline constexpr std::array<std::string_view, 22> TrackPaths{/* "Music\\Pub.mp3" ... "Music\\main_theme.mp3" */};
[[nodiscard]] constexpr std::string_view PathOf(Track track);
void Play(Audio::MusicPlayer& player, Track track);
void Stop(Audio::MusicPlayer& player, Track track);

// 0x6388dd..0x639413, the music block of 0x637700: the 11 groups in their order. inTheDeviasChurch runs only on map 2
// in the safe zone.
void StepTheMapMusic(Audio::MusicPlayer& player, std::int32_t map, bool safeZone, std::int32_t heroCellLayer,
                     const std::function<bool()>& inTheDeviasChurch);
// 0x622b00: 0x4acf60 over the 22 slots.
void StopTheWorldMusic(Audio::MusicPlayer& player);
} // namespace Client::Music
```

- Map names from `Maps/MapId.h`: `LorenciaMap`, `DungeonMap`, `ExileMap`, `DeviasMap`, `NoriaMap`, `AtlansMap`,
  `IcarusMap`, `TarkanMap`, `LostTowerMap`, `LandOfTrialsMap`, `AidaMap`, and `IsKalimaMap`.
- `MusicPlayer` keeps `Attach`, `Play`, `Stop`, `ReachedTheEnd`, `SetEnabled`, `SetBlocked` and `Current`.
- No CMake target yet (AR4); the files join `ClientLib` by glob.

- [ ] **Step 1:** commit a test that runs `MusicPlayer::StepWorld` and `Music::StepTheMapMusic` over every map 0..255,
  safe 0 and 1, cell layers 0..255 on map 0 and 0 and 4 on the others, and a church test that answers true or false
  and counts its calls, from every current track of the table and from none, and compares the event lists and the
  counts. CI passes it.
- [ ] **Step 2:** `Music::StopTheWorldMusic` against `MusicPlayer::StopWorld` from each of the 22 tracks and from
  none, in the same commit.
- [ ] **Step 3:** switch the two callers, delete the engine's copies and the comparison tests; the per-group tests
  move to `tests/music`, with `test_tracks.cpp` pinning the 22 paths in slot order.
- [ ] **Step 4:** add the module row and its lists; `check_layers.py` prints `0 new`; `--clean-root engine` finds no
  music; the whole suite, the goldens and Task 2's audio files unchanged.

### Task 6: every track named from `game/Music`

Title: `refactor(music): name every track from its home`, branch `refactor/music-track-names`.

**Files:**
- Modify: `Client/src/Scenes/CrywolfMusic.cpp` (the four private paths become roles initialized from `Track`),
  `Client/src/Scenes/LoginScene.cpp` (`0x634c31`, `0x64140d`, `0x64141b`, `0x63fbbd`, `0x63fbca`),
  `Client/src/Scenes/GameScene.cpp` (`castle`, `charge`)

- [ ] **Step 1:** replace each literal by the track, keeping order and citations; add the citations the login and
  credits sites lack.
- [ ] **Step 2:** `grep -rn '"Music\\\\' Client/src` finds only `Music/Tracks.h`; tests and goldens unchanged.

### Task 7: the siege chooses its music in its own step

Title: `refactor(castle-siege): choose the siege's music in 0x42b540's step`, branch `refactor/castle-siege-music`.
Skills: `refactoring` (Inline Function, Remove Parameter).

**Files:**
- Modify: `Client/src/World/CastleSiegeObjectStep.{h,cpp}` (`CastleSiegePrelude::updateMusic` becomes
  `Audio::MusicPlayer* music`; the choice of `0x42b5a6`/`0x42b5c3` is written in `StepCastleSiegePrelude`),
  `Client/src/Scenes/GameScene.cpp` (the lambda goes), `Client/tests/world/test_castle_siege_objects.cpp`,
  `Client/tests/world/test_castle_siege_prelude.cpp`

- [ ] **Step 1:** the tests of the prelude record the player's stream: on the clock's turn, with the siege, `castle`
  stopped and `charge` played, and the reverse without it; nothing between turns.
- [ ] **Step 2:** move the choice; `siegeMusicTime` stays in `WorldRuntime` until AR3.1.

### Task 8: the checks see the tracks and the event loops

Title: `build(music): check that each track and event loop is named once`, branch `build/music-named-values`.

**Files:**
- Modify: `Client/tools/architecture/check_named_values.py`, `Client/tools/architecture/test_check_named_values.py`,
  `Client/tools/architecture/named-values.tsv`, `Client/tools/architecture/named-values-baseline.tsv`,
  `docs/CODING_RULES.md` §7 "Named values" (the homes list gains `game/Music`)

- [ ] **Step 1:** a space can match string literals (`literal=<regex>` in `match`); the check reads the literals that
  `code_of` blanks today.
- [ ] **Step 2:** space `music-track`, home `game/Music`, `literal=^Music\\\\`, policy `home-only`: no baseline row
  after Task 6.
- [ ] **Step 3:** the `sound` space also matches names ending in `Music`, so `BloodCastleMusic` (`0x8b`),
  `ChaosCastleMusic` (`0x99`) and `ChaosCastleWaitMusic` (`0x9e`) join the baseline beside their `...Sound` twins
  until named-values Task 8 gives them one home in `Sound/SoundIds.h`; the bare `0x99` and `0x9e` of
  `Scenes/WorldTransitions.cpp` are listed in that task.
