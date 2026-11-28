#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Tables/SkillTable.h"
#include "Actors/ActorPickingBox.h"
#include "Protocol/Player/OptionData.h"
#include "Protocol/Outgoing.h"
#include "Ui/OldWindows.h"
#include "Ui/Widget.h"
#include "Terrain/TerrainPickingSegment.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <glm/ext/vector_float3.hpp>

namespace Client::Tables
{
struct ItemSetTables;
}

namespace Client::Input
{
class InputState;
}

namespace Client::Ui
{
struct GameUiState;
}

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Render::Text
{
class ITextMeasure;
}

namespace Client::Tables
{
class TextTable;
}

namespace Client::Ui
{
struct GameUiState;
}

// The game's UI in the step and in the frame, CL8 milestone 1 (06 § HUD y ventanas, "El orden" and "El clic es
// primero de la UI"): the dialogs before the step (0x623e70), the step's UI block (0x636ded..0x63705b) with 0x5ee140
// and 0x59ff10, and the frame's picking (0x59ec80). What belongs to another milestone or wave stops with its address;
// what the original does with no effect in MuRet (empty lists, objects nobody builds) goes as a comment in its place.
namespace Client::Terrain
{
struct TerrainPickingSegment;
struct WorldTerrain;
} // namespace Client::Terrain

namespace Client::Kernel
{
class WorldState;
struct FloorItemState;
struct WorldRuntime;
} // namespace Client::Kernel

namespace Client::Chat
{
struct GameChatContext;
} // namespace Client::Chat

namespace Client::Items
{
struct InventoryServices;
} // namespace Client::Items

namespace Client::Movement
{
struct GameMovementState;
} // namespace Client::Movement

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

