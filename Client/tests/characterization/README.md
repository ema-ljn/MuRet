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
MURET_CHARACTERIZATION_WRITE=1 ./Client/tests/characterization/test_characterization
MURET_CHARACTERIZATION_TRACE=1 ./Client/tests/characterization/test_characterization --gtest_filter=*LoginToLorencia
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
answer.

## What the harness changes from `main.cpp`

Fixed seeds instead of the time and `std::random_device`, a fake socket with the identity obfuscation and the test
keys, a fake clock, the recording frame target instead of GL and a 6 × 13 text measure instead of FreeType.
`AutoLogin` chooses the first server and sends the account, as with `--auto-login`. It leaves out the audio device,
the clipboard, the friend chat rooms, SDL's typed-text switch and the screenshots. When AR1 moves the composition
root out of `main.cpp`, the harness uses it instead of its copy.
