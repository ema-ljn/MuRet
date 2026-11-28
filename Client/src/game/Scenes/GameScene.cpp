#include "Scenes/GameScene.h"
#include "Maps/MapId.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Models/IModelLibrary.h"
#include "Hud/MapBanner.h"
#include "CastleSiege/SiegeBattleUi.h"
#include "Sound/MonsterSounds.h"
#include "Assets/MonsterModelTable.h"
#include "Audio/Playback.h"
#include "Effects/ArrowEffects.h"
#include "CastleSiege/SiegeStoneEffects.h"
#include "CastleSiege/SiegeGateEffects.h"
#include "Scenes/GameSceneFrameProjection.h"
#include "Actors/ActorPickingBox.h"
#include "Tables/GateTable.h"
#include "Tables/SkillTable.h"
#include "Tables/TextTable.h"
#include "Math/HalfLifeMath.h"
#include "Models/Pose.h"
#include "Terrain/DynamicWorldLight.h"
#include "Events/EventGlobals.h"
#include "Events/EventPackets.h"
#include "Events/ObjectCollapse.h"
#include "Actors/Drawing/PosePublishing.h"
#include "Kernel/PoseBuffer.h"
#include "Items/InventoryMoves.h"
#include "FrontEnd/SelectionState.h"
#include "Effects/GameParticles.h"
#include "Kernel/DrawTailState.h"
#include "Joints/FaunaJointCreation.h"
#include "Fauna/FaunaEffects.h"
#include "Chat/GameChat.h"
#include "Items/GameFloorItems.h"
#include "Messenger/GameLetters.h"
#include "Actors/Drawing/GameItemRenderState.h"
#include "Actors/Drawing/GameItemSpecialDrawing.h"
#include "Chat/GameMessages.h"
#include "Hud/GamePanels.h"
#include "Scenes/GameProjection.h"
#include "Hud/GameUi.h"
#include "PersonalShop/PersonalShopTitles.h"
#include "MapScripts/Icarus/IcarusObjectStep.h"
#include "MapScripts/Kalima/KalimaObjectStep.h"
#include "Effects/GameModelEffects.h"

#include "Kernel/ModelEffectArguments.h"
#include "Session/ModuleStates.h"
#include "Session/ServerSession.h"
#include "Session/SessionState.h"
#include "Core/Expected.h"
#include "Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Models/ModelTextureSlots.h"
#include "Render/ModelBatch.h"
#include "Render/ModelFog.h"
#include "Render/Pass3D.h"
#include "Actors/ActorName.h"
#include "Kernel/ActorState.h"
#include "Events/Crywolf/CameraTour.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Actors/CharacterStep.h"
#include "Chat/ChatBubbles.h"
#include "Pets/DarkSpirit.h"
#include "Actors/Drawing/GameCharacters.h"
#include "Guild/GuildState.h"
#include "Pets/HelperTable.h"
#include "Effects/HighModelEffects.h"
#include "Assets/ModelTextureTable.h"
#include "Render/Frame/SceneCanvas.h"
#include "Assets/TextureSets.h"
#include "Scenes/SceneContext.h"
#include "Skills/SkillDelays.h"
#include "Kernel/SceneId.h"
#include "Hud/Slides.h"
#include "Camera/TerrainPolygonFrame.h"
#include "Assets/TerrainTiles.h"
#include "Kernel/WorldRuntime.h"
#include "Assets/WorldModels.h"
#include "WorldObjects/WorldObjectsDrawing.h"
#include "Effects/GroundEffectDrawing.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatInput.h"
#include "Chat/ChatLog.h"
#include "Messenger/FriendModel.h"
#include "Ui/GameCursor.h"
#include "Ui/GameUiState.h"
#include "Messenger/LetterModel.h"
#include "Ui/OldWindows.h"
#include "Ui/PanelRegistry.h"
#include "Ui/Widget.h"
#include "Camera/GameCamera.h"
#include "Terrain/GroundMarks.h"
#include "Kernel/KalimaWater.h"
#include "Terrain/SphereFrustum.h"
#include "Terrain/TerrainCells.h"
#include "Terrain/TerrainCulling.h"
#include "CastleSiege/CastleSiegeObjectStep.h"
#include "Events/ChaosCastle/ChaosCastle.h"
#include "MapScripts/Crywolf/CrywolfWater.h"
#include "Events/EventMapObjectStep.h"
#include "Terrain/TerrainHeight.h"
#include "MapScripts/Tarkan/TarkanObjectStep.h"
#include "Terrain/TerrainPickingSegment.h"
#include "Kernel/WorldObject.h"
#include "Kernel/ObjectPass.h"
#include "Terrain/WorldTerrain.h"
#include "Audio/SoundId.h"
#include "Kernel/JointRequest.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <bit>
#include <chrono>
#include <functional>
#include <cstdint>
#include <optional>
#include <string_view>
#include <ratio>
#include <span>
#include <string>
#include <vector>

namespace Client::Scenes
{
namespace
{
// 0x636838: the game camera's yaw on the first step, 0xc2340000.
constexpr float EntryYaw = -45.0f;
// 0x636dce: what remains of the camera shake (0x7d325c8) on each step, the 0.2 of 0x6b8854.
constexpr float ShakeDecay = std::bit_cast<float>(std::uint32_t{0x3e4ccccd});
// 0x635b3d and 0x63726f: on map 6 the camera and the trapezoid point compare the hero's name.
constexpr std::int32_t NamedCameraMap = 6;
// 0x6372e0..0x6372f1: the point of the "webzen" branch (0x45c80000 and 0x4670a000) with the hero's z.
constexpr float SpectatorTargetX = 6400.0f;
constexpr float SpectatorTargetY = 15400.0f;
// 0x5fc33e: map 7 has no grass pass (its water goes with 0x5fa4b0, Terrain::DrawTerrainCell).
constexpr std::int32_t AnimatedWaterMap = 7;
// 0x6374bb
constexpr std::int32_t WithoutTerrainMap = 10;
// 0x5f8928..0x5f8947: 0x6c9d58, the grass pass, stays at 0 on maps 18 to 23 and on 30.
constexpr std::int32_t FirstWithoutGrassMap = 18;
constexpr std::int32_t LastWithoutGrassMap = 23;
// 0x63741d: on map 30 the draw sets or removes the fog (0x42b0a0).
constexpr std::int32_t CastleMap = Maps::CastleSiegeMap;
// 0x42b0a0 with the color 0x63746b..0x63747a builds on the stack, (0, 0, 0): glFogf(GL_FOG_MODE, GL_LINEAR), from
// 2000 (0x44fa0000) to 2700 (0x4528c000).
constexpr Render::ModelFog CastleSiegeFog{.color = glm::vec3(0.0f), .start = 2000.0f, .end = 2700.0f};
// 0x633f20: the map kept during the entry wait.
constexpr std::int32_t SelectionMap = Maps::CharacterSelectMap;
// 0x425070: 33 shares with 54 the object cases of 0x4250f0.
using Maps::AidaMap;
// 0x43ff10: Land of Trials, with its cases at 0x43ffc0, 0x4401e0 and 0x4403b0.
using Maps::LandOfTrialsMap;
// 0x4303c0: Crywolf, with its cases at 0x4303e0, 0x430510 and 0x4306a0 and its water's bubbles (0x5fa695).
using Maps::CrywolfMap;
// The reference maps, the only ones with the object cases in the game (0x6046a0 and 0x604ab0).
using Maps::DeviasMap;
using Maps::DungeonMap;
using Maps::LorenciaMap;
using Maps::LostTowerMap;
using Maps::NoriaMap;
// The Arena (World7): its cases in 0x604ab0 (0x6054fd) and 0x602ed0 (0x6039cb).
using Maps::ArenaMap;
// Atlans (World8): its cases in 0x604ab0 (the table 0x605c94); 0x602ed0 and 0x6002e0 have none.
using Maps::AtlansMap;
using Maps::DevilSquareMap;
using Maps::FirstBloodCastleMap;
using Maps::IcarusMap;
using Maps::LastBloodCastleMap;
using Maps::TarkanMap;
// 0x620486..0x6204b0: the 640 × 480 mouse goes back to the screen with · W / 640 and · H / 480, in integers.
constexpr std::int64_t Width640 = 640;
constexpr std::int64_t Height480 = 480;
// 0x6ba070: what 0x637355 multiplies the world width by for 0x5fbb00's first argument.
constexpr float WorldWidthFactor = 0.0015625f;

using Math::F32;

std::string MapText(std::int32_t map)
{
    return "mapa " + std::to_string(map);
}

// 0x63741d..0x637466 and 0x6375ea..0x63763a: map 30 (0x6c9dd0) and the published hero (0x743a170) in
// CastleSiege::InTheCastleSiegeFog's rectangle. Each read rereads both.
bool HeroInTheCastleSiegeFog(const Kernel::WorldState& world)
{
    return world.Runtime().map == CastleMap && CastleSiege::InTheCastleSiegeFog(world.Hero().position);
}

// 0x5f8928..0x5f8947: 0x6c9d58, which 0x5f87b0 turns on outside maps 18 to 23 and 30.
bool HasTheGrassPass(std::int32_t map)
{
    return !(map >= FirstWithoutGrassMap && map <= LastWithoutGrassMap) && map != CastleMap;
}

// 0x635c54..0x635d7f: the hero's server cell through the protection table, Y (+0x420) and then X (+0x41c), each in
// an access that closes (0x412890), and the attribute of 0x5f8290(X, Y).
std::uint8_t HeroCellAttribute(Kernel::ActorState& hero, const Data::Terrain::TerrainAttributes& attributes,
                               Crt::CrtRandom& random)
{
    const Actors::LogicalCell cell = Actors::ReadLogicalCell(hero, random);
    // 0x635f14..0x635fe3: the word of 0x7d0d348, whose low byte the camera looks at.
    return static_cast<std::uint8_t>(attributes.cells[Data::Terrain::CellIndex(cell.x, cell.y)]);
}
} // namespace

GameScene::GameScene(const SceneContext& context, App::ServerSession* session, const Models::IModelLibrary* models,
                     const Tables::ItemTable* items, const Models::ModelTextureSlots* gameSlots,
                     const Tables::GateTable* gates, const Tables::SkillTable* skills, const Tables::TextTable* texts,
                     GameChatTables chat)
    : m_context(context), m_session(session), m_models(models), m_items(items), m_gameSlots(gameSlots), m_gates(gates),
      m_skills(skills), m_texts(texts), m_chatTables(chat),
      m_windowTextures(context.textures, Assets::WindowTextureSet),
      m_sandTextures(context.textures, Assets::SandTextureSet), m_guildMarks(context.textures),
      m_characterDrawHooks(context.states != nullptr ? &context.states->monsters : nullptr), m_tiles(context.textures)
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
    if (m_context.states == nullptr)
    {
        return Core::Unexpected(std::string("the game does not enter without the modules' states"));
    }
    m_firstStep = true;
    m_holdFrame = true;
    // Outside scene 5, 0x4eb620 does not lower the waits. Its first frame in 5 takes the ms since the previous scene's
    // last (0x4eb746 writes 0x5767b7c in all); MuRet starts with 0 (07, Q-C1238).
    m_frameStarts = true;
    m_lastFrameStart.reset();
    m_groundEffects.Reset();
    auto& textureTable = LoadedFaunaModels().TextureTable();
    textureTable.Bind(m_context.textures);
    if (m_gameSlots != nullptr)
    {
        textureTable.Seed(*m_gameSlots, m_models);
    }
    m_tiles.BindTextureTable(textureTable);
    // The original uploads them in the load (0x61ccb0) and in the login (0x61c7c0) and does not release them; MuRet,
    // with each scene.
    if (auto loaded = m_windowTextures.Load(m_context.dataRoot); !loaded.HasValue())
        return loaded;
    return m_sandTextures.Load(m_context.dataRoot);
}

