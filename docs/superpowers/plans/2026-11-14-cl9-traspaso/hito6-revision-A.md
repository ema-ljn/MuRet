# CL9 hito 6, review A (packets, steps, trainer, dialogs, equip moves)

Rama claude/cl9-hito-6 @ d58ecf1b. Read-only. ASM = authority.

## Findings (most severe first)

### F1. HIGH: the 86 reader drops the item of result 100 (trainer mix): the trainer box gets a zero item
- `Client/src/Protocol/NpcWindows.cpp:199-215` (`ReadChaosMixResult`), consumed by `Client/src/Scenes/NpcWindows.cpp:555-562`.
- ASM 0x4da280: table 0x4da3a8/0x4da38c sends pkt[3]==100 to 0x4da373: `0x449c40`, then `0x44ad70(0, pkt+4, 1)` = `0x5bc740(0x723c80, 8, 4, 0, pkt+4, 1)`: the 7 item bytes from pkt[4] are read for 100 exactly as for 1.
- MuRet: `if (*result != ChaosMixSucceeded) return ChaosMixResult{.result = *result, .item = {}};` so for 100 the item is all zeros; `ApplyChaosMixResult(TrainerMix)` then places type 0 (0x54b3a0 of zeros = 0, not 0x1fff) with zero level/dur/options in trainer slot 0. Every real trainer mix result shows the wrong item. The test `NpcWindowRepliesTest.TheTrainerMixLeavesItsItemInTheTrainerBox` (test_npc_window_replies.cpp:511) calls the applier directly with an item, so it never sees the reader; test_npc_windows.cpp has no 100 case.
- Fix: read the item when `result == 1 || result == 100` (constant `TrainerMix`, move it to Protocol), add a protocol test `C1 0B 86 64 <7 bytes>` and a session-level test.

### F2. HIGH: Q-C921 is a misread; the chaos box drop cuts (and kills the UI step) after every mix result
- `Client/src/Scenes/InventoryMoves.cpp:1314-1319` (`DropOnOtherGrid`, from fce985e2), called each frame by `Client/src/Scenes/GameUi.cpp:1041-1052` whenever the chaos box is open, an item is on the cursor and `chaosMixState != 1`; doc `Client/docs/research/07-quirks-y-bugs.md:545` (Q-C921).
- ASM 0x5ca314..0x5ca337: with grid 0x7d2d380, `[0x7d2dc80] != 0` and `[[0x7d536fc]+4] == 0` the original executes `005ca333 c6 45 d3 00 MOV byte ptr [EBP-0x2d],0x0` and jumps to 0x5ca385: the local IS written (fits = 0, the drop is refused and the footprint marked), it is not an uninitialized read. The only negative-cell exit is 0x5ca302..0x5ca30e (column or row < 0), so a mouse anywhere right of / below the box's top-left corner (the whole inventory) reaches it.
- MuRet: `return Core::Unexpected("0x5ca337: ... lee [ebp - 0x2d] sin escribir (Q-C921)")`. `chaosMixState` is 2 after the 31 with grid 3 and after the 86 results 0, 1, 4, 9 and default (NpcWindows.cpp:331, 528-564), so the ordinary flow "mix, pick the result or the returned items up from the box, carry them to the inventory" fails the game UI step on the first frame. No test covers it.
- Fix: in that state set `allowed = false` (fits 0, marks as refused/jewel, no 24) and continue like the trainer filter; correct Q-C921 in 07 (it describes the "close and reopen the chaos machine to mix again" behaviour, *estático*), add a test with state 2 and the mouse over the inventory.

### F3. MEDIUM/LOW: Q-C200 grid overflow returns Unexpected from inside packet handlers (closes the session)
- `Client/src/Scenes/OwnItemPlacement.cpp:384-389` (`PlaceInGrid`) and `InventoryMoves.cpp:273`, reached from `Client/src/Scenes/NpcWindows.cpp:352-359` (`ApplyItemList`, 31), `:372` (`ApplyBuyResult`, 32 via `PlaceMovedItem`), `:538` and `:561` (`ApplyChaosMixResult`, 86), and the 24 results 2/3/5 (`PlaceMovedItemInOtherGrid`).
- ASM 0x5bc740 writes the footprint with no bounds check (Q-C200: OOB write in the original, UB). MuRet turns it into `Core::Unexpected`, which `ServerSession::CheckApplication` turns into `Fail(ApplicationFailed)`: the session closes on a malformed slot/type from the server.
- The lead's lesson is "UB inside a handler: discard, not cut". This is reachable only with bad server data (a GS 0.97 slot/type that does not fit), so it is not a normal-flow bug.
- Fix: in handler context, log and discard the packet (or the one entry of the 31) instead of failing; keep Unexpected for UI-originated calls if the lead prefers.

