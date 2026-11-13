#include "Scenes/GamePanels.h"

#include "Core/Expected.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameUi.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnDerivedStats.h"
#include "Scenes/OwnStatHelpers.h"
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
    // 0x659c70: 0x419840(−1) sobre [0x710148].
    context.questOpened = [&world, &random, tables]()
    { return OpenTheQuestList(QuestContext{.world = world, .random = random, .texts = tables.texts}, -1); };
    // El panel 1, la de amigos (CL9 hito 9): 0x490a00(+0x48) y 0x4914e0 sobre el administrador viejo.
    UI::OldWindowManager& oldWindows = runtime.oldWindows;
    context.friendsShown = [&oldWindows]() { return UI::FindTheOldWindow(oldWindows, oldWindows.friends) != nullptr; };
    context.friendsClosed = [&oldWindows]() { return UI::DropTheFriendsWindow(oldWindows); };
    return context;
}
} // namespace

GamePanels::GamePanels(WorldState& world, Render::Models::CrtRandom& random, GamePanelTables tables)
    : m_context(ContextOf(world, random, tables)), m_registry(world.Runtime().panels, m_context)
{
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

void GamePanels::LendTheFriends(std::function<Core::Expected<void, std::string>()> opened)
{
    m_context.friendsOpened = std::move(opened);
}
} // namespace Client::Scenes
