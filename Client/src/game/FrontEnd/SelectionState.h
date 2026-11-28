#pragma once

#include "Session/CharacterEvent.h"
#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Protocol/FrontEnd/CharacterList.h"
#include "Models/Cloth.h"
#include "Kernel/ActorState.h"
#include "Kernel/WorldState.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/WorldTerrain.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace Client::Assets
{
class FaunaModels;
}

namespace Client::Camera
{
struct GameCamera;
struct GameCameraInput;
} // namespace Client::Camera

namespace Client::Models
{
struct Skeleton;
}

namespace Client::Terrain
{
struct TerrainState;
}

// The selection on the durable pool (06, "Los personajes de la lista", and CL7.3 B): it builds in the
// first five records of WorldState and writes the current world in its WorldRuntime, without another
// copy. main owns both; the view uses the revisions, never the equality of the entries.
namespace Client::FrontEnd
{
class SelectionState
{
public:
    // `world` has to outlive this state.
    explicit SelectionState(Kernel::WorldState& world);

    // The pool's first five records, the list's slots.
    [[nodiscard]] std::span<const Kernel::ActorState, Kernel::SelectionActorCount> Slots() const;
    // 0x7437050 lives outside the pool and outlives the scene's visual object, like the tables.
    [[nodiscard]] Kernel::ActorState& PreviewActor();
    [[nodiscard]] const Kernel::ActorState& PreviewActor() const;
    [[nodiscard]] bool HasReceivedList() const;
    [[nodiscard]] bool IsInitialized() const;
    [[nodiscard]] const std::optional<std::string>& Failure() const;
    [[nodiscard]] const Terrain::WorldTerrain* Terrain() const;
    // The current world, WorldRuntime::map.
    [[nodiscard]] int Map() const;
    void SetScene(Terrain::CullingScene scene);
    // Both write `map` as the current world, like whoever calls 0x61a560.
    // The owner of `terrain` must keep it alive until RetainTerrain or the next BorrowTerrain.
    // If it is already the terrain this state retains, it keeps its ownership.
    void BorrowTerrain(int map, const Terrain::WorldTerrain* terrain);
    void RetainTerrain(int map, Terrain::WorldTerrain terrain);
    void BeginSelection();
    void EnterLogin();
    // CL7.3 B: Hero keeps logical cell 0; it copies its live attribute without writing XYZ. After an F1 02
    // with 1 the hero is the game's, in its cell, but the selection camera only checks 0x40 and
    // 0x80, and outside map 30, which MuRet does not load, no .att has them (05): with the game's terrain
    // or with 54's, it makes no difference (Q-C213).
    void RefreshCameraAttribute(Camera::GameCameraInput& input) const;
    // 0x633f20 with the list: the world loaded, 54 as the current world (0x633f44) and 0x61a560(54), LoadMap's
    // load in scene 4 (the .obj objects with their selection cases, 0x606660, and the grass
    // with its 65536 rand()). The terrain stays in `terrain.current` and this state lends it.
    // `random` is the CRT's rand(); without it, the tests' local one. `faunaModels`: MapLoadContext::faunaModels.
    [[nodiscard]] Core::Expected<void, std::string> InitializeSelection(const std::filesystem::path& dataRoot,
                                                                        Terrain::TerrainState& terrain,
                                                                        Camera::GameCameraInput& input,
                                                                        Crt::CrtRandom* random = nullptr,
                                                                        Assets::FaunaModels* faunaModels = nullptr);
    [[nodiscard]] Core::Expected<void, std::string> Apply(std::span<const App::CharacterEvent> events,
                                                          Camera::GameCamera& camera, Camera::GameCameraInput& input,
                                                          Models::ClothWorld* cloth = nullptr,
                                                          const Models::Skeleton* playerSkeleton = nullptr);
    // Called after the selection camera: 0x634680 writes Z to the five slots.
    void DrawSelection(Camera::GameCameraInput& input);

private:
    [[nodiscard]] Core::Expected<void, std::string> Construct(const Protocol::FrontEnd::CharacterEntry& entry,
                                                              const Camera::GameCamera& camera,
                                                              Camera::GameCameraInput& input, Models::ClothWorld& cloth,
                                                              const Models::Skeleton& playerSkeleton);
    [[nodiscard]] Core::Expected<void, std::string> Fail(std::string error);
    Kernel::WorldState& m_world;
    Kernel::ActorState m_preview;
    // Only whoever tests the state without the loop uses this rand(); main provides the whole session's.
    Models::ClothWorld m_localCloth;
    std::optional<Terrain::WorldTerrain> m_ownedTerrain;
    const Terrain::WorldTerrain* m_borrowedTerrain = nullptr;
    Terrain::CullingScene m_scene = Terrain::CullingScene::Other;
    bool m_receivedList = false;
    bool m_initialized = false;
    std::optional<std::string> m_failure;
};
} // namespace Client::FrontEnd
