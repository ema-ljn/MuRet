#include "Scenes/SelectionState.h"

#include "App/CharacterEvent.h"
#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Protocol/Character/CharacterList.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorConstruction.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterSlots.h"
#include "Scenes/MapLoad.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneWorlds.h"
#include "Scenes/SelectionActor.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldMap.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/GameCamera.h"
#include "World/TerrainCulling.h"
#include "World/WorldTerrain.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>

namespace Client::Scenes
{
namespace
{
constexpr std::size_t LoginActorSlot = 2;
} // namespace

SelectionState::SelectionState(WorldState& world) : m_world(world) {}

std::span<const ActorState, SelectionActorCount> SelectionState::Slots() const
{
    return m_world.Actors().first<SelectionActorCount>();
}

ActorState& SelectionState::PreviewActor()
{
    return m_preview;
}

const ActorState& SelectionState::PreviewActor() const
{
    return m_preview;
}

bool SelectionState::HasReceivedList() const
{
    return m_receivedList;
}

bool SelectionState::IsInitialized() const
{
    return m_initialized;
}

const std::optional<std::string>& SelectionState::Failure() const
{
    return m_failure;
}

const World::WorldTerrain* SelectionState::Terrain() const
{
    return m_ownedTerrain ? &*m_ownedTerrain : m_borrowedTerrain;
}

int SelectionState::Map() const
{
    return m_world.Runtime().map;
}

void SelectionState::SetScene(World::CullingScene scene)
{
    m_scene = scene;
}

void SelectionState::BorrowTerrain(int map, const World::WorldTerrain* terrain)
{
    m_world.Runtime().map = map;
    if (m_ownedTerrain && terrain == &*m_ownedTerrain)
    {
        return;
    }
    m_ownedTerrain.reset();
    m_borrowedTerrain = terrain;
}

void SelectionState::RetainTerrain(int map, World::WorldTerrain terrain)
{
    m_world.Runtime().map = map;
    m_ownedTerrain = std::move(terrain);
    m_borrowedTerrain = nullptr;
}

void SelectionState::BeginSelection()
{
    m_scene = World::CullingScene::CharacterSelect;
    m_receivedList = false;
    m_initialized = false;
}

void SelectionState::EnterLogin()
{
    m_scene = World::CullingScene::Login;
    // 0x517270 en escena 2 construye slot 2 con pet 1c16, sin tocar Hero ni el resto del pool: vivo
    // (0x511290) y con el lugar de clave (0x5172a8).
    ActorState& login = m_world.Actors()[LoginActorSlot];
    login.previousHad1c15 = false;
    login.alive = true;
    login.key = static_cast<std::uint16_t>(LoginActorSlot);
}

void SelectionState::RefreshCameraAttribute(World::GameCameraInput& input) const
{
    const World::WorldTerrain* terrain = Terrain();
    if (terrain != nullptr && terrain->attributes)
    {
        input.cellAttribute = static_cast<std::uint8_t>(terrain->attributes->cells[0]);
    }
}

Core::Expected<void, std::string> SelectionState::InitializeSelection(const std::filesystem::path& dataRoot,
                                                                      World::GameCameraInput& input,
                                                                      Render::Models::CrtRandom* random)
{
    if (m_initialized || !m_receivedList)
    {
        return {};
    }
    // 0x633f20: cambia a 54 después de los constructores de F300; no reconstruye esos actores.
    // Antes de cargarlo marca el mundo cargado (0x633f30): con eso corre y dibuja el juego que sigue
    // (06, "La entrada al mundo", paso 1). Es esta carga y no la entrada a la escena 4, que la
    // selección hace antes de tener la lista (0x6342cb).
    m_world.Runtime().drawingEnabled = true;
    constexpr std::uint8_t SelectionMap = WorldOf(SceneId::CharacterSelect)->map;
    // 0x633f44: el 54 es el mundo en curso antes de 0x61a560, aunque la carga falle. El préstamo anterior
    // deja de valer antes de que la carga pise el terreno del mundo.
    BorrowTerrain(SelectionMap, nullptr);
    // 0x61a560(54) → 0x6070a0: mata los efectos, las partículas y los demás pools antes de abrir los
    // archivos (conserva +0x16c y el viento); sin 0x510f70 en la escena 4.
    const MapLoadContext context{.dataRoot = dataRoot,
                                 .world = m_world,
                                 .scene = SceneId::CharacterSelect,
                                 .playerKey = 0,
                                 .random = random != nullptr ? *random : m_localCloth.random};
    if (auto loaded = LoadMap(SelectionMap, context); !loaded.HasValue())
    {
        return Fail(loaded.Error());
    }
    BorrowTerrain(SelectionMap, &m_world.Map().terrain);
    input.map = SelectionMap;
    RefreshCameraAttribute(input);
    m_initialized = true;
    return {};
}

Core::Expected<void, std::string> SelectionState::Construct(const Protocol::Character::CharacterEntry& entry,
                                                            const World::GameCamera& camera,
                                                            World::GameCameraInput& input,
                                                            Render::Models::ClothWorld& cloth,
                                                            const Render::Models::Skeleton& playerSkeleton)
{
    if (entry.slot >= SelectionActorCount)
    {
        return Core::Unexpected(std::string("slot de selección fuera del pool"));
    }
    const World::WorldTerrain emptyTerrain;
    const World::WorldTerrain* terrain = Terrain();
    ActorState& slot = m_world.Actors()[entry.slot];
    // 0x511290: consulta Height con XY 50/50, antes del XY final y del reset de 318.
    const ActorConstructionContext construction{.terrain = terrain != nullptr ? *terrain : emptyTerrain,
                                                .map = m_world.Runtime().map,
                                                .scene = m_scene,
                                                .fixedHeight = camera.fixedHeight};
    const auto built = ConstructSelectionActor(m_world, entry, construction, cloth, playerSkeleton);
    if (!built.HasValue())
    {
        return built;
    }
    // La cámara lee el héroe publicado (DAT_0743a170): el lugar 0 de WinMain hasta la primera entrada al
    // mundo, y después de un F1 02 con 1, el lugar del juego, que la lista solo construye si cae entre los
    // cinco (Q-C213).
    if (static_cast<std::size_t>(entry.slot) == m_world.Runtime().hero)
    {
        input.hero = slot.position;
    }
    return {};
}

Core::Expected<void, std::string> SelectionState::Apply(std::span<const App::CharacterEvent> events,
                                                        World::GameCamera& camera, World::GameCameraInput& input,
                                                        Render::Models::ClothWorld* cloth,
                                                        const Render::Models::Skeleton* playerSkeleton)
{
    const Render::Models::Skeleton emptySkeleton;
    Render::Models::ClothWorld& sharedCloth = cloth != nullptr ? *cloth : m_localCloth;
    const Render::Models::Skeleton& skeleton = playerSkeleton != nullptr ? *playerSkeleton : emptySkeleton;
    for (const App::CharacterEvent& event : events)
    {
        if (const auto* list = std::get_if<App::CharacterListReceived>(&event))
        {
            for (const Protocol::Character::CharacterEntry& entry : list->list.characters)
            {
                const auto constructed = Construct(entry, camera, input, sharedCloth, skeleton);
                if (!constructed.HasValue())
                {
                    return Fail(constructed.Error());
                }
            }
            m_receivedList = true;
        }
        else if (const auto* created = std::get_if<App::CharacterCreated>(&event))
        {
            const auto constructed = Construct(created->entry, camera, input, sharedCloth, skeleton);
            if (!constructed.HasValue())
            {
                return Fail(constructed.Error());
            }
        }
        // F302 desactiva la vista; conserva XYZ y 318 del registro, sin constructor nuevo.
    }
    RefreshCameraAttribute(input);
    return {};
}

void SelectionState::DrawSelection(World::GameCameraInput& input)
{
    for (ActorState& slot : m_world.Actors().first<SelectionActorCount>())
    {
        slot.position.z = CharacterSlotHeight;
        slot.light = {};
    }
    // Fuera de los cinco (un héroe del juego, Q-C213), su Z queda.
    if (m_world.Runtime().hero < SelectionActorCount)
    {
        input.hero.z = CharacterSlotHeight;
    }
}

Core::Expected<void, std::string> SelectionState::Fail(std::string error)
{
    if (!m_failure)
    {
        m_failure = error;
    }
    return Core::Unexpected(std::move(error));
}
} // namespace Client::Scenes
