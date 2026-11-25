#include "Scenes/GamePanels.h"
#include "Scenes/SiegeBattleUi.h"

#include "Crt/CrtRandom.h"
#include "Scenes/CastleWindows.h"

#include "Core/Expected.h"
#include "Protocol/CastleSiege.h"
#include "Protocol/Connection/Logout.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Pet/PetInfo.h"
#include "Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameUi.h"
#include "Scenes/GuildPanels.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/NpcWindows.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnDerivedStats.h"
#include "Scenes/OwnStatHelpers.h"
#include "Scenes/GuildWindow.h"
#include "Scenes/SecondGuildWindow.h"
#include "Scenes/PersonalShopLists.h"
#include "Scenes/SocialEvents.h"
#include "Scenes/Quests.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/OldWindows.h"
#include "UI/PanelRegistry.h"
#include "World/ItemState.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace Client::Scenes
{
namespace
{
// 0x65c966..0x65ca6d: [0x6eed3c] de +0x9304 a +0x9347 en 0 (los cuatro bytes y los dos nombres), y con el registro
// propio abierto alrededor (3.848 rand() al cerrarlo sin otros accesos), 0x550d10 si 0x550cf0 encuentra un lugar del
// equipo con la durabilidad (+0x1a) en 0, lo que pasa con cualquier lugar vacío.
Core::Expected<void, std::string> OpenTheCharacter(WorldState& world, Crt::CrtRandom& random, GamePanelTables tables)
{
    ItemSetState& sets = world.ItemSets();
    sets.names = {};
    sets.byte9304 = 0;
    sets.byte9305 = 0;
    sets.byte9306 = 0;
    sets.byte9307 = 0;
    WorldRuntime& runtime = world.Runtime();
    OwnCharacterRecord& own = world.OwnCharacter();
    const Render::Models::LogicalAccess access(runtime.ownRecordDepth, random, Render::Models::OwnRecordCloseRandoms);
    if (std::ranges::none_of(own.equipment, [](const World::ItemState& item) { return item.durability == 0; }))
        return {};
    return RecalculateOwnStats(own, OwnStatsContext{.itemSetTables = tables.itemSetTables,
                                                    .itemSets = &sets,
                                                    .items = tables.items,
                                                    .publishedHero = world.Actors()[runtime.hero],
                                                    .ownRecordAccesses = runtime.ownRecordDepth,
                                                    .random = random,
                                                    .held = HeldItemOf(runtime),
                                                    .scan505820 = runtime.fullSetScan});
}

UI::PanelContext ContextOf(WorldState& world, Crt::CrtRandom& random, GamePanelTables tables)
{
    WorldRuntime& runtime = world.Runtime();
    const ActorState& hero = world.Actors()[runtime.hero];
    UI::PanelContext context{.random = random, .dialogs = runtime.dialogs};
    context.map = runtime.map;
    context.cursorItemCount = runtime.cursorItemCount;
    context.heroClass = static_cast<std::uint8_t>(hero.characterClass & 7u);
    context.logoutRequested = runtime.ui.logoutRequested;
    // 0x6689d0: el ancho del mundo con Senior y su auxiliar abiertos.
    context.heroSiegeMark = HeroCanCraftAtTheCastle(world);
    context.inventoryBusy = [&world]() { return CastleSeniorBusy(world); };
    context.sound = [&runtime](std::int32_t id) { runtime.RequestSound(id); };
    context.characterOpened = [&world, &random, tables]() { return OpenTheCharacter(world, random, tables); };
    // Sin el cable, el cierre con aux en 0 (el que manda el 3D) corta; el 3D del GS cierra con 1 (0x4d5906).
    context.tradeClosed = [&world, &random](bool aux) { return CloseTheTradeGrids(world, random, aux, {}); };
    // Sin el cable, los envíos de abrir y cerrar las tiendas personales cortan (LendThePersonalShops).
    context.personalShopOpened = [&world, &random](UI::PanelId id)
    { return OpenThePersonalShop(world, random, id, {}); };
    context.personalShopClosed = [&world, &random]() { return CloseThePersonalShop(world, random, {}); };
    // 0x659c70: 0x419840(−1) sobre [0x710148].
    context.questOpened = [&world, &random, tables]()
    { return OpenTheQuestList(QuestContext{.world = world, .random = random, .texts = tables.texts}, -1); };
    // El panel 1, la de amigos (CL9 hito 9): 0x490a00(+0x48) y 0x4914e0 sobre el administrador viejo.
    UI::OldWindowManager& oldWindows = runtime.oldWindows;
    context.friendsShown = [&oldWindows]() { return UI::FindTheOldWindow(oldWindows, oldWindows.friends) != nullptr; };
    context.friendsClosed = [&runtime]()
    { return UI::DropTheFriendsWindow(runtime.oldWindows, runtime.ui.pressedButton); };
    return context;
}
} // namespace

GamePanels::GamePanels(WorldState& world, Crt::CrtRandom& random, GamePanelTables tables)
    : m_context(ContextOf(world, random, tables)), m_registry(world.Runtime().panels, m_context)
{
    LendTheCastleLifecycleTo(m_context, world, random, {});
}

void GamePanels::LendTheSiegeControls(WorldState& world, std::function<void(Protocol::Outgoing)> send)
{
    m_context.siegeControlOpened = [&world](UI::PanelId id,
                                            std::uint32_t auxiliary) -> Core::Expected<void, std::string>
    {
        if (id == UI::SiegeGatePanel)
            OpenTheSiegeGate(world, static_cast<std::uint16_t>(auxiliary));
        else
        {
            world.Runtime().panels.delegated.siegeWeapon = id == UI::SiegeWeaponPanel ? 1 : 2;
            world.Runtime().siegeBattleUi.weaponChoice = 0;
        }
        return {};
    };
    if (!send)
        return;
    m_context.siegeControlClosed = [&world, send = std::move(send)](UI::PanelId id) -> Core::Expected<void, std::string>
    {
        WorldRuntime& runtime = world.Runtime();
        if (id == UI::SiegeGatePanel)
        {
            // 0x44f120: primero baja los campos, después sale el 31.
            runtime.panels.delegated.siegeGate = 0;
            runtime.siegeBattleUi.gateKey = 0;
            send(Protocol::Connection::WriteWindowClose());
        }
        else
        {
            // 0x657ba7..0x657daf y 0x657dc1..0x657f8e: el registro manda el 31 antes de 0x44dcf0.
            send(Protocol::Connection::WriteWindowClose());
            runtime.panels.delegated.siegeWeapon = 0;
            runtime.siegeBattleUi.weaponChoice = 0;
        }
        return {};
    };
}

void GamePanels::LendTheCastleRequests(WorldState& world, std::function<void(Protocol::Outgoing)> send,
                                       CastleWindowServices services)
{
    // El aviso local de Senior ocupado necesita los servicios también sin un envío disponible.
    LendTheCastleLifecycleTo(m_context, world, m_context.random, send, services);
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
            send(Protocol::Connection::WriteWindowClose());
            opened = 0;
            world.Runtime().castleSiege.registration.tab = 0;
        }
        return {};
    };
}

