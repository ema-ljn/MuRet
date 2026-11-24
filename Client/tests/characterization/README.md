# The behavior golden

The safety net of the client refactor ([plan](../../../docs/superpowers/plans/2026-11-20-client-architecture-refactor.md),
AR0.3). Each scene in `scenes/` runs the whole client frame by frame, composed as `RunScenes` in `main.cpp` composes
it, and the `FrameDigest` of every frame must match `golden/<scene>.tsv` on Linux and on Windows. A digest has four
hashes: what the frame drew, the bytes the client sent, the CRT `rand()` state and a summary of the world (the map,
the living actors, the own life, mana and zen, the open panels and the chat lines).

A refactor PR never changes a golden: a difference is a refactor bug. A port PR that changes behavior on purpose
writes the golden again and says so in its description.

```bash
# From the build folder: compare, write the goldens again, or print what each frame did.
./Client/tests/characterization/test_characterization
MU_CHARACTERIZATION_WRITE=1 ./Client/tests/characterization/test_characterization
MU_CHARACTERIZATION_TRACE=1 ./Client/tests/characterization/test_characterization --gtest_filter=*LoginToLorencia
```

## A scene

`scenes/<Name>.tsv` has the columns `frame`, `event` and `value`, with frames going up, `#` lines as comments and an
`end` row on the last frame. The test `CharacterizationTest.<Name>` runs it. Each frame applies its rows, then runs
the network, one step, the draw and the end of the loop, and moves the clock 40 ms.

| Event | Value |
| --- | --- |
| `gs` | a packet from the server in hex, as it arrives (C1/C2) |
| `gs-encrypted` | a C1/C2 in hex that the harness encrypts into a C3/C4 with the GameServer's key, as the server does |
| `mouse` | `x y`, in pixels from the top left of the 640 × 480 screen |
| `button-down`, `button-up` | `left`, `right` or `middle` |
| `key-down`, `key-up` | a key: `A`..`Z`, `0`..`9`, `F1`..`F5`, `Enter`, `Escape`, `Space`, `Tab`, the arrows… |
| `text` | typed text |
| `end` | nothing: the scene's last frame |

The trace prints, for each frame, the scene, the session state and the packets the client sent, which says when to
answer. In the game it also prints the world summary that the digest hashes: the map, the own life, mana and zen, the
open panels, each living actor with its cell and action, and the newest chat line.

Things that make a click or a packet do nothing: the mouse picks one frame late, so it moves before the click frame;
a dropped item cannot be picked up on the frames right after the drop; an NPC click misses while the inventory is open,
because the view moves; and the client takes some packets only encrypted (`0x16`, `0x19`, `0x1C`, `0x24`, `0x30`,
`0x36`…), answering a plain one with `F1 03`.

## The scenes

| Scene | What it does |
| --- | --- |
| `LoginToLorencia` | the ConnectServer and GameServer login, the character list and the world entry in Lorencia |
| `WalkAndFight` | a walk, a skill on a spider that dies and the pick-up of its drop |
| `InventoryShopAndChat` | an item moved in the inventory, a potion bought from Amy's shop and a whisper |
| `EventEntry` | the Devil Square notice, Charon's window, the entry with an invitation and the countdown on map 9 |

## What the harness changes from `main.cpp`

Fixed seeds instead of the time and `std::random_device`, a fake socket with the identity obfuscation and the test
keys, a fake clock, the recording frame target instead of GL and a 6 × 13 text measure instead of FreeType.
`AutoLogin` chooses the first server and sends the account, as with `--auto-login`. It leaves out the audio device,
the clipboard, the friend chat rooms, SDL's typed-text switch and the screenshots. The harness is a copy of that
composition until AR1.4 gives `main.cpp` and the harness one to share, `App::GameClient`
([plan](../../../docs/superpowers/plans/2026-11-24-thin-main.md), Task 3).
