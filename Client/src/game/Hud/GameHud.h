#pragma once

#include "Core/Expected.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Tables/SkillTable.h"
#include "Tables/TextTable.h"
#include "Protocol/Outgoing.h"
#include "Render/SpriteBatch.h"
#include "Render/Frame/ITextDrawer.h"
#include "Ui/Widget.h"
#include "Audio/SoundId.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

// The game's HUD (06 § HUD y ventanas, CL8 milestone 2): 0x5a7320(1), which the game's draw calls at 0x637660, the
// overlays of 0x5b0550 (0x6376bb) and the game cursor 0x5ac2d0, called twice (0x6376d7 and 0x6376e1). In 640 × 480,
// with the GL state its primitives leave and the protection table's rand() in their order. What belongs to another
// wave stops with its address where its condition holds. The UI item models (grids, cursor and quick slots) go in the
// 3D pass of 0x5ac120 (Items/UiItemDrawing.h).
namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Ui
{
struct OldWindowManager;
}

namespace Client::Kernel
{
class WorldState;
struct CrywolfInterface;
struct HudState;
} // namespace Client::Kernel

namespace Client::Guild
{
class IGuildMarks;
} // namespace Client::Guild

namespace Client::Items
{
struct UiItemResources;
} // namespace Client::Items

namespace Client::Scenes
{
struct GameProjection;
} // namespace Client::Scenes

namespace Client::Hud
{

struct GameHudContext
{
    Kernel::WorldState& world;
    // The frame's rand() and the access counts of the own record and the hero's cells (+0x41c and +0x420): the real
    // ones, or the copies of a MuRet intermediate draw.
    Crt::CrtRandom& random;
    std::uint8_t& ownRecordDepth;
    std::uint8_t& cellXDepth;
    std::uint8_t& cellYDepth;
    // What the HUD writes that lasts from one frame to the next: HudState's globals and 0x576353c (Crywolf's smoke,
    // which 0x5a7520 turns off after drawing it once). In an intermediate draw, copies nobody reads again.
    Kernel::HudState& hud;
    std::uint8_t& crywolfSandOverlay;
    // What Crywolf's interface writes (0x4365c0): in an intermediate draw, the copy nobody reads again; nullptr,
    // WorldRuntime's.
    Kernel::CrywolfInterface* crywolfInterface = nullptr;
    // 0x41ed10(id, 0, 0): the sound the main bar asks for (0x22, low life), the CL10 hook.
    std::optional<Audio::SoundId>& sound;
    std::function<void(Audio::SoundId)> soundRequest{};