// 0x636290 and, then, 0x637700's part for the cloth: the wind (0x637cec) and the step counter (0x637f8c) go after the
// scene's logic (02, "Qué corre en cada paso"). A step cut leaves the scene in Failure without the cloth; after a
// cut, of the step or the draw, nothing.
void GameScene::Step(const Input::InputState& input)
{
    m_projectingFrame = false;
    if (m_failure)
    {
        return;
    }
    // 0x4eb620, at the start of an original frame and before its steps: in scene 5, 0x552f10 with the ms of
    // timeGetTime() since the previous frame (no cap: 02, "Lo que va por reloj"). The scene's first one, with 0
    // (Q-C1238); the frames the wait holds do not start one, and the next takes all its time (P-C267).
    if (m_frameStarts && m_context.world != nullptr && m_context.cloth != nullptr)
    {
        m_frameStarts = false;
        const std::uint32_t now =
            m_context.clock != nullptr
                ? static_cast<std::uint32_t>(
                      std::chrono::duration_cast<std::chrono::milliseconds>(m_context.clock->Now()).count())
                : 0U;
        const auto elapsed = static_cast<std::int32_t>(now - m_lastFrameStart.value_or(now));
        m_lastFrameStart = now;
        if (auto counted = Skills::CountDownSkillDelays(
                elapsed, {.world = *m_context.world, .random = m_context.cloth->random, .skills = m_skills});
            !counted.HasValue())
        {
            m_failure = counted.Error();
            return;
        }
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
        Models::StepTheClothWorld(*m_context.cloth);
    }
    // What the step's windows left for the chat rooms with a friend (Enter in a chat, the chats that were released,
    // also 0x48ea50's): the original sends it at once, through each room's connection.
    if (m_context.world != nullptr)
        Messenger::FlushTheChatRoomOrders(m_context.world->Runtime().oldWindows.chatRooms, m_session);
}

