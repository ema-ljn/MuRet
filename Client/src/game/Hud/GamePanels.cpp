#include "Hud/GamePanels.h"
#include "CastleSiege/SiegeBattleUi.h"

#include "Crt/CrtRandom.h"
#include "CastleSiege/CastleWindows.h"

#include "Core/Expected.h"
#include "Kernel/CharacterClass.h"
#include "Protocol/CastleSiege/CastleSiege.h"
#include "Protocol/Session/Logout.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Pets/PetInfo.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "Hud/GameUi.h"
#include "Guild/GuildPanels.h"
#include "Items/InventoryMoves.h"
#include "Player/ItemSetState.h"
#include "Npc/NpcWindows.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Player/OwnDerivedStats.h"
#include "Player/OwnStatHelpers.h"
#include "Guild/GuildWindow.h"
#include "Guild/SecondGuildWindow.h"
#include "PersonalShop/PersonalShopLists.h"
#include "Social/SocialEvents.h"
#include "Quests/Quests.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/OldWindows.h"
#include "Ui/PanelRegistry.h"
#include "Kernel/ItemState.h"
#include "Audio/SoundId.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace Client::Hud
{
namespace
{
// 0x65c966..0x65ca6d: [0x6eed3c] de +0x9304 a +0x9347 en 0 (los cuatro bytes y los dos nombres), y con el registro
// propio abierto alrededor (3.848 rand() al cerrarlo sin otros accesos), 0x550d10 si 0x550cf0 encuentra un lugar del
// equipo con la durabilidad (+0x1a) en 0, lo que pasa con cualquier lugar vacío.
Core::Expected<void, std::string> OpenTheCharacter(Kernel::WorldState& world, Crt::CrtRandom& random,
                                                   GamePanelTables tables)
{
    Player::ItemSetState& sets = world.ItemSets();
    sets.names = {};
    sets.byte9304 = 0;
    sets.byte9305 = 0;
    sets.byte9306 = 0;
    sets.byte9307 = 0;
    Kernel::WorldRuntime& runtime = world.Runtime();
    Kernel::OwnCharacterRecord& own = world.OwnCharacter();
    const Models::LogicalAccess access(runtime.ownRecordDepth, random, Models::OwnRecordCloseRandoms);
    if (std::ranges::none_of(own.equipment, [](const Kernel::ItemState& item) { return item.durability == 0; }))
        return {};
    return Player::RecalculateOwnStats(own, Player::OwnStatsContext{.itemSetTables = tables.itemSetTables,
                                                                    .itemSets = &sets,
                                                                    .items = tables.items,
                                                                    .publishedHero = world.Hero(),
                                                                    .ownRecordAccesses = runtime.ownRecordDepth,
                                                                    .random = random,
                                                                    .held = Player::HeldItemOf(runtime),
                                                                    .scan505820 = runtime.fullSetScan});
}

Ui::PanelContext ContextOf(Kernel::WorldState& world, Crt::CrtRandom& random, GamePanelTables tables)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    const Kernel::ActorState& hero = world.Hero();
    Ui::PanelContext context{.random = random, .dialogs = runtime.dialogs};
    context.map = runtime.map;
    context.cursorItemCount = runtime.cursorItemCount;
    context.heroClass = Kernel::BaseClassOf(hero.characterClass);
    context.logoutRequested = runtime.ui.logoutRequested;
    // 0x6689d0: el ancho del mundo con Senior y su auxiliar abiertos.
    context.heroSiegeMark = CastleSiege::HeroCanCraftAtTheCastle(world);
    context.inventoryBusy = [&world]() { return CastleSiege::CastleSeniorBusy(world); };
    context.sound = [&runtime](Audio::SoundId id) { runtime.RequestSound(id); };
    context.characterOpened = [&world, &random, tables]() { return OpenTheCharacter(world, random, tables); };
    // Sin el cable, el cierre con aux en 0 (el que manda el 3D) corta; el 3D del GS cierra con 1 (0x4d5906).
    context.tradeClosed = [&world, &random](bool aux) { return Social::CloseTheTradeGrids(world, random, aux, {}); };
    // Sin el cable, los envíos de abrir y cerrar las tiendas personales cortan (LendThePersonalShops).
    context.personalShopOpened = [&world, &random](Ui::PanelId id)
    { return PersonalShop::OpenThePersonalShop(world, random, id, {}); };
    context.personalShopClosed = [&world, &random]() { return PersonalShop::CloseThePersonalShop(world, random, {}); };
    // 0x659c70: 0x419840(−1) sobre [0x710148].
    context.questOpened = [&world, &random, tables]() {
        return Quests::OpenTheQuestList(Quests::QuestContext{.world = world, .random = random, .texts = tables.texts},
                                        -1);
    };
    // El panel 1, la de amigos (CL9 hito 9): 0x490a00(+0x48) y 0x4914e0 sobre el administrador viejo.
    Ui::OldWindowManager& oldWindows = runtime.oldWindows;
    context.friendsShown = [&oldWindows]() { return Ui::FindTheOldWindow(oldWindows, oldWindows.friends) != nullptr; };
    context.friendsClosed = [&runtime]()
    { return Ui::DropTheFriendsWindow(runtime.oldWindows, runtime.ui.pressedButton); };
    return context;
}
} // namespace

