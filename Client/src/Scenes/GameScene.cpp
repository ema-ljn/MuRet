#include "Scenes/GameScene.h"
#include "Scenes/GameSceneFrameProjection.h"
#include "Data/Tables/GateTable.h"
#include "Data/Tables/SkillTable.h"
#include "Data/Text/TextTable.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Pose.h"
#include "Scenes/DynamicWorldLight.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/SelectionState.h"
#include "Scenes/GameParticles.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/FaunaEffects.h"
#include "Scenes/GameChat.h"
#include "Scenes/GameFloorItems.h"
#include "Scenes/GameItemRenderState.h"
#include "Scenes/GameItemSpecialDrawing.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GameProjection.h"
#include "Scenes/GameUi.h"
#include "Scenes/IcarusObjectStep.h"
#include "Scenes/KalimaObjectStep.h"
#include "World/GameModelEffects.h"

#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Input/InputState.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/ModelBatch.h"
#include "Render/ModelFog.h"
#include "Render/Pass3D.h"
#include "Scenes/ActorName.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/ChatBubbles.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/GameCharacters.h"
#include "Scenes/HelperTable.h"
#include "Scenes/ModelTextureTable.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/TextureSets.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/Slides.h"
#include "Scenes/TerrainPolygonFrame.h"
#include "Scenes/TerrainTiles.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "World/GroundEffectDrawing.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/ChatLog.h"
#include "UI/GameCursor.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"
#include "UI/Widget.h"
#include "World/GameCamera.h"
#include "World/GroundMarks.h"
#include "World/KalimaWorld.h"
#include "World/SphereFrustum.h"
#include "World/TerrainCells.h"
#include "World/TerrainCulling.h"
#include "World/CastleSiegeObjectStep.h"
#include "World/ChaosCastle.h"
#include "World/CrywolfWater.h"
#include "World/EventMapObjectStep.h"
#include "World/TerrainHeight.h"
#include "World/TarkanObjectStep.h"
#include "World/TerrainPickingSegment.h"
#include "World/WorldObjects.h"
#include "World/WorldTerrain.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <bit>
#include <chrono>
#include <functional>
#include <cstdint>
#include <optional>
#include <ratio>
#include <span>
#include <string>
#include <vector>