void GameScene::StepEntry()
{
    // 0x636838: only the first step of scene 5 writes the yaw, without resetting the other globals.
    if (m_firstStep)
    {
        if (m_context.camera != nullptr)
            m_context.camera->yaw = EntryYaw;
        m_firstStep = false;
        // 0x636843..0x6368f4: the text fields (0x553570(1)), the chat field closed (0x6c8e68), 0x77f3b69 at 0, the two
        // Tab fields (0x6c8e6c; 0x6c8e70, 0x6c8e78 and 0x6c8e7c belong to the input without EDIT), 120 empty entries,
        // the login's EDIT empty and hidden (0x469420 and 0x469450(3), which MuRet does not have in the game), the chat
        // input (0x469320), the focus to the game window, the record empty (the vtable + 0x2c, 0x478d10) and the filter
        // too (0x45db50).
        if (m_context.world != nullptr)
        {
            Kernel::WorldRuntime& runtime = m_context.world->Runtime();
            Chat::ChatInputState& input = runtime.chat.input;
            Chat::ResetTheTextBuffers(input, true);
            runtime.ui.chatOpen = false;
            input.otherInput = false;
            input.fieldCount = Chat::ChatFieldCount;
            if (auto emptied = Chat::AddTheEmptyMessages(runtime, m_context.textMeasure); !emptied.HasValue())
            {
                m_failure = emptied.Error();
                return;
            }
            Chat::ResetTheChatInput(input, &runtime.ui.focusedList);
            input.focus = Chat::ChatFocus::Game;
            Chat::ClearTheLog(runtime.chat.log, runtime.ui.chatModes);
            Chat::ClearTheChatFilter(runtime.chat.log);
            // 0x6368f9..0x636915: the old windows (0x48ea50), the friends (0x49bac0) and the letters (0x49f260: the
            // list and, with 0x4a0560, the copies of 0x4a03c0).
            Ui::ResetTheOldWindows(runtime.oldWindows, runtime.ui.pressedButton);
            Messenger::ClearTheFriends(runtime.oldWindows.friendModel);
            Messenger::ClearTheLetters(runtime.oldWindows.letterModel);
            // 0x636920: 0x46b010, the queues of the Slide.bmd banner, its timer 0x3eb and 0x46b0e0 (the own record and
            // a Slide.bmd text, with their rand() calls).
            const auto now =
                m_context.clock != nullptr
                    ? static_cast<std::uint32_t>(
                          std::chrono::duration_cast<std::chrono::milliseconds>(m_context.clock->Now()).count())
                    : 0U;
            if (m_context.cloth != nullptr)
                Hud::StartTheSlides(*m_context.world, m_context.cloth->random, now);
            // 0x636925..0x63692b: 0x65dc00, the map name banner off and without a map.
            Hud::ResetTheMapBanner(runtime.hud.mapBanner, now);
            // 0x636935: the guild table (0x447ae0). F1 00 (0x4c1e54) and the character list (0x4c1f14) also empty it,
            // but before there is a world: entering the game leaves the same.
            Guild::ResetTheGuildTable(runtime.guild);
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
    // Enter does not allow entering without the three.
    if (m_context.world == nullptr || m_context.cloth == nullptr || m_context.states == nullptr)
    {
        return {};
    }
    ObserveMapLoad();
    Kernel::WorldRuntime& runtime = m_context.world->Runtime();
    // 0x636da3: state 0x3d marks the world loaded; without it and with the world not loaded, the step ends here
    // (0x636dbc).
    if (m_session != nullptr && m_session->State() == Session::SessionState::InWorld)
    {
        runtime.drawingEnabled = true;
    }
    else if (!runtime.drawingEnabled)
    {
        return {};
    }
    // 0x636dc2..0x636ddb: the camera shake shrinks on each step; the x87 multiplies and stores to float, like the exact
    // product in double rounded once.
    Camera::GameCameraInput& cameraInput = CameraInputState();
    cameraInput.shake = F32(static_cast<double>(cameraInput.shake) * static_cast<double>(ShakeDecay));
    // 0x636dd4..0x636de3: only with the initial marker 0x43 does it look for the first siege skill.
    if (m_context.world->Hero().byte371 == 0x43)
        CastleSiege::InitializeTheBattleSkill(*m_context.world, m_context.cloth->random);
    // 0x636de8: 0x5fbf60, the wave of the cells of the last draw's range window, with the step's clock (0x723c50's
    // branch, P-C24 c, does not turn on in MuRet). Copying the shaded light to the dynamic buffer comes before the
    // object and fauna lights (06 § Terreno). The range and the waves belong to the scene: the original shares them
    // with the selection (08 P-C41).
    const Terrain::WorldTerrain* stepTerrain = &MapTerrain();
    if (runtime.map == SelectionMap && m_context.selection != nullptr && m_context.selection->Terrain() != nullptr)
    {
        stepTerrain = m_context.selection->Terrain();
    }
    if (stepTerrain->lighting != nullptr)
    {
        DynamicLight().Update(*stepTerrain->lighting);
    }
    if (m_context.clock != nullptr)
    {
        Terrain::UpdateWaves(m_water, m_context.clock->Now(), DynamicLight().Range(),
                             static_cast<std::uint8_t>(runtime.map));
    }
    // 0x636ded..0x63705b: the step's UI, which runs on every step from the first, also during the wait.
    if (auto ui = StepTheUi(input); !ui.HasValue())
        return ui;
    m_context.world->Fauna().ClearSounds();
    // 0x63706a: 0x606560, the objects the last draw saw (0x6046a0), also during the wait.
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
    // 0x6370f4: 0x445230, the helpers, also during the wait.
    if (auto helpers = Pets::StepHelpers(Pets::HelperStepContext{.world = *m_context.world,
                                                                 .models = m_models,
                                                                 .random = m_context.cloth->random,
                                                                 .worldClock = worldClock,
                                                                 .scene = Kernel::SceneId::Game,
                                                                 .terrain = &MapTerrain(),
                                                                 .logicSteps = m_context.cloth->logicSteps,
                                                                 .cameraShake = &CameraInputState().shake});
        !helpers.HasValue())
    {
        return helpers;
    }
    // 0x6370f9: 0x557650, the bubbles.
    const Ui::Point bubbleMouse = Ui::MouseIn640x480(input, m_screen);
    if (auto bubbles = Chat::StepTheChatBubbles(
            *m_context.world,
            Chat::ChatBubbleStepInput{.mouseX = bubbleMouse.x, .mouseY = bubbleMouse.y, .screen = m_screen});
        !bubbles.HasValue())
        return bubbles;
    // 0x6370fe..0x637105: 0x451c10, the personal shop signs (0x7245c8) that changed key or type.
    PersonalShop::CheckTheShopKeys(*m_context.world);
    // 0x63710a: MoveHero leaves with more than 30, but the later logic goes on during the wait.
    if (auto moved = StepTheHero(input); !moved.HasValue())
        return moved;
    // 0x63710f: characters on the last draw's retained trapezoid.
    if (auto stepped = StepTheCharacters(worldClock, input.IsDown(Input::Key::Ctrl)); !stepped.HasValue())
        return stepped;
    if (auto effects = StepTheEffects(worldClock); !effects.HasValue())
        return effects;
    // 0x63717f: 0x5ac8d0, the function keys.
    const Ui::Point mouse = Ui::MouseIn640x480(input, m_screen);
    const Chat::GameChatContext chat = ChatContext();
    // CL9 milestone 5: the bar picks the item up from the floor with C1 05 22 (InventoryContext).
    const Items::InventoryServices inventory = InventoryContext();
    return Hud::StepTheFunctionKeys(Hud::GameUiContext{.world = *m_context.world,
                                                       .random = m_context.cloth->random,
                                                       .input = input,
                                                       .mouseX = mouse.x,
                                                       .mouseY = mouse.y,
                                                       .measure = m_context.textMeasure,
                                                       .screen = m_screen,
                                                       .chat = &chat,
                                                       .inventory = &inventory,
                                                       .fixedHeight = CameraState().fixedHeight,
                                                       .worldEntry = m_context.worldEntry,
                                                       .terrain = &MapTerrain()});
}

Core::Expected<void, std::string> GameScene::StepTheObjects()
{
    Kernel::WorldState& world = *m_context.world;
    const std::int32_t map = world.Runtime().map;
    std::span<Kernel::WorldObject> objects = world.Map().objects;
    if (Maps::IsKalimaMap(map))
    {
        // 0x606572 before the early return and the 0x60657a reset, even without a seen object.
        const auto selector = MapScripts::Kalima::StepKalimaObjectPrelude(world, *m_context.cloth);
        if (!selector.HasValue())
            return Core::Unexpected(selector.Error());
        world.Runtime().objectSelectionCount = 0;
        // 0x6063d0: not Icarus; 0x4055b0 and 0x42c500 return AL 0 for Kalima, without using the selector.
    }
    // 0x405490, first in 0x6061c0: the Chaos Castles, also before the early return and 0x60657a, with the previous
    // pass's count. The selector stays for 0x4055b0.
    const bool chaosCastle = Maps::IsChaosCastle(map);
    std::int32_t chaosCastleSelector = 0;
    if (chaosCastle)
    {
        const Kernel::ActorState& hero = world.Hero();
        const Events::ChaosCastle::ChaosCastlePreludeContext prelude{
            .random = &m_context.cloth->random,
            .heroPosition = hero.position,
            .heroAngles = hero.angles,
            .heroInSafeZone = hero.safeZone != 0,
            .createJoint = [&world, this](const Kernel::JointRequest& request) -> Core::Expected<void, std::string>
            {
                const auto created =
                    Joints::CreateFaunaJoint(world.DrawTail().joints, request, m_context.cloth->random);
                if (!created.HasValue())
                    return Core::Unexpected(created.Error());
                return {};
            }};
        const auto selector =
            Events::ChaosCastle::StepChaosCastlePrelude(world.Runtime().objectSelectionCount, prelude);
        if (!selector.HasValue())
            return Core::Unexpected(selector.Error() + ", en el " + MapText(map));
        chaosCastleSelector = selector.Value();
        world.Runtime().objectSelectionCount = 0;
    }
    // 0x6061c0 in Icarus, before the early return and 0x60657a, with the previous pass's count.
    std::optional<MapScripts::Icarus::IcarusObjectServices> icarus;
    std::int32_t icarusSelector = 0;
    if (map == IcarusMap)
    {
        const Kernel::ActorState& hero = world.Hero();
        icarus.emplace(MapScripts::Icarus::IcarusObjectServices{
            .random = m_context.cloth->random,
            .heroPosition = hero.position,
            .lighting = &DynamicLight().Lighting(),
            .effects = &world.Map().effects,
            .sprites = &m_sprites,
            .createJoint = [&world, this](const Kernel::JointRequest& request) -> Core::Expected<void, std::string>
            {
                const auto created =
                    Joints::CreateFaunaJoint(world.DrawTail().joints, request, m_context.cloth->random);
                if (!created.HasValue())
                    return Core::Unexpected(created.Error());
                return {};
            }});
        const auto selector =
            MapScripts::Icarus::StepIcarusObjectPrelude(world.Runtime().objectSelectionCount, *icarus);
        if (!selector.HasValue())
            return Core::Unexpected(selector.Error());
        icarusSelector = selector.Value();
        world.Runtime().objectSelectionCount = 0;
    }
    // 0x6062e6: the Blood Castles, also before the early return, with the step's clock.
    const float worldClock =
        m_context.clock != nullptr
            ? static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(m_context.clock->Now()).count())
            : 0.0f;
    const bool bloodCastle = map >= FirstBloodCastleMap && map <= LastBloodCastleMap;
    if (bloodCastle)
    {
        const Kernel::ActorState& hero = world.Hero();
        if (auto orb =
                Events::StepBloodCastlePrelude(m_context.cloth->random, hero.position, &world.Particles(), worldClock);
            !orb.HasValue())
            return orb;
    }
    // 0x42b540, the third link of 0x6061c0: the 30, also before the early return and 0x60657a.
    if (map == CastleMap)
    {
        // timeGetTime (0x6b8544) is the scene clock in ms; without a clock, the prelude stops at 0x42b557.
        CastleSiege::CastleSiegePrelude prelude{
            .siegeActive = world.Runtime().castleSiege.siegeActive,
            .musicTime = &world.Runtime().castleSiege.siegeMusicTime,
            .sound = [&world](Audio::SoundId id) { world.Runtime().RequestSound(id); },
            .random = &m_context.cloth->random,
            .logicSteps = m_context.cloth->logicSteps,
            .heroPosition = world.Hero().position,
            .heroY =
                [&world, this]()
            {
                auto& actor = world.Hero();
                const Models::LogicalAccess access(actor.cellYAccesses, m_context.cloth->random,
                                                   Models::SmallLogicalCloseRandoms, actor.cellYLeft);
                return static_cast<std::int32_t>(actor.cellY);
            },
            .emit = [&world, this](const Kernel::ModelEffectArguments& args) -> Core::Expected<void, std::string>
            {
                if (CastleSiege::IsSiegeStoneEffect(args.type))
                {
                    const auto created = CastleSiege::CreateSiegeStone(args, world.Map().effects, world.Hero().position,
                                                                       m_context.cloth->random);
                    if (!created.HasValue())
                        return Core::Unexpected(created.Error());
                    return {};
                }
                return Effects::CreateTheArrowEffect(args, world, m_context.cloth->random);
            },
            .updateMusic =
                [&world](bool siegeActive)
            {
                auto& runtime = world.Runtime();
                constexpr std::string_view castle = "Music\\castle.mp3";
                constexpr std::string_view charge = "Music\\charge.mp3";
                runtime.music.Stop(runtime.audio, siegeActive ? castle : charge);
                runtime.music.Play(runtime.audio, siegeActive ? charge : castle);
            }};
        if (m_context.clock != nullptr)
            prelude.systemTime = [clock = m_context.clock]() {
                return static_cast<std::uint32_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(clock->Now()).count());
            };
        if (auto stepped = CastleSiege::StepCastleSiegePrelude(prelude); !stepped.HasValue())
            return stepped;
        world.Runtime().objectSelectionCount = 0;
    }
    // 0x6065bd and 0x6065da: the collapse (0x5ff960, Events/ObjectCollapse.h) with each living one of the walk, the
    // seen ones of a sector that passed after its step and all of one that did not pass; the Chaos Castle stone comes
    // out with the hero's z and the shake is the camera's (0x7d325c8).
    const Events::ObjectCollapseContext collapseContext{.runtime = world.Runtime(),
                                                        .random = m_context.cloth->random,
                                                        .particles = &world.Particles(),
                                                        .worldClock = worldClock,
                                                        .heroZ = world.Hero().position.z,
                                                        .cameraShake = &CameraInputState().shake,
                                                        .attributes = MapTerrain().attributes.get()};
    const WorldObjects::AfterObjectStep collapse = [&collapseContext](Kernel::WorldObject& object)
    { return Events::CollapseTheObject(object, collapseContext); };
    // Without a seen object (before the first draw that reaches 0x6046a0), 0x606560 steps none of them: the collapse
    // with the living ones and the count remain (0x606610).
    if (std::ranges::none_of(objects, [](const Kernel::WorldObject& object) { return object.alive && object.seen; }))
    {
        if (auto walked = WorldObjects::WalkTheObjectSectors(objects, {}, collapse); !walked.HasValue())
            return Core::Unexpected(walked.Error() + ", en el " + MapText(map));
        Events::CountDownTheObjectCollapse(world.Runtime());
        return {};
    }
    // 0x4250f0 carries those of 33 and 54 (the selection's, seen in its draw, which go on during the wait for the game
    // entry).
    const bool aida = map == AidaMap || map == SelectionMap;
    if (map != LorenciaMap && map != DungeonMap && map != DeviasMap && map != NoriaMap && map != LostTowerMap &&
        map != ArenaMap && map != AtlansMap && map != TarkanMap && map != DevilSquareMap && map != IcarusMap &&
        map != LandOfTrialsMap && map != CastleMap && map != CrywolfMap && !Maps::IsKalimaMap(map) && !aida &&
        !bloodCastle && !chaosCastle)
    {
        return Core::Unexpected("0x606560: los objetos vistos del " + MapText(map) + " (0x604ab0) no están portados");
    }
    if (auto loaded = LoadTheObjectModels(map); !loaded.HasValue())
    {
        return loaded;
    }
    // 0x604ab0 reads the hero's cell layer and the world clock (0x5767b88), the step's; 1 and 4 create effects with a
    // model and 4's kick looks at the hero's action.
    const Kernel::ActorState& hero = world.Hero();
    const Kernel::ObjectStepGlobals globals{
        .heroCellLayer = world.Runtime().heroCellLayer,
        .worldClock = worldClock,
        .dynamicLighting = &DynamicLight().Lighting(),
        .particles = &world.Particles(),
        .heroPosition = &hero.position,
        .sound = [&](Audio::SoundId id) { world.Runtime().RequestSound(id); },
        // The 9's ambience sounds in a loop (0x41ed10(1, 0, 1)).
        .loopedSound = [&](Audio::SoundId id) { world.Runtime().RequestSound(id, std::nullopt, true); },
        .modelState = &world.Runtime().objectModelState,
        .effects = &world.Map().effects,
        .ownRecordDepth = &world.Runtime().ownRecordDepth,
        .heroAction = &hero.animation.action,
        // Q-C304: the 31 reads timeGetTime (0x6b8544) in the step; MuRet lends it the same clock in ms.
        .systemTime =
            [this]()
        {
            return m_context.clock != nullptr
                       ? static_cast<std::uint32_t>(
                             std::chrono::duration_cast<std::chrono::milliseconds>(m_context.clock->Now()).count())
                       : 0U;
        },
        .trialsAmbientTime = &world.Runtime().trialsAmbientTime,
        .siegeActive = &world.Runtime().castleSiege.siegeActive,
        .siegeCrownFlags = &world.Runtime().castleSiege.castleFlags7239e4};
    WorldObjects::AfterObjectStep after;
    if (icarus)
        after = [&](Kernel::WorldObject& object)
        {
            return MapScripts::Icarus::StepIcarusSeenObject(object, icarusSelector,
                                                            world.Runtime().objectSelectionCount, *icarus);
        };
    if (chaosCastle)
        after = [&](Kernel::WorldObject& object) -> Core::Expected<void, std::string>
        {
            Events::ChaosCastle::StepChaosCastleSeenObject(object, chaosCastleSelector,
                                                           world.Runtime().objectSelectionCount,
                                                           world.Runtime().chaosCastleTrapArmed);
            return {};
        };
    // 0x6063d0 → 0x42c500 on 30, with the step's clock.
    if (map == CastleMap)
        after = [&](Kernel::WorldObject& object)
        {
            return CastleSiege::StepCastleSiegeSeenObject(
                object, world.Runtime().castleSiege.siegeActive, worldClock,
                CastleSiege::CastleGateCollapseServices{
                    .emit = [&world](const Kernel::ModelEffectArguments& arguments) -> Core::Expected<void, std::string>
                    {
                        const auto created = CastleSiege::CreateCastleGateEffect(arguments, world.Map().effects);
                        if (!created.HasValue())
                            return Core::Unexpected(created.Error());
                        return {};
                    },
                    .sound = [&world](Audio::SoundId sound) { world.Runtime().RequestSound(sound); }});
        };
    if (auto stepped = WorldObjects::StepGameWorldObjects(objects, static_cast<std::uint8_t>(map), m_worldModels,
                                                          m_context.cloth->random, globals, after, collapse);
        !stepped.HasValue())
    {
        return Core::Unexpected(stepped.Error() + ", en el " + MapText(map));
    }
    // 0x606610..0x606618: the collapse count, at the end of each step.
    Events::CountDownTheObjectCollapse(world.Runtime());
    return {};
}

