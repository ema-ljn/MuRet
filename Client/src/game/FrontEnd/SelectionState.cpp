#include "FrontEnd/SelectionState.h"

#include "Session/CharacterEvent.h"
#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Protocol/FrontEnd/CharacterList.h"
#include "Models/Cloth.h"
#include "Models/Skeleton.h"
#include "Actors/ActorConstruction.h"
#include "Kernel/ActorState.h"
#include "FrontEnd/CharacterSlots.h"
#include "Guild/GuildReceive.h"
#include "Session/MapLoad.h"
#include "Kernel/SceneId.h"
#include "Scenes/SceneWorlds.h"
#include "FrontEnd/SelectionActor.h"
#include "Kernel/WorldState.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Camera/GameCamera.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/WorldTerrain.h"
#include "Terrain/TerrainState.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>

namespace Client::FrontEnd
{
namespace
{
constexpr std::size_t LoginActorSlot = 2;
} // namespace

SelectionState::SelectionState(Kernel::WorldState& world) : m_world(world) {}

std::span<const Kernel::ActorState, Kernel::SelectionActorCount> SelectionState::Slots() const
{
    return m_world.Actors().first<Kernel::SelectionActorCount>();
}

Kernel::ActorState& SelectionState::PreviewActor()
{
    return m_preview;
}

const Kernel::ActorState& SelectionState::PreviewActor() const
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

const Terrain::WorldTerrain* SelectionState::Terrain() const
{
    return m_ownedTerrain ? &*m_ownedTerrain : m_borrowedTerrain;
}

int SelectionState::Map() const
{
    return m_world.Runtime().map;
}

void SelectionState::SetScene(Terrain::CullingScene scene)
{
    m_scene = scene;
}

void SelectionState::BorrowTerrain(int map, const Terrain::WorldTerrain* terrain)
{
    m_world.Runtime().map = map;
    if (m_ownedTerrain && terrain == &*m_ownedTerrain)
    {
        return;
    }
    m_ownedTerrain.reset();
    m_borrowedTerrain = terrain;
}

void SelectionState::RetainTerrain(int map, Terrain::WorldTerrain terrain)
{
    m_world.Runtime().map = map;
    m_ownedTerrain = std::move(terrain);
    m_borrowedTerrain = nullptr;
}

void SelectionState::BeginSelection()
{
    m_scene = Terrain::CullingScene::CharacterSelect;
    m_receivedList = false;
    m_initialized = false;
}

void SelectionState::EnterLogin()
{
    m_scene = Terrain::CullingScene::Login;
    // 0x517270 in scene 2 builds slot 2 with pet 0x1c16, without touching Hero or the rest of the pool: alive
    // (0x511290) and with the key's slot (0x5172a8).
    Kernel::ActorState& login = m_world.Actors()[LoginActorSlot];
    login.previousHad1c15 = false;
    login.alive = true;
    login.key = static_cast<std::uint16_t>(LoginActorSlot);
}

void SelectionState::RefreshCameraAttribute(Camera::GameCameraInput& input) const
{
    const Terrain::WorldTerrain* terrain = Terrain();
    if (terrain != nullptr && terrain->attributes)
    {
        input.cellAttribute = static_cast<std::uint8_t>(terrain->attributes->cells[0]);
    }
}

Core::Expected<void, std::string> SelectionState::InitializeSelection(const std::filesystem::path& dataRoot,
                                                                      Terrain::TerrainState& terrain,
                                                                      Camera::GameCameraInput& input,
                                                                      Crt::CrtRandom* random,
                                                                      Assets::FaunaModels* faunaModels)
{
    if (m_initialized || !m_receivedList)
    {
        return {};
    }
    // 0x633f20: switches to 54 after the F300 constructors; it does not rebuild those actors.
    // Before loading it, it marks the world loaded (0x633f30): with that the game that follows runs and draws
    // (06, "La entrada al mundo", step 1). It is this load and not the entry to scene 4, which the
    // selection does before it has the list (0x6342cb).
    m_world.Runtime().drawingEnabled = true;
    constexpr std::uint8_t SelectionMap = Scenes::WorldOf(Kernel::SceneId::CharacterSelect)->map;
    // 0x633f44: 54 is the current world before 0x61a560, even if the load fails. The previous loan
    // stops being valid before the load overwrites the world's terrain.
    BorrowTerrain(SelectionMap, nullptr);
    // 0x61a560(54) → 0x6070a0: kills the effects, the particles and the other pools before opening the
    // files (it keeps +0x16c and the wind); without 0x510f70 in scene 4.
    const Scenes::MapLoadContext context{.dataRoot = dataRoot,
                                         .world = m_world,
                                         .terrain = terrain,
                                         .scene = Kernel::SceneId::CharacterSelect,
                                         .playerKey = 0,
                                         .random = random != nullptr ? *random : m_localCloth.random,
                                         .faunaModels = faunaModels};
    if (auto loaded = Scenes::LoadMap(SelectionMap, context); !loaded.HasValue())
    {
        return Fail(loaded.Error());
    }
    BorrowTerrain(SelectionMap, &terrain.current);
    input.map = SelectionMap;
    RefreshCameraAttribute(input);
    m_initialized = true;
    return {};
}

Core::Expected<void, std::string> SelectionState::Construct(const Protocol::FrontEnd::CharacterEntry& entry,
                                                            const Camera::GameCamera& camera,
                                                            Camera::GameCameraInput& input, Models::ClothWorld& cloth,
                                                            const Models::Skeleton& playerSkeleton)
{
    if (entry.slot >= Kernel::SelectionActorCount)
    {
        return Core::Unexpected(std::string("slot de selección fuera del pool"));
    }
    const Terrain::WorldTerrain emptyTerrain;
    const Terrain::WorldTerrain* terrain = Terrain();
    Kernel::ActorState& slot = m_world.Actors()[entry.slot];
    // 0x511290: queries Height with XY 50/50, before the final XY and the reset of 318.
    const Actors::ActorConstructionContext construction{.terrain = terrain != nullptr ? *terrain : emptyTerrain,
                                                        .map = m_world.Runtime().map,
                                                        .scene = m_scene,
                                                        .fixedHeight = camera.fixedHeight};
    const auto built = ConstructSelectionActor(m_world, entry, construction, cloth, playerSkeleton);
    if (!built.HasValue())
    {
        return built;
    }
    // The camera reads the published hero (DAT_0743a170): slot 0 of WinMain until the first world
    // entry, and after an F1 02 with 1, the game's slot, which the list only builds if it falls among the
    // five (Q-C213).
    if (static_cast<std::size_t>(entry.slot) == m_world.Runtime().hero)
    {
        input.hero = slot.position;
    }
    return {};
}

Core::Expected<void, std::string> SelectionState::Apply(std::span<const App::CharacterEvent> events,
                                                        Camera::GameCamera& camera, Camera::GameCameraInput& input,
                                                        Models::ClothWorld* cloth,
                                                        const Models::Skeleton* playerSkeleton)
{
    const Models::Skeleton emptySkeleton;
    Models::ClothWorld& sharedCloth = cloth != nullptr ? *cloth : m_localCloth;
    const Models::Skeleton& skeleton = playerSkeleton != nullptr ? *playerSkeleton : emptySkeleton;
    for (const App::CharacterEvent& event : events)
    {
        if (const auto* list = std::get_if<App::CharacterListReceived>(&event))
        {
            // 0x4c20b3..0x4c2101: before the characters, no war and no rival (0x576796c, 0x6c82f0 and 0x5767804) and
            // +0x374 of the 400 by the published hero's guild, like 0x4c1800 (CL9 milestone 8).
            Guild::ClearTheWar(m_world);
            for (const Protocol::FrontEnd::CharacterEntry& entry : list->list.characters)
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
        // F302 turns the view off; it keeps the record's XYZ and 318, without a new constructor.
    }
    RefreshCameraAttribute(input);
    return {};
}

void SelectionState::DrawSelection(Camera::GameCameraInput& input)
{
    for (Kernel::ActorState& slot : m_world.Actors().first<Kernel::SelectionActorCount>())
    {
        slot.position.z = CharacterSlotHeight;
        slot.light = {};
    }
    // Outside the five (a game hero, Q-C213), its Z stays.
    if (m_world.Runtime().hero < Kernel::SelectionActorCount)
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
} // namespace Client::FrontEnd