namespace Client::Scenes
{
namespace
{
// 0x636838: el yaw de la cámara del juego en el primer paso, 0xc2340000.
constexpr float EntryYaw = -45.0f;
// 0x636dce: lo que queda del temblor de la cámara (0x7d325c8) en cada paso, el 0,2 de 0x6b8854.
constexpr float ShakeDecay = std::bit_cast<float>(std::uint32_t{0x3e4ccccd});
// 0x635b3d y 0x63726f: en el mapa 6 la cámara y el punto del trapecio comparan el nombre del héroe.
constexpr std::int32_t NamedCameraMap = 6;
// 0x6372e0..0x6372f1: el punto de la rama de "webzen" (0x45c80000 y 0x4670a000) con la z del héroe.
constexpr float SpectatorTargetX = 6400.0f;
constexpr float SpectatorTargetY = 15400.0f;
// 0x5fc33e: el mapa 7 no tiene el pase del pasto (su agua va con 0x5fa4b0, World::DrawTerrainCell).
constexpr std::int32_t AnimatedWaterMap = 7;
// 0x6374bb: el mapa 10 no dibuja el terreno.
constexpr std::int32_t WithoutTerrainMap = 10;
// 0x5f8928..0x5f8947: 0x6c9d58, el pase del pasto, queda en 0 en los mapas 18 a 23 y en el 30.
constexpr std::int32_t FirstWithoutGrassMap = 18;
constexpr std::int32_t LastWithoutGrassMap = 23;
// 0x63741d: en el mapa 30 el dibujo pone o saca la niebla (0x42b0a0).
constexpr std::int32_t CastleMap = 30;
// 0x42b0a0 con el color que arma 0x63746b..0x63747a en la pila, (0, 0, 0): glFogf(GL_FOG_MODE, GL_LINEAR), de
// 2.000 (0x44fa0000) a 2.700 (0x4528c000).
constexpr Render::ModelFog CastleSiegeFog{.color = glm::vec3(0.0f), .start = 2000.0f, .end = 2700.0f};
// 0x633f20: mapa conservado durante la espera de entrada.
constexpr std::int32_t SelectionMap = 54;
// 0x425070: el 33 comparte con el 54 los casos de los objetos de 0x4250f0.
constexpr std::int32_t AidaMap = 33;
// 0x43ff10: Land of Trials, con sus casos en 0x43ffc0, 0x4401e0 y 0x4403b0.
constexpr std::int32_t LandOfTrialsMap = 31;
// 0x4303c0: Crywolf, con sus casos en 0x4303e0, 0x430510 y 0x4306a0 y las burbujas de su agua (0x5fa695).
constexpr std::int32_t CrywolfMap = 34;
// Los mapas de referencia, los únicos con los casos de los objetos en el juego (0x6046a0 y 0x604ab0).
constexpr std::int32_t LorenciaMap = 0;
constexpr std::int32_t DungeonMap = 1;
constexpr std::int32_t DeviasMap = 2;
constexpr std::int32_t NoriaMap = 3;
constexpr std::int32_t LostTowerMap = 4;
// La Arena (World7): sus casos en 0x604ab0 (0x6054fd) y 0x602ed0 (0x6039cb).
constexpr std::int32_t ArenaMap = 6;
// Atlans (World8): sus casos en 0x604ab0 (la tabla 0x605c94); 0x602ed0 y 0x6002e0 no tienen.
constexpr std::int32_t AtlansMap = 7;
constexpr std::int32_t TarkanMap = 8;
constexpr std::int32_t IcarusMap = 10;
constexpr std::int32_t DevilSquareMap = 9;
constexpr std::int32_t FirstBloodCastleMap = 11;
constexpr std::int32_t LastBloodCastleMap = 17;
// 0x620486..0x6204b0: el mouse de 640 × 480 vuelve a la pantalla con · W / 640 y · H / 480, en enteros.
constexpr std::int64_t Width640 = 640;
constexpr std::int64_t Height480 = 480;
// 0x6ba070: lo que 0x637355 multiplica por el ancho del mundo para el primer argumento de 0x5fbb00.
constexpr float WorldWidthFactor = 0.0015625f;

float F32(double value)
{
    return static_cast<float>(value);
}

std::string MapText(std::int32_t map)
{
    return "mapa " + std::to_string(map);
}

// Los componentes de un slot en el juego de MuRet: no sube las texturas de 0x61ccb0 que no son de los sprites.
std::optional<std::uint8_t> NoSlotComponents(std::int32_t /*slot*/)
{
    return std::nullopt;
}

// 0x63741d..0x637466 y 0x6375ea..0x63763a: el mapa 30 (0x6c9dd0) y el héroe publicado (0x743a170) en el rectángulo de
// World::InTheCastleSiegeFog. Cada lectura relee las dos cosas.
bool HeroInTheCastleSiegeFog(const WorldState& world)
{
    return world.Runtime().map == CastleMap &&
           World::InTheCastleSiegeFog(world.Actors()[world.Runtime().hero].position);
}

// 0x5f8928..0x5f8947: 0x6c9d58, que 0x5f87b0 prende fuera de los mapas 18 a 23 y 30.
bool HasTheGrassPass(std::int32_t map)
{
    return !(map >= FirstWithoutGrassMap && map <= LastWithoutGrassMap) && map != CastleMap;
}

// 0x635c54..0x635d7f: la celda del servidor del héroe por la tabla de protección, la Y (+0x420) y
// después la X (+0x41c), cada una en un acceso que se cierra (0x412890), y el atributo de 0x5f8290(X, Y).
std::uint8_t HeroCellAttribute(ActorState& hero, const Data::Terrain::TerrainAttributes& attributes,
                               Render::Models::CrtRandom& random)
{
    const LogicalCell cell = ReadLogicalCell(hero, random);
    // 0x635f14..0x635fe3: el word de 0x7d0d348, del que la cámara mira el byte bajo.
    return static_cast<std::uint8_t>(attributes.cells[Data::Terrain::CellIndex(cell.x, cell.y)]);
}
} // namespace

GameScene::GameScene(const SceneContext& context, App::ServerSession* session, const IModelLibrary* models,
                     const Data::Tables::ItemTable* items, const Render::Models::ModelTextureSlots* gameSlots,
                     const Data::Tables::GateTable* gates, const Data::Tables::SkillTable* skills,
                     const Data::Text::TextTable* texts, GameChatTables chat)
    : m_context(context), m_session(session), m_models(models), m_items(items), m_gameSlots(gameSlots), m_gates(gates),
      m_skills(skills), m_texts(texts), m_chatTables(chat), m_windowTextures(context.textures, WindowTextureSet),
      m_sandTextures(context.textures, SandTextureSet), m_tiles(context.textures)
{
}

GameScene::~GameScene() = default;

Core::Expected<void, std::string> GameScene::Enter()
{
    if (m_context.world == nullptr || m_context.cloth == nullptr)
    {
        return Core::Unexpected(std::string("el juego no entra sin el WorldState y el rand() de la CRT: el contador "
                                            "de la espera vive ahí y sus cierres gastan rand()"));
    }
    m_firstStep = true;
    m_holdFrame = true;
    m_groundEffects.Reset();
    auto& textureTable = m_context.world->FaunaResources().TextureTable();
    textureTable.Bind(m_context.textures);
    if (m_gameSlots != nullptr)
    {
        textureTable.Seed(*m_gameSlots, m_models);
    }
    m_tiles.BindTextureTable(textureTable);
    m_characterCloths.resize(ActorPoolSize);
    // El original las sube en la carga (0x61ccb0) y en el login (0x61c7c0) y no las suelta; MuRet, con cada escena.
    if (auto loaded = m_windowTextures.Load(m_context.dataRoot); !loaded.HasValue())
        return loaded;
    return m_sandTextures.Load(m_context.dataRoot);
}

// 0x636290 y, después, lo de 0x637700 para la tela: el viento (0x637cec) y el contador de pasos
// (0x637f8c) van detrás de la lógica de la escena (02, "Qué corre en cada paso"). Un corte del paso
// deja la escena en Failure sin la tela; después de un corte, del paso o del dibujo, nada.
void GameScene::Step(const Input::InputState& input)
{
    m_projectingFrame = false;
    if (m_failure)
    {
        return;
    }
    m_cameraFrames.Step();
    if (auto dialogs = StepTheDialogs(input); !dialogs.HasValue())
    {
        m_failure = dialogs.Error();
        return;
    }
    StepEntry();
    if (m_failure)
        return;
    if (auto stepped = StepWorld(input); !stepped.HasValue())
    {
        m_failure = stepped.Error();
        return;
    }
    if (m_context.cloth != nullptr)
    {
        Render::Models::StepTheClothWorld(*m_context.cloth);
    }
}

void GameScene::StepEntry()
{
    // 0x636838: solo el primer paso de la escena 5 escribe el yaw, sin reiniciar los otros globales.
    if (m_firstStep)
    {
        if (m_context.camera != nullptr)
            m_context.camera->yaw = EntryYaw;
        m_firstStep = false;
        // 0x636843..0x6368f4: los campos de texto (0x553570(1)), el campo del chat cerrado (0x6c8e68), 0x77f3b69 en
        // 0, los dos campos de Tab (0x6c8e6c; 0x6c8e70, 0x6c8e78 y 0x6c8e7c son de la entrada sin EDIT), 120 entradas
        // vacías, el EDIT del login vacío y oculto (0x469420 y 0x469450(3), que MuRet no tiene en el juego), la
        // entrada del chat (0x469320), el foco a la ventana del juego, el registro vacío (la vtable + 0x2c, 0x478d10) y
        // el filtro también (0x45db50).
        if (m_context.world != nullptr)
        {
            WorldRuntime& runtime = m_context.world->Runtime();
            UI::ChatInputState& input = runtime.chat.input;
            UI::ResetTheTextBuffers(input, true);
            runtime.ui.chatOpen = false;
            input.otherInput = false;
            input.fieldCount = UI::ChatFieldCount;
            if (auto emptied = AddTheEmptyMessages(runtime, m_context.textMeasure); !emptied.HasValue())
            {
                m_failure = emptied.Error();
                return;
            }
            UI::ResetTheChatInput(input);
            input.focus = UI::ChatFocus::Game;
            UI::ClearTheLog(runtime.chat.log, runtime.ui.chatModes);
            UI::ClearTheChatFilter(runtime.chat.log);
            // 0x636920: 0x46b010, las colas del cartel de Slide.bmd, su timer 0x3eb y 0x46b0e0 (el registro propio y
            // un texto de Slide.bmd, con sus rand()).
            if (m_context.cloth != nullptr)
            {
                const auto now =
                    m_context.clock != nullptr
                        ? static_cast<std::uint32_t>(
                              std::chrono::duration_cast<std::chrono::milliseconds>(m_context.clock->Now()).count())
                        : 0U;
                StartTheSlides(*m_context.world, m_context.cloth->random, now);
            }
        }
    }
    if (m_sent || m_session == nullptr)
    {
        return;
    }
    m_sent = true;
    m_session->EnterWorld();
}

Core::Expected<void, std::string> GameScene::StepWorld(const Input::InputState& input)
{
    // Enter no deja entrar sin los dos.
    if (m_context.world == nullptr || m_context.cloth == nullptr)
    {
        return {};
    }
    ObserveMapLoad();
    WorldRuntime& runtime = m_context.world->Runtime();
    // 0x636da3: el estado 0x3d marca el mundo cargado; sin él y con el mundo sin cargar, el paso
    // termina acá (0x636dbc).
    if (m_session != nullptr && m_session->State() == App::SessionState::InWorld)
    {
        runtime.drawingEnabled = true;
    }
    else if (!runtime.drawingEnabled)
    {
        return {};
    }
    // 0x636dc2..0x636ddb: el temblor de la cámara se reduce en cada paso; la x87 multiplica y guarda en
    // float, como el producto exacto en double redondeado una vez.
    World::GameCameraInput& cameraInput = CameraInputState();
    cameraInput.shake = F32(static_cast<double>(cameraInput.shake) * static_cast<double>(ShakeDecay));
    // 0x636dd4: 0x652940, con el héroe en +0x371 == 'C' (siempre, 0x511290), mira el asedio (0x7239ec) y
    // el gremio con 0x4294b0. El 0x7239ec solo lo escribe 0x4294c0, desde los paquetes de 0x4e0610 y
    // 0x4e2b10 que MuRet no aplica: sin asedio, no hace nada (*estático*, 08 P-C41).
    // 0x636de8: 0x5fbf60, la ola de las celdas de la ventana del rango del último dibujo, con el reloj
    // del paso (la rama de 0x723c50, P-C24 c, no se prende en MuRet). La copia de la luz sombreada al
    // buffer dinámico precede las luces de objetos y fauna (06 § Terreno). El rango y las olas
    // son de la escena: el original los comparte con la selección (08 P-C41).
    const World::WorldTerrain* stepTerrain = &m_context.world->Map().terrain;
    if (runtime.map == SelectionMap && m_context.selection != nullptr && m_context.selection->Terrain() != nullptr)
    {
        stepTerrain = m_context.selection->Terrain();
    }
    if (stepTerrain->lighting != nullptr)
    {
        m_context.world->DynamicLighting().Update(*stepTerrain->lighting);
    }
    if (m_context.clock != nullptr)
    {
        World::UpdateWaves(m_water, m_context.clock->Now(), m_context.world->DynamicLighting().Range(),
                           static_cast<std::uint8_t>(runtime.map));
    }
    // 0x636ded..0x63705b: la UI del paso, que corre en cada paso desde el primero, también en la espera.
    if (auto ui = StepTheUi(input); !ui.HasValue())
        return ui;
    // 0x63706a: 0x606560, los objetos que vio el último dibujo (0x6046a0), también en la espera.
    if (auto objects = StepTheObjects(); !objects.HasValue())
    {
        return objects;
    }
    const float worldClock =
        m_context.clock ? static_cast<float>(std::chrono::duration<double, std::milli>(m_context.clock->Now()).count())
                        : 0.0f;
    if (auto stepped = StepTheFloorItems(worldClock); !stepped.HasValue())
        return stepped;
    if (auto leaves = StepTheLeaves(); !leaves.HasValue())
        return leaves;
    if (auto fauna = StepTheFauna(); !fauna.HasValue())
        return fauna;
    // 0x6370f4: 0x445230, los ayudantes, también en la espera.
    if (auto helpers = StepHelpers(HelperStepContext{.world = *m_context.world,
                                                     .models = m_models,
                                                     .random = m_context.cloth->random,
                                                     .worldClock = worldClock,
                                                     .scene = SceneId::Game,
                                                     .terrain = nullptr,
                                                     .logicSteps = m_context.cloth->logicSteps,
                                                     .cameraShake = &CameraInputState().shake});
        !helpers.HasValue())
    {
        return helpers;
    }
    // 0x6370f9: 0x557650, los globos. 0x6370fe..0x637105: 0x451c10, las tiendas personales (0x7245c8), que no
    // tienen ninguna hasta la CL9.4.
    const UI::Point bubbleMouse = UI::MouseIn640x480(input, m_screen);
    if (auto bubbles = StepTheChatBubbles(
            *m_context.world,
            ChatBubbleStepInput{.mouseX = bubbleMouse.x, .mouseY = bubbleMouse.y, .screen = m_screen});
        !bubbles.HasValue())
        return bubbles;
    // 63710a: MoveHero sale con más de30, pero la lógica posterior sigue durante la espera.
    if (auto moved = StepTheHero(input); !moved.HasValue())
        return moved;
    // 63710f: personajes sobre el trapecio retenido del último dibujo.
    if (auto stepped = StepTheCharacters(worldClock); !stepped.HasValue())
        return stepped;
    if (auto effects = StepTheEffects(worldClock); !effects.HasValue())
        return effects;
    // 0x63717f: 0x5ac8d0, las teclas de función.
    const UI::Point mouse = UI::MouseIn640x480(input, m_screen);
    const GameChatContext chat = ChatContext();
    return StepTheFunctionKeys(GameUiContext{.world = *m_context.world,
                                             .random = m_context.cloth->random,
                                             .input = input,
                                             .mouseX = mouse.x,
                                             .mouseY = mouse.y,
                                             .measure = m_context.textMeasure,
                                             .screen = m_screen,
                                             .chat = &chat});
}

Core::Expected<void, std::string> GameScene::StepTheObjects()
{
    WorldState& world = *m_context.world;
    const std::int32_t map = world.Runtime().map;
    std::span<World::WorldObject> objects = world.Map().objects;
    if (World::IsKalimaMap(map))
    {
        // 606572 antes del early-return y del reset60657a, incluso sin un objeto visto.
        const auto selector = StepKalimaObjectPrelude(world, *m_context.cloth);
        if (!selector.HasValue())
            return Core::Unexpected(selector.Error());
        world.Runtime().objectSelectionCount = 0;
        // 6063d0: no esIcarus;4055b0 y42c500 retornanAL0 paraKalima, sin usar selector.
    }
    // 0x405490, primero en 0x6061c0: los Chaos Castle, también antes del early-return y del 0x60657a, con la cuenta
    // del pase anterior. El selector queda para 0x4055b0.
    const bool chaosCastle = World::IsChaosCastleMap(map);
    std::int32_t chaosCastleSelector = 0;
    if (chaosCastle)
    {
        const ActorState& hero = world.Actors()[world.Runtime().hero];
        const World::ChaosCastlePreludeContext prelude{
            .random = &m_context.cloth->random,
            .heroPosition = hero.position,
            .heroAngles = hero.angles,
            .heroInSafeZone = hero.safeZone != 0,
            .createJoint = [&world, this](const World::FishEffectRequest& request) -> Core::Expected<void, std::string>
            {
                const auto created = CreateFaunaJoint(world.DrawTail().joints, request, m_context.cloth->random);
                if (!created.HasValue())
                    return Core::Unexpected(created.Error());
                return {};
            }};
        const auto selector = World::StepChaosCastlePrelude(world.Runtime().objectSelectionCount, prelude);
        if (!selector.HasValue())
            return Core::Unexpected(selector.Error() + ", en el " + MapText(map));
        chaosCastleSelector = selector.Value();
        world.Runtime().objectSelectionCount = 0;
    }
    // 0x6061c0 en Icarus, antes del early-return y del 0x60657a, con la cuenta del pase anterior.
    std::optional<IcarusObjectServices> icarus;
    std::int32_t icarusSelector = 0;
    if (map == IcarusMap)
    {
        const ActorState& hero = world.Actors()[world.Runtime().hero];
        icarus.emplace(IcarusObjectServices{
            .random = m_context.cloth->random,
            .heroPosition = hero.position,
            .lighting = &world.DynamicLighting().Lighting(),
            .effects = &world.Map().effects,
            .sprites = &m_sprites,
            .createJoint = [&world, this](const World::FishEffectRequest& request) -> Core::Expected<void, std::string>
            {
                const auto created = CreateFaunaJoint(world.DrawTail().joints, request, m_context.cloth->random);
                if (!created.HasValue())
                    return Core::Unexpected(created.Error());
                return {};
            }});
        const auto selector = StepIcarusObjectPrelude(world.Runtime().objectSelectionCount, *icarus);
        if (!selector.HasValue())
            return Core::Unexpected(selector.Error());
        icarusSelector = selector.Value();
        world.Runtime().objectSelectionCount = 0;
    }
    // 0x6062e6: los Blood Castle, también antes del early-return, con el reloj del paso.
    const float worldClock =
        m_context.clock != nullptr
            ? static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(m_context.clock->Now()).count())
            : 0.0f;
    const bool bloodCastle = map >= FirstBloodCastleMap && map <= LastBloodCastleMap;
    if (bloodCastle)
    {
        const ActorState& hero = world.Actors()[world.Runtime().hero];
        if (auto orb =
                World::StepBloodCastlePrelude(m_context.cloth->random, hero.position, &world.Particles(), worldClock);
            !orb.HasValue())
            return orb;
    }
    // 0x42b540, el tercer eslabón de 0x6061c0: el 30, también antes del early-return y del 0x60657a.
    if (map == CastleMap)
    {
        // timeGetTime (0x6b8544) es el reloj de la escena en ms; sin reloj, el preludio corta en 0x42b557.
        World::CastleSiegePrelude prelude{.siegeActive = world.Runtime().siegeActive,
                                          .musicTime = &world.Runtime().siegeMusicTime,
                                          // MuRet todavía no tiene audio (CL10): queda el último pedido.
                                          .sound = [&world](std::int32_t id) { world.Runtime().lastSoundId = id; }};
        if (m_context.clock != nullptr)
            prelude.systemTime = [clock = m_context.clock]() {
                return static_cast<std::uint32_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(clock->Now()).count());
            };
        if (auto stepped = World::StepCastleSiegePrelude(prelude); !stepped.HasValue())
            return stepped;
        world.Runtime().objectSelectionCount = 0;
    }
    // Sin un objeto visto (antes del primer dibujo que llega a 0x6046a0), 0x606560 no toca ninguno.
    if (std::ranges::none_of(objects, [](const World::WorldObject& object) { return object.alive && object.seen; }))
    {
        return {};
    }
    // 0x4250f0 lleva los del 33 y los del 54 (los de la selección, vistos en su dibujo, que siguen durante la
    // espera de la entrada al juego).
    const bool aida = map == AidaMap || map == SelectionMap;
    if (map != LorenciaMap && map != DungeonMap && map != DeviasMap && map != NoriaMap && map != LostTowerMap &&
        map != ArenaMap && map != AtlansMap && map != TarkanMap && map != DevilSquareMap && map != IcarusMap &&
        map != LandOfTrialsMap && map != CastleMap && map != CrywolfMap && !World::IsKalimaMap(map) && !aida &&
        !bloodCastle && !chaosCastle)
    {
        return Core::Unexpected("0x606560: los objetos vistos del " + MapText(map) + " (0x604ab0) no están portados");
    }
    if (auto loaded = LoadTheObjectModels(map); !loaded.HasValue())
    {
        return loaded;
    }
    // 0x604ab0 lee la capa de la celda del héroe y el reloj del mundo (0x5767b88), el del paso; el 1 y el 4
    // crean efectos con modelo y la patada del 4 mira la acción del héroe.
    const ActorState& hero = world.Actors()[world.Runtime().hero];
    const World::ObjectStepGlobals globals{
        .heroCellLayer = world.Runtime().heroCellLayer,
        .worldClock = worldClock,
        .dynamicLighting = &world.DynamicLighting().Lighting(),
        .particles = &world.Particles(),
        .heroPosition = &hero.position,
        .sound = [&](std::int32_t id) { world.Runtime().lastSoundId = id; },
        // El ambiente del 9 suena en bucle (0x41ed10(1, 0, 1)); MuRet todavía no tiene audio (CL10).
        .loopedSound = [&](std::int32_t id) { world.Runtime().lastSoundId = id; },
        .modelState = &world.Runtime().objectModelState,
        .effects = &world.Map().effects,
        .ownRecordDepth = &world.Runtime().ownRecordDepth,
        .heroAction = &hero.animation.action,
        // Q-C304: el 31 lee timeGetTime (0x6b8544) en el paso; MuRet le presta el mismo reloj en ms.
        .systemTime =
            [this]()
        {
            return m_context.clock != nullptr
                       ? static_cast<std::uint32_t>(
                             std::chrono::duration_cast<std::chrono::milliseconds>(m_context.clock->Now()).count())
                       : 0U;
        },
        .trialsAmbientTime = &world.Runtime().trialsAmbientTime,
        .siegeActive = &world.Runtime().siegeActive};
    AfterObjectStep after;
    if (icarus)
        after = [&](World::WorldObject& object)
        { return StepIcarusSeenObject(object, icarusSelector, world.Runtime().objectSelectionCount, *icarus); };
    if (chaosCastle)
        after = [&](World::WorldObject& object) -> Core::Expected<void, std::string>
        {
            World::StepChaosCastleSeenObject(object, chaosCastleSelector, world.Runtime().objectSelectionCount,
                                             world.Runtime().chaosCastleTrapArmed);
            return {};
        };
    // 0x6063d0 → 0x42c500 en el 30, con el reloj del paso.
    if (map == CastleMap)
        after = [&](World::WorldObject& object)
        { return World::StepCastleSiegeSeenObject(object, world.Runtime().siegeActive, worldClock); };
    if (auto stepped = StepGameWorldObjects(objects, static_cast<std::uint8_t>(map), m_worldModels,
                                            m_context.cloth->random, globals, after);
        !stepped.HasValue())
    {
        return Core::Unexpected(stepped.Error() + ", en el " + MapText(map));
    }
    return {};
}