void GameScene::PrepareFrame()
{
    m_holdFrame = true;
    if (m_context.world == nullptr || m_context.cloth == nullptr || m_context.states == nullptr)
    {
        return;
    }
    ObserveMapLoad();
    Kernel::WorldRuntime& runtime = m_context.world->Runtime();
    // 0x6371a6: without the world loaded it returns 0 without reading the counter.
    if (!runtime.drawingEnabled)
    {
        return;
    }
    std::int32_t counter = 0;
    {
        // 0x6371b7..0x637229: it reads it with an access that closes (0x40b800) even if it then does not draw.
        const Models::LogicalAccess access(runtime.warpDepth, m_context.cloth->random,
                                           Models::SmallLogicalCloseRandoms);
        counter = runtime.warpCounter;
    }
    // 0x63722e: cmp esi, 0x1e and jle, signed.
    m_holdFrame = counter > Kernel::WaitDrawThreshold;
    if (m_holdFrame)
    {
        return;
    }
    // P-C40: the preventive invalidation is kept while the original frame has unported stages and branches. The player
    // that reaches 0x50c91e then publishes its real scan; if the draw stops before, the value stays unknown. It does
    // not represent a write of the original in PrepareFrame. Only before an original frame: an intermediate draw does
    // not reach 0x510d00 or publish (ScanTheActor), and [0x743a178] and [0x743a17c] keep the last frame's. If it
    // cleared them, the C (0x65ca1e → 0x550d10) would stop at 0x550990 after almost any draw above 21.33 FPS.
    if (m_cameraFrames.NextDrawIsAnOriginalFrame(OriginalFramePaceOf(m_context.pacing)))
    {
        runtime.fullSetScan = std::nullopt;
    }
}

