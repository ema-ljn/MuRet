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
#include "Models/MeshTraits.h"
#include "Models/ModelId.h"
#include "Models/ModelTextureSlots.h"
#include "Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Kernel/DrawTailState.h"
#include "Assets/SceneTextures.h"
#include "Assets/TerrainTiles.h"
#include "Models/TextureMetadata.h"
#include "Assets/WorldModels.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Particles/Sprites.h"
#include "Terrain/GroundMarks.h"
#include "Terrain/TerrainCells.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/TerrainLighting.h"
#include "Kernel/WorldObject.h"
#include "Terrain/WorldTerrain.h"

#include <glm/ext/vector_float2.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

// World 54 of the selection (P-C50): what 0x6342c0 advances and 0x634680 draws around the
// characters. The step: the waves of 0x5fbf60, the seen objects of 0x606560 (0x4250f0), the model
// effects of 0x52d220 (the butterfly 0xae) and the joints of 0x53c670. The draw: the terrain, the grass and the
// marks of 0x5fc260(0); the objects of 0x6046a0 with their cases of 0x425260; the ribbons, the joints(0), the
// model effects(0) and 0x549510 after the characters; and in the eye pass the sprites, the
// particles and the numbers. A MuRet intermediate draw (without an original frame) runs on copies:
// it takes no CRT rand() and does not change the step state. Contract: 06 § El mundo de la selección.
namespace Client::Actors::Drawing
{
struct GameItemRenderState;
} // namespace Client::Actors::Drawing

namespace Client::Kernel
{
struct ObjectModelStepState;
struct PoseBuffer;
class WorldState;
} // namespace Client::Kernel

namespace Client::FrontEnd
{

// The textures the selection lends to the sprites and the particles: its game's (0x61ca30:
// Impack03 at 0x719 and chasellight at 0x729) and, behind them, the world's with the load's globals.
class SelectionTextureLibrary final : public Models::IModelLibrary
{
public:
    SelectionTextureLibrary(const Models::IModelLibrary& world, const Assets::SceneTextures& scene)
        : m_world(world), m_scene(scene)
    {
    }
    [[nodiscard]] const Data::Models::Model* Find(Models::ModelId model) const override;
    [[nodiscard]] const Models::Skeleton* SkeletonOf(Models::ModelId model) const override;
    [[nodiscard]] std::span<const Render::TextureSlot> MeshTextures(Models::ModelId model) const override;
    [[nodiscard]] std::span<const Models::MeshTraits> MeshTraitsOf(Models::ModelId model) const override;
    [[nodiscard]] std::optional<Models::TextureMetadata> TextureMetadataOf(std::uint32_t selector) const override;
    [[nodiscard]] std::optional<Render::TextureSlot> TextureSlotOf(std::uint32_t selector) const override;
    [[nodiscard]] std::optional<glm::vec2> TextureSize(Render::TextureSlot slot) const override;

private:
    const Models::IModelLibrary& m_world;
    const Assets::SceneTextures& m_scene;
};

// What a selection draw needs lent for the world.
struct SelectionWorldFrame
{
    Render::ModelBatch& batch;
    const Terrain::TerrainPolygon& polygon;
    const Terrain::TerrainRange& range;
    Core::Duration frameClock;
    // The original frames the draw clock counts; with 0 everything goes on the projection.
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

    // The tiles with the grass (0x61a560) and the Object55 models (0x616ec0) of map `map`, if it does not
    // have them already; `globalModels` and `gameSlots` are the load's and have to live until Release.
    [[nodiscard]] Core::Expected<void, std::string> Load(const std::filesystem::path& dataRoot, std::uint8_t map,
                                                         const Models::ModelTextureSlots* gameSlots,
                                                         const Models::IModelLibrary* globalModels);
    void Release();
    [[nodiscard]] bool IsLoaded() const;
    [[nodiscard]] const Assets::WorldModels& Models() const;