void GameScene::PrepareFrame()
{
    m_holdFrame = true;
    if (m_context.world == nullptr || m_context.cloth == nullptr)
    {
        return;
    }
    ObserveMapLoad();
    WorldRuntime& runtime = m_context.world->Runtime();
    // 0x6371a6: sin el mundo cargado devuelve 0 sin leer el contador.
    if (!runtime.drawingEnabled)
    {
        return;
    }
    std::int32_t counter = 0;
    {
        // 0x6371b7..0x637229: lo lee con un acceso que se cierra (0x40b800) aunque después no dibuje.
        const Render::Models::LogicalAccess access(runtime.warpDepth, m_context.cloth->random,
                                                   Render::Models::SmallLogicalCloseRandoms);
        counter = runtime.warpCounter;
    }
    // 0x63722e: cmp esi, 0x1e y jle, con signo.
    m_holdFrame = counter > WaitDrawThreshold;
    if (m_holdFrame)
    {
        return;
    }
    // P-C40: se conserva la invalidación preventiva mientras el frame original tiene etapas y ramas
    // sin portar. El jugador que alcanza 0x50c91e publica después su scan real; si el dibujo corta
    // antes, el valor queda desconocido. No representa una escritura del original en PrepareFrame.
    runtime.fullSetScan = std::nullopt;
}

