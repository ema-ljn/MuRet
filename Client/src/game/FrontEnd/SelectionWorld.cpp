#include "FrontEnd/SelectionWorld.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Render/Billboard.h"
#include "Render/ITextureStore.h"
#include "Render/ModelBatch.h"
#include "Models/Cloth.h"
#include "Models/IModelLibrary.h"
#include "Models/MeshTraits.h"
#include "Models/ModelTextureSlots.h"
#include "Models/Skinning.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Events/ObjectCollapse.h"
#include "Actors/Drawing/PosePublishing.h"
#include "Kernel/PoseBuffer.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/DrawTailState.h"
#include "Scenes/GameDrawTail.h"
#include "Joints/JointDrawing.h"
#include "Actors/Drawing/GameItemRenderState.h"
#include "Effects/GameParticles.h"
#include "Assets/SceneTextures.h"
#include "Assets/TerrainTiles.h"
#include "Models/TextureMetadata.h"
#include "Models/ModelId.h"
#include "Kernel/WorldMap.h"
#include "WorldObjects/WorldObjectsDrawing.h"
#include "Particles/SpriteDrawing.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Effects/GameModelEffects.h"
#include "Kernel/JointRequest.h"
#include "Terrain/GroundMarks.h"
#include "Effects/ModelEffects.h"
#include "Kernel/WorldObject.h"
#include "Particles/Particles.h"
#include "Terrain/SphereFrustum.h"
#include "Particles/Sprites.h"
#include "Terrain/TerrainCells.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/TerrainLighting.h"
#include "Kernel/ObjectPass.h"
#include "Terrain/WorldTerrain.h"
#include "Audio/SoundId.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <ratio>
#include <span>
#include <string>
#include <vector>

