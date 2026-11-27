#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Render/ITextureStore.h"
#include "Render/ModelBatch.h"
#include "Models/Cloth.h"
#include "Models/IModelLibrary.h"
#include "Models/ModelTextureSlots.h"
#include "Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Scenes/GameDrawTail.h"
#include "Assets/SceneTextures.h"
#include "Assets/TerrainTiles.h"
#include "Models/TextureMetadata.h"
#include "Assets/WorldModels.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "Terrain/GroundMarks.h"
#include "Terrain/TerrainCells.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/TerrainLighting.h"
#include "World/WorldObjects.h"
#include "Terrain/WorldTerrain.h"

#include <glm/ext/vector_float2.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

// El mundo 54 de la selección (P-C50): lo que 0x6342c0 avanza y 0x634680 dibuja alrededor de los
// personajes. El paso: las olas de 0x5fbf60, los objetos vistos de 0x606560 (0x4250f0), los efectos con
// modelo de 0x52d220 (la mariposa 0xae) y las juntas de 0x53c670. El dibujo: el terreno, el pasto y las
// marcas de 0x5fc260(0); los objetos de 0x6046a0 con sus casos de 0x425260; las cintas, las juntas(0), los
// efectos con modelo(0) y 0x549510 después de los personajes; y en el pase de ojo los sprites, las
// partículas y los números. Un dibujo intermedio de MuRet (sin frame del original) corre sobre copias:
// no saca rand() de la CRT ni cambia el estado del paso. Contrato: 06 § El mundo de la selección.
namespace Client::World
{
struct ObjectModelStepState;
}

namespace Client::Actors::Drawing
{
struct GameItemRenderState;
} // namespace Client::Actors::Drawing

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct FaunaDrawCarry;

// Las texturas que la selección le presta a los sprites y a las partículas: las de su juego (0x61ca30:
// Impack03 en 0x719 y chasellight en 0x729) y, detrás, las del mundo con las globales de la carga.
class SelectionTextureLibrary final : public Models::IModelLibrary
{
public:
    SelectionTextureLibrary(const Models::IModelLibrary& world, const Assets::SceneTextures& scene)
        : m_world(world), m_scene(scene)
    {
    }
    [[nodiscard]] const Data::Models::Model* Find(std::uint16_t model) const override;
    [[nodiscard]] const Models::Skeleton* SkeletonOf(std::uint16_t model) const override;
    [[nodiscard]] std::span<const Render::TextureSlot> MeshTextures(std::uint16_t model) const override;
    [[nodiscard]] std::optional<Models::TextureMetadata> TextureMetadataOf(std::uint32_t selector) const override;
    [[nodiscard]] std::optional<Render::TextureSlot> TextureSlotOf(std::uint32_t selector) const override;
    [[nodiscard]] std::optional<glm::vec2> TextureSize(Render::TextureSlot slot) const override;

private:
    const Models::IModelLibrary& m_world;
    const Assets::SceneTextures& m_scene;
};

// Lo que un dibujo de la selección necesita prestado para el mundo.
struct SelectionWorldFrame
{
    Render::ModelBatch& batch;
    const Terrain::TerrainPolygon& polygon;
    const Terrain::TerrainRange& range;
    Core::Duration frameClock;
    // Los frames del original que cuenta el reloj del dibujo; con 0 todo va sobre la proyección.
    int frames = 1;
    float fixedHeight = Terrain::FixedCornerHeight;
    std::function<void()> flushModels{};
};

class SelectionWorld
{
public:
    SelectionWorld(Render::ITextureStore& store, Core::Log& log);
    SelectionWorld(const SelectionWorld&) = delete;
    SelectionWorld& operator=(const SelectionWorld&) = delete;
    SelectionWorld(SelectionWorld&&) = delete;
    SelectionWorld& operator=(SelectionWorld&&) = delete;
    ~SelectionWorld();

    // Los tiles con el pasto (0x61a560) y los modelos de Object55 (0x616ec0) del mapa `map`, si no los
    // tiene ya; `globalModels` y `gameSlots` son los de la carga y tienen que vivir hasta Release.
    [[nodiscard]] Core::Expected<void, std::string> Load(const std::filesystem::path& dataRoot, std::uint8_t map,
                                                         const Models::ModelTextureSlots* gameSlots,
                                                         const Models::IModelLibrary* globalModels);
    void Release();
    [[nodiscard]] bool IsLoaded() const;
    [[nodiscard]] const Assets::WorldModels& Models() const;