void GameScene::Render(SceneCanvas& canvas, float /*alpha*/)
{
    if (m_failure || m_holdFrame || m_context.world == nullptr || m_context.cloth == nullptr)
    {
        return;
    }
    const auto drawn = DrawTheFrame(canvas);
    if (!drawn.HasValue())
    {
        m_failure = drawn.Error();
    }
}

Core::Expected<void, std::string> GameScene::DrawTheFrame(SceneCanvas& canvas)
{
    WorldRuntime& runtime = m_context.world->Runtime();
    const World::WorldTerrain& terrain = m_context.world->Map().terrain;
    const int frames = m_cameraFrames.FramesAtDraw(OriginalFramePaceOf(m_context.pacing));
    m_framePoseCarry = &m_context.world->PoseCarry();
    m_frameSprites = &m_sprites;
    PrepareDrawProjection(frames);
    if (frames == 0)
    {
        m_projectedPoseCarry = m_context.world->PoseCarry();
        m_framePoseCarry = &m_projectedPoseCarry;
        m_projectedSprites.Clear();
        m_frameSprites = &m_projectedSprites;
    }
    if (frames == 0)
    {
        // El original publica 639482 en el orquestador, antes del despacho de escena.
        // Un frame intermedio sólo consulta el valor de su reloj sobre la copia proyectada.
        const float worldClock =
            static_cast<float>(std::chrono::duration<double, std::milli>(canvas.frameClock).count());
        UpdateGameItemGlowPulse(FrameItemRender(), worldClock);
    }
    if (!terrain.mapping || !terrain.heights || !terrain.attributes || !terrain.lighting)
    {
        return Core::Unexpected("0x6371a0: el " + MapText(runtime.map) + " no está cargado (0x61a560)");
    }
    if (auto uploaded = UploadTheTiles(static_cast<std::uint8_t>(runtime.map)); !uploaded.HasValue())
    {
        return uploaded;
    }
    // 0x63723a: 0x7d368a0, la niebla de 0x620db0, en 0: Begin la apaga en cada pase (ModelBatch::Begin).
    // 0x637241: 0x635ae0.
    const Render::Camera3D camera = CameraOfTheFrame(canvas, frames);
    m_drawnCamera = camera;
    m_screen = canvas.screen;
    // 0x63726f..0x637337: en el mapa 6 con "webzen" en el nombre (0x576960; la búsqueda de "webzen2" no cambia
    // nada, Q-C152), el punto fijo (6.400; 15.400) con la z del héroe; si no, el de 0x44dc60. El recorrido de
    // 0x4033d0 (el BD 00, que MuRet no aplica) no está prendido y no lo cambia.
    const World::GameCameraInput& cameraInput = CameraInputState();
    const glm::vec3 target = runtime.map == NamedCameraMap && cameraInput.webzen
                                 ? glm::vec3(SpectatorTargetX, SpectatorTargetY, cameraInput.hero.z)
                                 : World::CameraTargetOf(cameraInput);
    // 0x63733e: 0x5bb8d0, el ancho del mundo, con los rand() del frame (las consultas del comercio y de la tienda
    // con el inventario abierto): 640 sin ventanas, 450 con una y 260 con dos.
    const std::int32_t worldWidth = GamePanels(*m_context.world, FrameCloth().random).Registry().WorldWidth();
    // 0x63734d: el pase de 0x620db0(0, 0, w, 48) no dibuja; su modelview es la del pase del mundo.
    const glm::mat4 view = Render::MakePass3D(canvas.screen, GameWorldViewport, camera).view;
    // 0x637352..0x637366: 0x5fbb00(w · 0,0015625f, &p), los cinco planos, el rango de bloques y el trapecio; con w en
    // 640, 1. El trapecio y su nivel quedan en los globales de WorldRuntime, que lee 0x5fba90 en el paso que sigue.
    const float widthFactor = F32(static_cast<double>(worldWidth) * static_cast<double>(WorldWidthFactor));
    m_frustum = World::SphereFrustumOf(view, camera, widthFactor);
    m_terrainRange = World::TerrainRangeOf(view, camera, widthFactor);
    if (frames > 0)
        m_context.world->DynamicLighting().PublishRange(m_terrainRange);
    const auto polygon = PolygonOfTheFrame(camera, target, worldWidth, frames);
    if (!polygon.HasValue())
    {
        return Core::Unexpected(polygon.Error());
    }
    // 0x63738a: 0x5bb8d0 otra vez, con sus rand(), para el ancho del pase del mundo.
    const std::int32_t passWidth = GamePanels(*m_context.world, FrameCloth().random).Registry().WorldWidth();
    // 0x637391..0x637404: el glClearColor del mapa, que nadie usa (Q-C143).
    // 0x637410: el pase del mundo, 0x620db0(0, 0, w, 432), con el aspecto de su viewport.
    const Render::Pass3D pass =
        Render::MakePass3D(canvas.screen, Render::ViewportRect{0, 0, passWidth, GameWorldViewport.height}, camera);
    canvas.models.Begin(pass);
    auto drawn = DrawTheWorldPass(canvas, pass, polygon.Value(), frames);
    canvas.flushModels();
    if (!drawn.HasValue())
        return drawn;
    // 0x6203c0 del pase del mundo: la proyección que leen los globos del HUD (0x5a9050 → 0x6205b0). Sin ella, corta
    // solo si hay un globo vivo.
    const auto projection = MakeGameProjection(pass, camera.fieldOfView);
    return DrawTheInterface(canvas, projection.HasValue() ? &projection.Value() : nullptr);
}

