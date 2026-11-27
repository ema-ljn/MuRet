# The behavior golden

The safety net of the client refactor ([plan](../../../docs/superpowers/plans/2026-11-20-client-architecture-refactor.md),
AR0.3). Each scene in `scenes/` runs the whole client, `App::GameClient`, frame by frame, and the `FrameDigest` of
every frame must match `golden/<scene>.tsv` on Linux and on Windows. A digest has four hashes: what the frame drew,
the bytes the client sent, the CRT `rand()` state and a summary of the world (the map, the living actors, the own life,
mana and zen, the open panels and the chat lines). Every call the client makes to the audio device must match
`golden/<scene>.audio.tsv`, in order ([The audio](#the-audio)).

A refactor PR never changes a golden: a difference is a refactor bug. A port PR that changes behavior on purpose
writes the golden again and says so in its description.

```bash
# From the build folder: compare, write the goldens (both files of each scene) again, or print what each frame did.
./bin/test_characterization
MU_CHARACTERIZATION_WRITE=1 ./bin/test_characterization
MU_CHARACTERIZATION_TRACE=1 ./bin/test_characterization --gtest_filter=*LoginToLorencia
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
| `key-down`, `key-up` | a key: `A`..`Z`, `0`..`9`, `F1`..`F5`, `Enter`, `Escape`, `Space`, `Tab`, `PrintScreen`, the arrows… |
| `text` | typed text |
| `end` | nothing: the scene's last frame |

The trace prints, for each frame, the scene, the session state, the packets the client sent, which says when to
answer, and the calls to the audio device. In the game it also prints the world summary that the digest hashes: the
map, the own life, mana and zen, the open panels, each living actor with its cell and action, and the newest chat
line.

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
| `IdleKeepalive` | the login of `LoginToLorencia`, then idle in Lorencia until the first keepalive in the world, with the own character's speeds (frame 500, 20 seconds after the session started) |

## The audio

`golden/<scene>.audio.tsv` has one row per call to `Audio::IPlayback`, with the columns `frame` (`start` before the
first frame: the volume the client sets when it is built), `event` and `value`:

| Event | Value |
| --- | --- |
| `play-music` | the track's path (`Music\main_theme.mp3`), and `loop` if asked |
| `stop-music` | nothing |
| `play-sound` | the sound id in hex, `loop` if asked, and `at x y z` if it has an origin |
| `stop-sound` | the sound id in hex, and `rewind` if asked |
| `volume` | the effects' level |
| `listener` | `x y z` |
| `register` | the sound id in hex and its file |
| `release` | nothing: the world's sounds are freed |

Most rows are sounds the client asks for again every frame, as the original does: on map 54, the character scene's
world, `0x4250f0` asks for `0x246` once per object it steps (191 a frame, `0x4251fb`). The digests do not change
with the device: no branch it opens draws, sends, calls `rand()` or changes the world summary.

`CharacterizationTest.PrintScreenReadsTheScreenOnce` is not a golden scene, because the screenshot is a file: it
presses Print Screen in the login scene and expects one read of the screen and the JPEG in a temporary folder.

## The devices the harness gives `GameClient`

- Fixed seeds instead of the time and `std::random_device`, and a fixed local time for the screenshot's name.
- A fake socket with the identity obfuscation and the test keys, and `AutoLogin` with an account, as with
  `--auto-login`: it chooses the first server and sends the account.
- A fake clock, the recording frame target instead of GL, a fake texture store, a 6 × 13 text measure instead of
  FreeType and a screen reader of black pixels that counts its reads.
- A recording audio device instead of SDL3_mixer (`RecordingPlayback`), which only writes down each call.
- No clipboard or friend chat rooms. SDL's typed-text switch stays in `main.cpp`.
