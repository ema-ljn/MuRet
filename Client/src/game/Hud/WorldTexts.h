#pragma once

#include "Core/Expected.h"
#include "Kernel/WorldTextQueue.h"

#include <string>

namespace Client::Render
{
class SpriteBatch;
}

// The game's 3D texts (CL8 milestone 6; 06 § HUD y ventanas, "La pasada 2D del juego"): the .bss queue at 0x723c18, a
// std::deque of 0x4c-byte entries (the text in 64 and the position in three floats), filled by 0x4381f0 from the
// characters' body (0x4fd603, type 0xc5 with its name and +0x1d6, Monsters/MonsterStepCases.h) and emptied by the
// draw, 0x4383f0, the first thing of the 2D pass (0x63764b). None of this takes rand().
namespace Client::Render::Frame
{
class ITextDrawer;
} // namespace Client::Render::Frame
namespace Client::Kernel
{
struct GameProjection;
struct HudState;
} // namespace Client::Kernel

namespace Client::Hud
{

struct WorldTextDrawContext
{
    // 0x723c18. In a MuRet intermediate draw the queue is read without emptying it: the original's frame empties it.
    Kernel::WorldTextQueue& queue;
    // 0x6203c0 of this frame's world pass, the one 0x6205b0 reads. Without it, an entry in the queue stops.
    const Kernel::GameProjection* projection = nullptr;
    Render::SpriteBatch& sprites;
    Render::Frame::ITextDrawer& text;
    // The DC's font, 0x6c8e5c and 0x6c8e64 (the last text's stay) and 0x77f31b8 and 0x77f31bc (0x554180).
    Kernel::HudState& hud;
    bool projecting = false;
};

// 0x4383f0: glColor3f(1, 1, 1) and, while the queue has entries, the front one: its position with the z 370 higher
// (0x6b8b88) projected with 0x6205b0; with both coordinates at 0 or more, the bold font in the DC, background
// 0xff000064, text 0xffc8e6ff and 0x623690, the text centered on the x. Then it takes it off the queue, drawn or not.
[[nodiscard]] Core::Expected<void, std::string> DrawTheWorldTexts(WorldTextDrawContext& context);
} // namespace Client::Hud