void GameScene::ObserveMapLoad()
{
    const auto revision = m_context.world->Map().loadRevision;
    if (m_loadedRevision == revision)
        return;
    // Un mismo número de mapa también puede tener otra carga, incluso después de un fallo.
    // Estos owners son cachés de MuRet: no son escrituras ni contadores del original.
    m_groundEffects.Reset();
    m_tiles.Release();
    m_worldModels.Release();
    m_worldModelsMap.reset();
    m_context.world->Runtime().objectModelState.unlitMeshes.clear();
    m_projection.reset();
    m_projectingFrame = false;
    m_framePoseCarry = nullptr;
    m_frameSprites = nullptr;
    m_spriteReplay.Clear();
    m_spriteReplayValid = false;
    m_loadedRevision = revision;
}

Core::Expected<void, std::string> GameScene::UploadTheTiles(std::uint8_t map)
{
    if (m_tiles.LoadedMap() == map)
    {
        return {};
    }
    std::vector<std::string> skipped;
    if (auto loaded = m_tiles.Load(m_context.dataRoot, map, skipped, GrassTextures::Upload); !loaded.HasValue())
    {
        return Core::Unexpected("0x61a560: los tiles del terreno: " + loaded.Error());
    }
    for (const std::string& tile : skipped)
    {
        m_context.log.Warning("el juego va sin una textura del terreno, que el original saltea: {}", tile);
    }
    return {};
}