    // 0x5fbf60, después de la copia de la luz: las olas de la ventana del rango con el reloj del paso.
    void StepWaves(Core::Duration clock, const Terrain::TerrainRange& range, std::uint8_t map);
    // 0x606560 en la selección: los objetos vistos en el último dibujo, con la luz dinámica, el pool de
    // partículas y el pedido sonoro del mundo. `terrain` is the current map's (Terrain::TerrainState::current).
    [[nodiscard]] Core::Expected<void, std::string> StepObjects(Kernel::WorldState& world,
                                                                const Terrain::WorldTerrain& terrain,
                                                                Crt::CrtRandom& random, float worldClock);
    // 0x52d220 y 0x53c670, después de la vista previa: los efectos con modelo (la mariposa, con la altura
    // del terreno y los sprites del frame, y el calco 0x14e de los Fenrir, con timeGetTime() en `systemTime`) y las
    // juntas.
    [[nodiscard]] Core::Expected<void, std::string> StepEffects(Kernel::WorldState& world, Models::ClothWorld& cloth,
                                                                float worldClock,
                                                                const std::function<double(float, float)>& height,
                                                                std::uint32_t systemTime);

    // Al empezar un dibujo: con cero frames copia lo que el dibujo cambia (la luz dinámica, el rand(), las
    // partículas, los efectos, los objetos, la cola, los globales de los ítems y las mallas sin luz); con frames,
    // presta los del mundo. Los personajes de la lista (0x50ba30) usan los mismos.
    void BeginFrame(Kernel::WorldState& world, Models::ClothWorld& cloth, int frames);
    [[nodiscard]] Terrain::TerrainLighting& FrameLighting();
    [[nodiscard]] Crt::CrtRandom& FrameRandom();
    [[nodiscard]] Models::ClothWorld& FrameCloth();
    [[nodiscard]] World::Particles& FrameParticles();
    [[nodiscard]] World::ModelEffects& FrameEffects();
    [[nodiscard]] GameDrawTailState& FrameTail();
    // La tabla de los sprites del frame (0x5491e0): la del mundo, o la proyectada en un dibujo intermedio.
    [[nodiscard]] World::FrameSprites& FrameSprites();
    [[nodiscard]] Actors::Drawing::GameItemRenderState& FrameItemRender();
    [[nodiscard]] World::ObjectModelStepState& FrameModelState();

    // 0x5fc260(0): el terreno, el pasto y las marcas del suelo.
    [[nodiscard]] Core::Expected<void, std::string> DrawTerrain(Kernel::WorldState& world,
                                                                const Terrain::WorldTerrain& terrain,
                                                                const Terrain::GroundMarkTable& groundMarks,
                                                                const SelectionWorldFrame& frame);
    // 0x6046a0 con los casos de 0x602ed0 → 0x425260. `poseCarry` es el buffer global de la pose del frame.
    [[nodiscard]] Core::Expected<void, std::string>
    DrawObjects(Kernel::WorldState& world, const SelectionWorldFrame& frame, FaunaDrawCarry& poseCarry);
    // 0x52e590, 0x53c6a0(0), 0x52d620(0) y 0x549510, después de los personajes.
    [[nodiscard]] Core::Expected<void, std::string>
    DrawAfterCharacters(Kernel::WorldState& world, const Terrain::WorldTerrain& terrain,
                        const SelectionWorldFrame& frame, const Render::Camera3D& camera, FaunaDrawCarry& poseCarry);
    // 0x6213e0..0x6213f0: 0x549440(0), 0x548550(0) y 0x548eb0(0) con la modelview en la identidad.
    [[nodiscard]] Core::Expected<void, std::string> DrawEye(const Render::Pass3D& pass,
                                                            const SelectionWorldFrame& frame,
                                                            const Assets::SceneTextures& sceneTextures,
                                                            std::span<const DamageNumberState> damage);

private:
    struct Projection;
    [[nodiscard]] std::span<World::WorldObject> FrameObjects();

    Render::ITextureStore& m_store;
    Core::Log& m_log;
    Assets::TerrainTiles m_tiles;
    Assets::WorldModels m_models;
    std::optional<std::uint8_t> m_modelsMap;
    Terrain::TerrainWater m_water;
    World::FrameSprites m_sprites;
    World::FrameSprites m_spriteReplay;
    bool m_spriteReplayValid = false;
    World::FrameSprites m_projectedSprites;
    std::unique_ptr<Projection> m_projection;
    bool m_projecting = false;
    Kernel::WorldState* m_world = nullptr;
    Models::ClothWorld* m_cloth = nullptr;
};
} // namespace Client::Scenes
