#include "Scenes/GameScene.h"

#include "App/SessionState.h"
#include "Core/IClock.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Input/Key.h"
#include "Render/ModelBatch.h"
#include "Render/Billboard.h"
#include "Render/Models/Animation.h"
#include "Render/Pass3D.h"
#include "Scenes/CharacterAnimation.h"
#include "Scenes/CharacterHead.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/SelectionState.h"
#include "Scenes/TextureSets.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "Scenes/WorldResources.h"
#include "UI/GameCursor.h"
#include "UI/Widget.h"
#include "World/Particles.h"
#include "World/ParticleDrawing.h"
#include "World/Sprites.h"
#include "World/TerrainCulling.h"
#include "World/TerrainHeight.h"
#include "World/TerrainLighting.h"
#include "World/TerrainPickingSegment.h"
#include "World/WorldObjects.h"
#include "World/WorldTerrain.h"
#include "World/TerrainWalls.h"
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <algorithm>
#include <chrono>

#include "App/ServerSession.h"
#include "Core/Expected.h"
#include "Core/Log.h"
#include "Input/InputState.h"
#include "Protocol/Character/Opcodes.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/ClothWorldStep.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/ActorState.h"
#include "Scenes/ActorActions.h"
#include "Scenes/ActorMotion.h"
#include "Scenes/DarkHorse.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldFaunaDrawing.h"
#include "World/GameCamera.h"
#include "World/LeafWeather.h"
#include "World/Leaves.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <functional>
#include <format>
#include <ratio>
#include <string>
#include <system_error>
#include <vector>

