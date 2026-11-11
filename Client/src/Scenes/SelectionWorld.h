#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Render/ITextureStore.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/SceneTextures.h"
#include "Scenes/TerrainTiles.h"
#include "Scenes/TextureMetadata.h"
#include "Scenes/WorldModels.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/TerrainCells.h"
#include "World/TerrainCulling.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjects.h"
#include "World/WorldTerrain.h"

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
namespace Client::Scenes
{
struct FaunaDrawCarry;
class WorldState;

// Las texturas que la selección le presta a los sprites y a las partículas: las de su juego (0x61ca30:
// Impack03 en 0x719 y chasellight en 0x729) y, detrás, las del mundo con las globales de la carga.
class SelectionTextureLibrary final : public IModelLibrary
{
public:
    SelectionTextureLibrary(const IModelLibrary& world, const SceneTextures& scene) : m_world(world), m_scene(scene) {}
    [[nodiscard]] const Data::Models::Model* Find(std::uint16_t model) const override;
    [[nodiscard]] const Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override;
    [[nodiscard]] std::span<const Render::TextureSlot> MeshTextures(std::uint16_t model) const override;
    [[nodiscard]] std::optional<TextureMetadata> TextureMetadataOf(std::uint32_t selector) const override;
    [[nodiscard]] std::optional<Render::TextureSlot> TextureSlotOf(std::uint32_t selector) const override;
    [[nodiscard]] std::optional<glm::vec2> TextureSize(Render::TextureSlot slot) const override;

private:
    const IModelLibrary& m_world;
    const SceneTextures& m_scene;
};

// Lo que un dibujo de la selección necesita prestado para el mundo.
struct SelectionWorldFrame
{
    Render::ModelBatch& batch;
    const World::TerrainPolygon& polygon;
    const World::TerrainRange& range;
    Core::Duration frameClock;
    // Los frames del original que cuenta el reloj del dibujo; con 0 todo va sobre la proyección.
    int frames = 1;
    float fixedHeight = World::FixedCornerHeight;
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
                                                         const Render::Models::ModelTextureSlots* gameSlots,
                                                         const IModelLibrary* globalModels);
    void Release();
    [[nodiscard]] bool IsLoaded() const;
    [[nodiscard]] const WorldModels& Models() const;

    // 0x5fbf60, después de la copia de la luz: las olas de la ventana del rango con el reloj del paso.
    void StepWaves(Core::Duration clock, const World::TerrainRange& range, std::uint8_t map);
    // 0x606560 en la selección: los objetos vistos en el último dibujo, con la luz dinámica, el pool de
    // partículas y el pedido sonoro del mundo.
    [[nodiscard]] Core::Expected<void, std::string> StepObjects(WorldState& world, Render::Models::CrtRandom& random,
                                                                float worldClock);
    // 0x52d220 y 0x53c670, después de la vista previa: los efectos con modelo (la mariposa, con la altura
    // del terreno y los sprites del frame) y las juntas.
    [[nodiscard]] Core::Expected<void, std::string> StepEffects(WorldState& world, Render::Models::ClothWorld& cloth,
                                                                float worldClock,
                                                                const std::function<double(float, float)>& height);

    // Al empezar un dibujo: con cero frames copia lo que el dibujo cambia (la luz dinámica, el rand(), las
    // partículas, los efectos, los objetos y la cola); con frames, presta los del mundo.
    void BeginFrame(WorldState& world, Render::Models::ClothWorld& cloth, int frames);
    [[nodiscard]] World::TerrainLighting& FrameLighting();
    [[nodiscard]] Render::Models::CrtRandom& FrameRandom();
    [[nodiscard]] World::Particles& FrameParticles();

    // 0x5fc260(0): el terreno, el pasto y las marcas del suelo.
    [[nodiscard]] Core::Expected<void, std::string> DrawTerrain(WorldState& world, const World::WorldTerrain& terrain,
                                                                const SelectionWorldFrame& frame);
    // 0x6046a0 con los casos de 0x602ed0 → 0x425260. `poseCarry` es el buffer global de la pose del frame.
    [[nodiscard]] Core::Expected<void, std::string> DrawObjects(WorldState& world, const SelectionWorldFrame& frame,
                                                                FaunaDrawCarry& poseCarry);
    // 0x52e590, 0x53c6a0(0), 0x52d620(0) y 0x549510, después de los personajes.
    [[nodiscard]] Core::Expected<void, std::string>
    DrawAfterCharacters(WorldState& world, const World::WorldTerrain& terrain, const SelectionWorldFrame& frame,
                        const Render::Camera3D& camera, FaunaDrawCarry& poseCarry);
    // 0x6213e0..0x6213f0: 0x549440(0), 0x548550(0) y 0x548eb0(0) con la modelview en la identidad.
    [[nodiscard]] Core::Expected<void, std::string> DrawEye(const Render::Pass3D& pass,
                                                            const SelectionWorldFrame& frame,
                                                            const SceneTextures& sceneTextures,
                                                            std::span<const DamageNumberState> damage);

private:
    struct Projection;
    [[nodiscard]] World::ModelEffects& FrameEffects();
    [[nodiscard]] std::span<World::WorldObject> FrameObjects();
    [[nodiscard]] GameDrawTailState& FrameTail();
    [[nodiscard]] World::FrameSprites& FrameSprites();

    Render::ITextureStore& m_store;
    Core::Log& m_log;
    TerrainTiles m_tiles;
    WorldModels m_models;
    std::optional<std::uint8_t> m_modelsMap;
    World::TerrainWater m_water;
    World::FrameSprites m_sprites;
    World::FrameSprites m_spriteReplay;
    bool m_spriteReplayValid = false;
    World::FrameSprites m_projectedSprites;
    std::unique_ptr<Projection> m_projection;
    bool m_projecting = false;
    WorldState* m_world = nullptr;
    Render::Models::ClothWorld* m_cloth = nullptr;
};
} // namespace Client::Scenes