    void RequestSound(Audio::SoundId id) const
    {
        if (soundRequest)
            soundRequest(id);
        else
            sound = id;
    }
    // The old UI that 0x4a5120 reads and writes, the bar's icons (the window list of [0x5763524]: +0x144, the new
    // letter, and +0x148 and +0x14c, the drawings and blinks). Without it, WorldRuntime's; in an intermediate draw, the
    // projection's copy, which nobody reads again.
    Ui::OldWindowManager* oldWindows = nullptr;
    // A MuRet intermediate draw does not write the worn items' warnings (D+0x44): they go to a copy nobody reads, and
    // the original's next frame writes them again.
    bool projecting = false;
    // 0x6203c0 of this frame's world pass, which the bubbles read (0x5a9050 → 0x6205b0). Without it, a live bubble
    // stops.
    const Scenes::GameProjection* projection = nullptr;
    Render::SpriteBatch& sprites;
    Render::Frame::ITextDrawer& text;
    // Text.bmd, Item.bmd and Skill.bmd; without one, whatever reads it stops with an error.
    const Tables::TextTable* texts = nullptr;
    const Tables::ItemTable* items = nullptr;
    const Tables::SkillTable* skills = nullptr;
    // [0x6eed3c] + 0x8004, the 64 sets of ItemSetOption.bmd the set label reads (0x40eca0); without them, it stops.
    const Tables::ItemSetOptionTable* setOptions = nullptr;
    // [0x6eed3c] + 4, ItemSetType.bmd: each type's set, read by an item's name and options in its tooltip (0x40cf30,
    // 0x40eee0); without it, a set item stops.
    const Tables::ItemSetTypeTable* setTypes = nullptr;
    // 0x5767b88, in ms.
    float worldClock = 0.0f;
    // The frame's GetTickCount (Kernel::TickCountMs), read by the events' countdown (0x40bab0) and Crywolf's interface
    // clock (0x437410 and 0x437426), which MuRet also gives for the timeGetTime() of 0x63b0e3 (0x430180).
    std::uint32_t tickCount = 0;
    // 0x7d3682c and 0x7d36828, in 640 × 480 (Ui::MouseIn640x480).
    Ui::Point mouse{};
    // 0x7d36874
    bool leftDown = false;
    // GetAsyncKeyState of the arrows (0x25..0x28) at draw time: the EDIT subclass (0x467b10) checks them with the
    // WM_GETTEXT of 0x468320 (Ui::OldWindowDrawContext::arrowHeld).
    bool arrowHeld = false;
    // GetAsyncKeyState(0x11) at draw time, which 0x558bb0 checks to attack another player (0x558e7f).
    bool controlDown = false;
    // The chosen server's code (App::ServerSession::ChosenServer), the source of [0x6d5ec4] (the group's place) and
    // [0x6d5ecc] (the number) that the character window reads; without it, it stops.
    std::optional<std::uint16_t> chosenServer{};
    // [0x6d5edc], the character's slot: the key bank +0x26c + slot·0x100. Without a session, the initial bank 0.
    std::optional<std::uint8_t> chosenSlot{};
    // What the 3D stretch of 0x5ac120 needs to draw the UI items' models. Without it (a scene without the model
    // library, like the tests'), each item takes its rand() without drawing.
    Items::UiItemResources* uiItems = nullptr;
    // The guild marks (0x5f16a0) of the war scoreboard (0x5a7640); without them, the war stops.
    Guild::IGuildMarks* guildMarks = nullptr;
    // The session's send(), for the A9 that asks for the pets' tooltip (0x423c87..0x42403d). Without it (a scene
    // without a session, like the tests'), the request does not go out; an intermediate draw does not call it.
    std::function<void(Protocol::Outgoing)> send{};
};

// 0x5a7320(1): the HUD's 31 children in their order (06 § HUD y ventanas, "El HUD hijo por hijo"). In the base state
// (no windows, no party, no event) it draws its map's sands or smoke, the bottom bar, the experience, the AG, the orbs,
// the current skill, the cells, the numbers and the quick slots' counts, and takes from rand() the closes of the
// arrows' own record, the twelve durability warnings, the bar, the experience, the AG and the main bar, the trade and
// shop queries and the hero's two cells.
[[nodiscard]] Core::Expected<void, std::string> DrawTheHud(GameHudContext& context);

// 0x5b0550: the overlays of mode 0x77f3b08 (Tooltips/GameHelps.h): F1's first help (1) and, with the mode left, the
// second (2) or the item's (3); with another, IsOpen(2) and the /move window (the 4, GameMoveWindow.h).
[[nodiscard]] Core::Expected<void, std::string> DrawTheOverlays(GameHudContext& context);

// 0x5ac2d0: the game cursor, with its choice table (06 § El cursor del juego). With a dialog (0x7d3a10c) or
// 0x78abb20, the usual one; otherwise the floor item's (5), the talk one (6, which animates its UVs with the clock),
// the object's, the attack one (4, or 0x68d in the 30), the commands' (0x65a), the repair one (7, spinning with mode
// 2), the press one (3, or 0xa with 0x77f3b5c) and the normal one (2). What it reads from another wave stops with its
// address.
[[nodiscard]] Core::Expected<void, std::string> DrawTheGameCursor(GameHudContext& context);
} // namespace Client::Hud