void GameScene::Render(Render::Frame::SceneCanvas& canvas, float /*alpha*/)
{
    if (m_failure || m_holdFrame || m_context.world == nullptr || m_context.cloth == nullptr ||
        m_context.states == nullptr)
    {
        return;
    }
    const auto drawn = DrawTheFrame(canvas);
    if (!drawn.HasValue())
    {
        m_failure = drawn.Error();
    }
}

Core::Expected<void, std::string> GameScene::DrawTheFrame(Render::Frame::SceneCanvas& canvas)
{
    Kernel::WorldRuntime& runtime = m_context.world->Runtime();
    const Terrain::WorldTerrain& terrain = MapTerrain();
    const int frames = m_cameraFrames.FramesAtDraw(OriginalFramePaceOf(m_context.pacing));
    if (frames > 0)
        m_frameStarts = true;
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
        // The original publishes at 0x639482, in the orchestrator, before the scene dispatch. An intermediate frame
        // only queries its clock's value on the projected copy.
        const float worldClock =
            static_cast<float>(std::chrono::duration<double, std::milli>(canvas.frameClock).count());
        Actors::Drawing::UpdateGameItemGlowPulse(FrameItemRender(), worldClock);
    }
    if (!terrain.mapping || !terrain.heights || !terrain.attributes || !terrain.lighting)
    {
        return Core::Unexpected("0x6371a0: el " + MapText(runtime.map) + " no está cargado (0x61a560)");
    }
    if (auto uploaded = UploadTheTiles(static_cast<std::uint8_t>(runtime.map)); !uploaded.HasValue())
    {
        return uploaded;
    }
    // 0x63723a: 0x7d368a0, 0x620db0's fog, at 0: Begin turns it off on each pass (ModelBatch::Begin). 0x637241:
    // 0x635ae0.
    const Render::Camera3D camera = CameraOfTheFrame(canvas, frames);
    m_drawnCamera = camera;
    m_screen = canvas.screen;
    // 0x63726f..0x637337: on map 6 with "webzen" in the name (0x576960; the "webzen2" search changes nothing, Q-C152),
    // the fixed point (6400, 15400) with the hero's z; otherwise, 0x44dc60's and, with the camera tour running
    // (0x637324, 0x4033d0), its position (0x4034b0), like this frame's camera.
    const Camera::GameCameraInput& cameraInput = CameraInputState();
    const glm::vec3 target = runtime.map == NamedCameraMap && cameraInput.webzen
                                 ? glm::vec3(SpectatorTargetX, SpectatorTargetY, cameraInput.hero.z)
                                 : Camera::FollowedPointOf(cameraInput);
    // 0x63733e: 0x5bb8d0, the world width, with the frame's rand() calls (the trade and shop queries with the inventory
    // open): 640 without windows, 450 with one and 260 with two.
    const std::int32_t worldWidth = Hud::GamePanels(*m_context.world, FrameCloth().random).Registry().WorldWidth();
    // 0x63734d: the 0x620db0(0, 0, w, 48) pass does not draw; its modelview is the world pass's.
    const glm::mat4 view = Render::MakePass3D(canvas.screen, GameWorldViewport, camera).view;
    // 0x637352..0x637366: 0x5fbb00(w · 0.0015625f, &p), the five planes, the block range and the trapezoid; with w at
    // 640, 1. The trapezoid and its level stay in WorldRuntime's globals, which 0x5fba90 reads on the next step.
    const float widthFactor = F32(static_cast<double>(worldWidth) * static_cast<double>(WorldWidthFactor));
    m_frustum = Terrain::SphereFrustumOf(view, camera, widthFactor);
    m_terrainRange = Terrain::TerrainRangeOf(view, camera, widthFactor);
    if (frames > 0)
        DynamicLight().PublishRange(m_terrainRange);
    const auto polygon = PolygonOfTheFrame(camera, target, worldWidth, frames);
    if (!polygon.HasValue())
    {
        return Core::Unexpected(polygon.Error());
    }
    // 0x63738a: 0x5bb8d0 again, with its rand() calls, for the world pass width.
    const std::int32_t passWidth = Hud::GamePanels(*m_context.world, FrameCloth().random).Registry().WorldWidth();
    // 0x637391..0x637404: the map's glClearColor, which nobody uses (Q-C143). 0x637410: the world pass,
    // 0x620db0(0, 0, w, 432), with its viewport's aspect.
    const Render::Pass3D pass =
        Render::MakePass3D(canvas.screen, Render::ViewportRect{0, 0, passWidth, GameWorldViewport.height}, camera);
    canvas.models.Begin(pass);
    auto drawn = DrawTheWorldPass(canvas, pass, polygon.Value(), frames);
    canvas.flushModels();
    if (!drawn.HasValue())
        return drawn;
    // 0x6203c0 of the world pass: the projection the HUD bubbles read (0x5a9050 → 0x6205b0). Without it, it stops only
    // if there is a live bubble.
    const auto projection = MakeGameProjection(pass, camera.fieldOfView);
    return DrawTheInterface(canvas, projection.HasValue() ? &projection.Value() : nullptr);
}

void GameScene::ObserveMapLoad()
{
    const auto revision = m_context.world->Map().loadRevision;
    if (m_loadedRevision == revision)
        return;
    // The same map number can also have another load, even after a failure. These owners are MuRet caches: not writes
    // or counters of the original.
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
    if (auto loaded = m_tiles.Load(m_context.dataRoot, map, skipped, Assets::GrassTextures::Upload); !loaded.HasValue())
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
    // The load recreates durable BMDs, even if a projected draw asked for it.
    m_context.world->Runtime().objectModelState.unlitMeshes.clear();
    if (m_projectingFrame)
        FrameModelState().unlitMeshes.clear();
    if (auto loaded = m_worldModels.Load(m_context.dataRoot, static_cast<std::uint8_t>(map), m_context.textures,
                                         m_context.log, m_gameSlots, m_models, &LoadedFaunaModels().TextureTable());
        !loaded.HasValue())
    {
        return Core::Unexpected("0x616ec0: los modelos de los objetos del " + MapText(map) + ": " + loaded.Error());
    }
    // 0x42aa6b: the 30's spearman (0x612d90(0x4d)) reserves its .wav before publishing the table (0x6151fc, 0x61520f
    // and 0x615232), if it loaded with meshes.
    constexpr std::int32_t SpearmanMonster = 0x4d;
    if (const auto* spearman = m_worldModels.Find(Models::ModelId{Assets::SpearmanModel});
        spearman != nullptr && !spearman->meshes.empty())
    {
        if (Audio::IPlayback* audio = m_context.world->Runtime().audio)
            for (const Assets::MonsterModelOp& op : Assets::MonsterModelCaseOf(SpearmanMonster).ops)
                if (op.kind == Assets::MonsterModelOpKind::Sound)
                    audio->RegisterSound(static_cast<std::int32_t>(op.value), op.path);
        Sound::WriteMonsterSounds(SpearmanMonster, m_context.world->Runtime().modelSounds);
    }
    m_worldModelsMap = map;
    MapScripts::Tarkan::SeedTarkanModelState(static_cast<std::uint8_t>(map),
                                             m_context.world->Runtime().objectModelState);
    if (m_projectingFrame)
        MapScripts::Tarkan::SeedTarkanModelState(static_cast<std::uint8_t>(map), FrameModelState());
    LoadedFaunaModels().Bind(&m_worldModels);
    return {};
}

