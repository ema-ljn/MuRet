# CL9 hito 6 — review B (draws, dialogs, list, trainer, merge, P-C368)

Rama claude/cl9-hito-6 @ d58ecf1b. Read-only. ASM authority. Complete (2026-11-14). Nothing built (optional; ClientLib at -j1 too slow); all claims are static against the ASM.

## Findings (most severe first)

### F1 (Medium) Mix button textures 0x124/0x125 (and win_button 0x646) never loaded: chaos box and trainer mix buttons, and the 0xa8 buttons, are invisible
- Client/src/Scenes/TextureSets.h:52 (HudTextureSet) vs Client/src/Scenes/ChaosBoxDrawing.cpp:64-65,371-382.
- ASM 0x61ae60 loads `Interface\mix_button1.jpg` in 0x124 (0x61b286) and `Interface\mix_button2.jpg` in 0x125
  (0x61b2a6) (also level_02 0x121, accept_box01/02 0x122/0x123), same 0x63b870 (GL_NEAREST/GL_CLAMP) call as lock_01.
  0x5febf0 draws 0x124 at (x+75, y+300) 44x32 and 0x125 when pressed (0x5ff734, 0x5ff7c2).
- MuRet: HudTextureSet (89 entries) has lock_01..04 (0x126..0x129) but not 0x121..0x125, even though its own comment
  (TextureSets.h:50-51) names them. GlSpriteRenderer.cpp:69-71 logs "el slot 0x124 no tiene textura" and skips the
  quad, so the mix button (and its pressed state) never shows. Tests only check sprite commands, so they pass.
- Same gap hits the pet trainer box (9062bb78): PetTrainerDrawing.cpp:150-151 (MixSlot 0x124/0x125, 0x44d763/0x44d7f0).
- Related (pre-existing from CL8, newly used by hito 6): slot 0x646 `Interface\win_button.tga` (0x61ccb0 at 0x61d17a,
  loaded with 0x63bd00 like win_check) is in no MuRet texture set, so the three 0xa8 buttons of DrawTheYesNoBox
  (GameDialogDrawing.cpp:214 LuckButtonSlot, 0x630e82/0x63103d) are skipped too (only their text shows).