    // 0x5fbf60, after the light copy: the waves of the range's window with the step clock.
    void StepWaves(Core::Duration clock, const Terrain::TerrainRange& range, std::uint8_t map);
    // 0x606560 in the selection: the objects seen in the last draw, with the dynamic light, the particle pool
    // and the world's sound request. `terrain` is the current map's (Terrain::TerrainState::current), and
    // `dynamicLighting` the dynamic light (Terrain::TerrainState::dynamicLight).
    [[nodiscard]] Core::Expected<void, std::string> StepObjects(Kernel::WorldState& world,
                                                                const Terrain::WorldTerrain& terrain,
                                                                Terrain::TerrainLighting& dynamicLighting,
                                                                Crt::CrtRandom& random, float worldClock);
    // 0x52d220 and 0x53c670, after the preview: the model effects (the butterfly, with the terrain height
    // and the frame's sprites, and the Fenrir's decal 0x14e, with timeGetTime() in `systemTime`) and the
    // joints.
    [[nodiscard]] Core::Expected<void, std::string>
    StepEffects(Kernel::WorldState& world, Terrain::TerrainLighting& dynamicLighting, Models::ClothWorld& cloth,
                float worldClock, const std::function<double(float, float)>& height, std::uint32_t systemTime);

    // At the start of a draw: with zero frames it copies what the draw changes (the dynamic light, the rand(), the
    // particles, the effects, the objects, the tail, the items' globals and the unlit meshes); with frames,
    // it lends the world's. The list's characters (0x50ba30) use the same ones.
    void BeginFrame(Kernel::WorldState& world, Terrain::TerrainLighting& dynamicLighting, Models::ClothWorld& cloth,
                    int frames);
    [[nodiscard]] Terrain::TerrainLighting& FrameLighting();
    [[nodiscard]] Crt::CrtRandom& FrameRandom();
    [[nodiscard]] Models::ClothWorld& FrameCloth();
    [[nodiscard]] Particles::Particles& FrameParticles();
    [[nodiscard]] Effects::ModelEffects& FrameEffects();
    [[nodiscard]] Kernel::GameDrawTailState& FrameTail();
    // The frame's sprite table (0x5491e0): the world's, or the projected one in an intermediate draw.
    [[nodiscard]] Particles::FrameSprites& FrameSprites();
    [[nodiscard]] Actors::Drawing::GameItemRenderState& FrameItemRender();
    [[nodiscard]] Kernel::ObjectModelStepState& FrameModelState();

    // 0x5fc260(0): the terrain, the grass and the ground marks.
    [[nodiscard]] Core::Expected<void, std::string> DrawTerrain(Kernel::WorldState& world,
                                                                const Terrain::WorldTerrain& terrain,
                                                                const Terrain::GroundMarkTable& groundMarks,
                                                                const SelectionWorldFrame& frame);
    // 0x6046a0 with the cases of 0x602ed0 → 0x425260. `poseCarry` is the frame's global pose buffer.
    [[nodiscard]] Core::Expected<void, std::string>
    DrawObjects(Kernel::WorldState& world, const SelectionWorldFrame& frame, Kernel::PoseBuffer& poseCarry);
    // 0x52e590, 0x53c6a0(0), 0x52d620(0) and 0x549510, after the characters.
    [[nodiscard]] Core::Expected<void, std::string> DrawAfterCharacters(Kernel::WorldState& world,
                                                                        const Terrain::WorldTerrain& terrain,
                                                                        const SelectionWorldFrame& frame,
                                                                        const Render::Camera3D& camera,
                                                                        Kernel::PoseBuffer& poseCarry);
    // 0x6213e0..0x6213f0: 0x549440(0), 0x548550(0) and 0x548eb0(0) with the modelview at identity.
    [[nodiscard]] Core::Expected<void, std::string> DrawEye(const Render::Pass3D& pass,
                                                            const SelectionWorldFrame& frame,
                                                            const Assets::SceneTextures& sceneTextures,
                                                            std::span<const Kernel::DamageNumberState> damage);

private:
    struct Projection;
    [[nodiscard]] std::span<Kernel::WorldObject> FrameObjects();

    Render::ITextureStore& m_store;
    Core::Log& m_log;
    Assets::TerrainTiles m_tiles;
    Assets::WorldModels m_models;
    std::optional<std::uint8_t> m_modelsMap;
    Terrain::TerrainWater m_water;
    Particles::FrameSprites m_sprites;
    Particles::FrameSprites m_spriteReplay;
    bool m_spriteReplayValid = false;
    Particles::FrameSprites m_projectedSprites;
    std::unique_ptr<Projection> m_projection;
    bool m_projecting = false;
    Kernel::WorldState* m_world = nullptr;
    Terrain::TerrainLighting* m_dynamicLighting = nullptr;
    Models::ClothWorld* m_cloth = nullptr;
};
} // namespace Client::FrontEnd
