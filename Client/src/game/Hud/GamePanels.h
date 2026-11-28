#pragma once

#include "Core/Expected.h"
#include "Tables/ItemTable.h"
#include "Guild/GuildPanels.h"
#include "CastleSiege/CastleWindows.h"
#include "Guild/GuildWindow.h"
#include "Guild/SecondGuildWindow.h"
#include "Tables/TextTable.h"
#include "Protocol/Outgoing.h"
#include "Items/InventoryMoves.h"
#include "Npc/NpcWindows.h"
#include "Ui/PanelRegistry.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Client::Tables
{
struct ItemSetTables;
}

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Hud
{
class IGameUiRequests;
struct GameUiContext;

// What 0x550d10 reads from the load when opening the character calls it (0x65ca1e): Item.bmd (*[0x7758550]) and
// ItemSetType.bmd with ItemSetOption.bmd (0x40c600). Only the UI step lends them, the only one that opens the
// character; with one at nullptr, 0x550d10 stops with an error at the first thing that reads it.
struct GamePanelTables
{
    const Tables::ItemTable* items = nullptr;
    const Tables::ItemSetTables* itemSetTables = nullptr;
    // Text.bmd: the 609 of 0x419710's empty answers, when opening the quests (0x419840) shows a dialog with no answers.
    // nullptr: that dialog stops.
    const Tables::TextTable* texts = nullptr;
};

// The panel registry ([0x576352c]) over WorldRuntime's globals, with what its functions read from the world when it is
// built: the map (0x6c9dd0), the cursor item (0x7879fbc), the hero's class (+0x1d4 & 7) and the requested exit
// (0x576793c). The sounds (0x41ed10) stay in WorldRuntime::lastSoundId, the CL10 hook. It is short-lived: built where
// the original calls a registry function.
class GamePanels
{
public:
    GamePanels(Kernel::WorldState& world, Crt::CrtRandom& random, GamePanelTables tables = {});
    GamePanels(const GamePanels&) = delete;
    GamePanels& operator=(const GamePanels&) = delete;
    GamePanels(GamePanels&&) = delete;
    GamePanels& operator=(GamePanels&&) = delete;
    ~GamePanels() = default;

    [[nodiscard]] Ui::PanelRegistry& Registry()
    {
        return m_registry;
    }

    // 0x64ae10: the B2 03 of opening the castle registration, over the same world wire.
    void LendTheCastleRequests(Kernel::WorldState& world, std::function<void(Protocol::Outgoing)> send,
                               CastleSiege::CastleWindowServices services = {});
    void LendTheCastleWindowSteps(const GameUiContext& context);
    // The siege's weapon and gate controls.
    void LendTheSiegeControls(Kernel::WorldState& world, std::function<void(Protocol::Outgoing)> send);

    // 0x659aa0..0x659c3d: with `requests`, opening the party sets `world`'s count to 0 (HudState::partyCount) and asks
    // for the list (C1 03 42). Lent by the UI step, the one that opens the party; without it, or with nullptr, opening
    // it stops.
    void LendThePartyList(Kernel::WorldState& world, IGameUiRequests* requests);
    // 0x659ff6..0x65a8a3 and 0x65c039..0x65c946: with `send`, opening the pet, or the inventory with a Dark Lord, sends
    // the raven and horse requests (C1 06 A9 00 00 01 and A9 01 00 08, Protocol::Pets::WritePetInfoRequest). Lent by
    // the UI step and LendTheSession; without it, opening them stops.
    void LendThePetRequests(std::function<void(Protocol::Outgoing)> send);
    // 0x659f3d: opening panel 1 calls 0x491080(0x186, 0x106) (Messenger::OpenTheFriends), with what it reads from the
    // step (the view and the old windows' globals). Lent by the UI step; without it, opening it stops.
    void LendTheFriends(std::function<Core::Expected<void, std::string>()> opened);
    // The NPC windows' wires (Ui::PanelContext::npcWindowOpened and npcWindowClosing): Npc/NpcWindows.h with the world,
    // the rand(), the inventory services and the send.
    void LendTheNpcWindows(Kernel::WorldState& world, Crt::CrtRandom& random, Items::InventoryServices services,
                           Npc::NpcWindowSender send);

    // CL9 milestone 8: opening the guild asks for the list (C1 03 52), closing its two windows (0x6522f0 and 0x6570b0)
    // and answering the panels' dialog (0x4d6b60 and 0x653c00), with `send`. Without it, whatever uses it stops.
    void LendTheGuild(Kernel::WorldState& world, const Guild::GuildSender& send);

    // 0x6605b0 in the UI step, with the mouse in 640 × 480 and the step's clock; what the positions window sends goes
    // through `send`.
    void LendThePanelDialogStep(Kernel::WorldState& world, const Guild::GuildSender& send, std::int32_t mouseX,
                                std::int32_t mouseY, std::uint32_t now);

    // 0x657959: the step of guild window 2 ([0x7d536f4]) with the mouse, the clock, Text.bmd and the filters.
    void LendTheSecondGuildStep(const Guild::SecondGuildStepContext& context);
    // 0x657977: the guild window's step (Guild::LendTheGuildWindowStepTo).
    void LendTheGuildWindowStep(const Guild::GuildWindowStepContext& context);

    // 0x419900: closing the quests sends the C1 03 31 with `send`, and so do 0x658a18 and 0x658c0e when closing an
    // event NPC (0x1000 or 0x4000, CL9 milestone 11). Lent by the UI step, the A1 and A2 handlers (0x657a90) and the
    // closes of the world entry, the teleport, the regen and the events; without it, closing them stops.
    void LendTheWindowClose(std::function<void()> send);
    // The same with the C1 03 31 of `requests` (IGameUiRequests::SendWindowClose); with nullptr, without it.
    void LendTheWindowClose(IGameUiRequests* requests);

    // 0x65ac87, 0x65b359 and 0x5eb17f: the C1 04 F3 10 when opening a shop with 0x6c931c other than −1 and the 3F 07
    // when closing another's with its owner (PersonalShop::OpenThePersonalShop and CloseThePersonalShop), through
    // `send`. Without it, those sends stop; the rest of opening and closing the shops does not need it.
    void LendThePersonalShops(Kernel::WorldState& world, Crt::CrtRandom& random,
                              std::function<void(Protocol::Outgoing)> send);

    // Everything an original close sends to the GS from 0x657a90, 0x657ab0, 0x657880 and 0x65cae0: the C1 03 31 of the
    // quests and the event NPCs (`sendWindowClose`), the trade's C1 03 3D (0x657fe1), the shops' F3 10 and 3F 07, a
    // Dark Lord inventory's A9 and the closes of the two guild windows with the panels' dialog (0x657b50 and 0x657b73,
    // LendTheGuild) (`send`, which the trade and the shops open). Lent by the paths that open or close windows outside
    // the UI step: Esc (0x623f17), walking (0x56b39d), the 37 (0x659e46), the 3F 05 (0x65b319), the entry, the
    // teleport, the regen, the events, the quests and the 0x9c box. Without `send`, or without `sendWindowClose`, each
    // one's stops remain.
    void LendTheSession(Kernel::WorldState& world, Crt::CrtRandom& random, std::function<void(Protocol::Outgoing)> send,
                        std::function<void()> sendWindowClose);
    // The same with `requests` (SendGamePacket, SendGuildPacket for the guild and SendWindowClose); with nullptr,
    // nothing.
    void LendTheSession(Kernel::WorldState& world, Crt::CrtRandom& random, IGameUiRequests* requests);

private:
    Ui::PanelContext m_context;
    Ui::PanelRegistry m_registry;
};
} // namespace Client::Hud