Render::Camera3D GameScene::CameraOfTheFrame(Render::Frame::SceneCanvas& canvas, int frames)
{
    Kernel::WorldState& world = *m_context.world;
    const Kernel::WorldRuntime& runtime = world.Runtime();
    Kernel::ActorState& hero = world.Hero();
    Camera::GameCameraInput& input = CameraInputState();
    // What 0x635ae0 reads from the globals: the published hero (0x743a170), the world (0x6c9dd0) and the world clock
    // (0x5767b88, in ms, for map 5).
    input.hero = hero.position;
    input.map = static_cast<std::uint8_t>(runtime.map);
    input.scene = Terrain::CullingScene::Game;
    // 0x635b3d: the hero's name, with 0x576960 (Q-C152).
    input.webzen = Actors::ActorNameContains(hero, "webzen");
    input.worldTime =
        static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(canvas.frameClock).count());
    // 0x635ead and 0x637324: 0x4033d0 on [0x6e5904] and, if true, 0x4034b0. Nothing between the camera and the world
    // center touches the tour: both read it the same.
    const Events::CameraTour& tour = runtime.events.cameraTour;
    input.tour = Events::Crywolf::CameraTourRunning(tour)
                     ? std::optional(Camera::GameCameraTour{tour.position, tour.level})
                     : std::nullopt;
    if (frames == 0)
    {
        // An intermediate MuRet draw: it queries the last original frame's camera, without spending rand().
        return Camera::GameCameraOf(CameraState(), input);
    }
    // Each original frame ends at the cut below: of those the clock counts, the first runs and the cut does not let the
    // others run.
    // 0x635b3d..0x635b9e: the map 6 branch with "webzen" reads neither the hero's cell (nor its rand() calls) nor the
    // tour.
    if (!(input.map == NamedCameraMap && input.webzen))
    {
        input.cellAttribute = HeroCellAttribute(hero, *MapTerrain().attributes, m_context.cloth->random);
        // 0x635ec0: with the tour running, the hero's z at 300; it stays so for the rest of the frame (the hero's draw)
        // until a step writes it again.
        if (input.tour)
            input.hero.z = hero.position.z = Camera::CameraTourHeroHeight;
    }
    return Camera::StepGameCamera(CameraState(), input);
}

Core::Expected<Terrain::TerrainPolygon, std::string> GameScene::PolygonOfTheFrame(const Render::Camera3D& camera,
                                                                                  const glm::vec3& target,
                                                                                  std::int32_t worldWidth, int frames)
{
    const Kernel::WorldRuntime& runtime = m_context.world->Runtime();
    Terrain::TerrainPolygonInput input;
    input.scene = static_cast<std::int32_t>(Terrain::CullingScene::Game);
    input.map = runtime.map;
    input.translation = target;
    // The map 30 selector, like TerrainPolygonOf.
    input.objectPosition = glm::vec2(target.x, target.y);
    input.cameraYaw = camera.angles.z;
    input.worldWidth = worldWidth;
    // 0x5fb570: the two searches for the map 6 name, the measures' and the turn's.
    input.webzenProfile = input.webzenYaw = CameraInputState().webzen;
    if (frames == 0)
    {
        return Camera::TerrainPolygonBetweenFrames(m_context.states->camera, input);
    }
    // The requested level (0x7d3a0ec), the D (0x7d36ba4) and the goal (0x6d5ee0) the frame's camera left.
    return Camera::StepTheTerrainPolygon(m_context.states->camera, input, CameraState());
}