namespace Client::FrontEnd
{
namespace
{
constexpr std::int32_t SelectionScene = 4;

float Milliseconds(Core::Duration clock)
{
    return static_cast<float>(std::chrono::duration<double, std::milli>(clock).count());
}

// 0x5fc365: MuRet does not upload Effect\blood01.tga (P-C49); with a mark on, DrawGroundMarks stops.
std::optional<std::uint8_t> NoSlotComponents(std::int32_t /*slot*/)
{
    return std::nullopt;
}
} // namespace

const Data::Models::Model* SelectionTextureLibrary::Find(Models::ModelId model) const
{
    return m_world.Find(model);
}

const Models::Skeleton* SelectionTextureLibrary::SkeletonOf(Models::ModelId model) const
{
    return m_world.SkeletonOf(model);
}

std::span<const Render::TextureSlot> SelectionTextureLibrary::MeshTextures(Models::ModelId model) const
{
    return m_world.MeshTextures(model);
}

std::span<const Models::MeshTraits> SelectionTextureLibrary::MeshTraitsOf(Models::ModelId model) const
{
    return m_world.MeshTraitsOf(model);
}

std::optional<Models::TextureMetadata> SelectionTextureLibrary::TextureMetadataOf(std::uint32_t selector) const
{
    if (auto scene = m_scene.MetadataOf(selector))
        return scene;
    return m_world.TextureMetadataOf(selector);
}

std::optional<Render::TextureSlot> SelectionTextureLibrary::TextureSlotOf(std::uint32_t selector) const
{
    // The key came from its entry's 16-bit slot: nothing is truncated.
    if (m_scene.MetadataOf(selector))
        return Render::TextureSlot{static_cast<std::uint16_t>(selector)};
    return m_world.TextureSlotOf(selector);
}

std::optional<glm::vec2> SelectionTextureLibrary::TextureSize(Render::TextureSlot slot) const
{
    if (auto scene = m_scene.MetadataOf(slot.value))
        return scene->size;
    return m_world.TextureSize(slot);
}

// A single owner per intermediate draw: all the services lend these same copies.
struct SelectionWorld::Projection
{
    Terrain::TerrainLighting lighting{};
    Models::ClothWorld cloth{.random = Crt::CrtRandom{}};
    Particles::Particles particles{};
    Effects::ModelEffects effects{};
    std::vector<Kernel::WorldObject> objects{};
    Kernel::GameDrawTailState tail{};
    Actors::Drawing::GameItemRenderState items{};
    Kernel::ObjectModelStepState modelState{};
};

SelectionWorld::SelectionWorld(Render::ITextureStore& store, Core::Log& log)
    : m_store(store), m_log(log), m_tiles(store)
{
}

SelectionWorld::~SelectionWorld() = default;

Core::Expected<void, std::string> SelectionWorld::Load(const std::filesystem::path& dataRoot, std::uint8_t map,
                                                       const Models::ModelTextureSlots* gameSlots,
                                                       const Models::IModelLibrary* globalModels)
{
    if (m_tiles.LoadedMap() != map)
    {
        std::vector<std::string> skipped;
        if (auto loaded = m_tiles.Load(dataRoot, map, skipped, Assets::GrassTextures::Upload); !loaded.HasValue())
        {
            return Core::Unexpected("0x61a560: los tiles del terreno del mapa " + std::to_string(map) + ": " +
                                    loaded.Error());
        }
        for (const std::string& tile : skipped)
        {
            m_log.Warning("la selección va sin una textura del terreno, que el original saltea: {}", tile);
        }
    }
    if (m_modelsMap != map)
    {
        m_models.Release();
        m_modelsMap.reset();
        if (auto loaded = m_models.Load(dataRoot, map, m_store, m_log, gameSlots, globalModels); !loaded.HasValue())
        {
            return Core::Unexpected("0x616ec0: los modelos de los objetos del mapa " + std::to_string(map) + ": " +
                                    loaded.Error());
        }
        m_modelsMap = map;
    }
    return {};
}

void SelectionWorld::Release()
{
    m_tiles.Release();
    m_models.Release();
    m_modelsMap.reset();
    m_sprites.Clear();
    m_spriteReplay.Clear();
    m_spriteReplayValid = false;
    m_projection.reset();
    m_projecting = false;
}

bool SelectionWorld::IsLoaded() const
{
    return m_modelsMap.has_value() && m_tiles.LoadedMap().has_value();
}

const Assets::WorldModels& SelectionWorld::Models() const
{
    return m_models;
}

void SelectionWorld::StepWaves(Core::Duration clock, const Terrain::TerrainRange& range, std::uint8_t map)
{
    Terrain::UpdateWaves(m_water, clock, range, map);
}

Core::Expected<void, std::string> SelectionWorld::StepObjects(Kernel::WorldState& world,
                                                              const Terrain::WorldTerrain& terrain,
                                                              Terrain::TerrainLighting& dynamicLighting,
                                                              Crt::CrtRandom& random, float worldClock)
{
    std::span<Kernel::WorldObject> objects = world.Map().objects;
    // 0x5ff960 with each live one of the walk, also while waiting (Events/ObjectCollapse.h). The scene does not
    // have the game camera's shake: the Chaos Castle stone stops before, in the pool (0x4056e5, 0x4057bf and
    // 0x405899).
    const Events::ObjectCollapseContext collapseContext{.runtime = world.Runtime(),
                                                        .random = random,
                                                        .particles = &world.Particles(),
                                                        .worldClock = worldClock,
                                                        .heroZ = world.Hero().position.z,
                                                        .cameraShake = nullptr,
                                                        .attributes = terrain.attributes.get()};
    const WorldObjects::AfterObjectStep collapse = [&collapseContext](Kernel::WorldObject& object)
    { return Events::CollapseTheObject(object, collapseContext); };
    // Without a seen object (before the first draw that reaches 0x6046a0), 0x606560 steps none of them: what
    // remains is the collapse with the live ones and the count (0x606610).
    if (std::ranges::none_of(objects, [](const Kernel::WorldObject& object) { return object.alive && object.seen; }))
    {
        if (auto walked = WorldObjects::WalkTheObjectSectors(objects, {}, collapse); !walked.HasValue())
            return Core::Unexpected(walked.Error() + ", en la selección");
        Events::CountDownTheObjectCollapse(world.Runtime());
        return {};
    }
    const auto map = static_cast<std::uint8_t>(world.Runtime().map);
    const Kernel::ObjectStepGlobals globals{.heroCellLayer = world.Runtime().heroCellLayer,
                                            .worldClock = worldClock,
                                            .dynamicLighting = &dynamicLighting,
                                            .particles = &world.Particles(),
                                            .sound = [&world](Audio::SoundId id) { world.Runtime().RequestSound(id); },
                                            .modelState = &world.Runtime().objectModelState};
    if (auto stepped = WorldObjects::StepGameWorldObjects(objects, map, m_models, random, globals, {}, collapse);
        !stepped.HasValue())
    {
        return Core::Unexpected(stepped.Error() + ", en la selección");
    }
    Events::CountDownTheObjectCollapse(world.Runtime());
    return {};
}

Core::Expected<void, std::string> SelectionWorld::StepEffects(Kernel::WorldState& world,
                                                              Terrain::TerrainLighting& dynamicLighting,
                                                              Models::ClothWorld& cloth, float worldClock,
                                                              const std::function<double(float, float)>& height,
                                                              std::uint32_t systemTime)
{
    const Effects::ModelEffectStepServices services{
        .terrainHeight = height,
        .sprites = &m_sprites,
        .effects = &world.Map().effects,
        .lighting = &dynamicLighting,
        .systemTime = [systemTime]() { return systemTime; },
        .sound = [&world](Audio::SoundId id) { world.Runtime().RequestSound(id); },
        .joint = [&world, &cloth](const Kernel::JointRequest& request) -> Core::Expected<void, std::string>
        {
            const auto created = Joints::CreateFaunaJoint(world.DrawTail().joints, request, cloth.random);
            if (!created.HasValue())
                return Core::Unexpected(created.Error());
            return {};
        },
        .publishAction =
            [&world](std::uint16_t type, std::uint8_t action)
        {
            auto placement = world.ModelPlacement(type);
            placement.action = action;
            world.PlaceModel(type, placement);
        },
        .soundAt = [&world](Audio::SoundId id, const glm::vec3& origin) { world.Runtime().RequestSound(id, origin); }};
    for (Effects::ModelEffect& effect : world.Map().effects.Effects())
    {
        const auto* skeleton = effect.type == Effects::EarthquakeBurstEffect
                                   ? nullptr
                                   : m_models.SkeletonOf(Models::ModelId{static_cast<std::uint16_t>(effect.type)});
        if (auto stepped = Effects::StepGameModelEffect(effect, skeleton, world.Particles(), worldClock, cloth.random,
                                                        world.Map().kalima.get(), services);
            !stepped.HasValue())
        {
            return Core::Unexpected(stepped.Error() + ", en la selección (0x52d220)");
        }
    }
    // 0x634310 → 0x53c670: the same joints step as the game (0x637123), with the same globals: the
    // dynamic light (0x7b4ccc4), the terrain height (0x5f93c0), the 400 characters (0x743a16c), the effects table
    // (0x7491fd8), the hero ([0x743a170]) and the shared models (0x5769ad8). The owner arrow's collision
    // (0x520170, which the 0x60f/0xb and /0x19 with skill 1 ask for) stays without a service: without it, it stops
    // at 0x53ae6d. Neither the siege and the hero's targets (JointKernels::VolleyServices) nor the models of 0x20b
    // go: their joints (the 0x613 of width 80, the 0x61c/0 and the 0x632/0 and /1) are created by skills the
    // selection does not run; if they arrived, they stop.
    const auto actors = world.Actors();
    const std::size_t heroSlot = world.Runtime().hero;
    const Kernel::ActorState* hero = heroSlot < actors.size() ? &actors[heroSlot] : nullptr;
    if (auto stepped = Joints::StepJoints(
            world.DrawTail().joints, cloth.random, &cloth, &world.Particles(), worldClock, &m_sprites, &dynamicLighting,
            [&world](Audio::SoundId id) { world.Runtime().RequestSound(id); }, height, actors, {}, &world.Map().effects,
            hero, &world);
        !stepped.HasValue())
    {
        return Core::Unexpected(stepped.Error() + ", en la selección (0x53c670)");
    }
    return {};
}

void SelectionWorld::BeginFrame(Kernel::WorldState& world, Terrain::TerrainLighting& dynamicLighting,
                                Models::ClothWorld& cloth, int frames)
{
    m_world = &world;
    m_dynamicLighting = &dynamicLighting;
    m_cloth = &cloth;
    m_projecting = frames == 0;
    if (!m_projecting)
    {
        return;
    }
    if (!m_projection)
    {
        m_projection = std::make_unique<Projection>();
    }
    m_projection->lighting = dynamicLighting;
    m_projection->cloth = cloth;
    m_projection->particles = world.Particles();
    m_projection->effects = world.Map().effects;
    m_projection->objects = world.Map().objects;
    m_projection->tail = world.DrawTail();
    m_projection->items = world.Runtime().itemRender;
    m_projection->modelState = world.Runtime().objectModelState;
    // The intermediate draw's sprites come from the last original frame's sample (0x549510).
    m_projectedSprites.Clear();
}

Terrain::TerrainLighting& SelectionWorld::FrameLighting()
{
    return m_projecting ? m_projection->lighting : *m_dynamicLighting;
}

Crt::CrtRandom& SelectionWorld::FrameRandom()
{
    return m_projecting ? m_projection->cloth.random : m_cloth->random;
}

Models::ClothWorld& SelectionWorld::FrameCloth()
{
    return m_projecting ? m_projection->cloth : *m_cloth;
}

Actors::Drawing::GameItemRenderState& SelectionWorld::FrameItemRender()
{
    return m_projecting ? m_projection->items : m_world->Runtime().itemRender;
}

Kernel::ObjectModelStepState& SelectionWorld::FrameModelState()
{
    return m_projecting ? m_projection->modelState : m_world->Runtime().objectModelState;
}

Particles::Particles& SelectionWorld::FrameParticles()
{
    return m_projecting ? m_projection->particles : m_world->Particles();
}

Effects::ModelEffects& SelectionWorld::FrameEffects()
{
    return m_projecting ? m_projection->effects : m_world->Map().effects;
}

std::span<Kernel::WorldObject> SelectionWorld::FrameObjects()
{
    return m_projecting ? std::span(m_projection->objects) : std::span(m_world->Map().objects);
}

Kernel::GameDrawTailState& SelectionWorld::FrameTail()
{
    return m_projecting ? m_projection->tail : m_world->DrawTail();
}

Particles::FrameSprites& SelectionWorld::FrameSprites()
{
    return m_projecting ? m_projectedSprites : m_sprites;
}

Core::Expected<void, std::string> SelectionWorld::DrawTerrain(Kernel::WorldState& world,
                                                              const Terrain::WorldTerrain& terrain,
                                                              const Terrain::GroundMarkTable& groundMarks,
                                                              const SelectionWorldFrame& frame)
{
    if (!terrain.mapping || !terrain.heights || !terrain.attributes)
    {
        return Core::Unexpected(std::string("0x5fc260: el terreno 54 de la selección no está cargado (0x61a560)"));
    }
    const auto map = static_cast<std::uint8_t>(world.Runtime().map);
    Terrain::TerrainView view;
    view.mapping = terrain.mapping.get();
    view.heights = terrain.heights.get();
    view.attributes = terrain.attributes.get();
    view.lighting = &FrameLighting();
    view.tiles = m_tiles.Sizes();
    view.map = map;
    view.fixedHeight = frame.fixedHeight;
    // 0x5fc26b..0x5fc2c0: the water offset with the frame clock; 0x5fc2de: the terrain pass.
    m_water.offset = Terrain::WaterOffsetAt(frame.frameClock, map);
    Terrain::DrawTerrain(frame.batch, view, frame.range, frame.polygon, m_water);
    // 0x5fc32b..0x5fc353: the grass pass (0x6c9d58 on outside maps 18 to 23 and 30).
    const Terrain::TerrainGrass grass{.heights = m_tiles.GrassHeights(), .offsets = world.Map().grassOffsets};
    if (auto grown = Terrain::DrawGrass(frame.batch, view, frame.range, frame.polygon, m_water, grass);
        !grown.HasValue())
    {
        return Core::Unexpected(grown.Error() + ", en el pase del pasto de la selección");
    }
    // 0x5fc35b..0x5fc36a: the ground marks without depth test and with culling, and the depth test back.
    Render::ModelDrawState marksState = frame.batch.State();
    marksState.depthTest = false;
    marksState.cullEnabled = true;
    frame.batch.SetState(marksState);
    if (auto marked = Terrain::DrawGroundMarks(frame.batch, groundMarks, *terrain.heights, NoSlotComponents);
        !marked.HasValue())
    {
        return Core::Unexpected(marked.Error() + ", en la selección");
    }
    Render::ModelDrawState afterMarks = frame.batch.State();
    afterMarks.depthTest = true;
    frame.batch.SetState(afterMarks);
    return {};
}

Core::Expected<void, std::string>
SelectionWorld::DrawObjects(Kernel::WorldState& world, const SelectionWorldFrame& frame, Kernel::PoseBuffer& poseCarry)
{
    const auto map = static_cast<std::uint8_t>(world.Runtime().map);
    const float clock = Milliseconds(frame.frameClock);
    const auto systemTime =
        static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(frame.frameClock).count());
    const Kernel::GameObjectPass pass{.map = map,
                                      .originalFrame = frame.frames > 0,
                                      .random = &FrameRandom(),
                                      .worldClock = clock,
                                      .particles = &FrameParticles(),
                                      .onPose = [&](const Actors::Drawing::DrawnObject& drawn)
                                      { return Actors::Drawing::PublishPose(poseCarry, m_models, drawn, true); },
                                      .sprites = &FrameSprites(),
                                      .retainedPose = &poseCarry,
                                      .modelState = &world.Runtime().objectModelState,
                                      .publishProjectedPose = true,
                                      .effects = &FrameEffects(),
                                      // Q-C304: timeGetTime (0x6b8544) is the system clock; MuRet lends it the draw's.
                                      .systemTime = [systemTime]() { return systemTime; },
                                      .scene = SelectionScene,
                                      .objectDrawCounter = &world.Runtime().objectDrawCounter,
                                      .crywolf = &world.Runtime().crywolf};
    if (auto drawn = WorldObjects::DrawGameWorldObjects(frame.batch, m_models, FrameObjects(), frame.polygon,
                                                        &FrameLighting(), frame.frameClock, pass);
        !drawn.HasValue())
    {
        return Core::Unexpected(drawn.Error() + ", en la selección");
    }
    return {};
}

