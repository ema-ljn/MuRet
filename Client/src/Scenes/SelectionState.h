#pragma once

#include "App/CharacterEvent.h"
#include "Core/Expected.h"
#include "Protocol/Character/CharacterList.h"
#include "Scenes/ActorState.h"
#include "Scenes/WorldState.h"
#include "World/TerrainCulling.h"
#include "World/WorldTerrain.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace Client::World
{
struct GameCamera;
struct GameCameraInput;
} // namespace Client::World

// Fachada de los cinco actores de selección del WorldState durable; sin otra copia de historia.
// El constructor sin dueño explícito posee su único WorldState para usar la fachada aisladamente.
namespace Client::Scenes
{
class SelectionState
{
public:
    SelectionState();
    explicit SelectionState(WorldState& world);
    [[nodiscard]] WorldState& World();
    [[nodiscard]] const WorldState& World() const;
    [[nodiscard]] std::span<const ActorState, SelectionActorCount> Slots() const;
    [[nodiscard]] bool HasReceivedList() const;
    [[nodiscard]] bool IsInitialized() const;
    [[nodiscard]] const std::optional<std::string>& Failure() const;
    [[nodiscard]] const World::WorldTerrain* Terrain() const;
    [[nodiscard]] int Map() const;
    void SetScene(World::CullingScene scene);
    [[nodiscard]] World::CullingScene Scene() const;
    // El dueño de `terrain` debe mantenerlo vivo hasta RetainTerrain o el próximo BorrowTerrain.
    // Si ya es el terreno retenido por este estado, conserva su ownership.
    void BorrowTerrain(int map, const World::WorldTerrain* terrain);
    void RetainTerrain(int map, World::WorldTerrain terrain);
    void BeginSelection();
    void EnterLogin();
    // CL7.3 B: Hero conserva celda lógica 0; copia su atributo vivo sin escribir XYZ.
    void RefreshCameraAttribute(World::GameCameraInput& input) const;
    [[nodiscard]] Core::Expected<void, std::string> InitializeSelection(const std::filesystem::path& dataRoot,
                                                                        World::GameCameraInput& input);
    [[nodiscard]] Core::Expected<void, std::string> Apply(std::span<const App::CharacterEvent> events,
                                                          World::GameCamera& camera, World::GameCameraInput& input);
    // Se llama después de la cámara de selección: 0x634680 escribe Z a los cinco slots.
    void DrawSelection(World::GameCameraInput& input);

private:
    [[nodiscard]] Core::Expected<void, std::string> Construct(const Protocol::Character::CharacterEntry& entry,
                                                              const World::GameCamera& camera,
                                                              World::GameCameraInput& input);
    [[nodiscard]] Core::Expected<void, std::string> Fail(std::string error);
    std::unique_ptr<WorldState> m_ownedWorld;
    WorldState& m_world;
    std::optional<World::WorldTerrain> m_ownedTerrain;
    const World::WorldTerrain* m_borrowedTerrain = nullptr;
    int m_map = -1;
    World::CullingScene m_scene = World::CullingScene::Other;
    bool m_receivedList = false;
    bool m_initialized = false;
    std::optional<std::string> m_failure;
};
} // namespace Client::Scenes