namespace Client::Scenes
{
namespace
{
void BindLeafCreationRejections(World::LeafPoolHandlers& handlers, const WorldRuntime& state)
{
    handlers.creators[5] = [&state](World::Leaf&, std::int32_t) -> Core::Expected<std::uint8_t, std::error_code>
    {
        const std::int32_t map = state.map; // 52fe30: una lectura, antes de acceder a p/héroe/cámara/RNG.
        if (map != 3 && map != 7)
            return std::uint8_t{0};
        return Core::Unexpected(std::make_error_code(std::errc::function_not_supported));
    };
    handlers.creators[6] = [&state](World::Leaf&, std::int32_t) -> Core::Expected<std::uint8_t, std::error_code>
    {
        const std::int32_t map = state.map; // 42b1e0: otros mapas no llaman4294b0 ni leen héroe.
        if (map != 30 && map != 55)
            return std::uint8_t{0};
        return Core::Unexpected(std::make_error_code(std::errc::function_not_supported));
    };
    handlers.creators[7] = [&state](World::Leaf&, std::int32_t) -> Core::Expected<std::uint8_t, std::error_code>
    {
        // 443996: 43ff10 puro precede cualquier lectura de héroe y43ff20/RNG.
        if (!World::LeafMap31Predicate(state.map))
            return std::uint8_t{0};
        return Core::Unexpected(std::make_error_code(std::errc::function_not_supported));
    };
    handlers.creators[8] = [&state](World::Leaf&, std::int32_t) -> Core::Expected<std::uint8_t, std::error_code>
    {
        // 4292c0: ejecutar425070 (una lectura raw32); sólo AL!=0 permite leer q+16/llamar425090.
        const std::int32_t map = state.map;
        if (map != 33 && map != 54)
            return std::uint8_t{0};
        return Core::Unexpected(std::make_error_code(std::errc::function_not_supported));
    };
}

void BindLeafCreators(World::LeafPoolHandlers& handlers, WorldState& world, Render::Models::CrtRandom& random,
                      const World::GameCamera* camera)
{
    WorldRuntime& state = world.Runtime();
    handlers.creators[0] = [&world, &state, &random](World::Leaf& leaf, std::int32_t index)
    {
        return static_cast<std::uint8_t>(World::CreateExtendedRainLeaf(
            leaf, {state.map, state.leafSelector, world.Actors()[state.hero].position, state.leafWeather}, index,
            random));
    };
    handlers.creators[1] = [&world, &state, &random](World::Leaf& leaf, std::int32_t index)
    {
        return static_cast<std::uint8_t>(World::CreateEventRainLeaf(
            leaf, {state.map, state.leafSelector, world.Actors()[state.hero].position, state.leafWeather}, index,
            random));
    };
    handlers.creators[2] = [&world, &state, &random, camera](World::Leaf& leaf, std::int32_t)
    {
        const World::LeafHeroRead readHero = [&world, &state]() -> Core::Expected<const glm::vec3*, std::error_code>
        {
            // Resolver el índice vivo en cada uno de los tres puntos de52fa60, después del draw.
            if (state.hero >= world.Actors().size())
                return Core::Unexpected(std::make_error_code(std::errc::result_out_of_range));
            return &world.Actors()[state.hero].position;
        };
        const World::LeafRandomDraw draw = [&random]() -> Core::Expected<std::int32_t, std::error_code>
        { return random.Next(); };
        return World::CreateMapZeroLeafC3(leaf, state.map, camera != nullptr ? &camera->publishedY : nullptr, readHero,
                                          draw);
    };
    handlers.creators[3] = [&world, &state, &random](World::Leaf& leaf, std::int32_t index)
    {
        return static_cast<std::uint8_t>(World::CreateIcarusRainLeaf(
            leaf, {state.map, state.leafSelector, world.Actors()[state.hero].position, state.leafWeather}, index,
            random));
    };
    handlers.creators[4] = [&world, &state, &random](World::Leaf& leaf, std::int32_t)
    {
        return static_cast<std::uint8_t>(
            World::CreateSnowLeaf(leaf, state.map, world.Actors()[state.hero].position, random));
    };
    BindLeafCreationRejections(handlers, state);
    // Las familias admitidas C6..C9 y el default conservan su frontera, sin éxito inventado.
}

void BindLeafUpdaters(World::LeafPoolHandlers& handlers, WorldState& world, Render::Models::CrtRandom& random,
                      const World::GameCamera* camera)
{
    WorldRuntime& state = world.Runtime();
    // Guardas de U1/U2/U3 acreditadas: en mapa2 rechazan sin consultar terreno ni emitir splash.
    // Sus familias admitidas siguen pendientes de binding; no se ejecutan callbacks no-op.
    handlers.updaters[0] = [&state](World::Leaf&) -> Core::Expected<std::uint8_t, std::error_code>
    {
        const std::int32_t map = state.map;
        if (map != 9 && (map != 34 || state.leafSelector != 1))
            return std::uint8_t{0};
        return Core::Unexpected(std::make_error_code(std::errc::function_not_supported));
    };
    handlers.updaters[1] = [&state](World::Leaf&) -> Core::Expected<std::uint8_t, std::error_code>
    {
        const std::int32_t map = state.map;
        if (map < 18 || map > 23)
            return std::uint8_t{0};
        return Core::Unexpected(std::make_error_code(std::errc::function_not_supported));
    };
    handlers.updaters[2] = [&state, &random](World::Leaf& leaf) -> Core::Expected<std::uint8_t, std::error_code>
    {
        const std::int32_t map = state.map;
        if (map != 0 && map != 10)
            return std::uint8_t{0};
        // 5301a0: todo tipo distinto de66 usa seis draws y no consulta terreno ni splash.
        constexpr std::int32_t RainType = 0x66;
        if (leaf.type != RainType)
        {
            World::StepDriftingLeaf(leaf, random);
            return std::uint8_t{1};
        }
        return Core::Unexpected(std::make_error_code(std::errc::function_not_supported));
    };
    handlers.fall = [&world, &random, camera](World::Leaf& leaf) -> Core::Expected<void, std::error_code>
    {
        const World::WorldTerrain* terrain = world.Resources()->terrain.Terrain();
        if (terrain == nullptr || !terrain->heights || !terrain->attributes || camera == nullptr)
            return Core::Unexpected(std::make_error_code(std::errc::function_not_supported));
        constexpr std::int32_t Scene = static_cast<std::int32_t>(SceneId::Game);
        World::StepFallingLeaf(leaf, {Scene, *terrain->heights, *terrain->attributes, camera->fixedHeight}, random);
        return {};
    };
}
} // namespace

GameScene::GameScene(const SceneContext& context, App::ServerSession* session, const IModelLibrary* models,
                     const GameData* data)
    : m_context(context), m_session(session), m_models(models), m_data(data),
      m_cursorTextures(context.textures, WindowTextureSet)
{
}

Core::Expected<void, std::string> GameScene::Enter()
{
    m_firstStep = true;
    if (m_context.selection != nullptr)
        m_context.selection->SetScene(World::CullingScene::Game);
    return m_models != nullptr ? m_cursorTextures.Load(m_context.dataRoot) : Core::Expected<void, std::string>{};
}

// 636290: una inicialización; el tail sigue corriendo durante la espera con el estado heredado.
void GameScene::Step(const Input::InputState& input)
{
    if (m_failure)
        return;
    const Render::Models::ClothWorldStep logicStep(m_context.cloth);
    if (m_context.world != nullptr)
        m_context.world->Runtime().pendingLeftPress =
            m_context.world->Runtime().pendingLeftPress || input.WasPressed(Input::MouseButton::Left);
    if (m_firstStep)
    {
        if (m_context.camera != nullptr)
            m_context.camera->yaw = -45.0f;
        m_firstStep = false;
        SendEntryRequest();
    }
    if (m_context.world == nullptr)
        return;
    if (m_session != nullptr && m_session->State() == App::SessionState::InWorld)
        m_context.world->Runtime().drawingEnabled = true;
    if (m_context.world->Runtime().drawingEnabled)
        StepWorld(input);
}

void GameScene::SendEntryRequest()
{
    if (m_sent || m_session == nullptr)
    {
        return;
    }
    m_sent = true;
    if (m_context.world == nullptr || !m_context.world->SelectedActor())
    {
        m_context.log.Error("no se pide la entrada: falta el actor seleccionado durable");
        return;
    }
    const std::size_t selected = *m_context.world->SelectedActor();
    const ActorState& actor = m_context.world->Actors()[selected];
    // 636478..63661e: strlen del actor actual, copia sin NUL y padding hasta diez bytes.
    std::array<std::byte, Protocol::Character::NameSize> name{};
    for (std::size_t i = 0; i < name.size() && actor.name[i] != std::byte{0}; ++i)
    {
        name[i] = actor.name[i];
    }
    m_session->EnterWorld(name, static_cast<std::uint8_t>(selected));
}

void GameScene::PrepareFrame()
{
    m_holdFrame = true;
    if (m_context.world == nullptr || !m_context.world->Runtime().drawingEnabled || m_context.cloth == nullptr)
        return;
    WorldRuntime& runtime = m_context.world->Runtime();
    std::int32_t snapshot = 0;
    {
        const Render::Models::LogicalAccess access(runtime.warpDepth, m_context.cloth->random,
                                                   Render::Models::SmallLogicalCloseRandoms);
        snapshot = runtime.warpCounter;
    }
    m_holdFrame = snapshot > 30;
}

void GameScene::StepWorld(const Input::InputState& input)
{
    WorldState& world = *m_context.world;
    WorldRuntime& state = world.Runtime();
    if (m_context.cameraInput != nullptr)
        m_context.cameraInput->shake =
            static_cast<float>(static_cast<double>(m_context.cameraInput->shake) * static_cast<double>(0.2f));
    WorldResources* resources = world.Resources();
    if (resources != nullptr && m_context.clock != nullptr)
        resources->terrain.Step(m_context.clock->Now(), resources->terrainRange);
    if (resources != nullptr)
        StepWorldObjects(resources->objects, static_cast<std::uint8_t>(state.map), resources->models);
    if (m_context.cloth == nullptr)
    {
        if (resources != nullptr && ShouldStepLeaves())
            m_failure = "hojas: falta el RNG canónico de ClothWorld";
        return;
    }
    // 6370e5: después de objetos/ítems y antes de fauna, caballo, MoveHero y actores.
    if (resources != nullptr)
        StepLeaves();
    if (m_failure)
        return;
    if (m_models != nullptr)
        for (ActorState& actor : world.Actors().first<ActorTraversalCount>())
            if (actor.horse)
                if (const auto* skeleton = m_models->SkeletonOf(DarkHorseModel))
                    StepDarkHorse(*actor.horse, *skeleton, actor.animation.body.action);
    StepHeroPrefix(input);
    if (m_failure)
        return;
    RebuildWalls();
    for (ActorState& actor : world.Actors().first<ActorTraversalCount>())
    {
        if (!actor.active)
            continue;
        const World::TerrainPolygon& previousPolygon = resources != nullptr ? resources->terrainPolygon : m_polygon;
        actor.seen = World::PolygonContains(previousPolygon, static_cast<float>(actor.position.x * 0.01f),
                                            static_cast<float>(actor.position.y * 0.01f), -20.0);
        if (m_models != nullptr &&
            StepCharacterAnimation(actor.animation, actor.appearance, *m_models, Render::Models::AnimationScene::Other,
                                   &state.playerActionSpeeds))
        {
            if (m_data == nullptr)
            {
                m_failure = "falta contexto de Stop al terminar la animación";
                return;
            }
            const auto stopped = SetActorStop(actor, world, *m_data, *m_models, *m_context.cloth);
            if (!stopped.HasValue())
            {
                m_failure = stopped.Error();
                return;
            }
        }
        if (actor.type == 0x20a && actor.seen && (actor.objectFlags & 0x1020u) == 0)
            StepCharacterHead(actor.head, &actor != &world.Actors()[state.hero] && !actor.dying,
                              m_context.cloth->random);
        if (actor.alpha < actor.targetAlpha)
            actor.alpha = std::min(actor.targetAlpha,
                                   static_cast<float>(static_cast<double>(actor.alpha) + static_cast<double>(0.05f)));
    }
    if (resources != nullptr && m_models != nullptr)
    {
        StepEffects();
        if (m_failure)
            return;
        StepParticles();
    }
}

void GameScene::StepParticles()
{
    WorldState& world = *m_context.world;
    WorldRuntime& state = world.Runtime();
    if (!state.worldTime)
    {
        m_failure = "partículas: falta T canónico publicado desde qT raw32";
        return;
    }
    if (m_context.particleTrigonometry == nullptr)
    {
        m_failure = "partículas: falta el perfil matemático explícito";
        return;
    }
    const auto stepped =
        World::StepParticlesChecked(world.Resources()->particles, m_context.cloth->random, std::cref(*state.worldTime),
                                    &state.map, *m_context.particleTrigonometry);
    if (!stepped.HasValue())
        m_failure = std::format("partículas: Step falló tras su prefijo (causa {})", static_cast<int>(stepped.Error()));
}

bool GameScene::ShouldStepLeaves() const
{
    const WorldRuntime& state = m_context.world->Runtime();
    const std::int32_t map = state.map; // 63707d: sólo las comparaciones directas comparten snapshot.
    bool direct = false;
    if (map == 0)
        direct = state.heroTerrainLayer1 != 4;
    else if (map == 2)
    {
        const auto layer = std::bit_cast<std::int32_t>(state.heroTerrainLayer1);
        direct = layer != 3 && layer < 10; // JL signed; consume el DWORD vigente, sin consulta fresca.
    }
    else
        direct = map == 3 || map == 7 || map == 9 || map == 10 || (map >= 18 && map <= 23) || map == 30;
    if (direct || World::LeafMap31Predicate(state.map))
        return true;
    // 425070 toma su propia lectura para ambas igualdades, sin importar el pool Fish.
    const std::int32_t eventMap = state.map;
    if (eventMap == 33 || eventMap == 54)
        return true;
    return World::LeafMap34Predicate(state.map);
}

void GameScene::StepLeaves()
{
    if (!ShouldStepLeaves())
        return;
    WorldState& world = *m_context.world;
    WorldRuntime& state = world.Runtime();
    if (!state.worldTime)
    {
        // La ausencia se alcanza al leer T, después de la preparación y antes de Evaluate1.
        static_cast<void>(World::PrepareLeafWeather(state.leafWeather, state.map, state.leafSelector));
        m_failure = "hojas: falta T canónico publicado desde qT raw32";
        return;
    }
    if (state.hero >= world.Actors().size())
    {
        // Precondición del port, no error nativo alcanzado: evita formar referencias inválidas
        // en C1/C2/C4/C5. Las garantías de prefijo suponen préstamos de actor válidos.
        m_failure = "hojas: precondición del caller incumplida: índice del héroe fuera de Actors()";
        return;
    }
    World::LeafPoolHandlers handlers;
    BindLeafCreators(handlers, world, m_context.cloth->random, m_context.camera);
    BindLeafUpdaters(handlers, world, m_context.cloth->random, m_context.camera);
    const auto stepped = World::StepWorldLeaves(world.Resources()->leaves, state.leafWeather, state.map,
                                                state.leafSelector, *state.worldTime, m_context.leafWaves, handlers);
    if (!stepped.HasValue())
        m_failure = "hojas: binding alcanzado ausente o fallido: " + stepped.Error().message();
}

void GameScene::StepHeroPrefix(const Input::InputState& input)
{
    WorldState& world = *m_context.world;
    WorldRuntime& state = world.Runtime();
    ActorState& hero = world.Actors()[state.hero];
    if (state.cameraPathActive)
    {
        if (m_data == nullptr || m_models == nullptr)
            m_failure = "falta contexto de Stop durante el recorrido de cámara";
        else
        {
            const auto stopped = SetActorStop(hero, world, *m_data, *m_models, *m_context.cloth);
            if (!stopped.HasValue())
                m_failure = stopped.Error();
        }
        return;
    }
    if ((state.targets[2] != -1 && (world.Actors()[state.hero].characterFlags & 0x200000u) != 0) || hero.dying ||
        (hero.objectFlags & 0x1000u) != 0)
        return;
    std::int32_t snapshot = 0;
    {
        const Render::Models::LogicalAccess access(state.warpDepth, m_context.cloth->random,
                                                   Render::Models::SmallLogicalCloseRandoms);
        if (state.warpCounter > 0)
            --state.warpCounter;
        snapshot = state.warpCounter;
    }
    if (snapshot > 30)
        return;
    UpdateHeroHeadTarget(input);
    if (m_failure)
        return;
    if (hero.moving)
    {
        if (m_data == nullptr || m_models == nullptr)
        {
            m_failure = "falta contexto productivo de movimiento";
            return;
        }
        if ((hero.objectFlags & 0x20u) != 0)
        {
            const auto stopped = SetActorStop(hero, world, *m_data, *m_models, *m_context.cloth);
            if (!stopped.HasValue())
                m_failure = stopped.Error();
        }
        else
        {
            const auto moved = MoveActorAlongPath(hero, world, m_context, *m_data, *m_models, 1);
            if (!moved.HasValue())
                m_failure = moved.Error();
        }
    }
    if (!m_failure)
        ProcessHeroInput(input);
}

void GameScene::RebuildWalls()
{
    WorldResources* resources = m_context.world->Resources();
    if (resources == nullptr || !resources->terrain.IsLoaded())
        return;
    std::vector<World::TerrainOccupant> occupants;
    for (ActorState& actor : m_context.world->Actors().first<ActorTraversalCount>())
    {
        if (!actor.active || actor.dying || actor.kind == 8)
            continue;
        std::int32_t x = 0;
        std::int32_t y = 0;
        {
            const Render::Models::LogicalAccess access(actor.cellYDepth, m_context.cloth->random,
                                                       Render::Models::SmallLogicalCloseRandoms);
            y = std::bit_cast<std::int32_t>(actor.cellY);
        }
        {
            const Render::Models::LogicalAccess access(actor.cellXDepth, m_context.cloth->random,
                                                       Render::Models::SmallLogicalCloseRandoms);
            x = std::bit_cast<std::int32_t>(actor.cellX);
        }
        occupants.push_back({.logicalX = x, .logicalY = y, .live = true, .dying = false, .kind = actor.kind});
    }
    resources->walls.Rebuild(occupants);
    resources->terrain.Terrain()->attributes->cells = resources->walls.cells;
}

void GameScene::Render(SceneCanvas& canvas, float alpha)
{
    if (m_failure || m_holdFrame || m_context.world == nullptr || m_context.cloth == nullptr ||
        m_context.camera == nullptr || m_context.cameraInput == nullptr || m_models == nullptr)
        return;
    WorldState& world = *m_context.world;
    WorldResources* resources = world.Resources();
    if (resources == nullptr || !resources->terrain.IsLoaded())
        return;
    const World::WorldTerrain& terrain = *resources->terrain.Terrain();
    m_screen = canvas.screen;
    ActorState& hero = world.Actors()[world.Runtime().hero];
    World::GameCameraInput& input = *m_context.cameraInput;
    input.hero = hero.position; // estado del paso, sin interpolación posicional.
    input.map = static_cast<std::uint8_t>(world.Runtime().map);
    input.scene = World::CullingScene::Game;
    const auto worldClock = static_cast<float>(std::chrono::duration<double, std::milli>(canvas.frameClock).count());
    input.worldTime = worldClock;
    Render::Camera3D camera = World::GameCameraOf(*m_context.camera, input);
    const int frames = m_cameraFrames.FramesAt(canvas.frameClock, OriginalFramePaceOf(m_context.pacing));
    for (int frame = 0; frame < frames; ++frame)
    {
        std::int32_t x = 0;
        std::int32_t y = 0;
        {
            const Render::Models::LogicalAccess access(hero.cellYDepth, m_context.cloth->random,
                                                       Render::Models::SmallLogicalCloseRandoms);
            y = std::bit_cast<std::int32_t>(hero.cellY);
        }
        {
            const Render::Models::LogicalAccess access(hero.cellXDepth, m_context.cloth->random,
                                                       Render::Models::SmallLogicalCloseRandoms);
            x = std::bit_cast<std::int32_t>(hero.cellX);
        }
        input.cellAttribute = static_cast<std::uint8_t>(
            terrain.attributes
                ->cells[Data::Terrain::CellIndex(static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y))]);
        camera = World::StepGameCamera(*m_context.camera, input);
    }
    constexpr Render::ViewportRect RangeViewport{0, 0, 640, 48};
    constexpr Render::ViewportRect WorldViewport{0, 0, 640, 432};
    const auto rangePass = Render::MakePass3D(canvas.screen, RangeViewport, camera);
    m_range = World::TerrainRangeOf(rangePass.view, camera, 1.0f);
    resources->modelEffectPlanes = World::ModelEffectPlanesOf(resources->modelEffectPlanes, rangePass.view, camera);
    const auto polygon = resources->UpdateTerrainPolygon({.scene = 5,
                                                          .map = world.Runtime().map,
                                                          .translation = hero.position,
                                                          .objectPosition = {hero.position.x, hero.position.y},
                                                          .cameraYaw = camera.angles.z,
                                                          .worldWidth = 640,
                                                          .webzenProfile = input.webzen,
                                                          .webzenYaw = input.webzen},
                                                         *m_context.camera);
    if (!polygon.HasValue())
    {
        m_failure = "polígono: distancia o nivel efectivo fuera del dominio portable";
        return;
    }
    m_polygon = resources->terrainPolygon;
    resources->terrainRange = m_range;
    resources->terrainPolygon = m_polygon;
    const auto pass = Render::MakePass3D(canvas.screen, WorldViewport, camera);
    if (m_context.input != nullptr)
    {
        const UI::Point mouse = UI::MouseIn640x480(*m_context.input, canvas.screen);
        const auto [origin, end] =
            pass.RayThrough(static_cast<float>(static_cast<double>(mouse.x) * canvas.screen.width / 640.0),
                            static_cast<float>(static_cast<double>(mouse.y) * canvas.screen.height / 480.0));
        resources->pickingRay = World::TerrainPickingSegment{origin, end};
    }
    canvas.flushSprites();
    canvas.models.Begin(pass);
    if (m_context.leafView != nullptr)
        *m_context.leafView = Render::ViewRows(pass.view);
    constexpr std::uint8_t MapWithoutTerrain = 10; // 6374c6 excluye10 delprimer pase5fc260.
    if (input.map != MapWithoutTerrain)
        resources->terrain.DrawTerrain(canvas.models, canvas.frameClock, m_range, m_polygon,
                                       m_context.camera->fixedHeight);
    m_objectRandoms.resize(resources->objects.size());
    DrawWorldObjects(canvas.models, resources->models, resources->objects, m_polygon, resources->terrain.Lighting(),
                     input.map, canvas.frameClock, alpha,
                     ObjectFrameEffects{.random = &m_context.cloth->random,
                                        .sprites = &resources->sprites,
                                        .particles = &resources->particles,
                                        .scene = World::ObjectScene::Other,
                                        .frames = frames,
                                        .lastRandoms = m_objectRandoms});
    const auto ground = DrawGroundModelEffects(
        canvas.models, resources->effects.Effects(), *terrain.heights, worldClock, m_context.cloth->random,
        [&canvas]
        {
            // ModelBlend::Additive es el binding gráfico existente de620a90.
            canvas.models.SetState({.texture = std::nullopt, .blend = Render::ModelBlend::Additive});
        });
    if (!ground.HasValue())
    {
        m_failure = "falta binding del pase de efectos sobre el terreno";
        return;
    }
    for (ActorState& actor : world.Actors().first<ActorTraversalCount>())
    {
        if (!actor.active || !actor.seen || actor.alpha < 0.01f)
            continue;
        const glm::vec3 light = resources->terrain.Lighting() != nullptr
                                    ? World::LightAt(*resources->terrain.Lighting(), actor.position.x, actor.position.y)
                                    : glm::vec3{};
        DrawnCharacter character{
            .appearance = actor.appearance,
            .body = BodyBetweenSteps(actor.animation, *m_models, alpha, &world.Runtime().playerActionSpeeds),
            .object = {actor.position, actor.angles, actor.appearance.scale, 0.0f},
            .headAngles = actor.head.angles,
            .pet = actor.animation.pet.state,
            .wings = actor.animation.wings.state,
            .light = light,
            .cloth = &actor.cloth,
            .clothWorld = m_context.cloth,
            .map = input.map,
            .sprites = &resources->sprites,
            .pace = OriginalFramePaceOf(m_context.pacing),
            .alpha = actor.alpha};
        DrawCharacter(canvas.models, *m_models, character, canvas.frameClock);
    }
    if (m_context.leafDrawing != nullptr)
    {
        if (m_context.modelBatch != &canvas.models || m_context.leafView == nullptr)
        {
            m_failure = "hojas 621400: lote o vista canónicos ausentes/conflictivos";
            return;
        }
        const auto drawn = DrawWorldLeaves(canvas.models, resources->leaves, world.Runtime().map,
                                           world.Runtime().leafSelector, m_context.leafDrawing);
        if (!drawn.HasValue())
        {
            m_failure = "hojas 621400: binding de dibujo alcanzado ausente o fallido: " + drawn.Error().message();
            return;
        }
    }
    else if (world.Runtime().map != 55)
    {
        m_failure = "hojas: falta provider de dibujo WorldLeaves/621400; DrawLeaves legacy no lo representa";
        return;
    }
    else
        DrawLeaves(canvas.models, resources->leaves);
    // 52d620: el ground anterior observó visible viejo; ahora el despacho publica el nuevo AL.
    DrawEffects(canvas, alpha);
    if (m_failure)
        return;
    canvas.flushModels();
    if (resources->sprites.Any() || resources->particles.Any())
    {
        resources->sprites.MarkSeen();
        Render::Pass3D eye = pass;
        eye.view = glm::mat4(1.0f);
        canvas.models.Begin(eye);
        DrawFrameSprites(canvas.models, resources->sprites, *m_models, Render::ViewRows(pass.view));
        if (resources->particles.Any())
        {
            DrawParticles(canvas);
            if (m_failure)
                return;
        }
        canvas.flushModels();
    }
    if (m_context.input != nullptr)
        UI::DrawGameCursor(canvas.sprites, *m_context.input, canvas.screen);
}