GamePanels::GamePanels(Kernel::WorldState& world, Crt::CrtRandom& random, GamePanelTables tables)
    : m_context(ContextOf(world, random, tables)), m_registry(world.Runtime().panels, m_context)
{
    CastleSiege::LendTheCastleLifecycleTo(m_context, world, random, {});
}

void GamePanels::LendTheSiegeControls(Kernel::WorldState& world, std::function<void(Protocol::Outgoing)> send)
{
    m_context.siegeControlOpened = [&world](Ui::PanelId id,
                                            std::uint32_t auxiliary) -> Core::Expected<void, std::string>
    {
        if (id == Ui::SiegeGatePanel)
            CastleSiege::OpenTheSiegeGate(world, static_cast<std::uint16_t>(auxiliary));
        else
        {
            world.Runtime().panels.delegated.siegeWeapon = id == Ui::SiegeWeaponPanel ? 1 : 2;
            world.Runtime().castleSiege.siegeBattleUi.weaponChoice = 0;
        }
        return {};
    };
    if (!send)
        return;
    m_context.siegeControlClosed = [&world, send = std::move(send)](Ui::PanelId id) -> Core::Expected<void, std::string>
    {
        Kernel::WorldRuntime& runtime = world.Runtime();
        if (id == Ui::SiegeGatePanel)
        {
            // 0x44f120: primero baja los campos, después sale el 31.
            runtime.panels.delegated.siegeGate = 0;
            runtime.castleSiege.siegeBattleUi.gateKey = 0;
            send(Protocol::Session::WriteWindowClose());
        }
        else
        {
            // 0x657ba7..0x657daf y 0x657dc1..0x657f8e: el registro manda el 31 antes de 0x44dcf0.
            send(Protocol::Session::WriteWindowClose());
            runtime.panels.delegated.siegeWeapon = 0;
            runtime.castleSiege.siegeBattleUi.weaponChoice = 0;
        }
        return {};
    };
}

void GamePanels::LendTheCastleRequests(Kernel::WorldState& world, std::function<void(Protocol::Outgoing)> send,
                                       CastleSiege::CastleWindowServices services)
{
    // El aviso local de Senior ocupado necesita los servicios también sin un envío disponible.
    CastleSiege::LendTheCastleLifecycleTo(m_context, world, m_context.random, send, services);
    if (!send)
        return;
    m_context.castleRegistrationOpened = [&world, send]() -> Core::Expected<void, std::string>
    {
        auto& opened = world.Runtime().panels.delegated.castleSiege;
        if (opened == 0)
        {
            // 0x64ae10..0x64b0d9: primero el C1 04 B2 03; el byte +4 queda en 1 después del envío.
            send(Protocol::CastleSiege::WriteRegistrationRequest());
            opened = 1;
        }
        return {};
    };
    m_context.castleRegistrationClosed = [&world, send = std::move(send)]() -> Core::Expected<void, std::string>
    {
        auto& opened = world.Runtime().panels.delegated.castleSiege;
        if (opened != 0)
        {
            // 0x64b200..0x64b3db: el estado se modifica después de intentar el envío.
            send(Protocol::Session::WriteWindowClose());
            opened = 0;
            world.Runtime().castleSiege.registration.tab = 0;
        }
        return {};
    };
}

void GamePanels::LendTheCastleWindowSteps(const GameUiContext& context)
{
    CastleSiege::LendTheCastleWindowStepsTo(m_context, context);
}

void GamePanels::LendTheNpcWindows(Kernel::WorldState& world, Crt::CrtRandom& random, Items::InventoryServices services,
                                   Npc::NpcWindowSender send)
{
    m_context.npcWindowOpened = [&world, &random, services, send](Ui::PanelId id)
    {
        return Npc::NpcWindowOpened(
            Npc::NpcWindowContext{.world = world, .random = random, .services = services, .send = send}, id);
    };
    m_context.npcWindowClosing =
        [&world, &random, services = std::move(services), send = std::move(send)](Ui::PanelId id)
    {
        return Npc::NpcWindowClosing(
            Npc::NpcWindowContext{.world = world, .random = random, .services = services, .send = send}, id);
    };
}