Core::Expected<void, std::string> GameScene::LoadTheObjectModels(std::int32_t map)
{
    if (m_worldModelsMap == map)
    {
        return {};
    }
    m_worldModels.Release();
    m_worldModelsMap.reset();
    // La carga recrea BMD durables, aun si la pidió un dibujo proyectado.
    m_context.world->Runtime().objectModelState.unlitMeshes.clear();
    if (m_projectingFrame)
        FrameModelState().unlitMeshes.clear();
    if (auto loaded =
            m_worldModels.Load(m_context.dataRoot, static_cast<std::uint8_t>(map), m_context.textures, m_context.log,
                               m_gameSlots, m_models, &m_context.world->FaunaResources().TextureTable());
        !loaded.HasValue())
    {
        return Core::Unexpected("0x616ec0: los modelos de los objetos del " + MapText(map) + ": " + loaded.Error());
    }
    m_worldModelsMap = map;
    World::SeedTarkanModelState(static_cast<std::uint8_t>(map), m_context.world->Runtime().objectModelState);
    if (m_projectingFrame)
        World::SeedTarkanModelState(static_cast<std::uint8_t>(map), FrameModelState());
    m_context.world->FaunaResources().Bind(&m_worldModels);
    return {};
}

Render::Camera3D GameScene::CameraOfTheFrame(SceneCanvas& canvas, int frames)
{
    WorldState& world = *m_context.world;
    const WorldRuntime& runtime = world.Runtime();
    ActorState& hero = world.Actors()[runtime.hero];
    World::GameCameraInput& input = CameraInputState();
    // Lo que 0x635ae0 lee de los globales: el héroe publicado (0x743a170), el mundo (0x6c9dd0) y el reloj
    // del mundo (0x5767b88, en ms, para el mapa 5).
    input.hero = hero.position;
    input.map = static_cast<std::uint8_t>(runtime.map);
    input.scene = World::CullingScene::Game;
    // 0x635b3d: el nombre del héroe, con 0x576960 (Q-C152).
    input.webzen = ActorNameContains(hero, "webzen");
    input.worldTime =
        static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(canvas.frameClock).count());
    if (frames == 0)
    {
        // Un dibujo intermedio de MuRet: consulta la cámara del último frame original, sin gastar rand().
        return World::GameCameraOf(CameraState(), input);
    }
    // Cada frame original termina en el corte de abajo: de los que cuenta el reloj corre el primero y el
    // corte no deja correr los demás.
    // 0x635b3d..0x635b9e: la rama del mapa 6 con "webzen" no lee la celda del héroe (ni sus rand()).
    if (!(input.map == NamedCameraMap && input.webzen))
        input.cellAttribute = HeroCellAttribute(hero, *world.Map().terrain.attributes, m_context.cloth->random);
    return World::StepGameCamera(CameraState(), input);
}

Core::Expected<World::TerrainPolygon, std::string> GameScene::PolygonOfTheFrame(const Render::Camera3D& camera,
                                                                                const glm::vec3& target,
                                                                                std::int32_t worldWidth, int frames)
{
    WorldRuntime& runtime = m_context.world->Runtime();
    World::TerrainPolygonInput input;
    input.scene = static_cast<std::int32_t>(World::CullingScene::Game);
    input.map = runtime.map;
    input.translation = target;
    // El selector del mapa 30, como TerrainPolygonOf.
    input.objectPosition = glm::vec2(target.x, target.y);
    input.cameraYaw = camera.angles.z;
    input.worldWidth = worldWidth;
    // 0x5fb570: las dos búsquedas del nombre del mapa 6, la de las medidas y la del giro.
    input.webzenProfile = input.webzenYaw = CameraInputState().webzen;
    if (frames == 0)
    {
        return TerrainPolygonBetweenFrames(runtime, input);
    }
    // El nivel pedido (0x7d3a0ec), la D (0x7d36ba4) y la meta (0x6d5ee0) que dejó la cámara del frame.
    return StepTheTerrainPolygon(runtime, input, CameraState());
}