void GameScene::DrawParticles(SceneCanvas& canvas)
{
    WorldState& world = *m_context.world;
    if (m_context.modelBatch != &canvas.models || m_context.leafView == nullptr || m_context.particleDrawing == nullptr)
    {
        m_failure = "partículas: lote, vista o servicio canónicos ausentes/conflictivos";
        return;
    }
    if (!world.Runtime().worldTime)
    {
        m_failure = "partículas: falta T canónico publicado desde qT raw32";
        return;
    }
    const auto drawn = World::DrawParticlesChecked(world.Resources()->particles, 0,
                                                   std::cref(*world.Runtime().worldTime), *m_context.particleDrawing);
    if (!drawn.HasValue())
        m_failure =
            std::format("partículas: Draw falló tras su prefijo (slot {}, fase {}, causa {}, backend {})",
                        drawn.Error().slot, static_cast<int>(drawn.Error().error.phase),
                        static_cast<int>(drawn.Error().error.reason), drawn.Error().error.backendError.message());
}

std::optional<std::string> GameScene::Failure() const
{
    return m_failure;
}

void GameScene::Leave()
{
    m_cursorTextures.Release();
}

// Q-C70: sin conexión, el juego sigue; el texto 3 del área de mensajes es de la CL9.
std::optional<SceneId> GameScene::Next() const
{
    return std::nullopt;
}

bool GameScene::WantsToQuit() const
{
    return false;
}

bool GameScene::WantsTextInput() const
{
    return false;
}

bool GameScene::HoldsTheLastFrame() const
{
    return m_holdFrame;
}
} // namespace Client::Scenes
