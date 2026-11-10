#pragma once

#include "App/CharacterEvent.h"
#include "Core/Expected.h"
#include "Protocol/Character/CharacterList.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/WorldState.h"
#include "World/TerrainCulling.h"
#include "World/WorldTerrain.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace Client::World
{
struct GameCamera;
struct GameCameraInput;
} // namespace Client::World

namespace Client::Render::Models
{
struct Skeleton;
}

// La selección sobre el pool durable (06, "Los personajes de la lista", y CL7.3 B): construye en los
// cinco primeros registros de WorldState y escribe el mundo en curso en su WorldRuntime, sin otra
// copia. main es dueño de los dos; la vista usa las revisiones, nunca la igualdad de las entradas.
namespace Client::Scenes
{
class SelectionState
{
public:
    // `world` tiene que vivir más que este estado.
    explicit SelectionState(WorldState& world);

    // Los cinco primeros registros del pool, los lugares de la lista.
    [[nodiscard]] std::span<const ActorState, SelectionActorCount> Slots() const;
    // 0x7437050 vive fuera del pool y sobrevive al objeto visual de la escena, igual que las tablas.
    [[nodiscard]] ActorState& PreviewActor();
    [[nodiscard]] const ActorState& PreviewActor() const;
    [[nodiscard]] bool HasReceivedList() const;
    [[nodiscard]] bool IsInitialized() const;
    [[nodiscard]] const std::optional<std::string>& Failure() const;
    [[nodiscard]] const World::WorldTerrain* Terrain() const;
    // El mundo en curso, WorldRuntime::map.
    [[nodiscard]] int Map() const;
    void SetScene(World::CullingScene scene);
    // Las dos escriben `map` como mundo en curso, como quien llama a 0x61a560.
    // El dueño de `terrain` debe mantenerlo vivo hasta RetainTerrain o el próximo BorrowTerrain.
    // Si ya es el terreno retenido por este estado, conserva su ownership.
    void BorrowTerrain(int map, const World::WorldTerrain* terrain);
    void RetainTerrain(int map, World::WorldTerrain terrain);
    void BeginSelection();
    void EnterLogin();
    // CL7.3 B: Hero conserva celda lógica 0; copia su atributo vivo sin escribir XYZ. Después de un F1 02
    // con 1 el héroe es el del juego, en su celda, pero la cámara de la selección solo mira el 0x40 y el
    // 0x80, y fuera del mapa 30, que MuRet no carga, ningún .att los tiene (05): con el terreno del juego
    // o con el del 54, da lo mismo (Q-C213).
    void RefreshCameraAttribute(World::GameCameraInput& input) const;
    [[nodiscard]] Core::Expected<void, std::string> InitializeSelection(const std::filesystem::path& dataRoot,
                                                                        World::GameCameraInput& input);
    [[nodiscard]] Core::Expected<void, std::string> Apply(std::span<const App::CharacterEvent> events,
                                                          World::GameCamera& camera, World::GameCameraInput& input,
                                                          Render::Models::ClothWorld* cloth = nullptr,
                                                          const Render::Models::Skeleton* playerSkeleton = nullptr);
    // Se llama después de la cámara de selección: 0x634680 escribe Z a los cinco slots.
    void DrawSelection(World::GameCameraInput& input);

private:
    [[nodiscard]] Core::Expected<void, std::string> Construct(const Protocol::Character::CharacterEntry& entry,
                                                              const World::GameCamera& camera,
                                                              World::GameCameraInput& input,
                                                              Render::Models::ClothWorld& cloth,
                                                              const Render::Models::Skeleton& playerSkeleton);
    [[nodiscard]] Core::Expected<void, std::string> Fail(std::string error);
    WorldState& m_world;
    ActorState m_preview;
    // Solo quien prueba el estado sin el bucle usa este rand(); main aporta el de toda la sesión.
    Render::Models::ClothWorld m_localCloth;
    std::optional<World::WorldTerrain> m_ownedTerrain;
    const World::WorldTerrain* m_borrowedTerrain = nullptr;
    World::CullingScene m_scene = World::CullingScene::Other;
    bool m_receivedList = false;
    bool m_initialized = false;
    std::optional<std::string> m_failure;
};
} // namespace Client::Scenes
