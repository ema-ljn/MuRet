#pragma once

#include "Ui/OldControls.h"
#include "Ui/OldWindowBase.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The guild, the union and the war in the game (CL9 milestone 8; 06 § El gremio): the .bss and .data globals the
// packets write (0x4d74b0..0x4d9e50), what the two guild windows ([0x7d536f0] and [0x7d536f4]) keep from those
// packets, and window 2 with its buttons (Guild/SecondGuildWindow, phase 3). The guild window, with its tabs and its
// lists (phases 4 and 5), is in Guild/GuildWindow and Guild/GuildWindowTabs.
namespace Client::Guild
{
// 0x447b50: the guild table ([0x723c58], entries at 0x787aa48) has room for 2000.
inline constexpr std::size_t GuildTableSize = 2000;
// 0x447b90: the 32-byte mark, unpacked into 64 nibbles (+0x16..+0x55).
inline constexpr std::size_t GuildMarkNibbles = 64;
// 0x787a5e8..0x787aa47: 80 slots of 0xe bytes, the members of the hero's guild (0x4d7b60).
inline constexpr std::size_t GuildMemberSlots = 80;
// 0x4d9940 and 0x6570b0: the +0x1f2 index without a guild (−1) and the one 0x6570b0 puts back to −1 (*inferred*: the
// guild being created).
inline constexpr std::int16_t NoGuildIndex = -1;
inline constexpr std::int16_t CreatingGuildIndex = 1999;

// A 0x58-byte entry of 0x787aa48.
struct GuildTableEntry
{
    // +0: the guild's key (0x447b00 looks it up, 0x447b50 sets it). 0x447ae0 leaves them at −1: the GS sends key 0 for
    // "no guild", and with 0 the search would find entry 0.
    std::int32_t key = -1;
    // +4..+0xc: the union name (8 bytes and its zero, 0x447b90 from the 66's pkt[9]). The bubble draws it as "<%s>"
    // (0x556dd0) and the E6 that breaks the union erases it (0x4d9b2b).
    std::array<char, 9> unionName{};
    // +0xd..+0x15: the guild name (0x447b90 from the 66's pkt[0x11]). The bubble draws it as "[%s]" and the 62 looks
    // for the rival in it (0x4d832c).
    std::array<char, 9> guildName{};
    // +0x16..+0x55: the mark in nibbles.
    std::array<std::uint8_t, GuildMarkNibbles> mark{};
};

// A 0xe-byte slot of 0x787a5e8 (0x4d7b60).
struct GuildMemberSlot
{
    // +0..+9 from the 52's entry, +0xa at 0.
    std::array<char, 11> name{};
    std::uint8_t number = 0; // +0xb
    // +0xc: with bit 7 of the 52, its low 7; without it, 0xff.
    std::uint8_t server = 0;
    std::uint8_t status = 0; // +0xd
};

// A 0x14-byte element of the member list's deque (0x46c930): the click on its row flips +0 (0x46d3a0) and +4..+0x11
// is the member's slot.
struct GuildMemberRow
{
    std::int32_t toggled = 0;
    GuildMemberSlot slot{};
};

// A 0x40-byte element of the notice list's deque (0x46bca0, from 0x575d230): the click on its row flips +0 (0x46c3d0)
// and +4 is the notice row (strcpy, no limit: 0x459c70's rows are 0x6e wide).
struct GuildNoticeRow
{
    std::int32_t toggled = 0;
    std::string text;
};

// A 0x54-byte element of the union list's deque (0x46d900, from 0x5762e50): the click flips +0 (0x46e1e0), +4..+0x43
// is the mark in nibbles, +0x44..+0x4b the name (strncpy of 8) with +0x4c at 0 and +0x50 the number (pkt[0] of the
// E9's row, in a DWORD).
struct GuildUnionRow
{
    std::int32_t toggled = 0;
    std::array<std::uint8_t, GuildMarkNibbles> mark{};
    std::array<char, 9> name{};
    std::uint8_t byte0 = 0;
};

// The guild window, [0x7d536f0] (0x64f3e0), with its three lists on Ui::OldList (the elements, in their owner). The
// guild window's id: the original takes it from counter 0x5762eb0 like those of window 2; MuRet gives it −10, after
// those of window 2 (P-C280).
inline constexpr std::int32_t GuildWindowId = -10;

// 0x64f3e0: the base window (0x45a210) at (0x1c2, 0), 0xbe × 0x17c (0x45a9d0 and 0x45a9f0), which nobody moves.
[[nodiscard]] inline Ui::OldWindowBase GuildWindowBase()
{
    Ui::OldWindowBase base;
    base.id = GuildWindowId;
    base.x = 0x1c2;
    base.y = 0;
    base.width = 0xbe;
    base.height = 0x17c;
    return base;
}

// The member list, +0x94 of [0x7d536f0] (0x46c470, vtable 0x6b971c): at (0x1d2, 0x159), 0x9d × 0xeb, hanging from its
// bottom edge (+0x78 at 2), 18 rows (+0x104), up to 0x96 elements, with the arrows (+0xbc at 1) and the windows' bar
// (+0x12c at 1). No parent (+0x44 at 0). Its id comes from counter 0x5762eb0: MuRet gives it −11 (P-C280).
inline constexpr std::int32_t GuildMemberListId = -11;

[[nodiscard]] inline Ui::OldList GuildMemberList()
{
    Ui::OldList list;
    list.base.id = GuildMemberListId;
    list.base.x = 0x1d2;
    list.base.y = 0x159;
    list.base.width = 0x9d;
    list.base.height = 0xeb;
    list.base.rectMode = 2;
    list.rows = 0x12;
    return list;
}

// 0x64f3e0: the master's three buttons in the members tab (0x45ad80), 0x32 × 0x12 at y + 0x15e, with the command
// (0x45b150) and the Text.bmd text (0x45b170): +0x4b4 at x + 0x12 (2, 1307, the position), +0x53c at x + 0x46 (3,
// 1308, remove it) and +0x5c4 at x + 0x7a (4, 1309, kick the member). They hang from the window (+0x44 with its id).
// Ids: −12..−14 (P-C280).
inline constexpr std::size_t GuildAssignButton = 0;
inline constexpr std::size_t GuildDismissButton = 1;
inline constexpr std::size_t GuildKickButton = 2;
inline constexpr std::size_t GuildMemberButtonCount = 3;

[[nodiscard]] inline std::array<Ui::OldButton, GuildMemberButtonCount> GuildMemberButtons()
{
    constexpr std::array<std::int32_t, GuildMemberButtonCount> Lefts{0x12, 0x46, 0x7a};
    std::array<Ui::OldButton, GuildMemberButtonCount> buttons{};
    for (std::size_t index = 0; index < buttons.size(); ++index)
    {
        Ui::OldButton& button = buttons[index];
        button.base.id = GuildMemberListId - 1 - static_cast<std::int32_t>(index);
        button.base.parent = GuildWindowId;
        button.base.x = 0x1c2 + Lefts[index];
        button.base.y = 0x15e;
        button.base.width = 0x32;
        button.base.height = 0x12;
        button.command = 2 + static_cast<std::int32_t>(index);
    }
    return buttons;
}

// The notice list, +0x1c8 (0x46b800, vtable 0x6b95e8): hanging from its bottom edge, 6 rows, up to 0x96 elements,
// with the arrows (+0xbc at 1), the windows' bar (+0x114 at 13) and no parent. It stays at (0, 0), 100 × 100
// (0x45a210), until tab 0's draw (0x64fdf0: 0x88 × 0x50), and the union list hangs from its height (Q-C1293). The
// union list, +0x2f8 (0x46d440, vtable 0x6b97bc), the same, and its tab's draw sets its size (0x651400: 0xa0 × 0x50).
// Ids from counter 0x5762eb0: MuRet gives them −15 and −16 (P-C280).
inline constexpr std::int32_t GuildNoticeListId = -15;
inline constexpr std::int32_t GuildUnionListId = -16;

[[nodiscard]] inline Ui::OldList GuildSmallList(std::int32_t id)
{
    Ui::OldList list;
    list.base.id = id;
    list.base.rectMode = 2;
    list.rows = 6;
    return list;
}

// 0x64f3e0: the other three 0x32 × 0x12 buttons hanging from the window (0x45ad80, 0x45b150's command and 0x45b170's
// text): +0x42c (1, no text, 0x6e57a4) at (x + 0x46, y + height − 0x12), leave or disband the guild in tab 0; +0x64c
// (7, no text) at (x + 0x46, y + 0x15e) and +0x6d4 (8, 1422) at (x + 0x46, y + 0xdc), leave the union or kick a guild
// out of it in tab 2. Ids −17..−19 (P-C280).
inline constexpr std::size_t GuildLeaveButton = 0;
inline constexpr std::size_t GuildUnionLeaveButton = 1;
inline constexpr std::size_t GuildUnionKickButton = 2;
inline constexpr std::size_t GuildTabButtonCount = 3;

[[nodiscard]] inline std::array<Ui::OldButton, GuildTabButtonCount> GuildTabButtons()
{
    constexpr std::array<std::int32_t, GuildTabButtonCount> Commands{1, 7, 8};
    constexpr std::array<std::int32_t, GuildTabButtonCount> Tops{0x17c - 0x12, 0x15e, 0xdc};
    std::array<Ui::OldButton, GuildTabButtonCount> buttons{};
    for (std::size_t index = 0; index < buttons.size(); ++index)
    {
        Ui::OldButton& button = buttons[index];
        button.base.id = GuildUnionListId - 1 - static_cast<std::int32_t>(index);
        button.base.parent = GuildWindowId;
        button.base.x = 0x1c2 + 0x46;
        button.base.y = Tops[index];
        button.base.width = 0x32;
        button.base.height = 0x12;
        button.command = Commands[index];
    }
    buttons[GuildLeaveButton].label = std::string();
    buttons[GuildUnionLeaveButton].label = std::string();
    return buttons;
}

struct GuildWindowState
{
    Ui::OldWindowBase base = GuildWindowBase();
    // +0x80: the tab (0x64f3e0 leaves it at 1) and +0x84, the E9 request already made.
    std::int32_t tab = 1;
    std::int32_t unionRequested = 0;
    // +0x88..+0x90: the name from the 52's pkt[0xd..0x14] (0x651650) and its zero.
    std::array<char, 9> name88{};
    // +0x94: the member list (0x6517a0 adds 0x787a5e8's entry at the front, 0x6517d0 empties it) and its deque, in the
    // deque's order.
    Ui::OldList memberList = GuildMemberList();
    std::vector<GuildMemberRow> members;
    // +0x130 of the list: the first one to enter the empty list is the hero (0x46c992).
    std::int32_t heroFirst = 0;
    // +0x4b4, +0x53c and +0x5c4.
    std::array<Ui::OldButton, GuildMemberButtonCount> memberButtons = GuildMemberButtons();
    // +0x1c8: the guild notice list (0x651680 adds its rows, 0x651790 empties it) and its deque.
    Ui::OldList noticeList = GuildSmallList(GuildNoticeListId);
    std::vector<GuildNoticeRow> notice;
    // +0x2f8: the union list (0x6517e0 adds, 0x651830 empties) and its deque.
    Ui::OldList unionList = GuildSmallList(GuildUnionListId);
    std::vector<GuildUnionRow> unions;
    // +0x42c, +0x64c and +0x6d4. MuRet copies +0x6d4's text (1422) the first time it draws it.
    std::array<Ui::OldButton, GuildTabButtonCount> tabButtons = GuildTabButtons();
    // +0x428: the panels' dialog it opened (what 0x6600d0 gave).
    std::uint32_t dialog = 0;
};

// 0x654900: window 2's buttons (0x45ad80), in the order of their offsets, with the command and the Text.bmd text the
// vtable + 0x2c gives them (0x45b150 and 0x45b170): +0x80 Create (1, 1303), +0x108 Change the mark (2, 1304), +0x190
// Next (5, 1305) and +0x218 Back (4, 1306).
inline constexpr std::size_t SecondGuildCreateButton = 0;
inline constexpr std::size_t SecondGuildMarkButton = 1;
inline constexpr std::size_t SecondGuildNextButton = 2;
inline constexpr std::size_t SecondGuildBackButton = 3;
inline constexpr std::size_t SecondGuildButtonCount = 4;
// The ids of the window and its buttons. The original takes them from counter 0x5762eb0 (0x459ef0) when WinMain
// builds the registry; MuRet gives them −5..−9, after those of the panels' dialog (P-C280), which do not clash with
// the old windows' ones.
inline constexpr std::int32_t SecondGuildWindowId = -5;

// 0x654900: the base window (0x45a210) at (0x1c2, 0), 0xbe × 0x5a, which nobody moves (*static*: only the constructor
// calls 0x45a9d0 on [0x7d536f4]).
[[nodiscard]] inline Ui::OldWindowBase SecondGuildWindowBase()
{
    Ui::OldWindowBase base;
    base.id = SecondGuildWindowId;
    base.x = 0x1c2;
    base.y = 0;
    base.width = 0xbe;
    base.height = 0x5a;
    return base;
}

// 0x654926..0x654a7a: the four buttons, hanging from the window (+0x44 with its id). Page 0's draw (0x6563e0) places
// the first two, 100 × 20; the other two, 50 × 18, stay at (x + 0x61, y + 0x168) and (x + 0x2d, y + 0x168).
[[nodiscard]] inline std::array<Ui::OldButton, SecondGuildButtonCount> SecondGuildButtons()
{
    constexpr std::array<std::int32_t, SecondGuildButtonCount> Commands{1, 2, 5, 4};
    constexpr std::array<std::int32_t, SecondGuildButtonCount> Widths{100, 100, 0x32, 0x32};
    constexpr std::array<std::int32_t, SecondGuildButtonCount> Heights{20, 20, 0x12, 0x12};
    constexpr std::array<std::int32_t, SecondGuildButtonCount> Lefts{0, 0, 0x1c2 + 0x61, 0x1c2 + 0x2d};
    constexpr std::array<std::int32_t, SecondGuildButtonCount> Tops{0, 0, 0x168, 0x168};
    std::array<Ui::OldButton, SecondGuildButtonCount> buttons{};
    for (std::size_t index = 0; index < buttons.size(); ++index)
    {
        Ui::OldButton& button = buttons[index];
        button.base.id = SecondGuildWindowId - 1 - static_cast<std::int32_t>(index);
        button.base.parent = SecondGuildWindowId;
        button.base.x = Lefts[index];
        button.base.y = Tops[index];
        button.base.width = Widths[index];
        button.base.height = Heights[index];
        button.command = Commands[index];
    }
    return buttons;
}

// Guild window 2, [0x7d536f4] (0x654900): its base, its buttons and the dialogs it opened.
struct SecondGuildWindowState
{
    Ui::OldWindowBase base = SecondGuildWindowBase();
    // MuRet copies the texts (0x45b170) the first time it draws it, like those of the panels' dialog.
    std::array<Ui::OldButton, SecondGuildButtonCount> buttons = SecondGuildButtons();
    // +0x2a0 and +0x2a4: the panels' dialogs it opened (0x654f50 cancels them when it closes).
    std::uint32_t dialog2a0 = 0;
    std::uint32_t dialog2a4 = 0;
};

// The positions window (0x64e600 and 0x64f1c0, the hooks 0x6602b0 sets in the panels' dialog): its two 0x32 × 0x12
// global buttons (0x45ad80 from 0x64e590 and 0x64e5d0, before WinMain; the command and the text of 0x45b150 and the
// size of 0x45a9f0 in 0x64f3e0): 0x7d535b8 (5, 228) and 0x7d53640 (6, 229), hanging from the guild window (+0x44
// with its +0x40). Its draw places them. Ids from counter 0x5762eb0: MuRet gives them −20 and −21 (P-C280).
inline constexpr std::size_t GuildPositionsOkButton = 0;
inline constexpr std::size_t GuildPositionsCancelButton = 1;
inline constexpr std::size_t GuildPositionsButtonCount = 2;
inline constexpr std::int32_t GuildPositionsFirstId = -20;
// 0x7d536d8: the chosen position, the assistant's (0x40, the one 0x64f3e0 leaves) or the battle master's (0x20).
inline constexpr std::int32_t AssistantChoice = 0x40;
inline constexpr std::int32_t BattleMasterChoice = 0x20;

[[nodiscard]] inline std::array<Ui::OldButton, GuildPositionsButtonCount> GuildPositionsButtons()
{
    std::array<Ui::OldButton, GuildPositionsButtonCount> buttons{};
    for (std::size_t index = 0; index < buttons.size(); ++index)
    {
        Ui::OldButton& button = buttons[index];
        button.base.id = GuildPositionsFirstId - static_cast<std::int32_t>(index);
        button.base.parent = GuildWindowId;
        button.base.width = 0x32;
        button.base.height = 0x12;
        button.command = 5 + static_cast<std::int32_t>(index);
    }
    return buttons;
}

struct GuildPositionsState
{
    // 0x7d536d0 and 0x7d536d4: the box corner that the draw leaves (0x6576a0 with 0x96) and the next frame's step
    // reads; 0x64f3e0 sets them to 0.
    std::int32_t x = 0;
    std::int32_t y = 0;
    // 0x7d536d8. Nobody puts it back to 0x40 on opening: the last one stays.
    std::int32_t choice = AssistantChoice;
    // 0x7d535b8 and 0x7d53640. MuRet copies the texts (0x45b150) the first time it draws them.
    std::array<Ui::OldButton, GuildPositionsButtonCount> buttons = GuildPositionsButtons();
};

struct GuildState
{
    // 0x787a5e8 and 0x787aa48: the two arrays are contiguous, and the original writes to one from the other (Q-C1281).
    std::array<GuildMemberSlot, GuildMemberSlots> members{};
    std::array<GuildTableEntry, GuildTableSize> table{};
    // [0x723c58] + 4 (0x723c5c): how many entries the table has.
    std::uint32_t tableCount = 0;
    // 0x78aba38: the 52's member count; −1 on leaving (0x4d7d1a, 0x4e4327).
    std::int32_t memberCount = 0;
    // 0x78aba3c: the 52's score, without negatives.
    std::int32_t score = 0;
    // 0x78aba40: the key of whoever asked to join (the 50).
    std::int32_t requester = 0;