Core::Expected<void, std::string> GameScene::DrawTheWorldPass(SceneCanvas& canvas, const Render::Pass3D& pass,
                                                              const World::TerrainPolygon& polygon, int frames)
{
    const std::int32_t map = m_context.world->Runtime().map;
    // 0x63741d..0x637490: en el 30, con el héroe en el rectángulo de World::InTheCastleSiegeFog (Q-C334), 0x42b0a0
    // prende la niebla negra (GL_LINEAR de 2.000 a 2.700) para todo lo que sigue del pase, también el pase en el
    // ojo de los sprites y las partículas; afuera, glDisable(GL_FOG), que deja la de 0x620db0 (Begin).
    canvas.models.SetFog(HeroInTheCastleSiegeFog(*m_context.world) ? std::optional(CastleSiegeFog) : std::nullopt);
    // 0x6374aa: 0x620480(x, y, &0x7d366c0, 1) con el mouse en 640 × 480 (0x7d3682c y 0x7d36828), que
    // vuelve a los píxeles de la pantalla en enteros (0x620486..0x6204b0).
    const Input::InputState noInput;
    const UI::Point mouse = UI::MouseIn640x480(m_context.input != nullptr ? *m_context.input : noInput, canvas.screen);
    const auto pixelX = (static_cast<std::int64_t>(mouse.x) * canvas.screen.width) / Width640;
    const auto pixelY = (static_cast<std::int64_t>(mouse.y) * canvas.screen.height) / Height480;
    const auto [origin, end] = pass.RayThrough(static_cast<float>(pixelX), static_cast<float>(pixelY));
    m_mouseRay = World::TerrainPickingSegment{.origin = origin, .end = end};
    // 0x6374b2..0x6374c2: 0x437850() mira el mundo de Kalima (0x723c48), que MuRet no crea (MapLoad corta
    // en esos mapas): es falso. El mapa 10 no dibuja el terreno.
    if (FrameKalima() == nullptr && map != WithoutTerrainMap)
    {
        const World::WorldTerrain& terrain = m_context.world->Map().terrain;
        World::TerrainView view;
        view.mapping = terrain.mapping.get();
        view.heights = terrain.heights.get();
        view.attributes = terrain.attributes.get();
        view.lighting = &FrameLighting();
        view.tiles = m_tiles.Sizes();
        view.map = static_cast<std::uint8_t>(map);
        view.fixedHeight = CameraState().fixedHeight;
        // 0x5fa973: en el mapa 7 el agua va con 0x5fa4b0 y la cáustica del cuadro de DAT_07d2d360.
        view.waterFrame = FrameCloth().waterTextureFrame;
        // 0x5fc26b..0x5fc2c0: el corrimiento del agua con el reloj del frame; 0x5fc2de: 0x5fc190(0) con
        // 0x7d0d31c en 0, las celdas del rango que pasan el trapecio.
        m_water.offset = World::WaterOffsetAt(canvas.frameClock, view.map);
        // 0x5fa83f: 0x5fa830 llama a 0x405e00(x, y) con cada celda que dibuja, y en los mapas 18 a 23 saca rand()
        // (y, con rand() % 8 en 0, el área en 0, 3 o 6 y la celda en uno de sus rectángulos, la piedra y el
        // temblor, Q-C229). Las demás no lo llaman en MuRet: no saca nada.
        if (World::IsChaosCastleMap(map))
        {
            const float frameClock =
                static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(canvas.frameClock).count());
            const World::ChaosCastleCellContext cellContext{
                .area = m_context.world->Runtime().chaosCastleArea,
                .heroZ = m_context.world->Actors()[m_context.world->Runtime().hero].position.z,
                .random = &FrameCloth().random,
                .particles = &FrameParticles(),
                .worldClock = std::cref(frameClock),
                .cameraShake = m_projectingFrame ? &m_projection->cameraShake : &CameraInputState().shake};
            if (auto drawn = World::DrawTerrain(canvas.models, view, m_terrainRange, polygon, m_water,
                                                [&cellContext](std::uint8_t x, std::uint8_t y)
                                                { return World::StepChaosCastleCell(x, y, cellContext); });
                !drawn.HasValue())
            {
                return Core::Unexpected(drawn.Error() + ", en el " + MapText(map));
            }
        }
        else if (map == CrywolfMap)
        {
            // 0x5fa695: en el 34, las UV del agua sacan rand() y sueltan las burbujas 698/0 (World::CrywolfWater).
            const float frameClock =
                static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(canvas.frameClock).count());
            const ActorState& hero = m_context.world->Actors()[m_context.world->Runtime().hero];
            const World::CrywolfWaterContext waterContext{.heroZ = hero.position.z,
                                                          .heroAngles = hero.angles,
                                                          .random = &FrameCloth().random,
                                                          .particles = &FrameParticles(),
                                                          .worldClock = std::cref(frameClock)};
            if (auto drawn = World::DrawTerrain(canvas.models, view, m_terrainRange, polygon, m_water,
                                                [&view, &waterContext](std::uint8_t x, std::uint8_t y)
                                                { return World::StepCrywolfWaterCell(view, x, y, waterContext); });
                !drawn.HasValue())
            {
                return Core::Unexpected(drawn.Error() + ", en el " + MapText(map));
            }
        }
        else
        {
            World::DrawTerrain(canvas.models, view, m_terrainRange, polygon, m_water);
        }
        // 0x5fc32b..0x5fc353: con la mezcla de 0x620a00(1), 0x6c9d58 y fuera del mapa 7, el pase del pasto (0x5fc190
        // con 0x7d0d31c en 2) en las mismas celdas. No saca rand(): 0x405e00 solo lo hace en los mapas 18 a 23, que no
        // tienen este pase.
        if (HasTheGrassPass(map) && map != AnimatedWaterMap)
        {
            const World::TerrainGrass grass{.heights = m_tiles.GrassHeights(),
                                            .offsets = m_context.world->Map().grassOffsets};
            if (auto grown = World::DrawGrass(canvas.models, view, m_terrainRange, polygon, m_water, grass);
                !grown.HasValue())
            {
                return Core::Unexpected(grown.Error() + ", en el pase del pasto del " + MapText(map));
            }
        }
        // 0x5fc35b..0x5fc360: sin depth test (0x620870) y con culling (0x6208d0), que la mezcla de 0x549130 apaga.
        Render::ModelDrawState marksState = canvas.models.State();
        marksState.depthTest = false;
        marksState.cullEnabled = true;
        canvas.models.SetState(marksState);
        // 0x5fc365: 0x549130, las marcas del suelo de WorldRuntime, que la carga del mapa no vacía (Q-C228). MuRet
        // no sube Effect\blood01.tga (0x61d7aa), la del 0x5e3 de sus dos creadores, y no sabe sus componentes:
        // con una marca prendida, corta (08 P-C49).
        if (auto marked = World::DrawGroundMarks(canvas.models, m_context.world->Runtime().groundMarks,
                                                 *terrain.heights, NoSlotComponents);
            !marked.HasValue())
        {
            return Core::Unexpected(marked.Error() + ", en el " + MapText(map));
        }
        // 0x5fc36a: el depth test de vuelta (0x620850).
        Render::ModelDrawState afterMarks = canvas.models.State();
        afterMarks.depthTest = true;
        canvas.models.SetState(afterMarks);
    }
    // 0x6374ce: 0x6046a0, los objetos del .obj por sector. MuRet tiene los casos de los mapas 0 a 4, 6 a 23, 30,
    // 31, 33, 34 y Kalima.
    const bool knownObjects = (map >= LorenciaMap && map <= LostTowerMap) ||
                              (map >= ArenaMap && map <= World::LastChaosCastleMap) || map == CastleMap ||
                              map == LandOfTrialsMap || map == AidaMap || map == CrywolfMap || World::IsKalimaMap(map);
    if (!knownObjects &&
        std::ranges::any_of(FrameObjects(), [](const World::WorldObject& object) { return object.alive; }))
    {
        return Core::Unexpected("0x6374ce: los objetos del " + MapText(map) +
                                " (0x6046a0 → 0x602e90, 0x602ed0) no están portados: MuRet tiene los del 0 al 4, "
                                "los del 6 al 23, los del 30, los del 31, los del 33, los del 34 y los de Kalima");
    }
    if (auto loaded = LoadTheObjectModels(map); !loaded.HasValue())
    {
        return loaded;
    }
    WorldState& world = *m_context.world;
    const ActorState& hero = world.Actors()[world.Runtime().hero];
    // La luz del terreno en un punto (0x5f99e0) lee el buffer dinámico 0x7b4ccc4: el paso suma las luces
    // de los fuegos, las lámparas y la fauna al buffer dinámico. El reloj
    // del mundo (0x5767b88), el del frame, como el de la cámara.
    if (knownObjects)
    {
        if (auto objects = DrawGameWorldObjects(
                canvas.models, m_worldModels, FrameObjects(), polygon, &FrameLighting(), canvas.frameClock,
                GameObjectPass{
                    .map = static_cast<std::uint8_t>(map),
                    .originalFrame = frames > 0,
                    .random = &FrameCloth().random,
                    .heroClass = hero.characterClass,
                    .ownLevel = world.OwnCharacter().level,
                    .ownRecordDepth =
                        m_projectingFrame ? &m_projection->ownRecordDepth : &world.Runtime().ownRecordDepth,
                    .worldClock = static_cast<float>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(canvas.frameClock).count()),
                    .particles = &FrameParticles(),
                    .onPose = [&](const DrawnObject& drawn)
                    { return PublishFaunaPose(FramePoseCarry(), m_worldModels, drawn, true); },
                    .sprites = &FrameSpritesForDraw(),
                    .retainedPose = &FramePoseCarry(),
                    .createJoint = [&](const World::FishEffectRequest& request) -> Core::Expected<void, std::string>
                    {
                        const auto created = CreateFaunaJoint(FrameTail().joints, request, FrameCloth().random);
                        if (!created.HasValue())
                            return Core::Unexpected(created.Error());
                        return {};
                    },
                    .modelState = &FrameModelState(),
                    .publishProjectedPose = true,
                    .waterTextureFrame = FrameCloth().waterTextureFrame,
                    .hero = &hero,
                    .kalima = FrameKalima(),
                    .requestSound = [&](std::int32_t sound) { world.Fauna().Sound(sound, nullptr); },
                    .effects = &FrameEffects(),
                    // Q-C304: el 0x46 de Aida lee timeGetTime (0x6b8544); MuRet le presta el reloj del dibujo.
                    .systemTime =
                        [systemTime = static_cast<std::uint32_t>(
                             std::chrono::duration_cast<std::chrono::milliseconds>(canvas.frameClock).count())]()
                    { return systemTime; },
                    .scene = static_cast<std::int32_t>(SceneId::Game),
                    .terrainHeight =
                        [&](float x, float y)
                    {
                        const World::WorldTerrain& terrain = world.Map().terrain;
                        return static_cast<float>(World::TerrainHeightAt(*terrain.heights, *terrain.attributes, x, y,
                                                                         CameraState().fixedHeight));
                    },
                    .chaosCastleArea = world.Runtime().chaosCastleArea,
                    .siegeActive = world.Runtime().siegeActive,
                    .objectDrawCounter = &world.Runtime().objectDrawCounter,
                    .crywolf = &world.Runtime().crywolf});
            !objects.HasValue())
        {
            return Core::Unexpected(objects.Error() + ", en el " + MapText(map));
        }
    }
    // 0x6374d3: 0x52dc20, los efectos del suelo, sobre la tabla de 0x518cf0 (la de WorldMap).
    if (auto ground = m_groundEffects.Draw(
            canvas.models, FrameEffects().Effects(),
            World::GroundEffectResources{
                .heights = *world.Map().terrain.heights,
                .random = FrameCloth().random,
                .worldClock = static_cast<float>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(canvas.frameClock).count()),
                .map = static_cast<std::uint8_t>(map),
                .kalima = FrameKalima()},
            frames > 0);
        !ground.HasValue())
    {
        return Core::Unexpected(ground.Error() + ", en el " + MapText(map));
    }
    // 0x6374d8: 0x446b60, los pájaros (0x7d2dca8, 40 lugares). Los crea y los mueve 0x446120 desde el paso
    // (0x6370ea), que MuRet no corre (la frontera de StepWorld), y los apagan 0x6070a0 al cargar el mapa y
    // 0x4455c0. MuRet no tiene la tabla: sin pájaros vivos, 0x446b60 no dibuja ni saca rand(), y solo suma y
    // resta 90 al +0x24 de cada lugar que no es un 0x10f (Q-C277), que nadie ve (08 P-C55).
    if (auto birds = DrawTheBirds(canvas, polygon, frames); !birds.HasValue())
        return birds;
    // 0x6374dd: 0x510d00, los personajes, y al final 0x42da60 con el TerrainWall.
    if (auto characters = DrawTheCharacters(canvas, polygon, frames); !characters.HasValue())
    {
        return Core::Unexpected(characters.Error() + ", en el " + MapText(map));
    }
    // 0x6374e2..0x6374ed: 0x5fc260(1) solo con 0x77f3b18, el modo editor, que nadie prende. 0x6374f5: con
    // 0x7d36899 en 0 (nadie lo escribe), 0x608a00, los ítems del piso.
    if (auto tail = DrawTheTail(canvas, pass, polygon, frames); !tail.HasValue())
        return tail;
    // 0x6375ea..0x63763c: después de los efectos y antes del 2D (0x6219b0), con el héroe todavía en el rectángulo,
    // 0x42b0f0 apaga la niebla. Si salió durante el dibujo, queda prendida hasta el 0x620db0 que sigue (Q-C390).
    if (HeroInTheCastleSiegeFog(*m_context.world))
        canvas.models.SetFog(std::nullopt);
    return {};
}

