#include "Scenes/SelectionWorld.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Render/Billboard.h"
#include "Render/ITextureStore.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/Models/Skinning.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/DynamicWorldLight.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GameParticles.h"
#include "Scenes/SceneTextures.h"
#include "Scenes/TerrainTiles.h"
#include "Scenes/TextureMetadata.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameModelEffects.h"
#include "World/GroundMarks.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/SphereFrustum.h"
#include "World/Sprites.h"
#include "World/TerrainCells.h"
#include "World/TerrainCulling.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjects.h"
#include "World/WorldTerrain.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <ratio>
#include <span>
#include <string>
#include <vector>

namespace Client::Scenes
{
namespace
{
constexpr std::int32_t SelectionScene = 4;

float Milliseconds(Core::Duration clock)
{
    return static_cast<float>(std::chrono::duration<double, std::milli>(clock).count());
}

// 0x5fc365: MuRet no sube Effect\blood01.tga (P-C49); con una marca prendida, DrawGroundMarks corta.
std::optional<std::uint8_t> NoSlotComponents(std::int32_t /*slot*/)
{
    return std::nullopt;
}
} // namespace

const Data::Models::Model* SelectionTextureLibrary::Find(std::uint16_t model) const
{
    return m_world.Find(model);
}

const Render::Models::Skeleton* SelectionTextureLibrary::SkeletonOf(std::uint16_t model) const
{
    return m_world.SkeletonOf(model);
}

std::span<const Render::TextureSlot> SelectionTextureLibrary::MeshTextures(std::uint16_t model) const
{
    return m_world.MeshTextures(model);
}

std::optional<TextureMetadata> SelectionTextureLibrary::TextureMetadataOf(std::uint32_t selector) const
{
    if (auto scene = m_scene.MetadataOf(selector))
        return scene;
    return m_world.TextureMetadataOf(selector);
}

std::optional<Render::TextureSlot> SelectionTextureLibrary::TextureSlotOf(std::uint32_t selector) const
{
    // La clave salió del slot de 16 bits de su entrada: no se trunca nada.
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

// Un único dueño por dibujo intermedio: todos los servicios prestan estas mismas copias.
struct SelectionWorld::Projection
{
    World::TerrainLighting lighting{};
    Render::Models::ClothWorld cloth{.random = Render::Models::CrtRandom{}};
    World::Particles particles{};
    World::ModelEffects effects{};
    std::vector<World::WorldObject> objects{};
    GameDrawTailState tail{};
};

SelectionWorld::SelectionWorld(Render::ITextureStore& store, Core::Log& log)
    : m_store(store), m_log(log), m_tiles(store)
{
}

SelectionWorld::~SelectionWorld() = default;

Core::Expected<void, std::string> SelectionWorld::Load(const std::filesystem::path& dataRoot, std::uint8_t map,
                                                       const Render::Models::ModelTextureSlots* gameSlots,
                                                       const IModelLibrary* globalModels)
{
    if (m_tiles.LoadedMap() != map)
    {
        std::vector<std::string> skipped;
        if (auto loaded = m_tiles.Load(dataRoot, map, skipped, GrassTextures::Upload); !loaded.HasValue())
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

const WorldModels& SelectionWorld::Models() const
{
    return m_models;
}

void SelectionWorld::StepWaves(Core::Duration clock, const World::TerrainRange& range, std::uint8_t map)
{
    World::UpdateWaves(m_water, clock, range, map);
}

Core::Expected<void, std::string> SelectionWorld::StepObjects(WorldState& world, Render::Models::CrtRandom& random,
                                                              float worldClock)
{
    std::span<World::WorldObject> objects = world.Map().objects;
    // Sin un objeto visto (antes del primer dibujo que llega a 0x6046a0), 0x606560 no toca ninguno.
    bool anySeen = false;
    for (const World::WorldObject& object : objects)
    {
        anySeen = anySeen || (object.alive && object.seen);
    }
    if (!anySeen)
    {
        return {};
    }
    const auto map = static_cast<std::uint8_t>(world.Runtime().map);
    const World::ObjectStepGlobals globals{.heroCellLayer = world.Runtime().heroCellLayer,
                                           .worldClock = worldClock,
                                           .dynamicLighting = &world.DynamicLighting().Lighting(),
                                           .particles = &world.Particles(),
                                           .sound = [&world](std::int32_t id) { world.Runtime().lastSoundId = id; },
                                           .modelState = &world.Runtime().objectModelState};
    if (auto stepped = StepGameWorldObjects(objects, map, m_models, random, globals); !stepped.HasValue())
    {
        return Core::Unexpected(stepped.Error() + ", en la selección");
    }
    return {};
}

Core::Expected<void, std::string> SelectionWorld::StepEffects(WorldState& world, Render::Models::ClothWorld& cloth,
                                                              float worldClock,
                                                              const std::function<double(float, float)>& height)
{
    const World::ModelEffectStepServices services{.terrainHeight = height, .sprites = &m_sprites};
    for (World::ModelEffect& effect : world.Map().effects.Effects())
    {
        const auto* skeleton = m_models.SkeletonOf(static_cast<std::uint16_t>(effect.type));
        if (auto stepped = World::StepGameModelEffect(effect, skeleton, world.Particles(), worldClock, cloth.random,
                                                      world.Map().kalima.get(), services);
            !stepped.HasValue())
        {
            return Core::Unexpected(stepped.Error() + ", en la selección (0x52d220)");
        }
    }
    if (auto stepped =
            StepJoints(world.DrawTail().joints, cloth.random, &cloth, &world.Particles(), worldClock, &m_sprites);
        !stepped.HasValue())
    {
        return Core::Unexpected(stepped.Error() + ", en la selección (0x53c670)");
    }
    return {};
}

void SelectionWorld::BeginFrame(WorldState& world, Render::Models::ClothWorld& cloth, int frames)
{
    m_world = &world;
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
    m_projection->lighting = world.DynamicLighting().Lighting();
    m_projection->cloth = cloth;
    m_projection->particles = world.Particles();
    m_projection->effects = world.Map().effects;
    m_projection->objects = world.Map().objects;
    m_projection->tail = world.DrawTail();
    // Los sprites del dibujo intermedio salen de la muestra del último frame original (0x549510).
    m_projectedSprites.Clear();
}

World::TerrainLighting& SelectionWorld::FrameLighting()
{
    return m_projecting ? m_projection->lighting : m_world->DynamicLighting().Lighting();
}

Render::Models::CrtRandom& SelectionWorld::FrameRandom()
{
    return m_projecting ? m_projection->cloth.random : m_cloth->random;
}

World::Particles& SelectionWorld::FrameParticles()
{
    return m_projecting ? m_projection->particles : m_world->Particles();
}

World::ModelEffects& SelectionWorld::FrameEffects()
{
    return m_projecting ? m_projection->effects : m_world->Map().effects;
}

std::span<World::WorldObject> SelectionWorld::FrameObjects()
{
    return m_projecting ? std::span(m_projection->objects) : std::span(m_world->Map().objects);
}

GameDrawTailState& SelectionWorld::FrameTail()
{
    return m_projecting ? m_projection->tail : m_world->DrawTail();
}

World::FrameSprites& SelectionWorld::FrameSprites()
{
    return m_projecting ? m_projectedSprites : m_sprites;
}

Core::Expected<void, std::string> SelectionWorld::DrawTerrain(WorldState& world, const World::WorldTerrain& terrain,
                                                              const SelectionWorldFrame& frame)
{
    if (!terrain.mapping || !terrain.heights || !terrain.attributes)
    {
        return Core::Unexpected(std::string("0x5fc260: el terreno 54 de la selección no está cargado (0x61a560)"));
    }
    const auto map = static_cast<std::uint8_t>(world.Runtime().map);
    World::TerrainView view;
    view.mapping = terrain.mapping.get();
    view.heights = terrain.heights.get();
    view.attributes = terrain.attributes.get();
    view.lighting = &FrameLighting();
    view.tiles = m_tiles.Sizes();
    view.map = map;
    view.fixedHeight = frame.fixedHeight;
    // 0x5fc26b..0x5fc2c0: el corrimiento del agua con el reloj del frame; 0x5fc2de: el pase del terreno.
    m_water.offset = World::WaterOffsetAt(frame.frameClock, map);
    World::DrawTerrain(frame.batch, view, frame.range, frame.polygon, m_water);
    // 0x5fc32b..0x5fc353: el pase del pasto (0x6c9d58 prendido fuera de los mapas 18 a 23 y 30).
    const World::TerrainGrass grass{.heights = m_tiles.GrassHeights(), .offsets = world.Map().grassOffsets};
    if (auto grown = World::DrawGrass(frame.batch, view, frame.range, frame.polygon, m_water, grass); !grown.HasValue())
    {
        return Core::Unexpected(grown.Error() + ", en el pase del pasto de la selección");
    }
    // 0x5fc35b..0x5fc36a: las marcas del suelo sin depth test y con culling, y el depth test de vuelta.
    Render::ModelDrawState marksState = frame.batch.State();
    marksState.depthTest = false;
    marksState.cullEnabled = true;
    frame.batch.SetState(marksState);
    if (auto marked =
            World::DrawGroundMarks(frame.batch, world.Runtime().groundMarks, *terrain.heights, NoSlotComponents);
        !marked.HasValue())
    {
        return Core::Unexpected(marked.Error() + ", en la selección");
    }
    Render::ModelDrawState afterMarks = frame.batch.State();
    afterMarks.depthTest = true;
    frame.batch.SetState(afterMarks);
    return {};
}

Core::Expected<void, std::string> SelectionWorld::DrawObjects(WorldState& world, const SelectionWorldFrame& frame,
                                                              FaunaDrawCarry& poseCarry)
{
    const auto map = static_cast<std::uint8_t>(world.Runtime().map);
    const float clock = Milliseconds(frame.frameClock);
    const auto systemTime =
        static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(frame.frameClock).count());
    const GameObjectPass pass{.map = map,
                              .originalFrame = frame.frames > 0,
                              .random = &FrameRandom(),
                              .worldClock = clock,
                              .particles = &FrameParticles(),
                              .onPose = [&](const DrawnObject& drawn)
                              { return PublishFaunaPose(poseCarry, m_models, drawn, true); },
                              .sprites = &FrameSprites(),
                              .retainedPose = &poseCarry,
                              .modelState = &world.Runtime().objectModelState,
                              .publishProjectedPose = true,
                              .effects = &FrameEffects(),
                              // Q-C304: timeGetTime (0x6b8544) es el reloj del sistema; MuRet le presta el del dibujo.
                              .systemTime = [systemTime]() { return systemTime; },
                              .scene = SelectionScene,
                              .objectDrawCounter = &world.Runtime().objectDrawCounter,
                              .crywolf = &world.Runtime().crywolf};
    if (auto drawn = DrawGameWorldObjects(frame.batch, m_models, FrameObjects(), frame.polygon, &FrameLighting(),
                                          frame.frameClock, pass);
        !drawn.HasValue())
    {
        return Core::Unexpected(drawn.Error() + ", en la selección");
    }
    return {};
}

Core::Expected<void, std::string> SelectionWorld::DrawAfterCharacters(WorldState& world,
                                                                      const World::WorldTerrain& terrain,
                                                                      const SelectionWorldFrame& frame,
                                                                      const Render::Camera3D& camera,
                                                                      FaunaDrawCarry& poseCarry)
{
    const float clock = Milliseconds(frame.frameClock);
    GameDrawTailState& tail = FrameTail();
    // 0x52e590: las cintas.
    if (auto ribbons = DrawRibbons(frame.batch, tail.ribbons); !ribbons.HasValue())
        return ribbons;
    // 0x53c6a0(0): las juntas.
    const JointDrawResources joints{FrameRandom(), FrameSprites(), frame.frames > 0, clock};
    if (auto drawn = DrawJoints(frame.batch, tail.joints, 0, joints); !drawn.HasValue())
        return drawn;
    // 0x52d620(0): los efectos con modelo; la mariposa de la escena 4 sale con +0x58 = −2 (0x600328).
    if (!terrain.heights)
    {
        return Core::Unexpected(std::string("0x52d620: faltan las alturas del terreno 54"));
    }
    ModelEffectDrawResources resources{*terrain.heights, clock};
    resources.onPose = [&](const DrawnObject& object) { return PublishFaunaPose(poseCarry, m_models, object, true); };
    resources.onUntranslatedPose = [&](const DrawnObject& object)
    { return PublishFaunaPose(poseCarry, m_models, object, false); };
    resources.posedMeshes = [&]() { return std::span<const Render::Models::SkinnedMesh>(poseCarry.meshes); };
    resources.sprites = &FrameSprites();
    resources.dynamicLighting = &FrameLighting();
    resources.random = &FrameRandom();
    const World::SphereFrustum frustum = World::SphereFrustumOf(frame.batch.Pass().view, camera);
    if (auto drawn = DrawGameModelEffects(frame.batch, m_models, FrameEffects().Effects(), frustum, &FrameLighting(),
                                          frame.frameClock, 0, resources, 0);
        !drawn.HasValue())
    {
        return Core::Unexpected(drawn.Error() + ", en la selección");
    }
    // 0x549510: prende +0x16c de los sprites vivos. Un dibujo intermedio repite la muestra del último
    // frame original.
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
                                                          const SceneTextures& sceneTextures,
                                                          std::span<const DamageNumberState> damage)
{
    // 0x6213e0: la modelview en la identidad para el pase de ojo.
    Render::Pass3D eye = pass;
    eye.view = glm::mat4(1.0f);
    frame.batch.Begin(eye);
    const auto view = Render::ViewRows(pass.view);
    const SelectionTextureLibrary textures(m_models, sceneTextures);
    // 0x549440(0): dibuja y apaga los sprites.
    DrawFrameSprites(frame.batch, FrameSprites(), textures, view, 0);
    // 0x548550(0): las partículas.
    if (auto particles =
            DrawGameParticles(frame.batch, FrameParticles(), textures, view, Milliseconds(frame.frameClock), 0);
        !particles.HasValue())
    {
        return Core::Unexpected(particles.Error() + ", en la selección");
    }
    // 0x548eb0(0): los números.
    if (auto numbers = DrawDamage(frame.batch, damage, 0, view); !numbers.HasValue())
        return numbers;
    if (frame.flushModels)
        frame.flushModels();
    return {};
}
} // namespace Client::Scenes
