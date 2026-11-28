#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Player/OptionData.h"
#include "Tables/SkillTable.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Chat
{
struct GameChatContext;
} // namespace Client::Chat

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Input
{
class InputState;
} // namespace Client::Input

namespace Client::Items
{
struct InventoryServices;
} // namespace Client::Items

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Render::Text
{
class ITextMeasure;
} // namespace Client::Render::Text

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

namespace Client::Tables
{
struct ItemSetTables;
class TextTable;
} // namespace Client::Tables

namespace Client::Terrain
{
struct WorldTerrain;
} // namespace Client::Terrain

// What the game's UI step and the windows it steps read (CL8 milestone 1, 06 § HUD y ventanas).
namespace Client::Ui
{
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
    // 0x61c170("Data\\Macro.txt"): `content` (Tables::WrittenMacros) into the file.
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
} // namespace Client::Ui