void GamePanels::LendThePersonalShops(Kernel::WorldState& world, Crt::CrtRandom& random,
                                      std::function<void(Protocol::Outgoing)> send)
{
    if (!send)
        return;
    m_context.personalShopOpened = [&world, &random, send](Ui::PanelId id)
    { return PersonalShop::OpenThePersonalShop(world, random, id, send); };
    m_context.personalShopClosed = [&world, &random, send]()
    { return PersonalShop::CloseThePersonalShop(world, random, send); };
}

void GamePanels::LendTheSession(Kernel::WorldState& world, Crt::CrtRandom& random,
                                std::function<void(Protocol::Outgoing)> send, std::function<void()> sendWindowClose)
{
    if (send)
    {
        m_context.tradeClosed = [&world, &random, send](bool aux)
        { return Social::CloseTheTradeGrids(world, random, aux, send); };
        LendThePetRequests(send);
        LendTheCastleRequests(world, send);
        LendTheSiegeControls(world, send);
        // 0x657b50 y 0x657b73: los cierres de las dos ventanas del gremio (la vtable + 0x2c, sin mirar nada) y el
        // diálogo de los paneles, por el mismo socket.
        Guild::LendTheGuildTo(m_context, world, send);
        LendThePersonalShops(world, random, std::move(send));
    }
    if (sendWindowClose)
        LendTheWindowClose(std::move(sendWindowClose));
}

void GamePanels::LendTheSession(Kernel::WorldState& world, Crt::CrtRandom& random, IGameUiRequests* requests)
{
    if (requests == nullptr)
        return;
    LendTheSession(
        world, random, [requests](Protocol::Outgoing outgoing) { requests->SendGamePacket(std::move(outgoing)); },
        [requests]() { requests->SendWindowClose(); });
    Guild::LendTheGuildTo(m_context, world,
                          [requests](Protocol::Outgoing packet) { requests->SendGuildPacket(std::move(packet)); });
}

void GamePanels::LendTheWindowClose(std::function<void()> send)
{
    m_context.questClosed = send;
    m_context.eventNpcClosed = std::move(send);
}

void GamePanels::LendTheWindowClose(IGameUiRequests* requests)
{
    if (requests == nullptr)
    {
        m_context.questClosed = {};
        m_context.eventNpcClosed = {};
        return;
    }
    m_context.questClosed = [requests]() { requests->SendWindowClose(); };
    m_context.eventNpcClosed = m_context.questClosed;
}

void GamePanels::LendThePartyList(Kernel::WorldState& world, IGameUiRequests* requests)
{
    if (requests == nullptr)
        return;
    Kernel::WorldRuntime& runtime = world.Runtime();
    m_context.partyOpened = [&runtime, requests]()
    {
        runtime.hud.partyCount = 0;
        requests->RequestPartyList();
    };
}

void GamePanels::LendTheGuild(Kernel::WorldState& world, const Guild::GuildSender& send)
{
    Guild::LendTheGuildTo(m_context, world, send);
}

void GamePanels::LendThePanelDialogStep(Kernel::WorldState& world, const Guild::GuildSender& send, std::int32_t mouseX,
                                        std::int32_t mouseY, std::uint32_t now)
{
    Guild::LendThePanelDialogStepTo(m_context, world, send, mouseX, mouseY, now);
}

void GamePanels::LendTheSecondGuildStep(const Guild::SecondGuildStepContext& context)
{
    Guild::LendTheSecondGuildStepTo(m_context, context);
}

void GamePanels::LendTheGuildWindowStep(const Guild::GuildWindowStepContext& context)
{
    Guild::LendTheGuildWindowStepTo(m_context, context);
}

void GamePanels::LendThePetRequests(std::function<void(Protocol::Outgoing)> send)
{
    m_context.petRequests = [send = std::move(send)]()
    {
        if (!send)
            return;
        send(Protocol::Pets::WritePetInfoRequest(Protocol::Pets::LoginScenePetInfoRequest));
        send(Protocol::Pets::WritePetInfoRequest(
            {Protocol::Pets::DarkHorsePet, Protocol::Pets::InventoryPlace, Protocol::Pets::PetSlot}));
    };
}

void GamePanels::LendTheFriends(std::function<Core::Expected<void, std::string>()> opened)
{
    m_context.friendsOpened = std::move(opened);
}
} // namespace Client::Hud