void GamePanels::LendTheCastleWindowSteps(const GameUiContext& context)
{
    LendTheCastleWindowStepsTo(m_context, context);
}

void GamePanels::LendTheNpcWindows(WorldState& world, Crt::CrtRandom& random, InventoryServices services,
                                   NpcWindowSender send)
{
    m_context.npcWindowOpened = [&world, &random, services, send](UI::PanelId id) {
        return NpcWindowOpened(NpcWindowContext{.world = world, .random = random, .services = services, .send = send},
                               id);
    };
    m_context.npcWindowClosing =
        [&world, &random, services = std::move(services), send = std::move(send)](UI::PanelId id)
    {
        return NpcWindowClosing(NpcWindowContext{.world = world, .random = random, .services = services, .send = send},
                                id);
    };
}

void GamePanels::LendThePersonalShops(WorldState& world, Crt::CrtRandom& random,
                                      std::function<void(Protocol::Outgoing)> send)
{
    if (!send)
        return;
    m_context.personalShopOpened = [&world, &random, send](UI::PanelId id)
    { return OpenThePersonalShop(world, random, id, send); };
    m_context.personalShopClosed = [&world, &random, send]() { return CloseThePersonalShop(world, random, send); };
}

void GamePanels::LendTheSession(WorldState& world, Crt::CrtRandom& random, std::function<void(Protocol::Outgoing)> send,
                                std::function<void()> sendWindowClose)
{
    if (send)
    {
        m_context.tradeClosed = [&world, &random, send](bool aux)
        { return CloseTheTradeGrids(world, random, aux, send); };
        LendThePetRequests(send);
        LendTheCastleRequests(world, send);
        LendTheSiegeControls(world, send);
        // 0x657b50 y 0x657b73: los cierres de las dos ventanas del gremio (la vtable + 0x2c, sin mirar nada) y el
        // diálogo de los paneles, por el mismo socket.
        LendTheGuildTo(m_context, world, send);
        LendThePersonalShops(world, random, std::move(send));
    }
    if (sendWindowClose)
        LendTheWindowClose(std::move(sendWindowClose));
}

void GamePanels::LendTheSession(WorldState& world, Crt::CrtRandom& random, IGameUiRequests* requests)
{
    if (requests == nullptr)
        return;
    LendTheSession(
        world, random, [requests](Protocol::Outgoing outgoing) { requests->SendGamePacket(std::move(outgoing)); },
        [requests]() { requests->SendWindowClose(); });
    LendTheGuildTo(m_context, world,
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

void GamePanels::LendThePartyList(WorldState& world, IGameUiRequests* requests)
{
    if (requests == nullptr)
        return;
    WorldRuntime& runtime = world.Runtime();
    m_context.partyOpened = [&runtime, requests]()
    {
        runtime.hud.partyCount = 0;
        requests->RequestPartyList();
    };
}

void GamePanels::LendTheGuild(WorldState& world, const GuildSender& send)
{
    LendTheGuildTo(m_context, world, send);
}

void GamePanels::LendThePanelDialogStep(WorldState& world, const GuildSender& send, std::int32_t mouseX,
                                        std::int32_t mouseY, std::uint32_t now)
{
    LendThePanelDialogStepTo(m_context, world, send, mouseX, mouseY, now);
}

void GamePanels::LendTheSecondGuildStep(const SecondGuildStepContext& context)
{
    LendTheSecondGuildStepTo(m_context, context);
}

void GamePanels::LendTheGuildWindowStep(const GuildWindowStepContext& context)
{
    LendTheGuildWindowStepTo(m_context, context);
}

void GamePanels::LendThePetRequests(std::function<void(Protocol::Outgoing)> send)
{
    m_context.petRequests = [send = std::move(send)]()
    {
        if (!send)
            return;
        send(Protocol::Pet::WritePetInfoRequest(Protocol::Pet::LoginScenePetInfoRequest));
        send(Protocol::Pet::WritePetInfoRequest(
            {Protocol::Pet::DarkHorsePet, Protocol::Pet::InventoryPlace, Protocol::Pet::PetSlot}));
    };
}

void GamePanels::LendTheFriends(std::function<Core::Expected<void, std::string>()> opened)
{
    m_context.friendsOpened = std::move(opened);
}
} // namespace Client::Scenes
