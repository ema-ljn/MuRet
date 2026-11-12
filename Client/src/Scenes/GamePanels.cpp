#include "Scenes/GamePanels.h"

#include "Scenes/ActorState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/PanelRegistry.h"

#include <cstdint>

namespace Client::Scenes
{
namespace
{
UI::PanelContext ContextOf(WorldState& world, Render::Models::CrtRandom& random)
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
    return context;
}
} // namespace

GamePanels::GamePanels(WorldState& world, Render::Models::CrtRandom& random)
    : m_context(ContextOf(world, random)), m_registry(world.Runtime().panels, m_context)
{
}
} // namespace Client::Scenes