    // The war. 0x576796c: at war (the 62 and the 64 turn it on, the 63 and the 53 turn it off).
    std::uint8_t warOn = 0;
    // 0x576796d: the 61 and the 62 with type 1 turn it on; the 63 turns it off.
    std::uint8_t warType = 0;
    // 0x576796e: the 62's team (pkt[0xc]).
    std::uint8_t warTeam = 0;
    // 0x5767804..0x576780c: the rival's name (8 bytes and its zero).
    std::array<char, 9> rival{};
    // 0x5767816 (the WORD the 63 sets to 0, 0x4d8958) is EventGlobals::timeValue, the F3 22's time (08 P-C201).
    // 0x57678b4 and 0x57678b8: the 64's scores.
    std::int32_t ourScore = 0;
    std::int32_t theirScore = 0;
    // 0x6c82f0 (.data −1): the rival's index in the table.
    std::int32_t rivalIndex = -1;
    // 0x6c6498 (WORD, .data −1) and 0x6c649a (.data 0xff): the hero's guild index and rank, which the 65 copies from
    // +0x1f2 and +0x368 when the entry is the hero's (0x412120). *Static*: only 0x412140 reads them, map 30's panel
    // (the siege, CL9.5).
    std::int16_t heroGuildIndex = -1;
    std::uint8_t heroGuildStatus = 0xff;

