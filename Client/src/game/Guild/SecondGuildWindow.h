#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Guild/GuildPanels.h"
#include "Ui/PanelRegistry.h"

#include <cstdint>
#include <span>
#include <string>

namespace Client::Tables
{
class TextTable;
}

namespace Client::Render::Text
{
class ITextMeasure;
}

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Hud
{
struct GameHudContext;
} // namespace Client::Hud

// Guild window 2, [0x7d536f4] (0x654900, vtable 0x6baa9c; CL9 milestone 8, phase 3; 06 § El gremio): a guild's
// creation in pages. The 54 (0x4e4279) and the Guild Master's 01 (0x4e3450) open it; the registry steps it with
// 0x45aa80(0) (the vtable + 0x18, which calls + 0x20, 0x656df0) and draws it with + 0x14 (0x656e70). The page is
// 0x7d536ec (GuildState::createStep): 0, the question (0x6563e0 and 0x655d70); 1, the name and the mark (0x655130
// and 0x654f80); 2, the mark alone (0x655c70 and 0x655c10, which only 0x6538e0 reaches, without references:
// Q-C1288); 3, the confirmation (0x655ac0 and 0x6552b0), which sends the C1 55. The new guild is the table's entry
// 1999 (0x78a5970): the name at +0xd (0x78a597d) and the mark at +0x16 (0x78a5986).
namespace Client::Guild
{

struct SecondGuildStepContext
{
    Kernel::WorldState& world;
    // 0x7d3682c and 0x7d36828: the mouse in 640 × 480.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    // The step's GetTickCount(): the clock of the dialog the mark button opens (0x6600d0).
    std::uint32_t now = 0;
    // The screen for the busy dialog's notice (0x6600f9: 0x555190 cuts and measures in it).
    Core::PixelSize screen{};
    // What 0x6600d0 measures its rows with; without it, the mark button stops.
    const Render::Text::ITextMeasure* measure = nullptr;
    // Text.bmd: the mark button's 1269, and the 457 and 458 that 0x622c70 checks. Without it, whatever reads them
    // stops.
    const Tables::TextTable* texts = nullptr;
    // FilterName.bmd and Filter.bmd, the words 0x622c70 checks.
    std::span<const std::string> filterName{};
    std::span<const std::string> filter{};
    // What it sends (the 54, the 55): the session's GameServer. Without it, whatever sends stops.
    GuildSender send{};
};

// 0x45aa80(0) and 0x656df0: the empty queue (nobody sends it messages: the buttons send their command to the window
// with their parent's id with 0x490b40, and window 2 is not in the manager, *inferred*); with the mouse over it
// (0xbe × 0x5a) and another window under the mouse or grabbing it, nothing. Otherwise, the mouse over the 0xbe × 0x1b1
// window turns 0x775857c on and the page takes its step.
[[nodiscard]] Core::Expected<void, std::string> StepTheSecondGuildWindow(const SecondGuildStepContext& context);

// 0x657930 (0x657959): the step above in the registry, with what the UI step gives it. `context` is copied; what it
// looks at (the world, the measurer, Text.bmd and the filters) must outlive the registry's context.
void LendTheSecondGuildStepTo(Ui::PanelContext& panels, const SecondGuildStepContext& context);

// 0x656e70: glColor3f(1, 1, 1), the window's background (0x104 and 0x105) without blending, blending, the title in
// bold (0x554180 at (x + 0x23, y + 0xc) with a box of 120 · W / 640, colors 0xffdcdcdc on 0xff141414: 180, 1303, 1304
// or 228 depending on the page) and the page's draw.
[[nodiscard]] Core::Expected<void, std::string> DrawTheSecondGuildWindow(Hud::GameHudContext& context);
} // namespace Client::Guild