Core::Expected<void, std::string> SelectionWorld::DrawAfterCharacters(Kernel::WorldState& world,
                                                                      const Terrain::WorldTerrain& terrain,
                                                                      const SelectionWorldFrame& frame,
                                                                      const Render::Camera3D& camera,
                                                                      Kernel::PoseBuffer& poseCarry)
{
    const float clock = Milliseconds(frame.frameClock);
    Kernel::GameDrawTailState& tail = FrameTail();
    // 0x52e590
    if (auto ribbons = Scenes::DrawRibbons(frame.batch, tail.ribbons); !ribbons.HasValue())
        return ribbons;
    // 0x53c6a0(0)
    const Joints::JointDrawResources joints{FrameRandom(), FrameSprites(), frame.frames > 0, clock};
    if (auto drawn = Joints::DrawJoints(frame.batch, tail.joints, 0, joints); !drawn.HasValue())
        return drawn;
    // 0x52d620(0): the model effects; the scene 4 butterfly comes out with +0x58 = −2 (0x600328).
    if (!terrain.heights)
    {
        return Core::Unexpected(std::string("0x52d620: faltan las alturas del terreno 54"));
    }
    Scenes::ModelEffectDrawResources resources{*terrain.heights, clock};
    resources.onPose = [&](const Actors::Drawing::DrawnObject& object)
    { return Actors::Drawing::PublishPose(poseCarry, m_models, object, true); };
    resources.onUntranslatedPose = [&](const Actors::Drawing::DrawnObject& object)
    { return Actors::Drawing::PublishPose(poseCarry, m_models, object, false); };
    resources.posedMeshes = [&]() { return std::span<const Models::SkinnedMesh>(poseCarry.meshes); };
    resources.sprites = &FrameSprites();
    resources.dynamicLighting = &FrameLighting();
    resources.random = &FrameRandom();
    const Terrain::SphereFrustum frustum = Terrain::SphereFrustumOf(frame.batch.Pass().view, camera);
    if (auto drawn = Scenes::DrawGameModelEffects(frame.batch, m_models, FrameEffects().Effects(), frustum,
                                                  &FrameLighting(), frame.frameClock, 0, resources, 0);
        !drawn.HasValue())
    {
        return Core::Unexpected(drawn.Error() + ", en la selección");
    }
    // 0x549510: sets +0x16c of the live sprites. An intermediate draw repeats the last original
    // frame's sample.
    if (frame.frames > 0)
    {
        m_spriteReplay = m_sprites;
        m_spriteReplayValid = true;
    }
    else if (m_spriteReplayValid)
    {
        m_projectedSprites = m_spriteReplay;
    }
    FrameSprites().MarkSeen();
    static_cast<void>(world);
    return {};
}