namespace Client::Hud
{
// 0x553520 for the old windows' keys: the same latch in the step and when handling messages during the draw.
[[nodiscard]] bool OldWindowKeyEdge(Ui::GameUiState& ui, const Input::InputState& input, std::uint8_t virtualKey);

// 0x6578e0: chat/guild/panel fields and GetFocus other than the game window.
[[nodiscard]] bool GameTextInputBusy(const Kernel::WorldRuntime& runtime);

// What the dialogs and the panels ask of the rest of the client (the Esc menu, 0x624036..0x62553f, and the party): the
// wire, the disk and the log.
class IGameUiRequests
{
public:
    virtual ~IGameUiRequests() = default;
    // 0x61c210: the C1 1C F3 30 with `body`, in the clear.
    virtual void SendOptionData(const Protocol::Player::OptionBody& body) = 0;
    // 0x62411c..: the C3 F1 02 with `kind`. The step takes the C3's two rand() (the padding and the serial).
    virtual void RequestLogout(std::uint8_t kind) = 0;
    // 0x6599a0(4): the C1 03 42 in the clear, the party list request when opening its window.
    virtual void RequestPartyList() = 0;
    // CL9 milestone 8: the guild packets the UI sends (Protocol::Guild: the 51, 52, 54 and E6), in the clear.
    virtual void SendGuildPacket(Protocol::Outgoing packet) = 0;
    // A world packet Protocol builds (the trade, the party and the duel of CL9 milestone 7: 37, 3A, 3C, 3D, 41, 43 and
    // AC), as it goes out. The step takes a C3's two rand() before.
    virtual void SendGamePacket(Protocol::Outgoing outgoing) = 0;
    // 0x419900: the C1 03 31 in the clear, when closing the quest window (CL9 milestone 2).
    virtual void SendWindowClose() = 0;
    // 0x419bd0 and 0x41a9e0: the C3 A2 with the quest and the state. The step takes the C3's two rand().
    virtual void SendSetQuest(std::uint8_t quest, std::uint8_t state) = 0;
    // CL9 milestone 9: what the friends window and its boxes send (Protocol::Messenger, through
    // Messenger::FriendPacketOf).
    virtual void SendFriendPacket(Protocol::Outgoing packet) = 0;
    // 0x61c170("Data\\Macro.txt"): `content` (Chat::WrittenMacros) into the file.
    [[nodiscard]] virtual Core::Expected<void, std::string> SaveMacros(std::string_view content) = 0;
    // 0x41fa40 on the original's log (0x57630f0).
    virtual void Log(std::string_view line) = 0;
};

struct GameUiContext
{
    Kernel::WorldState& world;
    Crt::CrtRandom& random;
    const Input::InputState& input;
    // 0x7d3682c and 0x7d36828, in 640 × 480.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    // GetTextExtentPointA on the text DC (0x57634f4): the mouse over the chat log's names (0x45dec0) and the banner's
    // spaces and piece (0x46a9c0) measure with it. nullptr without text (the tests that do not check it): it chooses
    // none, and the banner that has to add a text stops.
    const Render::Text::ITextMeasure* measure = nullptr;
    // The screen in pixels (0x6d5d60 and 0x6d5d64): the old area's right click scales to 640 the widths its draw left,
    // and the banner uses it in its rx (0x46a9c0 and 0x46abc0).
    Core::PixelSize screen{640, 480};
    // What the macros (0x5a116f) send and notify. nullptr (the tests that do not check them): a macro is the error.
    const Chat::GameChatContext* chat = nullptr;
    // Text.bmd: the dialogs' texts (592 of the Esc menu, the 0x8c's pages). nullptr without a table: whatever reads one
    // stops with an error.
    const Tables::TextTable* texts = nullptr;
    // 0x6d5edc: the chosen one's slot, that of the skill keys of the outgoing F3 30 (0x61c2c6).
    std::optional<std::uint8_t> chosenSlot{};
    // The wire, Macro.txt and the log. nullptr (the tests that do not check them): a button that uses them stops with
    // an error.
    IGameUiRequests* requests = nullptr;
    // timeGetTime() for the banner's seconds (0x46ada0).
    std::uint32_t systemTime = 0;
    // Item.bmd, dressing the hero, the raven, the messages and the 24's send for picking up, dropping and equipping
    // (Items/InventoryMoves). nullptr (the tests that do not check them): whatever needs them stops.
    const Items::InventoryServices* inventory = nullptr;
    // 0x5f93c0 (the terrain height) without the map's cells: the camera's. Picking up with the space bar asks for it
    // (CL9 milestone 5).
    float fixedHeight = 1200.0f;
    // ItemSetType.bmd and ItemSetOption.bmd (0x40c600), which 0x550d10 reads with `inventory`'s Item.bmd when C or the
    // HUD button opens the character (0x65ca1e). nullptr if not loaded: opening it stops at 0x40e1b0.
    const Tables::ItemSetTables* itemSetTables = nullptr;
    // Skill.bmd (0x76bd1d8): the +0x31 of each skill in the bar's list (0x5a07a3). nullptr: the open list stops.
    const Tables::SkillTable* skills = nullptr;
    // CL9 milestone 10: what of the game the letters' portrait character uses (Messenger/LetterPortrait), which
    // outlives the step. nullptr: the portrait stops (P-C212).
    const Scenes::WorldEntryResources* worldEntry = nullptr;
    // The send of the NPC windows' packets (App::ServerSession::SendNpcWindowPacket, CL9 milestone 6). Empty (the tests
    // that do not check them): whatever sends stops.
    std::function<Core::Expected<void, std::string>(Protocol::Outgoing)> sendNpcWindow{};
    // The current map's terrain, under the item the space bar drops back (0x5f93c0); without it, an empty one.
    const Terrain::WorldTerrain* terrain = nullptr;
};

// The old windows' globals over the game's: the mouse's, the UI's, the keyboard focus (the chat input's) and the last
// sound.
[[nodiscard]] Ui::OldWindowGlobals OldWindowGlobalsOf(Kernel::WorldRuntime& runtime);

// What the old windows' step reads from the game, besides the mouse, the keys, the texts and the wire: the arrows
// (GetAsyncKeyState, 0x47b0f0), the whisper (0x4694c0), the bad words (0x622b40 with 0), the hero's name, the saved
// letter (0x4dcb70 from 0x4a11b0), the portrait character (Messenger/LetterPortrait) and 0x78abaac. Used by the UI
// step (0x490490) and the one that runs inside the draw (0x45aa50 from the old windows' draws,
// Ui::OldWindowDrawView::stepView), which must see the same. With `effects` false (MuRet's intermediate draw, on the
// windows' copy), the whisper, the saved letter and the portrait do nothing. The hooks keep what of `context` they
// use, not `context`.
void LendTheGameHooks(Ui::OldWindowStepView& view, const GameUiContext& context, bool effects);

// 0x637c4a..0x637c5c → 0x623e70: Esc and the open dialog, on every step of scene 5 before its logic.
[[nodiscard]] Core::Expected<void, std::string> StepTheGameDialogs(const GameUiContext& context);

// 0x636ded..0x63705b: the step's UI block, after the water waves (0x636de8) and before the objects (0x63706a).
[[nodiscard]] Core::Expected<void, std::string> StepTheGameUi(const GameUiContext& context);

struct FramePickingContext
{
    Kernel::WorldState& world;
    // The frame's rand(): the real one in a frame of the original, the copy in a projected one.
    Crt::CrtRandom& random;
    std::uint8_t& ownRecordDepth;
    // 0x6c8e28, 0x6c8e2c, 0x6c8e30, 0x6c8e34 and 0x6c8e38.
    std::array<std::int32_t, 5>& targets;
    // GetAsyncKeyState(0x12) at 0x59ef37.
    bool altDown = false;
    // The draw's mouse segment (GameScene::MouseRay): the camera (0x7d36834) and the far point (0x7d366c0).
    std::optional<Terrain::TerrainPickingSegment> ray{};
    // 0x7d3682c and 0x7d36828, in 640 × 480, which the party list reads (0x5e4140).
    Ui::Point mouse{};
    // The draw's camera position (0x7d36884), different from the ray's origin.
    glm::vec3 cameraPosition{};
    std::span<Actors::ActorPickingBox> pickingBoxes{};
    // CL9 milestone 5: the draw's floor items (0x77f46c8, or its copy in an intermediate draw) for 0x59e2e0, with the
    // same segment; without it (before the first draw) it does not search.
    std::span<Kernel::FloorItemState> floorItems{};
    // The current map's terrain, whose .map layers the pick reads in Lorencia and Devias. nullptr:
    // Terrain::EmptyTerrain().
    const Terrain::WorldTerrain* terrain = nullptr;
};

// 0x63717f → 0x5ac8d0: the function keys, at the end of the step's logic with the world loaded. The space bar picks up
// the nearest floor item (Items/FloorItemActions, CL9 milestone 5); F1 (0x77f3b08's helps and the set label), F2 (the
// normal chat in the log), F3 (the whisper mode) and F4 (the log's size and place) change their thing and sound. Print
// Screen (0x637cff, the capture) is not from here: it goes after any scene's step (Scenes::Screenshots).
[[nodiscard]] Core::Expected<void, std::string> StepTheFunctionKeys(const GameUiContext& context);

// 0x637641 → 0x59ec80: the frame's picking, 2D first. The previous chosen (0x6c8e30) stays if it is a monster (+0x88 at
// 2) with auto attack and no buttons; the other places go back to −1. Off the UI, without Alt: the characters of masks
// 0x22 and 1 (reversed with skill 0x1a to 0x1c of the Dark Wizard or the Elf, which opens the own record) into
// 0x6c8e30, with none those of mask 4 (the NPCs) into 0x6c8e2c (PickTheCharacter, 0x59e700) and, with no NPC, the floor
// item (0x59e2e0, CL9 milestone 5); with Alt, the floor item first and then 4, 0x22 and 1. The objects (0x59eb90)
// leave −1. The party (CL9 milestone 7, Party/PartyActors.h): without Alt, before searching, the list on the right
// (0x5e4140), which can choose a member; with the mouse on the UI, with what the list chose or with a chosen one and
// 0x6c8e38 set, 0x59e3b0 searches the members in the viewport. At the end, with no chosen one, 0x6c8e38 at −1 (except
// when the second character attempt hits: it returns earlier).
[[nodiscard]] Core::Expected<void, std::string> PickInTheFrame(const FramePickingContext& context);
} // namespace Client::Hud