### F4. LOW: StepTheCursorChoice cuts choices 5, 3 and 6 regardless of the answer
- `Client/src/Scenes/NpcWindowSteps.cpp:609-614` (fce985e2/dc627a0d).
- ASM 0x5d9c75 (choice 5) and 0x5d965a (3..6): only answer == 1 goes to the hito-7/CL9.4 code (C1 3F 06, trade, duel). Answer != 1 goes to 0x5d9634: answer 2 resets 0x78abb14 and the answer and returns, answer pending just returns.
- MuRet returns Unexpected as soon as the choice is 5 or 3..6, even with the answer pending or "no", so the cut fires under a wider condition than the original's.
- Unreachable today (nothing in hito 6 sets those choices), so LOW. Fix: cut only on `dialogAnswer == Yes`; reset on No; return false while pending.

### Info / lead's call
- I1. The 30 with 5 (GS 0.97 sends it for NPC class 234) goes to Open(0x2000), which is cut, so the session closes (plan decision 8). The 30 with 12/13 is cut as CL9.5 (`NpcWindows.cpp:308-311`). Both are documented decisions, not bugs.
- I2. `Client/src/UI/PanelRegistry.cpp:238-242` (e6dbc429): the 0x65c039 cut has the exact condition (Dark Lord, inventory opened by the close). But `Protocol::Pet::WritePetInfoRequest` already exists, and the original only sends two A9 packets (XOR32 in clear, no rand). The cut makes trainer mode 3 (0x449970 opens the inventory) unusable for Dark Lords. Suggest implementing the two sends instead of cutting.
- I3. `Client/src/Scenes/GameDialogSteps.cpp:860-863`: the 0x91 OK cut is labelled CL9.4, but the plan (P-C368) puts the EDIT in hito 6. It is unreachable now (the original reads the EDIT only with [0x6c7b8c]==1). Align the label.
- I4. `Client/src/Scenes/WarehouseKeypad.cpp:350-352`: the strcat overflow message embeds the digit being typed (part of the password). It is unreachable (length < digits), but per the privacy rule drop `number` from the string.
- I5. The ShowTheSentence overflow Unexpected depends only on Text.bmd (local data). OK.

## Checked with no findings
- 30: table 0x4d2914 (2->0x40, 3->0x100 + rates, 4->0x1000, 5->0x2000, 6->0x4000, 7->0x8000, 9->BC dialog 0xa1 if !0x6e5798, 12/13 CS, other->0x10000), warp math, sounds 0x19/0x1c.
- 31 (0x4c5f60): grids 3/5/0x78a7070, 32/120 cells, flag 1 chaos / 0 trainer, count JBE.
- 32: 0x5bc740 on the inventory with flag 0, sound 0x1d, lock 0x5767950=0; price before the rand on the buy.
- 33 ([0x7879fbc]=0); 34 (zen, 0x550d10 inside the access, sound 0x25); 81; 82/87.
- 83: tables 0x4e60c0/0x4e60dc, pending args 9bc/9c0/9c4/9c8, 0x4e6f80 vs 0x449d80, lock 0x78abb39.
- 86: tables 0x4da3a8/0x4da38c (except F1), texts 593-596, 649, 689, 1208, 1211.
- BC: 0x4e4547 table 0x4e60a8, 0x4da540 table 0x4da620, texts 1800/1801/1811/1812/1815/1816/1817/868, "%s%s %s", 0x402e50, bundle int8/10-1.
- AF 01 (0x628521), D0 00 (0x62a42b), 0xa8 (474, 0x78abb26), 0x91 cancel/Enter/answers.
- Keypad: 0x5e7b50 hit test; 0x5e7bf0 (sound, erase, cancel, OK only with 4 equal digits); mode table 0x5e9af0 (2..6, default); mode 5 memcmp 4; mode 6 = 31 clear + 0x657a90 + 99 C3; 83 layout vs GS (type +3, pass +4, number +6, size 0x10); 99 = 0x0D; 2 rand per C3 in modes 2/3/6; the 40-rand shuffle is identical in 0x5e7aa0/0x5e7d80/0x5d9b8f/0x5c00e4; withdraw path shuffle before the record close.
- StepTheWarehouse (0x5ea060), StepTheNpcShop, repair-all pricing, StepTheChaosBox, mix button JNC.
- PetTrainer.cpp (all, prices {0, 5000000, 1000000}); JewelMixAnswers.cpp and Protocol/JewelMix.cpp; DialogAnswers.cpp; GameDialogSteps.cpp 600-900.
- c2c70df3 (DropFromOtherGridOnTheEquipment) vs 0x5c007c..0x5c1d90, including ECX=[0x77f3cc8] and 0x449d50(5, slot, 0, dst).
- GS 0.97 layouts of 81/32/33/34 (item width mismatch already P-C190).
- Privacy: the warehouse password is never logged (apart from I4, unreachable).

## Not covered
- Deposit path 0x627100..0x6278f3 (inline record close with a rand at 0x6278c3), not fully traced.
- 0x5ca250 after 0x5ca385.
- JewelMix.cpp steps in depth; the ChaosMix type (ebaf89f0).
- Drawing, jewel dialog draws and the 0x6e5660 list (other reviewer).
- MSVC narrowing run; no build was made, tests were not run.
- Whether the tests cover maps 0, 2 and each case's map was not audited per test.