Core::Expected<void, std::string> GameScene::DrawTheWorldPass(Render::Frame::SceneCanvas& canvas,
                                                              const Render::Pass3D& pass,
                                                              const Terrain::TerrainPolygon& polygon, int frames)
{
    const std::int32_t map = m_context.world->Runtime().map;
    // 0x63741d..0x637490: on 30, with the hero in CastleSiege::InTheCastleSiegeFog's rectangle (Q-C334), 0x42b0a0 turns
    // on the black fog (GL_LINEAR from 2000 to 2700) for the rest of the pass, also the eye pass of the sprites and the
    // particles; outside, glDisable(GL_FOG), which leaves 0x620db0's (Begin).
    canvas.models.SetFog(HeroInTheCastleSiegeFog(*m_context.world) ? std::optional(CastleSiegeFog) : std::nullopt);
    // 0x6374aa: 0x620480(x, y, &0x7d366c0, 1) with the mouse at 640 × 480 (0x7d3682c and 0x7d36828), which goes back to
    // the screen pixels in integers (0x620486..0x6204b0).
    const Input::InputState noInput;
    const Ui::Point mouse = Ui::MouseIn640x480(m_context.input != nullptr ? *m_context.input : noInput, canvas.screen);
    const auto pixelX = (static_cast<std::int64_t>(mouse.x) * canvas.screen.width) / Width640;
    const auto pixelY = (static_cast<std::int64_t>(mouse.y) * canvas.screen.height) / Height480;
    const auto [origin, end] = pass.RayThrough(static_cast<float>(pixelX), static_cast<float>(pixelY));
    m_mouseRay = Terrain::TerrainPickingSegment{.origin = origin, .end = end};
    // 0x6374b2..0x6374c2: 0x437850() looks at the Kalima world (0x723c48), which MuRet does not create (MapLoad stops
    // on those maps): it is false. Map 10 does not draw the terrain.
    if (FrameKalima() == nullptr && map != WithoutTerrainMap)
    {
        const Terrain::WorldTerrain& terrain = MapTerrain();
        Terrain::TerrainView view;
        view.mapping = terrain.mapping.get();
        view.heights = terrain.heights.get();
        view.attributes = terrain.attributes.get();
        view.lighting = &FrameLighting();
        view.tiles = m_tiles.Sizes();
        view.map = static_cast<std::uint8_t>(map);
        view.fixedHeight = CameraState().fixedHeight;
        // 0x5fa973: on map 7 the water goes with 0x5fa4b0 and the caustic of DAT_07d2d360's frame.
        view.waterFrame = FrameCloth().waterTextureFrame;
        // 0x5fc26b..0x5fc2c0: the water shift with the frame's clock; 0x5fc2de: 0x5fc190(0) with 0x7d0d31c at 0, the
        // range's cells that pass the trapezoid.
        m_water.offset = Terrain::WaterOffsetAt(canvas.frameClock, view.map);
        // 0x5fa83f: 0x5fa830 calls 0x405e00(x, y) with each cell it draws, and on maps 18 to 23 takes rand() (and, with
        // rand() % 8 at 0, the area at 0, 3 or 6 and the cell in one of its rectangles, the stone and the shake,
        // Q-C229). The others do not call it in MuRet: it takes nothing.
        if (Maps::IsChaosCastle(map))
        {
            const float frameClock =
                static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(canvas.frameClock).count());
            const Events::ChaosCastle::ChaosCastleCellContext cellContext{
                .area = m_context.world->Runtime().chaosCastleArea,
                .heroZ = m_context.world->Hero().position.z,
                .random = &FrameCloth().random,
                .particles = &FrameParticles(),
                .worldClock = std::cref(frameClock),
                .cameraShake = m_projectingFrame ? &m_projection->cameraShake : &CameraInputState().shake};
            if (auto drawn =
                    Terrain::DrawTerrain(canvas.models, view, m_terrainRange, polygon, m_water,
                                         [&cellContext](std::uint8_t x, std::uint8_t y)
                                         { return Events::ChaosCastle::StepChaosCastleCell(x, y, cellContext); });
                !drawn.HasValue())
            {
                return Core::Unexpected(drawn.Error() + ", en el " + MapText(map));
            }
        }
        else if (map == CrywolfMap)
        {
            // 0x5fa695: on 34, the water UVs take rand() and release the 0x698/0 bubbles
            // (MapScripts/Crywolf/CrywolfWater.h).
            const float frameClock =
                static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(canvas.frameClock).count());
            const Kernel::ActorState& hero = m_context.world->Hero();
            const MapScripts::Crywolf::CrywolfWaterContext waterContext{.heroZ = hero.position.z,
                                                                        .heroAngles = hero.angles,
                                                                        .random = &FrameCloth().random,
                                                                        .particles = &FrameParticles(),
                                                                        .worldClock = std::cref(frameClock)};
            if (auto drawn = Terrain::DrawTerrain(canvas.models, view, m_terrainRange, polygon, m_water,
                                                  [&view, &waterContext](std::uint8_t x, std::uint8_t y) {
                                                      return MapScripts::Crywolf::StepCrywolfWaterCell(view, x, y,
                                                                                                       waterContext);
                                                  });
                !drawn.HasValue())
            {
                return Core::Unexpected(drawn.Error() + ", en el " + MapText(map));
            }
        }
        else
        {
            Terrain::DrawTerrain(canvas.models, view, m_terrainRange, polygon, m_water);
        }
        // 0x5fc32b..0x5fc353: with 0x620a00(1)'s blending, 0x6c9d58 and outside map 7, the grass pass (0x5fc190 with
        // 0x7d0d31c at 2) on the same cells. It takes no rand(): 0x405e00 only does on maps 18 to 23, which do not have
        // this pass.
        if (HasTheGrassPass(map) && map != AnimatedWaterMap)
        {
            const Terrain::TerrainGrass grass{.heights = m_tiles.GrassHeights(),
                                              .offsets = m_context.world->Map().grassOffsets};
            if (auto grown = Terrain::DrawGrass(canvas.models, view, m_terrainRange, polygon, m_water, grass);
                !grown.HasValue())
            {
                return Core::Unexpected(grown.Error() + ", en el pase del pasto del " + MapText(map));
            }
        }
        // 0x5fc35b..0x5fc360: without depth test (0x620870) and with culling (0x6208d0), which 0x549130's blending
        // turns off.
        Render::ModelDrawState marksState = canvas.models.State();
        marksState.depthTest = false;
        marksState.cullEnabled = true;
        canvas.models.SetState(marksState);
        // 0x5fc365: 0x549130, WorldRuntime's ground marks, which the map load does not empty (Q-C228). The slot's byte
        // +0x28, from the shared texture table (0x7d3a198, seeded in Enter with what GameModels uploaded):
        // Effect\blood01.tga at 0x5e3 (0x61d7aa, EffectTextureSet). A slot that was not uploaded (another type, or the
        // missing file) stops (08 P-C49).
        const Assets::ModelTextureTable& textureTable = LoadedFaunaModels().TextureTable();
        const auto components = [&textureTable](std::int32_t slot) -> std::optional<std::uint8_t>
        {
            const auto metadata = textureTable.Metadata(static_cast<std::uint32_t>(slot));
            return metadata ? std::optional(metadata->components) : std::nullopt;
        };
        if (auto marked = Terrain::DrawGroundMarks(canvas.models, m_context.states->terrain.groundMarks,
                                                   *terrain.heights, components);
            !marked.HasValue())
        {
            return Core::Unexpected(marked.Error() + ", en el " + MapText(map));
        }
        // 0x5fc36a: the depth test back (0x620850).
        Render::ModelDrawState afterMarks = canvas.models.State();
        afterMarks.depthTest = true;
        canvas.models.SetState(afterMarks);
    }
    // 0x6374ce: 0x6046a0, the .obj objects per sector. MuRet has the cases of maps 0 to 4, 6 to 23, 30, 31, 33, 34 and
    // Kalima.
    const bool knownObjects = (map >= LorenciaMap && map <= LostTowerMap) ||
                              (map >= ArenaMap && map <= Maps::LastChaosCastleMap) || map == CastleMap ||
                              map == LandOfTrialsMap || map == AidaMap || map == CrywolfMap || Maps::IsKalimaMap(map);
    if (!knownObjects &&
        std::ranges::any_of(FrameObjects(), [](const Kernel::WorldObject& object) { return object.alive; }))
    {
        return Core::Unexpected("0x6374ce: los objetos del " + MapText(map) +
                                " (0x6046a0 → 0x602e90, 0x602ed0) no están portados: MuRet tiene los del 0 al 4, "
                                "los del 6 al 23, los del 30, los del 31, los del 33, los del 34 y los de Kalima");
    }
    if (auto loaded = LoadTheObjectModels(map); !loaded.HasValue())
    {
        return loaded;
    }
    Kernel::WorldState& world = *m_context.world;
    const Kernel::ActorState& hero = world.Hero();
    // The terrain light at a point (0x5f99e0) reads the dynamic buffer 0x7b4ccc4: the step adds the lights of the
    // fires, the lamps and the fauna to the dynamic buffer. The world clock (0x5767b88), the frame's, like the
    // camera's.
    if (knownObjects)
    {
        if (auto objects = WorldObjects::DrawGameWorldObjects(canvas.models, m_worldModels, FrameObjects(), polygon,
                                                              &FrameLighting(), canvas.frameClock,
                                                              Kernel::GameObjectPass{.map =
                                                                                         static_cast<std::uint8_t>(map),
                                                                                     .originalFrame = frames > 0,
                                                                                     .random = &FrameCloth().random,
                                                                                     .heroClass = hero.characterClass,
                                                                                     .ownLevel =
                                                                                         world.OwnCharacter().level,
                                                                                     .ownRecordDepth =
                                                                                         m_projectingFrame
                                                                                             ? &m_projection
                                                                                                    ->ownRecordDepth
                                                                                             : &world.Runtime()
                                                                                                    .ownRecordDepth,
                                                                                     .worldClock = static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(canvas
                                                                                                                                                                                .frameClock)
                                                                                                                          .count()),
                                                                                     .particles = &FrameParticles(),
                                                                                     .onPose =
                                                                                         [&](const Actors::
                                                                                                 Drawing::DrawnObject& drawn) {
                                                                                             return Actors::Drawing::
                                                                                                 PublishPose(
                                                                                                     FramePoseCarry(),
                                                                                                     m_worldModels,
                                                                                                     drawn, true);
                                                                                         },
                                                                                     .sprites = &FrameSpritesForDraw(),
                                                                                     .retainedPose = &FramePoseCarry(),
                                                                                     .createJoint = [&](const Kernel::JointRequest&
                                                                                                            request) -> Core::
                                                                                                                         Expected<void, std::string>
                                                                                     {
                                                                                         const auto created =
                                                                                             Joints::CreateFaunaJoint(
                                                                                                 FrameTail().joints,
                                                                                                 request,
                                                                                                 FrameCloth().random);
                                                                                         if (!created.HasValue())
                                                                                             return Core::Unexpected(
                                                                                                 created.Error());
                                                                                         return {};
                                                                                     },
                                                                                     .modelState = &FrameModelState(),
                                                                                     .publishProjectedPose = true,
                                                                                     .waterTextureFrame =
                                                                                         FrameCloth().waterTextureFrame,
                                                                                     .hero = &hero,
                                                                                     .kalima = FrameKalima(),
                                                                                     .requestSound =
                                                                                         [&](Audio::SoundId sound)
                                                                                     {
                                                                                         if (frames > 0 &&
                                                                                             !m_projectingFrame)
                                                                                         {
                                                                                             world.Fauna().Sound(
                                                                                                 sound.Value(),
                                                                                                 nullptr);
                                                                                             world.Runtime()
                                                                                                 .RequestSound(sound);
                                                                                         }
                                                                                     },
                                                                                     .effects = &FrameEffects(),
                                                                                     // Q-C304: Aida's 0x46 reads
                                                                                     // timeGetTime (0x6b8544); MuRet
                                                                                     // lends it the draw clock.
                                                                                     .systemTime =
                                                                                         [systemTime = static_cast<
                                                                                              std::uint32_t>(std::chrono::duration_cast<
                                                                                                                 std::chrono::milliseconds>(canvas
                                                                                                                                                .frameClock)
                                                                                                                 .count())]()
                                                                                     { return systemTime; },
                                                                                     .scene = static_cast<std::int32_t>(
                                                                                         Kernel::SceneId::Game),
                                                                                     .terrainHeight =
                                                                                         [&](float x, float y)
                                                                                     {
                                                                                         const Terrain::WorldTerrain&
                                                                                             terrain = MapTerrain();
                                                                                         return static_cast<float>(
                                                                                             Terrain::TerrainHeightAt(
                                                                                                 *terrain.heights,
                                                                                                 *terrain.attributes, x,
                                                                                                 y,
                                                                                                 CameraState()
                                                                                                     .fixedHeight));
                                                                                     },
                                                                                     .chaosCastleArea =
                                                                                         world.Runtime()
                                                                                             .chaosCastleArea,
                                                                                     .siegeActive =
                                                                                         world.Runtime()
                                                                                             .castleSiege.siegeActive,
                                                                                     .objectDrawCounter =
                                                                                         &world.Runtime()
                                                                                              .objectDrawCounter,
                                                                                     .crywolf =
                                                                                         &world.Runtime().crywolf});
            !objects.HasValue())
        {
            return Core::Unexpected(objects.Error() + ", en el " + MapText(map));
        }
    }
    // 0x6374d3: 0x52dc20, the ground effects, on 0x518cf0's table (WorldMap's).
    if (auto ground = m_groundEffects.Draw(
            canvas.models, FrameEffects().Effects(),
            Effects::GroundEffectResources{
                .heights = *MapTerrain().heights,
                .random = FrameCloth().random,
                .worldClock = static_cast<float>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(canvas.frameClock).count()),
                .map = static_cast<std::uint8_t>(map),
                .kalima = FrameKalima(),
                .ownerPosition = Effects::ActorPositionOf(world)},
            frames > 0);
        !ground.HasValue())
    {
        return Core::Unexpected(ground.Error() + ", en el " + MapText(map));
    }
    // 0x6374d8: 0x446b60, the birds (0x7d2dca8, 40 slots). 0x446120 creates and moves them from the step (0x6370ea),
    // which MuRet does not run (the StepWorld boundary), and 0x6070a0 on the map load and 0x4455c0 turn them off. MuRet
    // does not have the table: without live birds, 0x446b60 neither draws nor takes rand(), and only adds and subtracts
    // 90 to the +0x24 of each slot that is not a 0x10f (Q-C277), which nobody sees (08 P-C55).
    if (auto birds = DrawTheBirds(canvas, polygon, frames); !birds.HasValue())
        return birds;
    // 0x6374dd: 0x510d00, the characters, and at the end 0x42da60 with the TerrainWall.
    if (auto characters = DrawTheCharacters(canvas, polygon, frames); !characters.HasValue())
    {
        return Core::Unexpected(characters.Error() + ", en el " + MapText(map));
    }
    // 0x6374e2..0x6374ed: 0x5fc260(1) only with 0x77f3b18, the editor mode, which nobody turns on. 0x6374f5: with
    // 0x7d36899 at 0 (nobody writes it), 0x608a00, the floor items.
    if (auto tail = DrawTheTail(canvas, pass, polygon, frames); !tail.HasValue())
        return tail;
    // 0x6375ea..0x63763c: after the effects and before the 2D (0x6219b0), with the hero still in the rectangle,
    // 0x42b0f0 turns the fog off. If it left during the draw, it stays on until the following 0x620db0 (Q-C390).
    if (HeroInTheCastleSiegeFog(*m_context.world))
        canvas.models.SetFog(std::nullopt);
    return {};
}

