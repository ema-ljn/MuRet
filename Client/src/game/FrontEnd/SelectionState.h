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

// La selección sobre el pool durable (06, "Los personajes de la lista", y CL7.3 B): construye en los
// cinco primeros registros de WorldState y escribe el mundo en curso en su WorldRuntime, sin otra
// copia. main es dueño de los dos; la vista usa las revisiones, nunca la igualdad de las entradas.
namespace Client::FrontEnd
{
class SelectionState
{
public:
    // `world` tiene que vivir más que este estado.
    explicit SelectionState(Kernel::WorldState& world);

    // Los cinco primeros registros del pool, los lugares de la lista.
    [[nodiscard]] std::span<const Kernel::ActorState, Kernel::SelectionActorCount> Slots() const;
    // 0x7437050 vive fuera del pool y sobrevive al objeto visual de la escena, igual que las tablas.
    [[nodiscard]] Kernel::ActorState& PreviewActor();
    [[nodiscard]] const Kernel::ActorState& PreviewActor() const;
    [[nodiscard]] bool HasReceivedList() const;
    [[nodiscard]] bool IsInitialized() const;
    [[nodiscard]] const std::optional<std::string>& Failure() const;
    [[nodiscard]] const Terrain::WorldTerrain* Terrain() const;
    // El mundo en curso, WorldRuntime::map.
    [[nodiscard]] int Map() const;
    void SetScene(Terrain::CullingScene scene);
    // Las dos escriben `map` como mundo en curso, como quien llama a 0x61a560.
    // El dueño de `terrain` debe mantenerlo vivo hasta RetainTerrain o el próximo BorrowTerrain.
    // Si ya es el terreno retenido por este estado, conserva su ownership.
    void BorrowTerrain(int map, const Terrain::WorldTerrain* terrain);
    void RetainTerrain(int map, Terrain::WorldTerrain terrain);
    void BeginSelection();
    void EnterLogin();
    // CL7.3 B: Hero conserva celda lógica 0; copia su atributo vivo sin escribir XYZ. Después de un F1 02
    // con 1 el héroe es el del juego, en su celda, pero la cámara de la selección solo mira el 0x40 y el
    // 0x80, y fuera del mapa 30, que MuRet no carga, ningún .att los tiene (05): con el terreno del juego
    // o con el del 54, da lo mismo (Q-C213).
    void RefreshCameraAttribute(Camera::GameCameraInput& input) const;
    // 0x633f20 con la lista: el mundo cargado, el 54 como mundo en curso (0x633f44) y 0x61a560(54), la carga
    // de LoadMap en la escena 4 (los objetos del .obj con sus casos de la selección, 0x606660, y el pasto
    // con sus 65.536 rand()). El terreno queda en `terrain.current` y este estado lo presta.
    // `random` es el rand() de la CRT; sin él, el local de las pruebas. `faunaModels`: MapLoadContext::faunaModels.
    [[nodiscard]] Core::Expected<void, std::string> InitializeSelection(const std::filesystem::path& dataRoot,
                                                                        Terrain::TerrainState& terrain,
                                                                        Camera::GameCameraInput& input,
                                                                        Crt::CrtRandom* random = nullptr,
                                                                        Assets::FaunaModels* faunaModels = nullptr);
    [[nodiscard]] Core::Expected<void, std::string> Apply(std::span<const App::CharacterEvent> events,
                                                          Camera::GameCamera& camera, Camera::GameCameraInput& input,
                                                          Models::ClothWorld* cloth = nullptr,
                                                          const Models::Skeleton* playerSkeleton = nullptr);
    // Se llama después de la cámara de selección: 0x634680 escribe Z a los cinco slots.
    void DrawSelection(Camera::GameCameraInput& input);

private:
    [[nodiscard]] Core::Expected<void, std::string> Construct(const Protocol::FrontEnd::CharacterEntry& entry,
                                                              const Camera::GameCamera& camera,
                                                              Camera::GameCameraInput& input, Models::ClothWorld& cloth,
                                                              const Models::Skeleton& playerSkeleton);
    [[nodiscard]] Core::Expected<void, std::string> Fail(std::string error);
    Kernel::WorldState& m_world;
    Kernel::ActorState m_preview;
    // Solo quien prueba el estado sin el bucle usa este rand(); main aporta el de toda la sesión.
    Models::ClothWorld m_localCloth;
    std::optional<Terrain::WorldTerrain> m_ownedTerrain;
    const Terrain::WorldTerrain* m_borrowedTerrain = nullptr;
    Terrain::CullingScene m_scene = Terrain::CullingScene::Other;
    bool m_receivedList = false;
    bool m_initialized = false;
    std::optional<std::string> m_failure;
};
} // namespace Client::FrontEnd
