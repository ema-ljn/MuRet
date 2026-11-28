#pragma once

#include "Core/Expected.h"

#include <array>
#include <cstdint>
#include <string>

namespace Client::Render
{
class SpriteBatch;
}

namespace Client::Tables
{
class TextTable;
}

namespace Client::Crt
{
class CrtRandom;
}

// The game's hover and Alt (CL8 milestone 6; 06 § HUD y ventanas, "La pasada 2D del juego"): 0x5bbbc0, second in the
// 2D pass (0x637650), between the 3D texts (0x4383f0) and the HUD (0x5a7320). It puts the name of the chosen one under
// the mouse (an NPC or a player in a bubble with 0x557170, a monster at the top center with 0x623690) and, with Alt,
// the floor items' names (0x5ba790, CL9 milestone 5: Items/FloorItemNames.h). It reads what the frame's picking leaves
// (0x59ec80): the characters since CL9 (milestone 1) and the floor item since milestone 5; the objects stay at −1.
namespace Client::Render::Frame
{
class ITextDrawer;
} // namespace Client::Render::Frame
namespace Client::Kernel
{
class WorldState;
struct GameDrawTailState;
struct HudState;
} // namespace Client::Kernel

namespace Client::Hud
{

struct GameHoverContext
{
    Kernel::WorldState& world;
    // The floor items (0x77f46d0, 1000 of 0x220 bytes) of this frame's drawing.
    const Kernel::GameDrawTailState& tail;
    // The frame's rand(), for the world width (0x5bb8d0) of a monster's name.
    Crt::CrtRandom& random;
    // 0x6c8e28 (the floor item), 0x6c8e2c (the NPC), 0x6c8e30 (the character), 0x6c8e34 and 0x6c8e38, as this frame's
    // picking left them.
    const std::array<std::int32_t, 5>& targets;
    // 0x77f3b66 (durabilityWarning), the DC font, 0x6c8e5c and 0x6c8e64, and 0x77f31b8 and 0x77f31bc.
    Kernel::HudState& hud;
    // 0x77f36d4 + 0x12 · 4, 0x553520's Alt latch, and 0x78abb38, the floor names on. Only this function uses them.
    std::int32_t& altLatch;
    std::uint8_t& altNames;
    // GetAsyncKeyState(0x12) at drawing time.
    bool altDown = false;
    Render::SpriteBatch& sprites;
    Render::Frame::ITextDrawer& text;
    // A MuRet intermediate drawing does not write the bubbles (0x557170): the bubbles' drawing reads a copy of the
    // table from the original's last frame, which is the one that writes them. The rest of the state it writes reaches
    // it in copies.
    bool projecting = false;
    // Text.bmd: the bubble's guild positions (0x556dd0).
    const Tables::TextTable* texts = nullptr;
    // What a floor item's name reads (0x5ba790): Item.bmd, Text.bmd, Skill.bmd and the sets, the mouse and the same
    // `hud`, `sprites` and `text`. Without it, a name stops.
    Ui::GameHudContext* names = nullptr;
};

// 0x5bbbc0: in Chaos Castle (18 to 23), without "webzen" in the hero's name, with an NPC or a character chosen it does
// nothing. Otherwise, 0x77f3b66 at 1, glColor3f(1, 1, 1), 0x620980 (no blend) and, in this order: with an NPC chosen,
// its name alone in a bubble (0x557170 with the empty text of 0x78abb3c, mode 0 and no color); otherwise, with a
// character and 0x6c8e38 at −1, a monster (+0x88 at 2) goes with its name centered at half the world width and at
// y = 10 (0x623690, with the DC font, the background 0xff000064 and the text 0xffc8e6ff) and the rest, without a
// personal shop at 0x7245c8 (0x451770), in a bubble like the NPC; otherwise, with a floor item chosen, its name at the
// mouse (0x5ba790 with 0). Then Alt: the edge (0x553520(0x12)) flips 0x78abb38, and with it on or Alt down, the name
// of each live and visible floor item that is not the chosen one, at its projection (0x5ba790 with 1).
[[nodiscard]] Core::Expected<void, std::string> DrawTheHover(GameHoverContext& context);
} // namespace Client::Hud

namespace Client::Ui
{
struct GameHudContext;
} // namespace Client::Ui