Core::Expected<void, std::string> GameScene::DrawTheCharacters(SceneCanvas& canvas,
                                                               const World::TerrainPolygon& polygon, int frames)
{
    WorldState& world = *m_context.world;
    if (m_models == nullptr)
    {
        return DrawGameCharacters(world.Actors(), world.Runtime().hero, world.Map().terrain.attributes->cells);
    }
    std::optional<std::string> poseFailure;
    ItemSpecialDrawContext special;
    ItemDrawContext itemContext;
    FillTheItemContexts(special, itemContext, canvas.frameClock, frames);
    const auto drawn = DrawGameCharacters(
        canvas.models, *m_models, world,
        GameCharacterPass{.dynamicLighting = &FrameLighting(),
                          .clothWorld = &FrameCloth(),
                          .frameClock = canvas.frameClock,
                          .originalFrames = frames,
                          .cloths = m_characterCloths,
                          .sprites = &FrameSpritesForDraw(),
                          .poseCarry = &FramePoseCarry(),
                          .joints = FrameTail().joints,
                          .particles = &FrameParticles(),
                          .effects = &FrameEffects(),
                          .itemContext = &itemContext,
                          .pace = OriginalFramePaceOf(m_context.pacing),
                          .onPose =
                              [&](std::uint16_t model, std::span<const Render::Models::BoneTransform> bones,
                                  const Render::Models::ObjectTransform& transform, bool translated)
                          {
                              const auto published =
                                  PublishFaunaPoseRaw(FramePoseCarry(), *m_models, model, bones, transform, translated);
                              if (!published.HasValue() && !poseFailure)
                                  poseFailure = published.Error();
                          },
                          .publishProjectedPose = true,
                          .poseBones = [&](std::uint16_t)
                          { return std::span<const Render::Models::BoneTransform>(FramePoseCarry().bones); },
                          .worldModels = m_worldModelsMap ? &m_worldModels : nullptr,
                          .polygon = &polygon,
                          .fixedHeight = CameraState().fixedHeight});
    if (poseFailure)
        return Core::Unexpected(*poseFailure);
    return drawn;
}

FaunaDrawCarry& GameScene::FramePoseCarry()
{
    return m_framePoseCarry ? *m_framePoseCarry : m_context.world->PoseCarry();
}

World::FrameSprites& GameScene::FrameSpritesForDraw()
{
    return m_frameSprites ? *m_frameSprites : m_sprites;
}

void GameScene::Leave()
{
    if (m_context.world)
        m_context.world->FaunaResources().Bind(nullptr);
    m_tiles.Release();
    m_worldModels.Release();
    m_worldModelsMap.reset();
    m_windowTextures.Release();
    m_sandTextures.Release();
}

// Q-C70: sin conexión, el juego sigue; el texto 3 del área de mensajes es de la CL9.
std::optional<SceneId> GameScene::Next() const
{
    return std::nullopt;
}

// 0x62828e: el OK de los diálogos 0x70, 0x6f, 0x1a y 0x1c manda WM_DESTROY a la ventana.
bool GameScene::WantsToQuit() const
{
    return m_context.world != nullptr && m_context.world->Runtime().ui.destroyRequested;
}

// Mientras el campo del chat está abierto, uno de sus EDIT tiene el foco y recibe los WM_CHAR.
bool GameScene::WantsTextInput() const
{
    return m_context.world != nullptr && m_context.world->Runtime().ui.chatOpen;
}

bool GameScene::HoldsTheLastFrame() const
{
    return m_holdFrame;
}

std::optional<std::string> GameScene::Failure() const
{
    return m_failure;
}

const std::optional<World::TerrainPickingSegment>& GameScene::MouseRay() const
{
    return m_mouseRay;
}

World::GameCamera& GameScene::CameraState()
{
    return m_context.camera != nullptr ? *m_context.camera : m_localCamera;
}

World::GameCameraInput& GameScene::CameraInputState()
{
    return m_context.cameraInput != nullptr ? *m_context.cameraInput : m_localCameraInput;
}
} // namespace Client::Scenes
