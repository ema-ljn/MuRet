#pragma once

#include "Chat/ChatLog.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// The game UI's globals that the UI step (0x636ded..0x63705b), the dialogs (0x623e70), MoveHero and the frame's
// picking read and write (06 § HUD y ventanas, "El clic es primero de la UI"). All in the .bss unless they say
// otherwise; no scene zeroes them outside the places cited.
namespace Client::Ui
{
// 0x77f36d4: 0x553520's latch, one int per virtual key. Shared by Esc (0x623e70 and 0x490490), the chat keys, the
// panel keys, Q/W/E, B, R and the siege panel's Tab: the first to read a key down sets its latch to 1 and the rest see
// it as already read until it is released.
inline constexpr std::size_t VirtualKeyCount = 256;
// VK_MENU, the hover's Alt (0x5bbd6a).
inline constexpr std::size_t AltVirtualKey = 0x12;

// 0x553520(vk), with that key's latch: with the key down and its latch at 0, sets it to 1 and returns true; with the
// key up, resets it to 0 and returns false. GetAsyncKeyState reads the keyboard at that moment: MuRet reads the
// frame's state (Input::InputState).
inline bool LatchTheKey(std::int32_t& latch, bool down)
{
    if (!down)
    {
        latch = 0;
        return false;
    }
    if (latch != 0)
        return false;
    latch = 1;
    return true;
}

// Where 0x78abaa8 points: MuRet keeps the grid or the equipment and the index.
struct HoveredItem
{
    enum class Place : std::uint8_t
    {
        None,
        InventoryCell,  // 0x78a9bb0 + index · 0x48
        Equipment,      // [0x7681eb4] + 0xb6c + lugar · 0x48
        OwnTradeCell,   // 0x78a92a8 + index · 0x48 (CL9 milestone 7)
        OtherTradeCell, // 0x78a5a20 + index · 0x48
        ShopCell,       // 0x78ab0b8 + index · 0x48, the personal shops' grid (CL9 milestone 7, phase 5)
        NpcGridCell,    // 0x78a7070 + index · 0x48, the shop and the warehouse (CL9 milestone 6)
        ChaosBoxCell,   // 0x7d2d380 + index · 0x48
        TrainerBoxCell, // 0x723c80 + index · 0x48
    };
    Place place = Place::None;
    std::size_t index = 0;
};

struct GameUiState
{
    // 0x775857c: 0x636e04 zeroes it each step, the UI sets it to 1, and MoveHero (0x59b37a, 0x59b853), 0x57cfc0
    // (0x57cff2) and the picking (0x59ec80) read it.
    std::uint8_t mouseOnUi = 0;
    // 0x78aba28 and 0x78aba2c (.bss 0): the corner of the right-side NPC windows. Set to (0x104, 0) by the steps and
    // the draws of the event NPC windows (0x5e5920, 0x5f58f0), the shop (0x5f4720), the warehouse (0x5f4e30), the chaos
    // box (0x5febf0) and the siege. Read by the inventory (0x5e4e9e) for the mouse over the UI, the 3D items and the
    // grids' digits in the same frame (0x5f7610, 0x5f7840), those windows' steps (0x5ed960, 0x5ea060 and 0x5fe610),
    // their grids in the inventory (0x5e571c) and the cursor item (0x5d8d50).
    std::int32_t npcWindowX = 0;
    std::int32_t npcWindowY = 0;
    // 0x6c9320 (.data −1): the HUD's skill tooltip. 0x636dfa resets it to −1 each step.
    std::int32_t skillTooltip = -1;
    // 0x78a9bac and 0x78a9ba8 (0x5a06a4, 0x5a06ba).
    std::int32_t skillTooltipX = 0;
    std::int32_t skillTooltipY = 0;
    // 0x78abaa8: the item under the mouse, a pointer in the original. 0x636df2 zeroes it each step, the inventory and
    // trade grids (0x5c429e) point it at the cell and the equipment (0x5c3aad) at the slot; picking up zeroes it. Read
    // by Ctrl+Q/W/E (0x5c4b09) and the tooltip.
    HoveredItem hoveredItem{};
    // 0x78aba27: 0x5c4110's last argument, or 0 from the equipment (0x5c4298, 0x5c3ab3).
    std::uint8_t field78aba27 = 0;
    // 0x78abaa4: the item under the mouse is from the personal shops' grid. Only 0x5c4110 writes it: 0 on entry
    // (0x5c4163, past its three exits) and 1 with the mouse over an item of 0x78ab0b8 (0x5c4324). The tooltip's price
    // reads it (0x5b2f7f).
    std::uint8_t shopGridHovered = 0;
    // 0x78abb28 and 0x78abb2c: which window and slot the tooltip's item is from (0x5aed40): 0 and the inventory slot
    // (the cell + 12, or the equipment one) from the inventory; 2 and 3 with the cell from one's own trade grid and
    // from the other's.
    std::int32_t field78abb28 = 0;
    std::int32_t field78abb2c = 0;
    // 0x5762eb8: the window under the mouse. 0x636e2f zeroes it each step, the chat log (0x45aa80) sets it to its id,
    // and 0x5ee140 and the unowned click (0x636e66) read it.
    std::int32_t hovered = 0;
    // 0x5762eb4: the window that grabbed the mouse (the chat log's border or bar). With any other than 0, 0x623e70
    // (0x637c4a), the /move window (0x5ecdd1) and the panels (0x5ee167) do not run.
    std::int32_t capture = 0;
    // 0x5762ec0: the list that takes the arrow keys. The chat log sets it and the unowned click with the chat closed
    // zeroes it (0x636ead).
    std::int32_t focusedList = 0;
    // 0x5762ec4: the pressed old button (0x45b200), until it is released over it or the mouse leaves.
    std::int32_t pressedButton = 0;
    // 0x7798bf4 (the bar's button, 0x5a06e7).
    std::uint8_t skillListOpen = 0;
    // 0x6c7a88 (.data 1): the chat log's place, which its constructor (0x45b620) zeroes from WinMain before the first
    // scene; and 0x6c8dd1 (.data 1), F2, the normal chat in view.
    Ui::ChatModes chatModes{};
    // 0x6c8e68: while the chat field is open, its EDIT has the focus (the unowned click does not take it, 0x636e80).
    bool chatOpen = false;
    // 0x576793c: the exit asked for from the Esc menu (0x62411c). With 1, Open does not open (0x6599a0).
    bool logoutRequested = false;
    // 0x78abb39: the lock of the outgoing C1 .. 24. With 1, Esc does nothing in 0x623e70 (0x623eff) and the equipment
    // and the grid do not run (0x5bec50, 0x5c4110). The sends set it to 1 (0x5ca557, 0x5bf294, 0x59bf56) and the
    // encrypted 24 to 0 (0x4d1400).
    std::uint8_t field78abb39 = 0;
    // 0x5767954: above 0, the equipment step (0x5bec50) and the grids' (0x5c4110) return without doing anything. Using
    // an item sets it to 10 (0x5a2bf7, 0x5a3619... and 0x5c592d, CL9 milestone 5) and the replies reset it to 0: the 26
    // with 0xfd (0x4d35dc), the 28 (0x4c5ec0), the 2A (0x4d3cc0) and the 2C (0x4de500).
    std::int32_t field5767954 = 0;
    // 0x78abb26: the third byte of the C1 06 26 (0x5a2e61, 0x5a3883, 0x5c5ba4...). Two dialogs of 0x623e70 set it to 1
    // and 0 (0x628bdc and 0x6293b5, milestone 6) before the item is used; until then, the .bss 0.
    std::uint8_t field78abb26 = 0;
    // 0x78abad9: with 1, 0x5ee140 does not run its children (0x5ee181) and 0x5ebca0 does nothing. It is the duel
    // (CL9.4, P-C59).
    std::uint8_t field78abad9 = 0;
    // 0x78abaac: with 1, 0x5ee140 does not call 0x5d8d50 (0x5ee1de). Milestone 4: 0x5bf1fb and 0x5c2c56 (in 0x5bec50)
    // set it to 1 and 0x5e5212 to 0.
    std::uint8_t field78abaac = 0;
    // 0x78abb20: the warehouse keypad's mode. With 0, 0x5e7bf0 returns 0 and 0x5ee140 goes on. Set by 0x5e7aa0
    // (Npc::OpenTheKeypad), dropping on the inventory with the lock (0x5cb432) and the warehouse's button 2 (0x5ea227).
    // Also by 0x5d9c19 (the yes with 0x78abb14 at 4), with 6, and 0x5c0139 (dropping a warehouse item on the equipment
    // with the lock).
    std::int32_t field78abb20 = 0;
    // 0x7d36880: the right click's edge. WM_RBUTTONDOWN sets it if the button was not down, WM_RBUTTONUP clears it (02,
    // the WndProc table) and whoever uses it clears it.
    bool rightClick = false;
    // 0x7d36849: the left double click (WM_LBUTTONDBLCLK, 0x4b47b6), until the next message (0x4b3444), from
    // MouseMessageState (P-C182, StepTheDialogs). The friend lists (0x461f00, the chat with a friend) and the letter
    // lists (0x464510, reading the letter) read and clear it.
    bool doubleClick = false;
    // 0x7d3685c: WM_RBUTTONDOWN sets it, WM_RBUTTONUP clears it and whoever uses it clears it (the chat log, 0x45d760).
    bool rightDown = false;
    // 0x7d36840: the right release's edge. WM_RBUTTONDOWN clears it (0x4b3d15), WM_RBUTTONUP with the button down sets
    // it (0x4b40ac) and 0x57cfc0 clears it when it releases the charging skill (0x57d13e).
    bool rightReleased = false;
    // 0x7d368a4: the charging skill's frames (the 0x28 as the 0x3a). 0x57cfc0 sets it to 1 at the start (0x57d1a0),
    // raises it each step with the right button down and resets it to 0 on release or with an action that does not
    // allow skills (0x57d495).
    std::int32_t rightHoldFrames = 0;
    // 0x7d368a1: the warehouse's automatic move (0x5c9e10), consumed by 0x5ca250. The coordinates stand in for the
    // mouse's for a while; the physical mouse does not change.
    bool automaticMove = false;
    std::int32_t automaticMouseX = 0;
    std::int32_t automaticMouseY = 0;
    // 0x6c8e3c (.data 1): written by the F3 30 (bit 0x01, 0x4dd7d7), the Esc menu (0x625632) and the return to the
    // selection (0x4c2533, to 1). Its readers in MuRet: the picking (0x59ee4b), with the previous pick (0x6c8e30), and
    // MoveHero (0x59b22f and 0x59b89e) and 0x57cfc0 (0x57d1bc), with 0x6c8e38 other than −1, which nobody in MuRet
    // writes yet: the attack (0x59d3fd) and the skills are milestones 3 and 4 (P-C162).
    bool autoAttack = true;
    // 0x7d3a118: WinMain reads it from the registry (VolumeLevel, 0x4bad33..0x4bad55) and, outside 0 to 9, sets 5; then
    // 0x632d80 (0x41eed0's DirectSound, from CL10). MuRet has no registry: 5. Changed by the options (0x6447d9) and the
    // game's Esc menu (0x6257b8).
    std::int32_t volume = 5;
    // 0x6c9dcc (.data 4): the items' glow detail (Models/ItemGlow.h). Changed by the options (0x64480a) and the game's
    // Esc menu (0x62589b).
    std::uint8_t glowDetail = 4;
    // 0x7d366d8: (short) HIWORD(wParam) / 120 of the last WM_MOUSEWHEEL (0x4b4c19). Nobody zeroes it per step: it stays
    // until someone uses it (the chat log, 0x479960; the menu's volume, 0x625782).
    std::int32_t wheel = 0;
    // 0x77f36d4.
    std::array<std::int32_t, VirtualKeyCount> keyLatches{};
    // 0x78abb38: the floor items' names shown. The hover's Alt edge (0x553520(0x12), 0x5bbd6c) flips it (0x5bbd7f) and
    // only that draw reads it (0x5bbd86).
    std::uint8_t altNames = 0;
    // 0x5763588: the WndProc sets it (0x4b50ff, except in Chaos Castle outside the safe zone), the dialogs read and
    // clear it (0x6263df and 0x62da58) and the frame's tail clears it (0x63b0b7).
    bool enterPressed = false;
    // SendMessageA(hWnd, WM_DESTROY) of dialogs 0x70, 0x6f, 0x1a and 0x1c (0x62828e): the game closes.
    bool destroyRequested = false;
    // 0x7d3a120 and 0x7d3a124: the volume bar's corner, written by the options' draw (0x62db60) and read by its step
    // (0x62570a).
    std::int32_t volumeBarX = 0;
    std::int32_t volumeBarY = 0;
    // 0x7d3a128
    std::uint8_t volumeDragging = 0;
    // 0x41eed0(v): what the menu asks DirectSound for when the volume changes (0x6257e5 and 0x6257fa), −10000 at 0 or
    // −2000 · log10(10 / v). The CL10 hook, like WorldRuntime::lastSoundId's sound.
    std::optional<std::int32_t> volumeRequest{};
};

} // namespace Client::Ui
