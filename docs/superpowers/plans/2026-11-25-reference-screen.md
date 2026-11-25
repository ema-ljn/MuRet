# The 640 x 480 reference screen: findings and plan for one home

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** the 640 x 480 screen `main.exe` lays its 2D on is named once, and every conversion between that screen and
the window's pixels is a named function of one home, so no file spells `Width640` or `/ 640` again.

**Architecture:** `main.exe` keeps the window's real size in two globals and converts with four integer counts and one
x87 count, each written inline where it is needed. MuRet copies each site faithfully and names the number again in
each file: 32 private definitions under 8 name families and 6 types. The plan gives the number and the five counts one
home in `engine/Render`, `Render/ReferenceScreen.h` (today `Render/ScreenScale.h`, which already holds the x87 count),
and moves each site to it only after its ASM was read. A site whose count is its own keeps it and takes the number
from the home.

**Tech Stack:** C++20, GoogleTest, the behavior golden (`Client/tests/characterization`), `check_named_values.py`,
`check_pure_move.py`, `check_layers.py`, `module-map.tsv`.

**Spec:** [`CODING_RULES.md`](../../CODING_RULES.md) §7 "Named values" (rules 1, 2, 3, 4, 6 and 7) and §4; the
refactor design [`2026-11-20-client-architecture-design.md`](../specs/2026-11-20-client-architecture-design.md)
(`engine/Render`, layer 4); the named-values plan [`2026-11-23-named-values.md`](2026-11-23-named-values.md); the AR1
plan [`2026-11-22-ar1-engine-apart.md`](2026-11-22-ar1-engine-apart.md) (Task 4's rename precedent, Tasks 5 to 5e).

Measured on `main` 57e08e8 (2026-11-25); line numbers are that commit's, and PRs 456 and 457 (open) shift some. The
original was read in the research repository's ASM of `main.exe` c32bbd5f
(`client-research/mainexe-vmxp-c32bbd5f/decompiled/functions/`). An address in a table below was read there for this
plan unless the row says "to read".

## The answer in short

- **One number, 32 private names.** `Width640`, `DesignWidth`, `VirtualWidth`, `ScreenWidth`, `OriginalWidth`,
  `InverseOf640`, `WorldWidthFactor` and `BarDesignWidth` are the same 640 in 19 files, as `int`, `std::int32_t`,
  `std::uint32_t`, `std::int64_t`, `float` and `double`. Eight more lines convert with a bare `640` or `480`. The
  default screen `Core::PixelSize{640, 480}` is written 29 times, and the ratio has no name: 32 calls spell it
  `ScaleX(1.0f, screen)`.
- **The original has five counts, not one.** Reference units to pixels in the x87 (`0x620d50`, `0x620d80`), and four
  integer counts: to pixels unsigned, to pixels signed, to reference units unsigned (`DIV`), to reference units signed
  (`IDIV`). MuRet has every one of them in two files or more, so each gets one function (§4: two places, one helper).
- **The same count is ported three ways today.** The unsigned `(W * x) / 640` is `std::uint32_t` in
  `Scenes/GameProjection.cpp:79`, `std::int64_t` in `Scenes/GameScene.cpp:985` and signed `int` in
  `Render/Pass3D.cpp:38`. All three agree for every value that reaches them. One function ends the question.
- **The word is Reference** (`ReferenceWidth`, `ReferenceHeight`, `ReferenceScreen`, "reference units"), the home is
  `engine/Render`, and the conversions are functions. Emanuel decided the three on 2026-11-25.
- **640 x 480 is the engine's, not the game's.** It is the unit the original's 2D primitives are written in
  (`0x620d50`, `0x621b60`), so naming it in `engine/Render` does not break rule 7. A window that happens to be
  640 pixels wide is another space and keeps its code.
- **It is the first step toward more resolutions.** After Task 4 no site holds a conversion, so the policy for
  screens that are not 4:3 (Emanuel chose anchors to the edges, like MuMain) is written against the home's functions
  and not against each site. That plan is separate (see "More resolutions later").

## What `main.exe` does

### The real size and the ratios

| Piece | Address | What it is |
| --- | --- | --- |
| width, height | `0x6d5d60`, `0x6d5d64` | the window's real size, written by `0x4b90a0` (`0x4b93c4..0x4b9426`) from the registry's five resolutions; read in 75 and 39 functions |
| inverse of 640 | `0x6ba070` | `1/640` as a float (`0x3acccccd`), not exact |
| inverse of 480 | `0x6ba06c` | `1/480` as a float (`0x3b088889`), not exact |
| ratio x | `0x5762ea8` | `W` times the float at `0x6ba070`, computed once (`0x4b9441..0x4b9458`: `FILD`, `FMUL`, `FSTP`); 236 reads in 71 functions, 102 of them `FDIV` and 26 `FMUL` |
| ratio y | `0x5762eac` | `H` times the float at `0x6ba06c` (`0x4b945e..0x4b9467`); 114 reads in 50 functions |

Because the inverses are not exact, the ratios are not `W / 640` and `H / 480`: at 800 x 600 the y ratio is
`0x3fa00001`, one bit above 1.25, and at 1024 x 768 it is `0x3fccccce` while the x ratio is `0x3fcccccd`.

### The five counts

| Count | Instructions | Read at | In the binary |
| --- | --- | --- | --- |
| reference units to pixels, x87 | `FILD` size, `FMUL` coordinate, `FMUL` inverse | `0x620d50`, `0x620d80` | 8 call sites each, in 4 functions; the ratios above do the same with the coordinate 1 |
| reference units to pixels, unsigned | 32-bit `IMUL` by the size, `MUL` by `0xcccccccd` and `SHR EDX, 9` (640) or by `0x88888889` and `SHR EDX, 8` (480) | `0x620486..0x6204a8`, `0x620db3..0x620dfa` | about 94 sites in 34 functions by 640, about 6 by 480 |
| reference units to pixels, signed | 32-bit `IMUL` by the size, `IMUL` by `0x66666667` (640) or `0x88888889` (480), `SAR EDX, 8` and the sign bit added: toward zero | `0x5562dc..0x55631c`, `0x45214b..0x4521b1`, `0x452ee3..0x452eff` | about 5 sites by 640, about 4 by 480 |
| pixels to reference units, unsigned | times 640 or 480 in 32 bits (`IMUL` by the immediate, or `LEA` and `SHL`), `DIV` by the size | `0x4b3597..0x4b359f`, `0x5b8b3c..0x5b8b45`, `0x5f3148..0x5f314e` | 62 `DIV` by the width global and 7 by the height global, more through a register (`0x448603`) |
| pixels to reference units, signed | the same product, `CDQ` and `IDIV` by the size | `0x620610..0x620626`, `0x5a90e1..0x5a90e2`, `0x5a916c..0x5a91bb` | 2 `IDIV` by the width global and 3 by the height global, more through a register (`0x5a916d`) |

The products wrap at 32 bits in all four integer counts. With a negative coordinate the signed and the unsigned
counts differ: `-1` at 1024 pixels gives `-1` signed and 6710884 unsigned.

### What is not the reference screen

- **A real window width.** Four functions switch on `0x6d5d60` being 640, 800, 1024 or 1280: `0x448580`, `0x448fa0`,
  `0x5b0550` and `0x4b9b80`. In MuRet: `Scenes/GameItemHelp.cpp:110,132`, `Scenes/GameMoveWindow.cpp:82`,
  `Render/Text/FontSet.h`, `App/Resolution.h:27`, and the default window of `Platform/Window.h:25` and
  `App/Config.h:77` (the registry's resolution 0).
- **A second design screen, 800 x 600.** `0x65eeb0` multiplies the real size by `1/800` and `1/600` as floats
  (`0x65eeed`, `0x65eef9`; `0x6ba884`, `0x6ba880`) for the images of the loading screens and the credits. MuRet has
  the function (`Render::DesignScale`, `Render/UiImage.h:77`) and names the size three times
  (`Scenes/LoadingScene.cpp:29`, `Scenes/WorldLoadingScene.cpp:43`, `UI/CreditsWindow.cpp:56`). Two spaces, two
  names (rule 3): this plan leaves it, see "Found on the way".
- **Other numbers worth 640 or 480:** a sound id (`BurstSound`, `Scenes/MonsterAttackStep.cpp:566`), a text line
  (`Scenes/GuildReceive.cpp:61`), a coordinate (`ItemLeft`, `Scenes/QuestWindowDrawing.cpp:104`).

## What MuRet has today

`Render/ScreenScale.{h,cpp}` (`engine/Render`) holds `ScaleX` and `ScaleY`, the x87 count, with the two inverses
private in the `.cpp`. 23 files of `Client/src` include it, 3 tests and one lab. Everything else is local:

| What | Count | Where |
| --- | ---: | --- |
| private definitions of the number | 32 in 19 files | `DesignWidth/Height` 10, `Width640/Height480` 7, `VirtualWidth/Height` 6, `ScreenWidth/Height` 3, `OriginalWidth/Height` 2, `InverseOf640/480` 2, `WorldWidthFactor` 1, `BarDesignWidth` 1 |
| bare `640` or `480` in a conversion | 8 lines in 7 files | `Render/Text/TextLayout.cpp:58,77,84`, `Scenes/GameMoveWindow.cpp:99`, `Scenes/GameInventoryHud.cpp:449`, `Scenes/GameUi.cpp:1980`, `Scenes/GameCharacterHud.cpp:168`, `UI/MessageArea.cpp:243` |
| `Core::PixelSize{640, 480}` as "no screen yet" | 29 in 25 files | member defaults of the scene headers and `resources.screen ? resources.screen() : …` |
| the ratio spelled `ScaleX(1.0f, screen)` or `ScaleY(1.0f, screen)` | 18 and 14, in 19 files | `Render/Text/TextLayout.cpp`, 12 files of `Scenes/`, 6 of `UI/` |
| local helpers that repeat a count | 5 | `Scaled` (`Scenes/ShopSigns.cpp:101`, identical in `Scenes/ChatBubbleDraw.cpp:109`), `BoxWidth` (`Scenes/GameWindowDrawing.cpp:165`) and `BoxPixels` (`Scenes/GameCharacterHud.cpp:165`), `ScaledTo` (`UI/GameCursor.cpp:34`), `Shrink` (`Render/Text/TextLayout.cpp:30`) |
| role names worth 640 | 5 | `GaugeRight`, `Panel2000Right`, `WindowRight`, `FullWorldWidth`, `SelectionWorldWidth` |
| a function named after the number | 1 | `UI::MouseIn640x480` (`UI/GameCursor.h:26`), 8 files and one test |

## What popular clients do

Fourteen projects were read at the commit given. Several let the GPU stretch a finished frame, which MuRet cannot
copy: the original's results are compared bit by bit and it computes them on the CPU. What transfers is how the code
is organized.

| Project | Commit | The size, named once | The conversion |
| --- | --- | --- | --- |
| ioquake/ioq3 | 52088b9e | `SCREEN_WIDTH`, `SCREEN_HEIGHT` (`code/qcommon/q_shared.h:374`) | `cgs.screenXScale` computed once; `CG_AdjustFrom640` (`code/cgame/cg_drawtools.c:33`) |
| ValveSoftware/halflife | b1b5cf58 | `ScreenWidth`, `ScreenHeight` (`cl_dll/cl_util.h:70`) | the macros `XRES`, `YRES` |
| ValveSoftware/source-sdk-2013 | 3e957b70 | `GetProportionalBase` (`ISurface.h:297`) | `GetProportionalScaledValue`, `GetProportionalNormalizedValue` (`IScheme.h:111`) |
| FWGS/mainui_cpp | 93e89c78 | 1024 x 768 in `uiStatic` | `uiStatic.scaleX` (`BaseMenu.cpp:1096`), `Point::Scale()` |
| sezero/quakespasm | 13f16f2b | one canvas per HUD area | `GL_SetCanvas` (`Quake/gl_draw.c:680`) |
| ZDoom/gzdoom | c26ce2e6 | `DTA_VirtualWidth`, `DTA_320x200` (`src/common/2d/v_draw.h:59`) | the draw call takes the virtual size |
| chocolate-doom/chocolate-doom | a2dfd51f | `SCREENWIDTH`, `SCREENHEIGHT` (`src/i_video.h:27`) | the game draws at that size; one blit scales |
| scummvm/scummvm | 16159a2a | the game's own size | `convertVirtualToWindow`, `convertWindowToVirtual` (`backends/graphics/windowed.h:228,270`) |
| OpenRCT2/OpenRCT2 | 28bb5e55 | `ScreenSize`, `ScreenCoordsXY` (types) | `windowScale`; the mouse is divided once, at the edge (`UiContext.cpp:402`) |
| OpenTTD/OpenTTD | 0bfa0a36 | sizes written for the unscaled interface | `ScaleGUITrad` (`src/zoom_func.h:117`) |
| OpenMW/openmw | 7161a352 | the layout's size | `ScalingLayer` (`components/myguiplatform/scalinglayer.hpp:9`) |
| diasurgical/devilutionX | e1c37777 | `UISize{640, 480}` (`Source/utils/display.cpp:140`) | `GetUIRectangle()` |
| ihhub/fheroes2 | 4a8d8db5 | `Display::DEFAULT_WIDTH`, `DEFAULT_HEIGHT` (`src/engine/screen.h:197`) | no scale: panels are placed from the display's size |
| sven-n/MuMain | 6fd2d519 | `REFERENCE_WIDTH`, `REFERENCE_HEIGHT` (`src/source/App/stdafx.h:210`) | `UITransform` (`src/source/UI/Scaling/UITransform.h`) |

What they share: the size has one name; the real size has one owner and the ratio is computed once; a conversion is a
named function per direction; the mouse is converted with the same function that draws.

## The home

`Render/ReferenceScreen.h`, module `engine/Render`, namespace `Client::Render`. Coordinates on the 640 x 480 screen
are "reference units"; the window's are "pixels".

```cpp
// main.exe lays out its 2D on a 640 x 480 screen and stretches each axis to the window.
inline constexpr int ReferenceWidth = 640;
inline constexpr int ReferenceHeight = 480;
inline constexpr Core::PixelSize ReferenceScreen{ReferenceWidth, ReferenceHeight};

// 0x6ba070 and 0x6ba06c: the inverses main.exe multiplies by. Neither is exact.
inline constexpr float InverseOfReferenceWidth = 1.0f / ReferenceWidth;
inline constexpr float InverseOfReferenceHeight = 1.0f / ReferenceHeight;

// Reference units to pixels.
[[nodiscard]] float ScaleX(float x, Core::PixelSize screen); // 0x620d50
[[nodiscard]] float ScaleY(float y, Core::PixelSize screen); // 0x620d80
[[nodiscard]] float RatioX(Core::PixelSize screen);          // 0x4b9441..0x4b9458: the float kept at 0x5762ea8
[[nodiscard]] float RatioY(Core::PixelSize screen);          // 0x4b945e..0x4b9467: the float kept at 0x5762eac
// 0x620486 and 0x620499: the unsigned count.
[[nodiscard]] constexpr std::uint32_t PixelsFromReferenceX(std::uint32_t x, Core::PixelSize screen);
[[nodiscard]] constexpr std::uint32_t PixelsFromReferenceY(std::uint32_t y, Core::PixelSize screen);
// 0x5562dc and 0x5562f7: the signed count, toward zero.
[[nodiscard]] constexpr std::int32_t SignedPixelsFromReferenceX(std::int32_t x, Core::PixelSize screen);
[[nodiscard]] constexpr std::int32_t SignedPixelsFromReferenceY(std::int32_t y, Core::PixelSize screen);

// Pixels to reference units. A screen without a positive size gives 0: main.exe would divide by zero.
// 0x4b3597 and 0x4b35ba: DIV.
[[nodiscard]] constexpr std::uint32_t ReferenceFromPixelsX(std::uint32_t pixels, Core::PixelSize screen);
[[nodiscard]] constexpr std::uint32_t ReferenceFromPixelsY(std::uint32_t pixels, Core::PixelSize screen);
// 0x620610 and 0x620625: IDIV.
[[nodiscard]] constexpr std::int32_t SignedReferenceFromPixelsX(std::int32_t pixels, Core::PixelSize screen);
[[nodiscard]] constexpr std::int32_t SignedReferenceFromPixelsY(std::int32_t pixels, Core::PixelSize screen);
```

What each computes, with `W` and `H` the fields of `screen`:

| Function | Count |
| --- | --- |
| `ScaleX`, `ScaleY` | as today: `double(W) * double(x) * double(InverseOfReferenceWidth)`, stored as `float` |
| `RatioX`, `RatioY` | `double(W) * double(InverseOfReferenceWidth)`, stored as `float`; the same bits as `ScaleX(1.0f, screen)` |
| `PixelsFromReferenceX` | `std::uint32_t(W) * x / 640u`: the product wraps, the division is unsigned |
| `SignedPixelsFromReferenceX` | the same wrapped product read as `std::int32_t`, divided by 640 toward zero |
| `ReferenceFromPixelsX` | `pixels * 640u / std::uint32_t(W)`: the product wraps, the division is unsigned; 0 when `W <= 0` |
| `SignedReferenceFromPixelsX` | the same wrapped product read as `std::int32_t`, divided by `W` toward zero; 0 when `W <= 0` |

The `Y` functions do the same with `H` and 480. The names differ by signedness instead of overloading, so an
implicit conversion cannot choose the count.

**The parameter.** Every function takes the size the reference screen is stretched over. Today that is the window's
(`window.PixelSize()`, passed down as `screen`), so no result changes. It is where the resolutions plan hooks in:
with anchors, each panel passes the rectangle its anchor fits the reference screen to, so the parameter gains an
origin there. This plan does not add it (§4: no abstraction for a hypothetical future).

**The registry.** Two rows in `Client/tools/architecture/named-values.tsv`, because a `values=` range is one range:

```text
reference-width	Render/ReferenceScreen.h	name=(?:Reference|Design|Virtual|Original|Screen)Width|Width640;values=640..640	home-only	The width of the 640 x 480 screen main.exe lays its 2D on. A real window size is another space.
reference-height	Render/ReferenceScreen.h	name=(?:Reference|Design|Virtual|Original|Screen)Height|Height480;values=480..480	home-only	The height of that screen.
```

Tried on `main` 57e08e8 with the home at its present path: 25 baseline rows in 16 files, and `DesignHeight = 600` of
the loading screen stays out. The check does not see the float and `double` copies nor `BarDesignWidth`; the tables
below list them.

## Site by site

"Home" means the site calls the function of its table. Each site keeps its guard, its casts around the call and its
citation (rule 4).

### A. Reference units to pixels, unsigned: `PixelsFromReferenceX`, `PixelsFromReferenceY`

| Site | Address | Today | After |
| --- | --- | --- | --- |
| `Scenes/GameProjection.cpp:79-87` (`UnprojectGamePoint`) | `0x620486..0x6204a8` | `std::uint32_t`, `WrappedProduct` then `/ VirtualWidth` | home, with a `Core::PixelSize` built from `projection.screenWidth` and `screenHeight` |
| `Scenes/GameScene.cpp:985-986` | cites the same range (the call at `0x6374aa`) | `std::int64_t` product, signed division | home; `Width640`, `Height480` (`:174-175`) go |
| `Render/Pass3D.cpp:36-42` (`ScaledViewport`) | `0x620db3..0x620dfa`, four `MUL` | signed `int` | home; `OriginalWidth`, `OriginalHeight` go |
| `Scenes/NpcWindows.cpp:265-267` | `0x4d28e4..0x4d28f2` (x), `0x4d28a9..0x4d28bb` (y) | `std::uint32_t` | home; `VirtualHeight` goes |
| `Scenes/GameMovement.cpp:203-204,209-210` (`EventWindowCursor`) | `0x56b72f..0x56b741` (y), `0x56b76d..0x56b779` (x) | the compiler's magic numbers spelled out: `(n * 0x88888889) >> 40`, `(n * 0xcccccccd) >> 41` | home: the same quotient for every `std::uint32_t`; `DivideBy480`, `DivideBy640` go |
| `Scenes/GameInventoryHud.cpp:449` | `0x5f240b..0x5f243f` | `screenWidth * 120u / 640u` | home |
| `Scenes/GameWindowDrawing.cpp:165-169` (`BoxWidth`, 5 callers) | to read: no citation | `std::uint32_t` | the helper keeps its name and calls the home |
| `Scenes/GameCharacterHud.cpp:164-169` (`BoxPixels`, 20 callers) | to read: it cites its callee `0x554180` | `std::uint32_t` | the same; merging the two helpers is the HUD's AR4 work |

### B. Reference units to pixels, signed: `SignedPixelsFromReferenceX`, `SignedPixelsFromReferenceY`

| Site | Address | Today | After |
| --- | --- | --- | --- |
| `Scenes/ChatBubbleDraw.cpp:183-184` | `0x5562dc..0x55631c` | the local `Scaled(screen.width, bubble.x, DesignWidth)` | home |
| `Scenes/ShopSigns.cpp:267` | `0x45214b..0x452164` | the local `Scaled` | home |
| `Scenes/ShopSigns.cpp:269-270` | `0x452181..0x4521b1` | two calls of `Scaled` | home |
| `Scenes/ShopSigns.cpp:322` | `0x452ee3..0x452eff` | `Scaled(screen.height, BarRows, DesignHeight)` | home; `Scaled`, `DesignWidth`, `DesignHeight` of this file go |

### C. Pixels to reference units, unsigned: `ReferenceFromPixelsX`, `ReferenceFromPixelsY`

| Site | Address | Today | After |
| --- | --- | --- | --- |
| `UI/GameCursor.cpp:34-55` (`MouseIn640x480`) | `0x4b3597..0x4b359f` (x), `0x4b35ba` (y) | `ScaledTo(word, Width640, screen.width)`, 0 without a screen | home; `ScaledTo` goes; the function becomes `UI::MouseInReferenceUnits` (8 files and `tests/ui/test_game_cursor.cpp`) |
| `Scenes/ChatBubbleDraw.cpp:241-242` | `0x5566f9` | `Bits(width) * Bits(DesignWidth) / Bits(screen.width)`, no guard | home |
| `Scenes/ChatBubbles.cpp:339-342` | `0x557710..0x557719` (height) | a signed `int` product, then an unsigned division; skips the bubble without a screen | home, keeps its `continue`; `Width640`, `Height480` go |
| `Scenes/ItemTooltips.cpp:239` | `0x5b8b3c..0x5b8b45` | `sum * DesignHeight / screenHeight` | home, keeps its `Unexpected` |
| `Scenes/FloorItemNames.cpp:457` | `0x5bb864`, `0x5bb88b` | `half * DesignWidth / W`, 0 without a screen | home; the guard goes, the home gives the same 0 |
| `Scenes/GameDialogDrawing.cpp:742` | `0x62e211` | the width as is without a screen | home inside its guard |
| `Scenes/GameInventoryHud.cpp:1288` | `0x5f3148..0x5f314e` | `step * DesignWidth / screenWidth` | home, keeps its `Unexpected` |
| `Scenes/GameMoveWindow.cpp:99` (`RowOf`) | `0x4485fd..0x448603`, `0x449073..0x44907a` | `step * 640u / W`, no guard | home |
| `Render/Text/TextLayout.cpp:30-40` (`Shrink`, from `:58` and `:84`) and `:77` | `0x6353aa..0x6353d7` | `position - size * 640 / W`, the position as is without a screen | home; `Shrink` goes, since `position - 0` is the position |
| `Scenes/GameUi.cpp:1980` | to read: no citation at the line | `width * 640u / W`, the width as is without a screen | home inside its guard |
| `UI/MessageArea.cpp:243` | to read: no citation at the line | the same | the same |

### D. Pixels to reference units, signed: `SignedReferenceFromPixelsX`, `SignedReferenceFromPixelsY`

| Site | Address | Today | After |
| --- | --- | --- | --- |
| `Scenes/GameProjection.cpp:71-74` (`ProjectGamePoint`) | `0x620610..0x620626` | `WrappedProduct(screenX, VirtualWidth) / projection.screenWidth` | home; the four `Virtual…` definitions of this file go |
| `Scenes/ChatBubbleDraw.cpp:359` | `0x5a90e1..0x5a90e2` | `Scaled(bubble.width, DesignWidth, screen.width) / 2` | home |
| `Scenes/ChatBubbleDraw.cpp:373-380` | `0x5a916c..0x5a91bb`, four `IDIV` | four calls of `Scaled` | home; `Scaled`, `DesignWidth`, `DesignHeight` of this file go |

### E. The ratio: `RatioX`, `RatioY`

The 18 `ScaleX(1.0f, screen)` and 14 `ScaleY(1.0f, screen)` become `RatioX(screen)` and `RatioY(screen)` in their 19
files (`Render/Text/TextLayout.cpp:152-153` cites `0x5762ea8` and `0x5762eac` already). A site that divides by the
ratio keeps dividing.

### F. Sites with a count of their own

They keep the count and take the number from the home.

| Site | Address | The count | After |
| --- | --- | --- | --- |
| `Render/Text/TextLayout.cpp:186-193` | `0x5540e3..0x5540f8` | `H * 47 / 640`, signed: the height over the reference width | `BarDesignWidth` becomes `ReferenceWidth` |
| `UI/SlideBanner.cpp:137` | `0x46aa31..0x46aa37` | `640.0f` (`0x6b8cb0`) over the space's width, times the float at `0x5762ea8`, in the x87 | the `double DesignWidth` becomes `static_cast<double>(Render::ReferenceWidth)`; `RatioX` |
| `Scenes/GameScene.cpp:807` | `0x63735b` | the world's width times the float at `0x6ba070` | `WorldWidthFactor` (`:177`) becomes `Render::InverseOfReferenceWidth` |
| `Scenes/NpcWindows.cpp:264` | `0x4d28c7..0x4d28df` | `(mouseX * 260) / 640`, signed | `VirtualWidth` becomes `Render::ReferenceWidth` |
| `Scenes/GameMovement.cpp:205-208` | `0x56b758..0x56b761` | `(mouseX * 640) / 260`, signed, by `0x7e07e07f` | `Width640` becomes `Render::ReferenceWidth`, cast as today |

### G. The number as an edge or a default

| Site | Today | After |
| --- | --- | --- |
| `UI/OldWindows.cpp:36-37` (12 uses), `UI/LetterWindows.cpp:90` (2 uses) | `ScreenWidth = 0x280`, `ScreenHeight = 0x1e0`: where a dragged window stops | `using Render::ReferenceWidth;` and `using Render::ReferenceHeight;` in the same place; the uses take the home's name |
| `UI/HudPrimitives.cpp:34` `GaugeRight`, `Scenes/NpcWindowSteps.cpp:140` `Panel2000Right`, `Scenes/EventChipWindow.cpp:54` `WindowRight`, `UI/PanelRegistry.h:318` `FullWorldWidth`, `Scenes/CharacterSelectScene.cpp:147` `SelectionWorldWidth` | a role name with the literal | the role name and its type stay; the initializer is `Render::ReferenceWidth` (rule 2) |
| 29 `Core::PixelSize{640, 480}` in 25 files | "no screen yet" | `Render::ReferenceScreen` |
| `Render/Pass3D.h:23-24` (`ViewportRect`) | `width = 640`, `height = 480` | `ReferenceWidth`, `ReferenceHeight` |
| `World/TerrainCulling.h:70,110` | `worldWidth = 640`, `screenWidth = 640` | `Render::ReferenceWidth` |
| `Scenes/GameTooltips.cpp:495-496` | `mouse.x < 0x280`, `mouse.y < 0x1e0`: the mouse inside the screen | the home's names |

**Stays as it is, seen on purpose:** the rectangles of one window, which rule 6 keeps local
(`Scenes/SiegeBattleUi.cpp:137,182`, `Scenes/GameHud.cpp:338,1497,1503`, `Scenes/GameUi.cpp:1938`,
`Scenes/LoginScene.cpp:203`, `UI/CreditsWindow.cpp:491`, the viewports of `Scenes/GameScene.h:77` and
`Scenes/SceneCameras.h:16`); everything under "What is not the reference screen"; and the tests, which name their
input `Screen640` (a real window of that size).

## More resolutions later

Emanuel's goal, said on 2026-11-25 after approving this plan, is to give the client more resolutions. That is a
separate plan, after this one. What it has to decide, so that this plan does not decide it by accident:

- **Which sizes.** Today the client accepts the original's five or a mode of the monitor, in a fixed window
  (`App/Resolution.h`, `Platform/Window.cpp:48`; decided 2026-11-03). Fullscreen, borderless and a custom size are
  missing.
- **How the 2D looks when the screen is not 4:3.** The options were: stretched per axis, as `main.exe` does and MuRet
  does today; one scale with the 4:3 block centered; one scale with each panel anchored to its edge. **Emanuel chose
  the anchors, like MuMain's `UITransform`, on 2026-11-25** (on the project chat's card). So that plan gives every
  window and HUD piece an anchor, the same transform draws a panel and reads the mouse over it, and at the original's
  five sizes the bytes must stay the original's.
- **The 3D.** `0x620db0` already gives the projection the aspect of the scaled viewport, but the terrain trapezoid of
  `0x5fb570` (`World/TerrainCulling.h`) does not look at the aspect, so a wide screen would show terrain that was
  culled. Deduced from the code, not run.
- **The font.** `Render/Text/FontSet.h` stops at step 15 from 1280 pixels up.
- **The code that switches on the real width**, and the second design screen of 800 x 600.

What this plan gives it: after Task 4 the arithmetic is in twelve functions of one header, all taking the size the
reference screen is fitted to, and the mouse and the drawing go through the same ones.

## Found on the way

Not part of these tasks; each needs its own yes.

- **The 800 x 600 design screen** has the same problem in small: `DesignWidth` and `DesignHeight`
  (`Scenes/LoadingScene.cpp:29-30`), `MosaicWidth` and `MosaicHeight` (`Scenes/WorldLoadingScene.cpp:43-44`),
  `PerWidth` and `PerHeight` (`UI/CreditsWindow.cpp:56-57`). Its home would be beside `Render::DesignScale`.
- **260, the world's width with two panels open,** is `TwoPanelWorldWidth` (`UI/PanelRegistry.h:320`),
  `TalkWorldWidth` (`Scenes/NpcWindows.cpp:111`) and the divisor behind `DivideBy260`
  (`Scenes/GameMovement.cpp:200`).
- **Two sites divide by the screen's size with no guard:** `Scenes/GameMoveWindow.cpp:99` and
  `Scenes/ChatBubbleDraw.cpp:241-242`. Task 4 ends both, because the home gives 0 there.
- **`Scenes/ChatBubbles.cpp:339`** multiplies two `int`s where the original wraps at 32 bits. Task 4 ends it.
- **The default window** (`Platform/Window.h:25`, `App/Config.h:77`) spells the registry's resolution 0 as two
  literals instead of `App::OriginalResolutions[0]`. It belongs to the resolutions plan.

## Decisions

Emanuel, 2026-11-25, on the analysis that preceded this plan:

| Question | Decided |
| --- | --- |
| The word | **Reference**, as MuMain's `REFERENCE_WIDTH`, over the names the code uses today (Design, Virtual, Original, Screen) |
| The home | **`engine/Render`**, in the file that already holds `0x620d50` and `0x620d80`. The 640 x 480 of `engine/Platform` is a real window, another space |
| The shape | **the named size and one function per count of the original**; no private copy of the number, no local helper that repeats a count |

For the resolutions plan, not this one: on a screen that is not 4:3 the 2D keeps its proportions with **each panel
anchored to its edge, like MuMain** (Emanuel, 2026-11-25; the recommendation was the centered 4:3 block).

## Global Constraints

- Fidelity first. No task changes a result for a value that reaches the site today, the order of the original's calls
  or of the `rand()` draws.
- Every task is a refactor: no golden and no test expectation changes, on Linux and Windows, Debug and `math-release`.
- A site calls a home function only after its ASM was read at its address and the instructions are that function's
  row in "The five counts" (rule 4). The PR lists site, address, instructions and function. A site that does not
  match keeps its count, takes the number from the home, and the PR says so.
- A site keeps what it does without a screen (its guard, its `Unexpected`, its `continue`), unless the home's 0 gives
  the same result, and then the PR names the site.
- Every address cited in a file a PR touches stays cited; the PR gives the count of `0x` citations in those files,
  before and after.
- Everything in English. A comment only if strictly necessary, one line.
- A file renamed keeps its content in a rename-only PR first (as PR 453 did): CI runs `check_pure_move.py` on every
  `type(ARn.m)` PR.
- `engine/Render` names no game value (rule 7): the home holds the reference screen and nothing of MU's windows.
- PRs target `main`. One task per PR, and Emanuel says yes before the next.
- GitHub CI is the build and the final word.

## Review Focus

- **A site moved to the wrong signedness passes every on-screen test.** The two counts differ only below zero: a
  chat bubble or a shop sign left of the screen, a point projected behind the camera. Task 4 pins a negative
  coordinate at each site it moves (step 2).
- **A screen with no size.** Tests without a window pass `Core::PixelSize{}`. The dividing functions give 0 and each
  site keeps its own answer; Task 3 pins the 0, Task 4 keeps each site's test for it.
- **A product that wraps.** `0x800000 * 640` does not fit in 32 bits; the original wraps and `GameScene.cpp:985`
  (64 bits) does not. Task 3 pins the wrapped quotient.
- **The ratio is not a division.** At 800 x 600 the y ratio is `0x3fa00001`. A `RatioY` written as `H / 480.0f`
  passes at 640 x 480 and at 1920 x 1080. Task 3 pins the bits at the five original sizes.
- **A 640 that is not the reference screen.** Renaming a real width (`GameItemHelp.cpp:110`), the loading screen's
  600 or a sound id would be a wrong identity. Task 5 touches only the sites of table G, and the registry rows match
  by name with one exact value each.
- **The height over the width.** `TextLayout.cpp:193` divides `H * 47` by 640, as `0x5540e3..0x5540f8` does. It must
  not become `SignedPixelsFromReferenceY`.

---

## Tasks

Task 1 is this document. Tasks 2 to 5 are AR1.1 work, start after PRs 456 and 457 merge (they edit the same files of
`Scenes/`), and go before AR1.2's move, interleaved with AR1 Tasks 5e and 5c and the music plan's as Emanuel says.

| Task | What | Needs |
| ---: | --- | --- |
| 1 | this plan | |
| 2 | rename the `ScreenScale` files to `ReferenceScreen` | PRs 456 and 457 |
| 3 | the home: the names, the ratios, the integer conversions, the registry rows | 2 |
| 4 | the conversions call the home (tables A to F) | 3 |
| 5 | the edges and the defaults take the home's names (table G) | 4 |

### Task 1: the plan

This PR. Title: `docs(refactor): plan one home for the 640 x 480 reference screen`.

**Files:**
- Create: `docs/superpowers/plans/2026-11-25-reference-screen.md`

- [x] **Step 1:** write the findings and the tasks.

The AR1 plan and the named-values plan get their pointers to this document in Task 3, because PRs 456, 457 and 458
edit both files today.

### Task 2: rename the files

Title: `refactor(AR1.1): rename the ScreenScale files to ReferenceScreen`, branch
`refactor/ar1.1-reference-screen-files`. Skills: `muret-move`.

**Files:**
- Rename: `Client/src/Render/ScreenScale.{h,cpp}` to `Client/src/Render/ReferenceScreen.{h,cpp}`;
  `Client/tests/render/test_screen_scale.cpp` to `Client/tests/render/test_reference_screen.cpp`
- Modify, include lines only: the 22 other files of `Client/src` that include the header (`Render/OriginalQuad.cpp`,
  `Render/SpriteBatch.cpp`, `Render/Text/TextLayout.cpp`, 12 files of `Scenes/`, 7 of `UI/`),
  `Client/tests/render/test_original_quad.cpp`, `Client/tests/scenes/test_castle_windows.cpp`,
  `labs/cl5.0-pipeline/OriginalQuad.h`
- Modify: the two rows' `file` column in `Client/docs/research/coverage/module-map.tsv`; the path where a text names
  it (`labs/CMakeLists.txt:80`, `labs/cl5.0-pipeline/README.md`, `docs/learning/ola-cl5.0-el-pipeline-grafico.md`)

- [ ] **Step 1:** `git mv` the three files and change the include lines. The test suite keeps the name
  `ScreenScaleTest` until Task 3. No manifest cites the test (`cl9-vm-evidence.json` has no `screen_scale`).
- [ ] **Step 2:** run and expect no finding:

  ```bash
  python3 -I Client/tools/architecture/check_pure_move.py --base origin/main --head HEAD
  python3 -I Client/tools/architecture/check_layers.py --src Client/src --modules Client/modules.tsv \
    --map Client/docs/research/coverage/module-map.tsv --baseline Client/tools/architecture/layers-baseline.tsv
  ```

- [ ] **Step 3:** push; CI builds the client and the labs. After the squash, the next PR records it as the
  reference's LEEME says ("When a refactor moves code"): the hash in `.git-blame-ignore-revs` and the three files in
  `entradas/moves.tsv`.

### Task 3: the home

Title: `refactor(AR1.1): give the 640 x 480 reference screen one home`, branch
`refactor/ar1.1-reference-screen-home`. Skills: `cpp-pro`, `clean-code`, `superpowers:test-driven-development`.

**Files:**
- Modify: `Client/src/Render/ReferenceScreen.{h,cpp}`, `Client/tests/render/test_reference_screen.cpp`
- Modify: `Client/tools/architecture/named-values.tsv` (the two rows of "The home"),
  `Client/tools/architecture/named-values-baseline.tsv` (their 25 rows)
- Modify: `docs/CODING_RULES.md` §7 rule 1 (the list of homes gains "the 640 x 480 reference screen and its
  conversions in `engine/Render`"), `docs/superpowers/plans/2026-11-22-ar1-engine-apart.md` and
  `docs/superpowers/plans/2026-11-23-named-values.md` (one pointer each to this plan)

**Interfaces:**
- Produces: the header of "The home", exactly. The integer functions are `constexpr` in the header; `ScaleX`,
  `ScaleY`, `RatioX` and `RatioY` stay in the `.cpp`, whose private `InverseOf640` and `InverseOf480` give way to the
  public inverses. The two `static_assert`s on the bits (`0x3acccccd`, `0x3b088889`) move beside them.
- The header's comment becomes English and one line per citation; `0x620d50`, `0x620d80`, `0x621b60`, `0x622000`,
  `0x621aa0`, `0x6ba070` and `0x6ba06c` stay cited.
- No site of `Client/src` changes in this task.

- [ ] **Step 1: write the failing tests.** The suite becomes `ReferenceScreenTest`; its three tests keep their
  assertions. Add:

  ```cpp
  constexpr PixelSize Screen800{800, 600};

  TEST(ReferenceScreenTest, NamesTheScreenAndItsInverses)
  {
      EXPECT_EQ(ReferenceScreen.width, 640);
      EXPECT_EQ(ReferenceScreen.height, 480);
      EXPECT_EQ(std::bit_cast<std::uint32_t>(InverseOfReferenceWidth), 0x3acccccdu);
      EXPECT_EQ(std::bit_cast<std::uint32_t>(InverseOfReferenceHeight), 0x3b088889u);
  }

  TEST(ReferenceScreenTest, TheRatioIsTheScaleOfOne)
  {
      for (const PixelSize screen :
           {Screen640, Screen800, Screen1024, PixelSize{1280, 1024}, PixelSize{1600, 1200}, Screen1920})
      {
          EXPECT_EQ(std::bit_cast<std::uint32_t>(RatioX(screen)), std::bit_cast<std::uint32_t>(ScaleX(1.0f, screen)));
          EXPECT_EQ(std::bit_cast<std::uint32_t>(RatioY(screen)), std::bit_cast<std::uint32_t>(ScaleY(1.0f, screen)));
      }
      EXPECT_EQ(std::bit_cast<std::uint32_t>(RatioX(Screen640)), 0x3f800000u);
      EXPECT_EQ(std::bit_cast<std::uint32_t>(RatioX(Screen800)), 0x3fa00000u);
      EXPECT_EQ(std::bit_cast<std::uint32_t>(RatioY(Screen800)), 0x3fa00001u);
      EXPECT_EQ(std::bit_cast<std::uint32_t>(RatioX(Screen1024)), 0x3fcccccdu);
      EXPECT_EQ(std::bit_cast<std::uint32_t>(RatioY(Screen1024)), 0x3fccccceu);
      EXPECT_EQ(std::bit_cast<std::uint32_t>(RatioY(PixelSize{1280, 1024})), 0x40088889u);
      EXPECT_EQ(std::bit_cast<std::uint32_t>(RatioY(PixelSize{1600, 1200})), 0x40200001u);
  }

  TEST(ReferenceScreenTest, ReferenceUnitsBecomePixels)
  {
      EXPECT_EQ(PixelsFromReferenceX(320, Screen1024), 512u);
      EXPECT_EQ(PixelsFromReferenceY(25, Screen1024), 40u);
      EXPECT_EQ(SignedPixelsFromReferenceX(320, Screen1024), 512);
      EXPECT_EQ(SignedPixelsFromReferenceY(47, Screen1920), 105);
  }

  TEST(ReferenceScreenTest, BelowZeroTheSignedCountGoesTowardZeroAndTheUnsignedOneWraps)
  {
      EXPECT_EQ(SignedPixelsFromReferenceX(-1, Screen1024), -1);
      EXPECT_EQ(PixelsFromReferenceX(0xffffffffu, Screen1024), 6710884u);
      EXPECT_EQ(SignedPixelsFromReferenceX(-3, Screen800), -3);
      EXPECT_EQ(PixelsFromReferenceX(0xfffffffdu, Screen800), 6710882u);
      EXPECT_EQ(SignedPixelsFromReferenceY(-36, Screen1024), -57);
      EXPECT_EQ(SignedReferenceFromPixelsX(-1, Screen1024), 0);
      EXPECT_EQ(ReferenceFromPixelsX(0xffffffffu, Screen1024), 4194303u);
      EXPECT_EQ(SignedReferenceFromPixelsX(-512, Screen1024), -320);
      EXPECT_EQ(SignedReferenceFromPixelsY(-767, Screen1024), -479);
  }

  TEST(ReferenceScreenTest, PixelsBecomeReferenceUnits)
  {
      EXPECT_EQ(ReferenceFromPixelsX(512, Screen1024), 320u);
      EXPECT_EQ(ReferenceFromPixelsY(767, Screen1024), 479u);
      EXPECT_EQ(ReferenceFromPixelsX(65535, Screen640), 65535u);
  }

  TEST(ReferenceScreenTest, TheProductWrapsAt32Bits)
  {
      EXPECT_EQ(ReferenceFromPixelsX(0x800000u, Screen1024), 1048576u);
  }

  TEST(ReferenceScreenTest, WithoutAScreenThePixelsGiveZero)
  {
      for (const PixelSize screen : {PixelSize{}, PixelSize{-640, -480}})
      {
          EXPECT_EQ(ReferenceFromPixelsX(7, screen), 0u);
          EXPECT_EQ(ReferenceFromPixelsY(7, screen), 0u);
          EXPECT_EQ(SignedReferenceFromPixelsX(7, screen), 0);
          EXPECT_EQ(SignedReferenceFromPixelsY(7, screen), 0);
      }
  }

  // GameMovement.cpp spells the compiler's division; the home divides.
  TEST(ReferenceScreenTest, TheMagicNumbersAreTheDivision)
  {
      for (std::uint64_t n = 0; n <= 0xffffffffu; n += 9973)
      {
          ASSERT_EQ((n * 0x88888889u) >> 40u, n / 480);
          ASSERT_EQ((n * 0xcccccccdu) >> 41u, n / 640);
      }
  }
  ```

- [ ] **Step 2:** build the test and see it fail to compile on the missing names.
- [ ] **Step 3:** write the header and the `.cpp` from "The home".
- [ ] **Step 4:** run `ctest -R ReferenceScreenTest`; all pass.
- [ ] **Step 5:** add the two registry rows and regenerate the baseline with `--write-baseline`; `git diff` of the
  baseline adds 25 rows that start with `reference-` and nothing else. Run the check without `--write-baseline`:
  `0 new`.
- [ ] **Step 6:** the rule's line and the two pointers; commit and push.

### Task 4: the conversions call the home

Title: `refactor(AR1.1): convert through the reference screen's home`, branch
`refactor/ar1.1-reference-screen-conversions`. Skills: `refactoring`, `clean-code`,
`superpowers:systematic-debugging` on any difference.

**Files:**
- Modify: the 20 files of tables A to D and F and the 19 of table E (three are in both)
- Modify: `UI/GameCursor.{h,cpp}` and the six files that call `MouseIn640x480`; `Client/tests/ui/test_game_cursor.cpp`
- Modify: the tests of step 2; `Client/tools/architecture/named-values-baseline.tsv` (rows leave)

**Interfaces:**
- Consumes: the twelve functions and the two inverses of Task 3.
- Produces: `[[nodiscard]] UI::Point UI::MouseInReferenceUnits(const Input::InputState& input, Core::PixelSize screen)`,
  the present `MouseIn640x480` under its new name.

- [ ] **Step 1: read the ASM of every site** of tables A to D and F at its address, and fill the PR's table: site,
  address, instructions, function. The five rows marked "to read" get their address here. A site that does not match
  its table moves to table F's treatment.
- [ ] **Step 2: pin each site before changing it.** A site whose test never passes a coordinate below zero, a
  product over 32 bits or an empty screen gets those cases added to its existing test, with today's results:
  `tests/scenes/test_shop_signs.cpp` (a sign left of the screen), `tests/ui/test_game_cursor.cpp`,
  `tests/render/test_text_layout.cpp`, `tests/scenes/test_npc_window_replies.cpp`,
  `tests/scenes/test_game_draw_tail.cpp` (`ProjectGamePoint` behind the left edge),
  `tests/scenes/test_game_scene_viewports.cpp`. `EventWindowCursor`, `RowOf` and the chat bubbles have no test of
  their own: each gets one. Run them: all pass on the unchanged code.
- [ ] **Step 3: move the sites, one table at a time,** and run the tests of step 2 after each table. The private
  definitions and the local helpers of the tables go with their last use.
- [ ] **Step 4: the ratio.** `ScaleX(1.0f, screen)` and `ScaleY(1.0f, screen)` become `RatioX(screen)` and
  `RatioY(screen)`; `grep -rnE 'Scale[XY]\(1\.0f' Client/src` prints nothing.
- [ ] **Step 5: rename `MouseIn640x480`** to `MouseInReferenceUnits` in its eight files and its test.
- [ ] **Step 6: verify.** The whole suite and the behavior golden unchanged; the check prints the `reference-` rows
  that no longer happen, and they leave the baseline (the rows of table G stay); the count of `0x` citations in the
  touched files, before and after, goes in the PR.

### Task 5: the edges and the defaults

Title: `refactor(AR1.1): name the reference screen from its home`, branch `refactor/ar1.1-reference-screen-names`.
Skills: `refactoring`.

**Files:**
- Modify: the sites of table G (about 35 files of `Client/src`)
- Modify: `Client/tools/architecture/named-values-baseline.tsv` (the last `reference-` rows leave)

**Interfaces:**
- Consumes: `Render::ReferenceWidth`, `Render::ReferenceHeight`, `Render::ReferenceScreen`.

- [ ] **Step 1:** replace by table G. A using-declaration goes where the private definition was; a role name keeps
  its name and type.
- [ ] **Step 2:** only names changed, so the code must not: compare the rebuilt objects of the optimized build with
  debug info stripped, before and after. Where an object differs (a copy of `ReferenceScreen` instead of two
  immediates), the PR lists the file and the test that covers it.
- [ ] **Step 3:** the check prints `0 new` with no `reference-` row left in the baseline, and

  ```bash
  grep -rnE '\b(Width640|Height480|InverseOf640|InverseOf480|MouseIn640x480)\b' Client/src
  grep -rnE 'PixelSize( [A-Za-z_]+)?\{640, 480\}' Client/src
  ```

  print nothing.
- [ ] **Step 4:** the whole suite and the behavior golden unchanged; commit and push.

## Done when

- `Client/src` defines 640 and 480 as the reference screen once, in `Render/ReferenceScreen.h`, and
  `check_named_values.py` fails a second definition.
- No file outside the home multiplies or divides by 640 or 480 to change between reference units and pixels, except
  the five sites of table F, which name the home's number.
- Every golden and every test is unchanged.
