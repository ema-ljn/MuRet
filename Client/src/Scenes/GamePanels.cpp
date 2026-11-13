#include "Scenes/GamePanels.h"

#include "Core/Expected.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameUi.h"
#include "Scenes/GuildPanels.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnDerivedStats.h"
#include "Scenes/OwnStatHelpers.h"
#include "Scenes/SecondGuildWindow.h"
#include "Scenes/SocialEvents.h"
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
Core::Expected<void, std::string> OpenTheCharacter(WorldState& world, Render::Models::CrtRandom& random,
                                                   GamePanelTables tables)
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

UI::PanelContext ContextOf(WorldState& world, Render::Models::CrtRandom& random, GamePanelTables tables)
{
    WorldRuntime& runtime = world.Runtime();
    const ActorState& hero = world.Actors()[runtime.hero];
    UI::PanelContext context{.random = random, .dialogs = runtime.dialogs};
    context.map = runtime.map;
    context.cursorItemCount = runtime.cursorItemCount;
    context.heroClass = static_cast<std::uint8_t>(hero.characterClass & 7u);
    context.logoutRequested = runtime.ui.logoutRequested;
    // 0x6689d0 solo lo lee el ancho del mundo con la lista de CS abierta, que MuRet no abre (CL9.5).
    context.heroSiegeMark = false;
    context.sound = [&runtime](std::int32_t id) { runtime.lastSoundId = id; };
    context.characterOpened = [&world, &random, tables]() { return OpenTheCharacter(world, random, tables); };
    // El panel 1, la de amigos (CL9 hito 9): 0x490a00(+0x48) y 0x4914e0 sobre el administrador viejo.
    UI::OldWindowManager& oldWindows = runtime.oldWindows;
    context.friendsShown = [&oldWindows]() { return UI::FindTheOldWindow(oldWindows, oldWindows.friends) != nullptr; };
    context.friendsClosed = [&oldWindows]() { return UI::DropTheFriendsWindow(oldWindows); };
    // Sin el cable, el cierre con aux en 0 (el que manda el 3D) corta; el 3D del GS cierra con 1 (0x4d5906).
    context.tradeClosed = [&world, &random](bool aux) { return CloseTheTradeGrids(world, random, aux, {}); };
    return context;
}
} // namespace

GamePanels::GamePanels(WorldState& world, Render::Models::CrtRandom& random, GamePanelTables tables)
    : m_context(ContextOf(world, random, tables)), m_registry(world.Runtime().panels, m_context)
{
}

void GamePanels::LendTheTradeCancel(WorldState& world, Render::Models::CrtRandom& random, IGameUiRequests* requests)
{
    if (requests == nullptr)
        return;
    m_context.tradeClosed = [&world, &random, requests](bool aux)
    {
        return CloseTheTradeGrids(world, random, aux, [requests](Protocol::Outgoing outgoing)
                                  { requests->SendGamePacket(std::move(outgoing)); });
    };
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

void GamePanels::LendThePanelDialogStep(WorldState& world, std::int32_t mouseX, std::int32_t mouseY, std::uint32_t now)
{
    LendThePanelDialogStepTo(m_context, world, mouseX, mouseY, now);
}

void GamePanels::LendTheSecondGuildStep(const SecondGuildStepContext& context)
{
    LendTheSecondGuildStepTo(m_context, context);
}

void GamePanels::LendTheFriends(std::function<Core::Expected<void, std::string>()> opened)
{
    m_context.friendsOpened = std::move(opened);
}
} // namespace Client::Scenes
