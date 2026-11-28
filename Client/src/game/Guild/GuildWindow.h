#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Render/Text/FontSet.h"
#include "Guild/GuildPanels.h"
#include "Guild/GuildState.h"
#include "Ui/OldWindowBase.h"
#include "Ui/PanelDialog.h"
#include "Ui/PanelRegistry.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace Client::Tables
{
class TextTable;
}

namespace Client::Render::Text
{
class ITextMeasure;
}

namespace Client::Ui
{
struct GameUiState;
}

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Ui
{
struct GameHudContext;
} // namespace Client::Ui

// The guild window, [0x7d536f0] (0x64f3e0, vtable 0x6baa58; CL9 milestone 8, phases 4 and 5; 06 § El gremio): the
// registry opens it with panel 0x10, steps it with 0x45aa80(0) (the vtable + 0x18, which calls + 0x20, 0x651840) and
// draws it with + 0x14 (0x651c20). It has three tabs (+0x80: 0, 1 and 2, from 1), each with its step (0x64fc00,
// 0x650350 and 0x650c10) and its draw (0x64fdf0, 0x6507d0 and 0x651400, Guild/GuildWindowTabs), and three lists on
// Ui::OldList: the members (+0x94), the notice (+0x1c8) and the union (+0x2f8).
namespace Client::Guild
{

struct GuildWindowStepContext
{
    Kernel::WorldState& world;
    // 0x7d3682c and 0x7d36828: the mouse in 640 × 480.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    // The screen for the notice of leaving the union (0x555190 cuts and measures in it).
    Core::PixelSize screen{};
    // What it sends (the E9) and the answer of the dialog it cancels: the session's GameServer.
    GuildSender send{};
    // 0x553520(vk), the edge of PgUp (0x21) and PgDn (0x22), and GetAsyncKeyState for the list's arrows (0x47b0f0).
    // Without them, the members tab stops.
    std::function<bool(std::uint8_t)> keyEdge{};
    std::function<bool(std::uint8_t)> keyDown{};
    // The step's GetTickCount(), the measurer and Text.bmd: the dialogs of the master's buttons (0x6600d0, texts 1367
    // to 1369). Without them, the button that opens one stops.
    std::uint32_t now = 0;
    const Render::Text::ITextMeasure* measure = nullptr;
    const Tables::TextTable* texts = nullptr;
};

// 0x6517a0 → 0x46c930, the vtable + 0x94 of the member list: without a name, nothing. With the list empty, +0x130 says
// whether the one entering is the hero (+0x1d9). It puts it at the front (the element of 0x575de08: +0 at 0 and the
// name with strncpy of 0xb), with more than 0x96 drops from the end (0x47b7e0), and chooses and scrolls: 0, one more
// of scroll with more than the rows, clamps it, 1 if it is the only one, and at the end the last (the count) with the
// scroll to the end (−10000).
void AddTheGuildMember(Kernel::WorldState& world, const GuildMemberSlot& slot);

// 0x6517d0 → 0x47a410, the vtable + 0x2c: empties it, sets +0 to 0 on all (0x47ab40, none left), chooses 0 and puts
// the scroll back to 0.
void ClearTheGuildMembers(GuildWindowState& window);

// 0x651680(text), the guild notice of the 0D type 2 (0x4c665f): 0x459c70 cuts it into up to 5 rows 0x6e wide with the
// DC's font (Chat::WrapByWidth), adds each non-empty row with the vtable + 0x94 of list +0x1c8 (0x46bca0: like
// 0x46c930, at the front, without the hero) and scrolls it by the count minus the rows (+0x34 with +0x38): the last
// one in stays visible.
[[nodiscard]] Core::Expected<void, std::string> AddTheGuildNotice(GuildWindowState& window,
                                                                  const Render::Text::ITextMeasure& measure,
                                                                  Render::Text::FontKind font, Core::PixelSize screen,
                                                                  std::string_view text);

// 0x651790 → 0x480260, the notice list's vtable + 0x2c: like 0x47a410.
void ClearTheGuildNotice(GuildWindowState& window);

// 0x6517e0(mark, name, number) → 0x46d900, the union list's vtable + 0x94: without a name (or empty), nothing;
// otherwise, the element of 0x5762e50 (+0 at 0, the mark, the name with strncpy of 8 and +0x4c at 0, the number at
// +0x50) at the front, like 0x46c930 without the hero.
void AddTheUnion(GuildWindowState& window, const std::array<std::uint8_t, GuildMarkNibbles>& mark,
                 std::string_view name, std::uint8_t number);

// 0x651830 → 0x4818b0, the union list's vtable + 0x2c: like 0x47a410.
void ClearTheUnions(GuildWindowState& window);

// 0x45aa80(0) on a registry window, without a parent and outside the manager (the two guild ones, like the chat log):
// with the mouse over it and another one under the mouse or grabbing it, it does not take its step. With the left
// button down, 0x490aa0 does not find it to bring it to the front (*inferred*).
[[nodiscard]] bool PassesTheGuildWindowGate(const Ui::OldWindowBase& base, Ui::GameUiState& ui, std::int32_t mouseX,
                                            std::int32_t mouseY);

// 0x651840: the mouse over 0xbe × 0x1b1 turns 0x775857c on; the click on the close button (0x18 × 0x18 at (0x19,
// 0x18b)) turns it off, leaves 0x77f3b10 at 0 and 0x6c8dcc at 6, plays 0x19 and closes the window (0x6522f0). With the
// hero in a guild (+0x368 other than 0xff) and the left button down, the tabs (0x34 × 0x10 at x + 0xf, 0x43 and 0x77,
// y + 0x4b): the chosen one cancels its own dialog (0x64fbd0) and the union one first asks once for the list (C1 03
// E9) if the hero's guild has a union (+4 of its entry). Then, the tab's step.
//
// 0x64fc00, tab 0: the notice list (0x480e00 with its row 0x46c3d0) with 0x5762eb4, and the +0x42c button. The union
// master (guild +0xd equal to the union +4 of its entry) gets notice 1270 (OK); the other master, the dialog to
// disband the guild (1363 to 1366, Confirm, 0x64de90); one who is not a master leaves: its slot in 0x787a5e8
// (0x64faa0 with its name) to 0x6d5e2c and the game's dialog 0x7d with the chat fields like 0x64de90.
//
// 0x650350, the members tab: 0x5762eb4 with the list's id, the list (0x45aa80(0) and 0x47b0f0, with its row 0x46d3a0)
// and 0x5762eb4 at 0; PgUp and PgDn scroll it by the rows it shows (the vtable + 0x38 and + 0x34). With the hero as
// master (+0x368 at 0x80), the three buttons (0x45b200) on the chosen one: the position one (not on the master)
// stores its rank and its name and opens the positions window (the empty panels' dialog with the hooks of 0x64e600
// and 0x64f1c0); remove (only on 0x40 or 0x20) and kick (not on the master) open the panels' dialog with Confirm and
// Cancel, answered by 0x64def0 and 0x64e550 (GuildPanels).
//
// 0x650c10, the union tab: list +0x2f8 (0x4824d0 with 0x46e1e0) with 0x5762eb4 and, with the hero as master and a
// union (+4 of its entry), +0x64c: the union master gets notice 1271; the other sends C1 07 E5 01 02 with its key and
// gives notice 1387 (0x555190, type 1). Then +0x6d4 on the chosen row that is not its guild: its name to 0x7d535a8
// and the dialog of 1423 with the name and 1369 (Confirm, 0x650830, the EB).
[[nodiscard]] Core::Expected<void, std::string> StepTheGuildWindow(const GuildWindowStepContext& context);

// 0x64e600, the positions window's step (+8 of the panels' dialog, after the mouse over its box): the mouse over
// 0xd5 × 0xfa from the corner the draw left belongs to the UI; with the click's edge, the 0xaa × 0x14 boxes at
// (x + 0x14, y + 0x13) and (x + 0x14, y + 0x29) choose 0x40 or 0x20. Button 0x7d535b8 sends C1 0F E1 (1 if the chosen
// one had no position, 2 if it had; the position and its name) and C1 03 52, and cancels the dialog (0x6603c0, with
// `answer`); then 0x7d53640 cancels it. Without `send`, the E1 stops.
[[nodiscard]] Core::Expected<void, std::string> StepTheGuildPositions(Kernel::WorldState& world,
                                                                      const GuildSender& send, std::int32_t mouseX,
                                                                      std::int32_t mouseY,
                                                                      const Ui::PanelDialogAnswer& answer);

// 0x657977: the step above in the registry. `context` is copied; the world must outlive the registry's context.
void LendTheGuildWindowStepTo(Ui::PanelContext& panels, const GuildWindowStepContext& context);

// 0x651c20: the windows' background without blending, the title (180, in bold, 0xffdcdcdc on 0) and, with the hero in
// a guild, "%s ( Score:%d )" (0xff64ffc8) or, without one, texts 185 to 187 (the normal font, 0xffe6e6e6); the close
// button with its help (220); with a guild, the three tabs (0x621aa0's lines under the others, 0x646 and texts 946,
// 1330 and 1352 centered in 52 · rx) and the tab's draw.
[[nodiscard]] Core::Expected<void, std::string> DrawTheGuildWindow(Ui::GameHudContext& context);
} // namespace Client::Guild