    // The E5's union request (0x6565e0): 0x7d536e0 the relation, 0x7d536ea the request, 0x7d536e8 and 0x7d536e9 the
    // key.
    std::uint8_t relation = 0;
    std::uint8_t relationRequest = 0;
    std::uint8_t relationKeyHigh = 0;
    std::uint8_t relationKeyLow = 0;

    // 0x7d536e4 and 0x7d536ec: what window 2 builds (1, the new guild; 2, the mark, which only 0x6538e0 turns on,
    // without references: Q-C1288) and which page it is on (0..3); 0x6570b0 puts them back to 0.
    std::int32_t createKind = 0;
    std::int32_t createStep = 0;
    // 0x78aba44: the chosen color of the mark editor's palette (0x6535c0 changes it, 0x653750 draws it).
    std::int32_t markColor = 0;

    // 0x7d535a8..0x7d535b3 and 0x7d535b4: the member chosen in the members tab (its name, which 0x64def0's E1 sends)
    // and its rank (for the positions window, 0x64e600).
    std::array<char, 12> chosenName{};
    std::uint8_t chosenStatus = 0;
    // 0x7d536c8: the slot in 0x787a5e8 of the one being kicked (0x64faa0, −1 if absent); 0x64e550 copies it to 0x6d5e2c
    // (.data −1), which the game's dialog 0x7d reads.
    std::int32_t kickIndex = 0;
    std::int32_t kickMember = -1;