Core::Expected<void, std::string> GameScene::DrawTheCharacters(Render::Frame::SceneCanvas& canvas,
                                                               const Terrain::TerrainPolygon& polygon, int frames)
{
    Kernel::WorldState& world = *m_context.world;
    if (m_models == nullptr)
    {
        return Actors::Drawing::DrawGameCharacters(
            world.Actors(), world.Runtime().hero, MapTerrain().attributes->cells, &world.Runtime(),
            m_projectingFrame ? std::span(m_projection->actorPickingBoxes) : std::span<Actors::ActorPickingBox>{});
    }
    std::optional<std::string> poseFailure;
    Actors::Drawing::ItemSpecialDrawContext special;
    Actors::Drawing::ItemDrawContext itemContext;
    FillTheItemContexts(special, itemContext, canvas.frameClock, frames);
    const auto drawn = Actors::Drawing::DrawGameCharacters(
        canvas.models, *m_models, world,
        Actors::Drawing::GameCharacterPass{
            .dynamicLighting = &FrameLighting(),
            .clothWorld = &FrameCloth(),
            .frameClock = canvas.frameClock,
            .originalFrames = frames,
            // 0x510d00 outlines the chosen ones the previous draw's picking left (0x59ec80 runs after the characters):
            // 0x6c8e30 and 0x6c8e2c.
            .hovered = world.Runtime().selectedTargets[2],
            .targeted = world.Runtime().selectedTargets[1],
            .cloths = world.ActorCloths(),
            .sprites = &FrameSpritesForDraw(),
            .poseCarry = &FramePoseCarry(),
            .joints = FrameTail().joints,
            .particles = &FrameParticles(),
            .effects = &FrameEffects(),
            .itemContext = &itemContext,
            .pace = OriginalFramePaceOf(m_context.pacing),
            .onPose =
                [&](std::uint16_t model, std::span<const Math::BoneTransform> bones,
                    const Models::ObjectTransform& transform, bool translated)
            {
                const auto published =
                    Actors::Drawing::PublishPoseRaw(FramePoseCarry(), *m_models, model, bones, transform, translated);
                if (!published.HasValue() && !poseFailure)
                    poseFailure = published.Error();
            },
            .publishProjectedPose = true,
            .pickingBoxes =
                m_projectingFrame ? std::span(m_projection->actorPickingBoxes) : std::span<Actors::ActorPickingBox>{},
            .poseBones = [&](std::uint16_t) { return std::span<const Math::BoneTransform>(FramePoseCarry().bones); },
            .worldModels = m_worldModelsMap ? &m_worldModels : nullptr,
            .polygon = &polygon,
            .fixedHeight = CameraState().fixedHeight,
            .terrain = &MapTerrain(),
            .guildMarks = &m_guildMarks,
            .kalima = FrameKalima(),
            .cameraShake = m_projectingFrame ? &m_projection->cameraShake : &CameraInputState().shake,
            .faunaModels = &LoadedFaunaModels(),
            .drawBuffers = &m_characterBuffers,
            .hooks = &m_characterDrawHooks,
            .effectHooks = &m_effectHooks});
    if (poseFailure)
        return Core::Unexpected(*poseFailure);
    return drawn;
}

Kernel::PoseBuffer& GameScene::FramePoseCarry()
{
    return m_framePoseCarry ? *m_framePoseCarry : m_context.world->PoseCarry();
}

Particles::FrameSprites& GameScene::FrameSpritesForDraw()
{
    return m_frameSprites ? *m_frameSprites : m_sprites;
}

void GameScene::Leave()
{
    if (m_context.states != nullptr)
        LoadedFaunaModels().Bind(nullptr);
    m_tiles.Release();
    m_worldModels.Release();
    m_worldModelsMap.reset();
    m_windowTextures.Release();
    m_sandTextures.Release();
}

// Q-C70: without a connection, the game goes on; text 3 of the message area belongs to CL9.
std::optional<Kernel::SceneId> GameScene::Next() const
{
    return std::nullopt;
}

// 0x62828e: the OK of dialogs 0x70, 0x6f, 0x1a and 0x1c sends WM_DESTROY to the window.
bool GameScene::WantsToQuit() const
{
    return m_context.world != nullptr && m_context.world->Runtime().ui.destroyRequested;
}

// While the chat field is open, one of its EDITs has the focus and receives the WM_CHAR.
bool GameScene::WantsTextInput() const
{
    if (m_context.world == nullptr)
        return false;
    // The open chat, the EDIT of an old window with the focus (CL9 milestone 9) or one of the dialogs' two: that of
    // [0x5763504] (milestone 11 and CL9 milestone 8) or that of [0x5763508] (CL9 milestone 8). The one with the Windows
    // focus receives the WM_CHAR.
    const Kernel::WorldRuntime& runtime = m_context.world->Runtime();
    const Chat::ChatFocus focus = runtime.chat.input.focus;
    return runtime.ui.chatOpen || focus == Chat::ChatFocus::OldWindow || focus == Chat::ChatFocus::Dialog ||
           focus == Chat::ChatFocus::NumberField;
}

bool GameScene::HoldsTheLastFrame() const
{
    return m_holdFrame;
}

std::optional<std::string> GameScene::Failure() const
{
    return m_failure;
}

const std::optional<Terrain::TerrainPickingSegment>& GameScene::MouseRay() const
{
    return m_mouseRay;
}

Camera::GameCamera& GameScene::CameraState()
{
    return m_context.camera != nullptr ? *m_context.camera : m_localCamera;
}

Camera::GameCameraInput& GameScene::CameraInputState()
{
    return m_context.cameraInput != nullptr ? *m_context.cameraInput : m_localCameraInput;
}
} // namespace Client::Scenes
