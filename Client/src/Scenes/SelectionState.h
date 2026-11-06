#pragma once

#include "App/CharacterEvent.h"
#include "Core/Expected.h"
#include "Protocol/Character/CharacterList.h"
#include "World/TerrainCulling.h"
#include "World/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace Client::World
{
struct GameCamera;
struct GameCameraInput;
} // namespace Client::World

// El pool mínimo que sobrevive a las escenas: 06 § Los personajes de la lista y CL7.3 B.
// main es su dueño; la vista usa las revisiones, nunca la igualdad de las entradas.
namespace Client::Scenes
{
struct SelectionSlotState
{
    glm::vec3 position{};
    std::uint64_t revision = 0;
    bool previousHad1c15 = false;
};

class SelectionState
{
public:
    [[nodiscard]] const std::array<SelectionSlotState, 5>& Slots() const;
    [[nodiscard]] bool HasReceivedList() const;
    [[nodiscard]] bool IsInitialized() const;
    [[nodiscard]] const std::optional<std::string>& Failure() const;
    [[nodiscard]] const World::WorldTerrain* Terrain() const;
    [[nodiscard]] int Map() const;
    void SetScene(World::CullingScene scene);
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
    std::array<SelectionSlotState, 5> m_slots{};
    std::optional<World::WorldTerrain> m_ownedTerrain;
    const World::WorldTerrain* m_borrowedTerrain = nullptr;
    int m_map = -1;
    World::CullingScene m_scene = World::CullingScene::Other;
    bool m_receivedList = false;
    bool m_initialized = false;
    std::optional<std::string> m_failure;
};
} // namespace Client::Scenes
