#include "Scenes/SelectionState.h"

#include "App/CharacterEvent.h"
#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "GameLogic/CharacterAppearance.h"
#include "Protocol/Character/CharacterList.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterSlots.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneWorlds.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/TerrainCulling.h"
#include "World/TerrainHeight.h"
#include "World/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

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
constexpr std::uint16_t HeightOffsetPet = 0x1c15;
constexpr std::uint16_t SafeAttribute = 1;
constexpr float ConstructorPosition = 50.0f;
constexpr double PetHeightOffset = 30.0;
constexpr double LargePetHeightOffset = 90.0;
constexpr int FirstLargeOffsetMap = 8;
constexpr int SecondLargeOffsetMap = 10;
constexpr std::size_t LoginActorSlot = 2;

bool SkipsHeight(World::CullingScene scene)
{
    // 0x5f93c0 sale antes de consultar las tablas sólo con estos valores nativos de escena.
    constexpr std::uint8_t InitialScene = 0;
    constexpr std::uint8_t LoadingScene = 1;
    constexpr std::uint8_t WorldLoadingScene = 3;
    const auto number = static_cast<std::uint8_t>(scene);
    return number == InitialScene || number == LoadingScene || number == WorldLoadingScene;
}
} // namespace

SelectionState::SelectionState(WorldState& world) : m_world(world) {}

std::span<const ActorState, SelectionActorCount> SelectionState::Slots() const
{
    return m_world.Actors().first<SelectionActorCount>();
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
    // 0x517270 en escena 2 construye slot 2 con pet 1c16, sin tocar Hero ni el resto del pool.
    m_world.Actors()[LoginActorSlot].previousHad1c15 = false;
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
                                                                      World::GameCameraInput& input)
{
    if (m_initialized || !m_receivedList)
    {
        return {};
    }
    // 0x633f20: cambia a 54 después de los constructores de F300; no reconstruye esos actores.
    // RetainTerrain deja el 54 como mundo en curso, también para la carga del mundo que sigue.
    constexpr std::uint8_t SelectionMap = WorldOf(SceneId::CharacterSelect)->map;
    auto terrain = World::LoadWorldTerrain(dataRoot, SelectionMap);
    if (!terrain.HasValue())
    {
        return Fail(terrain.Error());
    }
    RetainTerrain(SelectionMap, std::move(terrain).Value());
    input.map = SelectionMap;
    RefreshCameraAttribute(input);
    m_initialized = true;
    return {};
}

Core::Expected<void, std::string> SelectionState::Construct(const Protocol::Character::CharacterEntry& entry,
                                                            const World::GameCamera& camera,
                                                            World::GameCameraInput& input)
{
    if (entry.slot >= SelectionActorCount)
    {
        return Core::Unexpected(std::string("slot de selección fuera del pool"));
    }
    const World::WorldTerrain* terrain = Terrain();
    if (terrain == nullptr || !terrain->attributes || !terrain->heights)
    {
        return Core::Unexpected(std::string("faltan las tablas del terreno vigente para construir el personaje"));
    }
    ActorState& slot = m_world.Actors()[entry.slot];
    const int map = m_world.Runtime().map;
    const bool safe = (terrain->attributes->cells[0] & SafeAttribute) != 0;
    // 0x511290: consulta Height con XY 50/50, antes del XY final y del reset de 318.
    double height = SkipsHeight(m_scene)
                        ? 0.0
                        : World::TerrainHeightAt(*terrain->heights, *terrain->attributes, ConstructorPosition,
                                                 ConstructorPosition, camera.fixedHeight);
    if (map != -1 && slot.previousHad1c15 && !safe)
    {
        height += map == FirstLargeOffsetMap || map == SecondLargeOffsetMap ? LargePetHeightOffset : PetHeightOffset;
    }
    // El FSTP a float ocurre después de la suma; la rama sin offset no suma cero.
    slot.position = glm::vec3(CharacterSlots[entry.slot].position, static_cast<float>(height));
    const auto characterClass = Protocol::Character::CharacterClassOf(entry.classByte);
    const GameLogic::CharacterAppearance appearance = entry.withoutEquipment
                                                          ? GameLogic::ClassAppearance(characterClass, 0)
                                                          : GameLogic::AppearanceOf(characterClass, 0, entry.equipment);
    slot.previousHad1c15 = appearance.pet == HeightOffsetPet;
    ++slot.revision;
    if (entry.slot == 0)
    {
        input.hero = slot.position;
    }
    return {};
}

Core::Expected<void, std::string> SelectionState::Apply(std::span<const App::CharacterEvent> events,
                                                        World::GameCamera& camera, World::GameCameraInput& input)
{
    for (const App::CharacterEvent& event : events)
    {
        if (const auto* list = std::get_if<App::CharacterListReceived>(&event))
        {
            for (const Protocol::Character::CharacterEntry& entry : list->list.characters)
            {
                const auto constructed = Construct(entry, camera, input);
                if (!constructed.HasValue())
                {
                    return Fail(constructed.Error());
                }
            }
            m_receivedList = true;
        }
        else if (const auto* created = std::get_if<App::CharacterCreated>(&event))
        {
            const auto constructed = Construct(created->entry, camera, input);
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
    }
    input.hero.z = CharacterSlotHeight;
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