Core::Expected<void, std::string> SelectionWorld::DrawEye(const Render::Pass3D& pass, const SelectionWorldFrame& frame,
                                                          const Assets::SceneTextures& sceneTextures,
                                                          std::span<const Kernel::DamageNumberState> damage)
{
    // 0x6213e0: the modelview at identity for the eye pass.
    Render::Pass3D eye = pass;
    eye.view = glm::mat4(1.0f);
    frame.batch.Begin(eye);
    const auto view = Render::ViewRows(pass.view);
    const SelectionTextureLibrary textures(m_models, sceneTextures);
    // 0x549440(0): draws the sprites and turns them off.
    Particles::DrawFrameSprites(frame.batch, FrameSprites(), textures, view, 0);
    // 0x548550(0)
    if (auto particles = Effects::DrawGameParticles(frame.batch, FrameParticles(), textures, view,
                                                    Milliseconds(frame.frameClock), 0);
        !particles.HasValue())
    {
        return Core::Unexpected(particles.Error() + ", en la selección");
    }
    // 0x548eb0(0)
    if (auto numbers = Scenes::DrawDamage(frame.batch, damage, 0, view); !numbers.HasValue())
        return numbers;
    if (frame.flushModels)
        frame.flushModels();
    return {};
}
} // namespace Client::FrontEnd