- Fix: add `{"Interface\\mix_button1.jpg", 0x124, NearestClamp}` and `mix_button2.jpg` 0x125 (and level_02 0x121,
  accept_box01/02 0x122/0x123, which CL8's GameCharacterHud.cpp:698 already draws as 0x121) after level_01, in
  0x61ae60's order, and bump the array size; add `{"Interface\\win_button.tga", 0x646, <the 0x63bd00 params of win_check>}`
  before win_scrollbar (0x61ccb0 order). A test that every slot the hito 6 draws use is in HudTextureSet would catch it.

### F2 (Low) Kind 2 (Devil Square invitation) with eye level >= +7 closes the client from the draw (P-C197)
- Client/src/Scenes/ChaosMix.cpp:785-790 (MixQuoteOf), called every frame from DrawTheChaosBox.
- ASM 0x5fee3a: `MOV ESI,[EAX*4 + 0x6c9da0]` with no bound. For level 8.. it reads fixed .data: 0x6c9dc0 is the
  string "%s + %d%%" (level 8 -> 0x2b207325), 0x6c9dc4.. the next bytes. Deterministic, no UB, original keeps going.
- MuRet returns Core::Unexpected -> the HUD draw fails. Reachable with an eye/key +7..+15 (item level is 4 bits; a
  GM-made or a later-server item). Documented as P-C197, but it is a state the original tolerates.
- Fix: model the read as the little-endian dwords of the .data bytes after 0x6c9dbc (read them from main.exe), or
  at least keep drawing with the bytes of "%s + %d%%" for 8 and 9 and cut only beyond what is mapped.

### F3 (Low) WarehouseFee: signed overflow (UB) for level > 46340
- Client/src/Scenes/NpcWindowDrawing.cpp:181 `static_cast<int32_t>(level) * static_cast<int32_t>(level)`.
- ASM 0x5f545e..0x5f5464: `MOVZX`-like word into ESI, `IMUL ECX,ESI` (32-bit wrap), FILD signed.
- level is the u16 from the server; a hostile/buggy server value > 46340 is C++ UB (MSVC wraps, but UB lets the
  optimizer assume otherwise). Fix: `static_cast<std::int32_t>(static_cast<std::uint32_t>(level) * level)`.

### F4 (Low, doc) 08 P-C368: the vtable map is right, but +0x30 and +8 leave out effects a port needs
- Verified against the EDIT vtable 0x6b9550 (set by 0x4674c0 at 0x4674f6): +4 0x468260, +8 0x467f50, +0x14 0x468320,
  +0x30 0x4682a0, +0x3c 0x467f30, +0x4c 0x467ee0, +0x50 0x467ec0 are all correct. Imports confirmed: ShowWindow
  (0x6b8480), SendMessageA (0x6b8430), SetWindowTextA (0x6b847c), GetWindowTextA (0x6b8478), SetFocus (0x6b8494),
  GetFocus (0x6b8458), PostMessageA (0x6b8490). Correct too: +0x3c = EM_LIMITTEXT(n); +0x4c: NULL -> SetWindowTextA("" at
  .bss 0x6e57a4), strlen > 0xff -> nothing; +0x50: GetWindowTextA(buf, n), nothing with buf NULL; 0x630883 is
  `mov [ecx+0x4c], 9`; +0x14 tests flag 8 of +0x4c (0x468477); the 0x73 sequence 0x630872..0x6308c3 and the 0x91 one
  0x6305c9..0x6305de (followed by 0x620980) are as written.
- Missing from the row (needed when the field is ported):
  - +0x30 0x4682a0 always writes `[0x5762ec0] = [this+0x40]` (0x4682f1..0x4682f7): the field takes the "list with the
    arrow keys" global (MuRet `GameUiState::focusedList`) every time the 0x91/0x73 draw calls it, i.e. every frame.
    Also SetFocus runs for any n (when hwnd != 0), and with [0x6c7b8c]==1, GetFocus()==[0x5763544] and (+0x4c & 3)==0 it
    also calls 0x4673a0 (IME: 0x66b8b2/0x66b8b8/0x66b8a0 on the main window). EM_SETSEL is PostMessageA(0xb1, -2, -1).
  - +8 0x467f50 returns early when (w, h) equals (+0x58, +0x5c), clamps w to 0xee, stores w/h, and calls SetWindowPos
    (ftol(w*rx), ftol(h*ry), SWP_NOMOVE|SWP_NOZORDER) + EM_SETSEL(0,0) only when hwnd != 0 AND +0x44 (parent id) != 0.
  - +4 0x468260 stores the state in +0x48 and shows (SW_SHOW) for any state other than 3; nothing without hwnd.
- Fix: add these to P-C368 (no code change in this hito).

### F5 (Info) Trainer draw tests cover maps 0 and 2 only
- Client/tests/scenes/test_pet_trainer_drawing.cpp:54 `Maps{0, 2}`; P-C198 says the trainer tests cover Lorencia,
  Devias and Noria. All other hito 6 draw tests (npc window, keypad, chaos box, tooltips, dialogs, jewel list/unmix)
  use 0, 2 and 3. Fix: add 3 (the draw is map independent, so it is only a rule/doc mismatch).

### F6 (Info, inferred area) rightReleased is not cleared by key messages without text
- Client/src/Scenes/GameSceneUi.cpp LatchTheRightButton (395cd803). 0x4b3444 (WndProc 0x4af4e0) clears 0x7d36849 for
  every message that reaches it with [0x5763580] set, before the message switch (0x4b347a), not only mouse/text ones.
  MuRet clears it on left/middle buttons, wheel, text and mouse moves, but not on WM_KEYDOWN/KEYUP of keys without text
  (arrows, Shift, F keys). Edge: right release + arrow key in the same frame keeps the release for the jewel list
  (0x46f223). Already *inferido* under P-C96/P-C366; at most mention "any message" in GameUiState.h:122-123.

## Checked, no findings
- 09659a8e shop 0x5f4720 / grids (0x5f7840 IsOpen order and rands, cut 0x5f76df only with trade/mode 3), dfff8217
  warehouse 0x5f4e30 and keypad 0x5e9b10/0x5e7b50, 6e1fe1d7 tooltip price 0x5b2d23/0x5b1200, ebaf89f0 chaos box
  0x5febf0 (title, name/kind texts, rates/prices vs jump tables 0x5ff878/0x5ff828, warnings table 0x5ff8c0, button),
  c7d4b66b yes/no 0x630cc4 and rows 0x632637..0x632a2a, 34014cc2 jewel dialogs 0xa1/0xa2/0xa4/0xa5/0xa6, 0x91 frame,
  title tails, jump table 0x632bf0: coords, sizes, UVs, colours, fonts, text ids, branch order (details in my notes).
- 395cd803 list (vtable 0x6b85f8): ctor 0x46e290 (shown 8, thumb 13.0, barMode 1, +0xbc 1, 0xb4 x 0x6d, cap 150),
  clear 0x482f80 (+0x48(0), Select(0), scroll 0), add 0x46e940 (bounds 0..0x40, dup check, dead level==-1 check,
  push_back, Select(0), scroll rules, sort by cell via 0x46e4a0), chosen 0x483aa0, Select/Next/Previous
  0x4839c0/0x4839f0/0x483a50, Scroll 0x4833e0, message 0x4834a0 (jump table 0x4835e8: 0xf/0x10 -> 0x483564), step
  0x483bc0 (keys 0x25/0x27/0x26/0x28, repeat <=5, wheel *-3, arrows/track/thumb rects, counters <=0xf, sound 0x19,
  thumb grab blocked by 0x402af0's 0x5762eb4), 0x45aa80/0x402af0 (capture/hovered/focus globals), row hover 0x46f1a0
  (click toggle, right-release ask with cell (0,0x40) and level != 0xff; end never read), RowY 0x46f2c0, draw 0x4832e0
  + 0x4845d0 iterator, frame 0x46ebe0 (all quads, UVs 0.8125/0.90625/0.40625/0.09375/0.5, thumb 0.6875/0.03125/0.0625,
  count<shown branch), row 0x46f020 ("%s,  %d" at 0x6c7afc, 300-byte buffer, 0x6c62e0 0 -> 1806 else 1807, veil 0.5,
  0xff000000/0xffc8dce6, 0x465950(x+6, RowY, ..., 0,0,1,0,0x280), returns 1), PlaceTheListBar vs 0x484490 (pure
  refactor of the chat log; 0x6b86a0 = 0.0, 0x6b8818 = 2.0), OK/cancel 0x625dc3..0x625f37 (234.5/314.5/324.5/404.5,
  +10, +25), no rand().
- 882e738e 0xa4 step 0x625cf4..0x625dbe and cancel 0x625f4d (235/80/160/185/279.5/359.5/200/225, click read first).
- 9062bb78 trainer: 0x44b510 dispatch, menu 0x44b610, recovery 0x44c850 (buttons 0x96+0x3c i, -1 price -> normal font
  0xff1933ff, zen < price -> 0xffffffff/0xc80000ff, record rands per button), box 0x44d680 (grid 275/110 8x4, bars
  0x117, mix button + hover 335..379 x 300..332, texts 1205/1623/1212/1213/584/1622/586/587, kinds 0..2, prices
  0x6b8c68 {0, 5000000, 1000000}, y 65/+15/+135, close 280/395 hover 280..304 x 395..419); kind > 2 unreachable
  (ScanTheTrainerBox writes 0..2 only).
- Merge 4240fed2: `git merge-tree` of the parents gives the committed tree for every file except the 5 conflicted
  ones (04, 07, 08, plan, test_game_inventory_hud); their resolutions keep both sides (07 Q-C950..959, 08 rows with
  P-C194/P-C198 struck and P-C199 = the 4b version, 04 keeps the fuller 83 and the 4b BC answers; the test drops the
  trainer cut that 4b ported). No Q-C/P-C id lost (07 535+8+5=548, 08 140+3+3=146).
- Sprintf cuts 0x62f1dc, 0x62f6bd, 0x62fd43, 0x630ed3, 0x632855, and 0x6327eb / 0x5ff258 (0x1c always comes with cape
  3 or 4 from ScanTheChaosBox, ChaosMix.cpp:455-463, never reset to another value): unreachable with real data.
- Privacy: test strings are invented (e.g. "Domador", Spanish UI words). Maps 0/2/3 in the other new tests.

## Not covered
- Packets, steps (other than the 0xa4 list/OK/cancel) and item moves: the other reviewer.
- No build or test run; no MSVC narrowing / clang-tidy pass on the changed files.
- 0x402a90's dialog swap (0x6236f0/0x633300) side effects on the click when MuRet defers the ask until after the
  list step (the original asks from inside the row loop, 0x46f297): only an edge with right-release and a left click
  in the same frame.
- The +0x44 parent of [0x5763504] (who sets it) for F4.