    GuildWindowState window{};
    SecondGuildWindowState secondWindow{};
    GuildPositionsState positions{};
};

// 0x447b00(key): the first entry with the key, looking from 0 up to the count inclusive (one more: with the count at
// 0, entry 0, with the key at −1 from 0x447ae0). −1 if none. Past the 2000 it stops: the original would read outside
// the table.
[[nodiscard]] inline std::optional<std::int32_t> FindGuild(const GuildState& guild, std::int32_t key)
{
    for (std::size_t index = 0; index <= guild.tableCount; ++index)
    {
        if (index >= GuildTableSize)
            return std::nullopt;
        if (guild.table[index].key == key)
            return static_cast<std::int32_t>(index);
    }
    return -1;
}

// 0x447b50(key): with the count at 2000 or more, −1; otherwise, the key in the entry at the count, which goes up. The
// entry is not cleared: the names and the mark are whatever was there (zeros, in the .bss, or those from before
// 0x447ae0) until its 66 arrives. Only 0x447ae0 lowers the count.
[[nodiscard]] inline std::int32_t AddGuild(GuildState& guild, std::int32_t key)
{
    if (guild.tableCount >= GuildTableSize)
        return -1;
    const std::uint32_t index = guild.tableCount;
    guild.table[index].key = key;
    ++guild.tableCount;
    return static_cast<std::int32_t>(index);
}

// 0x447ae0 (on 0x723c58): the count at 0 and the 2000 keys at −1, without touching the names or the marks. Called by
// 0x723c58's constructor (0x447a80), the accepted F1 00 (0x4c1e54), the character list (0x4c1f14) and the game entry
// (0x636935).
inline void ResetTheGuildTable(GuildState& guild)
{
    guild.tableCount = 0;
    for (GuildTableEntry& entry : guild.table)
        entry.key = -1;
}

// 0x787a5e8 (the members, of 0xe) and 0x787aa48 (the table, of 0x58) are contiguous (Q-C1281): entry −1 starts at
// byte 10 of member 73's name, its +4 is byte 0 of member 74's name and its +0xd, byte 9.
inline constexpr std::size_t MemberBeforeTheTable = 74;

// A table name up to the first 0, as strcmp and sprintf read it.
template <std::size_t Size> [[nodiscard]] std::string_view GuildTextOf(const std::array<char, Size>& text)
{
    return {text.data(), static_cast<std::size_t>(std::ranges::find(text, '\0') - text.begin())};
}

// The guild (+0xd) of entry `index` as strcmp and sprintf read it: −1 gives byte 9 of member 74's name and the 0 of
// its byte 10 (+0xa, always 0). Outside the table, nullopt (the original would read other memory).
[[nodiscard]] inline std::optional<std::string_view> GuildNameAt(const GuildState& guild, std::int16_t index)
{
    constexpr std::size_t NameByteBeforeTheTable = 9;
    if (index == NoGuildIndex)
    {
        const auto& name = guild.members[MemberBeforeTheTable].name;
        return std::string_view(name.data() + NameByteBeforeTheTable, name[NameByteBeforeTheTable] == '\0' ? 0 : 1);
    }
    if (index < 0 || static_cast<std::size_t>(index) >= GuildTableSize)
        return std::nullopt;
    return GuildTextOf(guild.table[static_cast<std::size_t>(index)].guildName);
}

// The union (+4) of entry `index` as strcmp reads it: −1 gives member 74's name (its bytes 0 to 9 and the 0 of +0xa).
// Outside the table, nullopt.
[[nodiscard]] inline std::optional<std::string_view> UnionNameAt(const GuildState& guild, std::int16_t index)
{
    if (index == NoGuildIndex)
        return GuildTextOf(guild.members[MemberBeforeTheTable].name);
    if (index < 0 || static_cast<std::size_t>(index) >= GuildTableSize)
        return std::nullopt;
    return GuildTextOf(guild.table[static_cast<std::size_t>(index)].unionName);
}

// The first byte of the union (+4) of entry `index`: −1 gives byte 0 of member 74's name. Outside the table, nullopt.
[[nodiscard]] inline std::optional<char> UnionByteAt(const GuildState& guild, std::int16_t index)
{
    if (index == NoGuildIndex)
        return guild.members[MemberBeforeTheTable].name[0];
    if (index < 0 || static_cast<std::size_t>(index) >= GuildTableSize)
        return std::nullopt;
    return guild.table[static_cast<std::size_t>(index)].unionName[0];
}
} // namespace Client::Guild
