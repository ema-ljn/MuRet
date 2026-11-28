#include "Network/Xor32Packet.h"
#include "Models/IModelLibrary.h"
#include "Scenes/GameScene.h"

#include "Kernel/PacingMode.h"
#include "Audio/Playback.h"
#include "Crt/CrtRandom.h"
#include "Sound/MonsterSounds.h"
#include "Session/ServerSession.h"
#include "Session/SessionState.h"
#include "Session/WorldEvent.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Tables/PlayerModels.h"
#include "Tables/GateRecord.h"
#include "Tables/GateTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Tables/TextTable.h"
#include "Input/InputState.h"
#include "Input/InputEvent.h"
#include "Input/Key.h"
#include "Network/PacketSerials.h"
#include "Render/ModelBatch.h"
#include "Models/Animation.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Render/Pass3D.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Chat/ChatBubbles.h"
#include "Pets/DarkHorse.h"
#include "Events/EventGlobals.h"
#include "Actors/FullSetScan.h"
#include "Fauna/FaunaStep.h"
#include "Fauna/FaunaState.h"
#include "Weather/GameLeaves.h"
#include "Tables/GameData.h"
#include "Kernel/DrawTailState.h"
#include "Assets/GameModels.h"
#include "Pets/HelperTable.h"
#include "Session/MapLoad.h"
#include "Session/ModuleStates.h"
#include "Camera/SceneCameras.h"
#include "Scenes/SceneContext.h"
#include "Kernel/SceneId.h"
#include "Assets/WorldModels.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Camera/GameCamera.h"
#include "Terrain/GroundMarks.h"
#include "Weather/Leaves.h"
#include "Weather/LeafWeather.h"
#include "Particles/Particles.h"
#include "Terrain/TerrainCells.h"
#include "Terrain/TerrainCulling.h"
#include "Pathfinding/TerrainWalls.h"
#include "WorldObjects/WorldObjects.h"
#include "Terrain/WorldTerrain.h"
#include "Chat/ChatLog.h"
#include "Ui/MessageArea.h"

#include "support/AppPrinters.h"
#include "support/Bytes.h"
#include "support/FakeClock.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/PlayerActors.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/RecordingTextDrawer.h"
#include "support/ScenePrinters.h"
#include "support/SameBytes.h"
#include "support/SessionHarness.h"
#include "support/TestGameServer.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::Scenes::GameScene: scene 5 while it waits for the world and with the world. It sends the F3 03 on its
// first step (0x636290), lowers the wait counter like MoveHero (0x59af01) and holds the frame while it is above 30
// (0x6371a0); the reference maps present the frame with full resources. 06 § La entrada al mundo, § El paso del juego
// and § El dibujo del juego. The wait and the deliberate stops keep their state and CRT oracles.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::SceneId;
using Client::Kernel::WaitDrawThreshold;
using Client::Kernel::WorldLoadingWait;
using Client::Kernel::WorldRuntime;
using Client::Kernel::WorldState;
using Client::Models::ClothWorld;
using Client::Scenes::GameScene;
using Client::Scenes::SceneContext;
using Client::Session::ModuleStates;
using Client::Session::SessionState;
using Client::Tests::FakeTextureStore;
using Client::Tests::LoggedInSession;

constexpr std::uint32_t Seed = 0x13572468u;
// The idle hero's body in 0x508f00 (0x4fd5f0): the own record that 0x4fc450 opens and closes, with the 3848 rand()
// of 0x40b4d0, and the rand() % 6 of 0x4fd7e4.
constexpr int HeroBodyDraws = Client::Models::OwnRecordCloseRandoms + 1;
// 0x637128: the particle wind takes two rand(), even with the table empty.
constexpr int ParticleWindDraws = 2;
// 0x636ded..0x63705b, the step's UI with everything closed: the own record of the skill bar (0x5a05e5), the trade
// and NPC shop queries of Q, W and E (0x5a343e) and those of IsOpen(0) (0x5a5511).
constexpr int GameUiStepDraws = Client::Models::OwnRecordCloseRandoms + 2 + 2;
// 0x636920, on the scene's first step: 0x46b010 and 0x46b0e0, which opens and closes the own record. Without
// Slide.bmd no stretch has texts and it makes no random pick.
constexpr int SlideStartDraws = Client::Models::OwnRecordCloseRandoms;
// 0x4eb620, at the start of each frame of the original and before the step: in scene 5, 0x552f10 (the skill wait)
// opens and closes the own record.
constexpr int FrameStartDraws = Client::Models::OwnRecordCloseRandoms;
constexpr int FirstStepDraws = FrameStartDraws + SlideStartDraws + GameUiStepDraws;
// 0x637641: the Dark Wizard's (class 0) frame picking reads its skill from the own record (0x59efda).
constexpr int FramePickingDraws = Client::Models::OwnRecordCloseRandoms;
// 0x637660: the HUD (0x5a7320) in the base state: 19 own record closes (the arrows, the twelve durability warnings,
// the bottom bar, the experience, the AG and the three of the main bar), those of the hero's two cells and nine trade
// and NPC shop queries (the warnings, the windows, the 3D, the grids and the siege panel).
constexpr int HudBaseDraws =
    19 * Client::Models::OwnRecordCloseRandoms + 2 * Client::Models::SmallLogicalCloseRandoms + 9;

CrtRandom RandomAfter(int draws)
{
    CrtRandom random(Seed);
    for (int i = 0; i < draws; ++i)
    {
        static_cast<void>(random.Next());
    }
    return random;
}

CrtRandom Advanced(CrtRandom random, int draws)
{
    for (int i = 0; i < draws; ++i)
        static_cast<void>(random.Next());
    return random;
}
constexpr int HeroActionPrefixRandoms = Client::Models::OwnRecordCloseRandoms + 60;

// The rand() after a step: the scene's `draws` and then the cloth (0x637700).
CrtRandom RandomAfterAStep(int draws)
{
    ClothWorld reference{.random = RandomAfter(draws)};
    Client::Models::StepTheClothWorld(reference);
    return reference.random;
}

std::uint32_t StateAfterAStep(int draws)
{
    return RandomAfterAStep(draws).State();
}

// 0x599f41..0x599ffd: real Gate.bmd, one logical read (4 rand) per comparison reached, short-circuiting X>=x1,
// Y>=y1, X<=x2, Y<=y2. Entering the rectangle also queries the wait: these fixtures keep the counter positive.
int GateProbeRandoms(const Client::Tables::GateTable& gates, std::int32_t map, const Client::Kernel::ActorState& hero)
{
    int draws = 0;
    for (std::size_t slot = 0; slot < gates.Count(); ++slot)
    {
        const auto gate = Client::Tables::ReadGateRecord(gates, slot);
        if (!gate || gate->flag != 1 || gate->map != map)
            continue;
        draws += 4;
        if (hero.cellX < gate->x1)
            continue;
        draws += 4;
        if (hero.cellY < gate->y1)
            continue;
        draws += 4;
        if (hero.cellX > gate->x2)
            continue;
        draws += 4;
        if (hero.cellY > gate->y2)
            continue;
        draws += 4; // 0x59a018: ReadWait; with a positive wait it does not open the own record.
    }
    return draws;
}

// Composition oracle: runs the already checked kernels of 0x637082/0x6370ea/0x6370ef on new owners, with the hero
// and weather from before the step. It does not take the observed CRT state. Families 0/2 need no outside creators;
// the loaded library only lends BMD.
Client::Core::Expected<void, std::string>
AdvanceEnvironmentPrefix(Client::Kernel::WorldState& world, Client::Session::ModuleStates& states,
                         const Client::Kernel::ActorState& hero, Client::Weather::LeafWeather weather,
                         std::int32_t heroCellLayer, Client::Models::ClothWorld& reference)
{
    const Client::Terrain::WorldTerrain& terrain = states.terrain.current;
    Client::Weather::Leaves leaves;
    Client::Fauna::FaunaState fauna;
    const float cameraY = 0.0f; // The scene has not done its first game draw yet.
    const float worldClock = 0.0f;
    Client::Weather::GameLeafContext leafContext{leaves,           weather,   *terrain.heights,    *terrain.attributes,
                                                 hero.position,    cameraY,   world.Runtime().map, heroCellLayer,
                                                 reference.random, worldClock};
    if (auto stepped = Client::Weather::StepGameLeaves(leafContext); !stepped.HasValue())
        return stepped;
    Client::Fauna::FaunaStepContext faunaContext{fauna,
                                                 world.Runtime().map,
                                                 world.Runtime().storm,
                                                 hero.position,
                                                 hero.animation.action,
                                                 hero.safeZone,
                                                 terrain,
                                                 states.terrain.dynamicLight.Lighting(),
                                                 states.assets.fauna,
                                                 reference,
                                                 worldClock};
    return Client::Fauna::StepGameFauna(faunaContext);
}

void ChooseSlotThree(LoggedInSession& online)
{
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    online.session.RequestCharacterList();
    online.ReceiveTheList({{0, "char0001"}, {3, "char0002"}});
    online.session.ChooseCharacter(std::uint8_t{3});
    ASSERT_NE(online.session.ChosenCharacter(), nullptr);
}

// The session/wait doubles stand for no real map. They lend an inert grid to satisfy the bindings before the
// counter without creating leaves or fauna of a real family.
void GiveInertTerrain(Client::Terrain::WorldTerrain& terrain)
{
    terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    terrain.mapping = std::make_unique<Client::Data::Terrain::TerrainMapping>();
}
constexpr int InertFishCandidateRandoms = 3 * 2; // 0x447030: three inactive slots, X/Y; map−1 rejects.

// Six pixels per byte: the tests' texts do not reach the 256 where 0x555190 cuts.
class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

struct Harness
{
    explicit Harness(Client::Core::Log& log, Client::App::ServerSession* session = nullptr)
        : scene(SceneContext{.dataRoot = Client::Tests::RepoDataDir(),
                             .textures = store,
                             .log = log,
                             .cloth = &cloth,
                             .world = &world,
                             .textMeasure = &letters,
                             .states = &states},
                session)
    {
        GiveInertTerrain(states.terrain.current);
    }

    [[nodiscard]] WorldRuntime& Runtime()
    {
        return world.Runtime();
    }

    FakeTextureStore store;
    LetterMeasure letters;
    ClothWorld cloth{.random = CrtRandom(Seed)};
    WorldState world;
    ModuleStates states;
    GameScene scene;
    const Client::Input::InputState input;
};

TEST(GameSceneTest, TheFirstStepSendsF303)
{
    LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(ChooseSlotThree(online));
    Harness tested(online.log, &online.session);
    ASSERT_TRUE(tested.scene.Enter().HasValue());

    tested.scene.Step(tested.input);
    tested.scene.Step(tested.input);

    EXPECT_EQ(online.session.State(), SessionState::EnteringWorld);
    EXPECT_FALSE(std::ranges::any_of(online.sink.lines, [](const std::string& line)
                                     { return line.find("se ignora la entrada al mundo") != std::string::npos; }));
}

class GameSceneNpcDialogCloseTest : public ::testing::TestWithParam<std::uint8_t>
{
};

// 0x623f17→0x657880→0x65cae0: Esc is handled before the world step, with the scene's transport. The helper gets no
// sender, so the GameUiContext that GameScene builds is tested too.
TEST_P(GameSceneNpcDialogCloseTest, EscapeSendsTheNpcCloseThroughTheSceneSession)
{
    const auto online = std::make_unique<LoggedInSession>();
    ASSERT_NO_FATAL_FAILURE(ChooseSlotThree(*online));
    const auto tested = std::make_unique<Harness>(online->log, &online->session);
    ASSERT_TRUE(tested->scene.Enter().HasValue());
    tested->scene.Step(tested->input);
    ASSERT_EQ(tested->scene.Failure(), std::nullopt);
    ASSERT_EQ(online->session.State(), SessionState::EnteringWorld);
    online->socket.sent.clear();

    auto& runtime = tested->Runtime();
    const std::uint8_t kind = GetParam();
    if (kind == 0)
        runtime.panels.warehouse = 1;
    else if (kind == 1)
        runtime.panels.chaosBox = 1;
    else if (kind == 2)
        runtime.panels.delegated.trainer = 1;
    else
        runtime.panels.npcShop = 1;
    Client::Input::InputState input;
    input.Apply({.kind = Client::Input::InputEvent::Kind::KeyDown, .key = Client::Input::Key::Escape});

    tested->scene.Step(input);

    EXPECT_EQ(tested->scene.Failure(), std::nullopt);
    EXPECT_EQ(runtime.panels.warehouse, 0);
    EXPECT_EQ(runtime.panels.chaosBox, 0);
    EXPECT_EQ(runtime.panels.delegated.trainer, 0);
    EXPECT_EQ(runtime.panels.npcShop, 0);
    EXPECT_EQ(runtime.dialogs.current, 0);
    Client::Network::PacketSerials serials;
    const auto received = Client::Tests::AsTheGameServerReceives(online->socket.sent, serials);
    ASSERT_EQ(received.packets.size(), 1u);
    const std::uint8_t opcode = kind == 0 ? 0x82 : kind == 1 ? 0x87 : 0x31;
    const auto plain = Client::Network::Xor32Decode(received.packets[0]);
    EXPECT_TRUE(Client::Tests::SameBytes(plain.Bytes(), Client::Tests::Bytes({0xc1, 3, opcode})));
}

INSTANTIATE_TEST_SUITE_P(NpcKinds, GameSceneNpcDialogCloseTest,
                         ::testing::Values(std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{3}));

// 0x63684b..0x6368f4: the first step closes the chat field, empties the old area with 120 entries, the log and the
// filter. The steps after it do not repeat it.
TEST(GameSceneTest, TheFirstStepEmptiesTheChat)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Harness tested(log);
    WorldRuntime& runtime = tested.Runtime();
    runtime.ui.chatOpen = true;
    runtime.chat.log.entries.push_back(Client::Chat::ChatEntry{.name = "Ana", .text = "hola", .type = 0});
    runtime.chat.log.filter[0] = "dragon";
    runtime.chat.area.rows[0].text = "viejo";
    runtime.chat.area.count = 1;
    ASSERT_TRUE(tested.scene.Enter().HasValue());

    tested.scene.Step(tested.input);

    ASSERT_FALSE(tested.scene.Failure().has_value()) << *tested.scene.Failure();
    EXPECT_FALSE(runtime.ui.chatOpen);
    EXPECT_TRUE(runtime.chat.log.entries.empty());
    EXPECT_TRUE(runtime.chat.log.filter[0].empty());
    EXPECT_EQ(runtime.chat.area.rows[0].text, "");
    EXPECT_EQ(runtime.chat.area.count, static_cast<std::int32_t>(Client::Ui::MessageAreaRows));

    runtime.chat.log.entries.push_back(Client::Chat::ChatEntry{.name = "Beto", .text = "chau", .type = 0});
    tested.scene.Step(tested.input);
    EXPECT_EQ(runtime.chat.log.entries.size(), 1u);
}

// Q-C70: in the game (mode 5), the FD_CLOSE opens no notice and the game goes on without a connection: text 3 in the
// message area belongs to CL9. The scene does not go back to the login.
TEST(GameSceneTest, AFailedSessionKeepsTheGame)
{
    LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(ChooseSlotThree(online));
    Harness tested(online.log, &online.session);
    ASSERT_TRUE(tested.scene.Enter().HasValue());
    tested.scene.Step(tested.input);

    online.socket.peerClosed = true;
    static_cast<void>(online.session.Poll());
    tested.scene.Step(tested.input);

    EXPECT_EQ(online.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.scene.Next(), std::nullopt);
    EXPECT_FALSE(tested.scene.WantsToQuit());
}

// The original logs the slot and the name; MuRet, the slot (the name is the player's).
TEST(GameSceneTest, TheLogHasTheSlotNotTheName)
{
    LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(ChooseSlotThree(online));
    Harness tested(online.log, &online.session);
    ASSERT_TRUE(tested.scene.Enter().HasValue());

    tested.scene.Step(tested.input);

    EXPECT_TRUE(std::ranges::any_of(online.sink.lines, [](const std::string& line)
                                    { return line.find("del lugar 3") != std::string::npos; }));
    EXPECT_FALSE(std::ranges::any_of(online.sink.lines, [](const std::string& line)
                                     { return line.find("char000") != std::string::npos; }));
}

// The wait lives in the WorldState and its closes spend the cloth's rand(): without either, the game does not enter
// instead of drawing without waiting. Without the modules' states, neither.
TEST(GameSceneTest, DoesNotEnterWithoutTheWorldTheRandomOrTheStates)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Harness tested(log);
    GameScene withoutWorld(
        SceneContext{
            .dataRoot = Client::Tests::RepoDataDir(), .textures = tested.store, .log = log, .cloth = &tested.cloth},
        nullptr);
    GameScene withoutRandom(SceneContext{.dataRoot = Client::Tests::RepoDataDir(),
                                         .textures = tested.store,
                                         .log = log,
                                         .world = &tested.world,
                                         .states = &tested.states},
                            nullptr);
    GameScene withoutStates(SceneContext{.dataRoot = Client::Tests::RepoDataDir(),
                                         .textures = tested.store,
                                         .log = log,
                                         .cloth = &tested.cloth,
                                         .world = &tested.world},
                            nullptr);

    EXPECT_FALSE(withoutWorld.Enter().HasValue());
    EXPECT_FALSE(withoutRandom.Enter().HasValue());
    const auto entered = withoutStates.Enter();
    ASSERT_FALSE(entered.HasValue());
    EXPECT_NE(entered.Error().find("modules' states"), std::string::npos) << entered.Error();
}

// 06, "La entrada al mundo", steps 3, 4 and 6: with the world loaded and the counter the load left, the draw holds;
// the steps lower it without getting there, and the F3 03 reply sets it to 30 (0x4c48eb), which no longer holds.
TEST(GameSceneTest, HoldsTheFrameUntilTheWorldArrives)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Harness tested(log);
    tested.Runtime().drawingEnabled = true;
    tested.Runtime().warpCounter = WorldLoadingWait;
    ASSERT_TRUE(tested.scene.Enter().HasValue());
    EXPECT_TRUE(tested.scene.HoldsTheLastFrame()) << "antes del primer PrepareFrame retiene";

    for (int step = 0; step < 3; ++step)
    {
        tested.scene.Step(tested.input);
        tested.scene.PrepareFrame();
        EXPECT_TRUE(tested.scene.HoldsTheLastFrame());
    }
    EXPECT_GT(tested.Runtime().warpCounter, WaitDrawThreshold);

    tested.Runtime().warpCounter = WaitDrawThreshold;
    tested.scene.PrepareFrame();

    EXPECT_FALSE(tested.scene.HoldsTheLastFrame());
}

// 0x6374dd: with the world loaded and the counter at 30 or less, the game draw reaches the characters (0x510d00), and
// 0x50ba30 runs 0x505820 with each player it sees (0x50c91e). MuRet does not draw them: it does not know what was
// left in [0x743a178] and [0x743a17c]. Held, or without the world loaded, it does not reach them and they stay.
TEST(GameSceneTest, AFrameThatReachesTheCharactersForgetsTheLastScan)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Harness tested(log);
    tested.Runtime().fullSetScan = Client::Actors::Scan505820{.dword743a178 = 12, .byte743a17c = 1};
    ASSERT_TRUE(tested.scene.Enter().HasValue());

    tested.scene.PrepareFrame();
    EXPECT_TRUE(tested.Runtime().fullSetScan.has_value());

    tested.Runtime().drawingEnabled = true;
    tested.Runtime().warpCounter = WaitDrawThreshold + 1;
    tested.scene.PrepareFrame();
    ASSERT_TRUE(tested.scene.HoldsTheLastFrame());
    EXPECT_TRUE(tested.Runtime().fullSetScan.has_value());

    tested.Runtime().warpCounter = WaitDrawThreshold;
    tested.scene.PrepareFrame();
    ASSERT_FALSE(tested.scene.HoldsTheLastFrame());
    EXPECT_FALSE(tested.Runtime().fullSetScan.has_value());
}

// 0x6371b7..0x637229: the draw reads the counter with an access that closes (0x40b800) even if it then holds; the
// close spends 4 rand() only if it leaves the access at 0. 0x63722e compares signed: 31 holds and 30 does not.
// Asking again does not read again.
TEST(GameSceneTest, WarpGateReadsAndClosesOnceEvenWhenItRetainsTheFrame)
{
    struct Case
    {
        std::int32_t counter;
        std::uint8_t depth;
        bool holds;
        int draws;
    };
    for (const Case& probe : {Case{31, 0, true, 4}, Case{31, 1, true, 0}, Case{31, 255, true, 0}, Case{30, 0, false, 4},
                              Case{30, 1, false, 0}, Case{30, 255, false, 0}, Case{-1, 0, false, 4}})
    {
        SCOPED_TRACE(::testing::Message() << probe.counter << " con " << static_cast<int>(probe.depth));
        Client::Tests::RecordingSink sink;
        Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
        Harness tested(log);
        tested.Runtime().drawingEnabled = true;
        tested.Runtime().warpCounter = probe.counter;
        tested.Runtime().warpDepth = probe.depth;
        ASSERT_TRUE(tested.scene.Enter().HasValue());

        tested.scene.PrepareFrame();
        const bool holds = tested.scene.HoldsTheLastFrame();
        const bool again = tested.scene.HoldsTheLastFrame();

        EXPECT_EQ(holds, probe.holds);
        EXPECT_EQ(again, probe.holds);
        EXPECT_EQ(tested.Runtime().warpCounter, probe.counter);
        EXPECT_EQ(tested.Runtime().warpDepth, probe.depth);
        EXPECT_EQ(tested.cloth.random.State(), RandomAfter(probe.draws).State());
    }
}

// 0x59ae8d..0x59afd1: MoveHero lowers the counter if it is positive in one access and reads it again in another;
// with both closing at 0 that is 8 rand(), before the cloth's (0x637700). The gates from before that counter are
// covered in GameMovement. The inert grid lets characters, wind and cloth complete without mistaking a wait double
// for a real map.
TEST(GameSceneTest, AStepLowersTheCounterWithTwoAccesses)
{
    struct Case
    {
        std::int32_t counter;
        std::uint8_t depth;
        std::int32_t after;
        int draws;
    };
    for (const Case& probe : {Case{WorldLoadingWait, 0, WorldLoadingWait - 1, 8}, Case{31, 1, 30, 0},
                              Case{1, 255, 0, 0}, Case{0, 0, 0, 8}, Case{-5, 0, -5, 8}})
    {
        SCOPED_TRACE(::testing::Message() << probe.counter << " con " << static_cast<int>(probe.depth));
        Client::Tests::RecordingSink sink;
        Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
        Harness tested(log);
        tested.Runtime().drawingEnabled = true;
        tested.Runtime().movement.inputBlocked = true;
        tested.Runtime().warpCounter = probe.counter;
        tested.Runtime().warpDepth = probe.depth;
        ASSERT_TRUE(tested.scene.Enter().HasValue());

        tested.scene.Step(tested.input);

        EXPECT_EQ(tested.Runtime().warpCounter, probe.after);
        EXPECT_EQ(tested.Runtime().warpDepth, probe.depth);
        ASSERT_EQ(tested.scene.Failure(), std::nullopt);
        const int wind = ParticleWindDraws; // The caller goes on with particles during the wait too.
        EXPECT_EQ(tested.cloth.random.State(),
                  StateAfterAStep(FirstStepDraws + InertFishCandidateRandoms + probe.draws + wind));
    }
}

// 0x63710f calls 0x508f00 in the wait too. That step's return already aged the ribbon.
TEST(GameSceneTest, TheWorldWaitAgesItsRibbonOnce)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Harness tested(log);
    tested.Runtime().drawingEnabled = true;
    tested.Runtime().movement.inputBlocked = true;
    tested.Runtime().warpCounter = WorldLoadingWait;
    ASSERT_TRUE(tested.scene.Enter().HasValue());
    auto& ribbon = tested.world.DrawTail().ribbons[0];
    ribbon.alive = true;
    ribbon.life = 3;
    ribbon.count = 4;

    tested.scene.Step(tested.input);

    ASSERT_EQ(tested.scene.Failure(), std::nullopt);
    EXPECT_EQ(ribbon.life, 2);
    EXPECT_EQ(ribbon.count, 3);
}

// Only the dark horse skeleton (0xbf), with a one-key action on one bone.
class HorseSkeletonOnly final : public Client::Models::IModelLibrary
{
public:
    HorseSkeletonOnly()
    {
        Client::Models::SkeletonBone bone;
        bone.parent = -1;
        bone.actions = {{Client::Models::BoneKey{}}};
        m_skeleton.bones = {bone};
        m_skeleton.actions = {
            Client::Models::ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 1, .lockPositions = false}};
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(Client::Models::ModelId /*model*/) const override
    {
        return nullptr;
    }
    [[nodiscard]] const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        if (model.Value() == Client::Tables::PlayerSkeletonModel)
        {
            return &m_player;
        }
        return model.Value() == Client::Pets::DarkHorseModel ? &m_skeleton : nullptr;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot>
    MeshTextures(Client::Models::ModelId /*model*/) const override
    {
        return {};
    }

private:
    Client::Models::Skeleton m_skeleton;
    // The live rider goes through the characters' step (0x4f9580), which asks for Player.bmd.
    Client::Models::Skeleton m_player = Client::Tests::PlayerActionsSkeleton();
};

// 0x6370f4: 0x445230, the helpers, before MoveHero (0x63710a) and in the wait too. With the rider dead, scene 5's
// step turns its horse off and the wait goes on, with MoveHero's 8 rand(). With the rider alive outside a safe cell,
// the breath draws one rand() after the inert map's six fish candidates (with Seed, one rand() before, the frame
// start, the slide and the step's UI, % 3 gives 0) and, with no draw before, comes out of bone 27 at zero in the
// global buffer: the 0x5f2 is born, MoveHero lowers the counter and the wait goes on, with the rider's body advanced
// by Player.bmd (0x4f9580).
TEST(GameSceneTest, TheHorseStepsBeforeMoveHeroAlsoInTheWait)
{
    constexpr std::size_t RiderSlot = 5;
    constexpr int Skipped = 1;
    CrtRandom first = RandomAfter(Skipped + FirstStepDraws + InertFishCandidateRandoms);
    ASSERT_EQ(first.Next() % 3, 0);
    for (const bool riderAlive : {false, true})
    {
        SCOPED_TRACE(riderAlive ? "con el jinete vivo" : "con el jinete muerto");
        Client::Tests::RecordingSink sink;
        Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
        FakeTextureStore store;
        ClothWorld cloth{.random = RandomAfter(Skipped)};
        WorldState world;
        ModuleStates states;
        GiveInertTerrain(states.terrain.current);
        const HorseSkeletonOnly models;
        GameScene scene(SceneContext{.dataRoot = Client::Tests::RepoDataDir(),
                                     .textures = store,
                                     .log = log,
                                     .cloth = &cloth,
                                     .world = &world,
                                     .states = &states},
                        nullptr, &models);
        world.Runtime().drawingEnabled = true;
        world.Runtime().warpCounter = WorldLoadingWait;
        states.terrain.current.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        Client::Kernel::ActorState& rider = world.Actors()[RiderSlot];
        rider = Client::Tests::UnarmedPlayer(1);
        rider.alive = true;
        rider.byte88 = 1;
        rider.position = {10050.0f, 10050.0f, 170.0f};
        const auto created = Client::Pets::CreateHelper(world, Client::Pets::DarkHorseModel, rider.position, RiderSlot);
        ASSERT_TRUE(created.HasValue()) << created.Error();
        ASSERT_EQ(created.Value(), std::optional<std::size_t>{0});
        rider.alive = riderAlive;
        ASSERT_TRUE(scene.Enter().HasValue());

        scene.Step(Client::Input::InputState{});

        if (riderAlive)
        {
            EXPECT_EQ(scene.Failure(), std::nullopt) << scene.Failure().value_or("");
            EXPECT_TRUE(world.Helpers()[0].alive);
            EXPECT_TRUE(world.Particles().All()[0].alive);
            EXPECT_EQ(world.Particles().All()[0].type, Client::Particles::BreathParticle);
            EXPECT_EQ(world.Runtime().warpCounter, WorldLoadingWait - 1);
        }
        else
        {
            EXPECT_EQ(scene.Failure(), std::nullopt);
            EXPECT_FALSE(world.Helpers()[0].alive);
            EXPECT_EQ(world.Runtime().warpCounter, WorldLoadingWait - 1);
            EXPECT_EQ(cloth.random.State(),
                      StateAfterAStep(Skipped + FirstStepDraws + InertFishCandidateRandoms + 8 + ParticleWindDraws));
        }
    }
}

// 0x636da3 and 0x6371a6: without the world loaded and without state 0x3d, the step leaves before MoveHero and the
// draw returns 0 without reading the counter. The frame start (0x4eb620), the first step's slide (0x636920) and the
// cloth run anyway.
TEST(GameSceneTest, WithoutTheLoadedWorldNeitherTheStepNorTheDrawReadTheCounter)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Harness tested(log);
    tested.Runtime().warpCounter = 30;
    ASSERT_TRUE(tested.scene.Enter().HasValue());

    tested.scene.Step(tested.input);
    tested.scene.PrepareFrame();

    EXPECT_TRUE(tested.scene.HoldsTheLastFrame());
    EXPECT_FALSE(tested.Runtime().drawingEnabled);
    EXPECT_EQ(tested.Runtime().warpCounter, 30);
    EXPECT_EQ(tested.cloth.random.State(), StateAfterAStep(FrameStartDraws + SlideStartDraws));
}

// 0x636da3..0x636dac: with the session in the world (the 0x3d that the F3 03 reply publishes), the step marks the
// world loaded even if the F1 02 had lowered it, and goes on to MoveHero.
TEST(GameSceneTest, TheWorldStateTurnsTheDrawingOn)
{
    LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(ChooseSlotThree(online));
    Harness tested(online.log, &online.session);
    ASSERT_TRUE(tested.scene.Enter().HasValue());
    tested.scene.Step(tested.input);
    ASSERT_EQ(online.session.State(), SessionState::EnteringWorld);
    online.eventSink.onWorld = [](const Client::App::WorldEvent&,
                                  Client::App::ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        session.PublishWorldEntry();
        return {};
    };
    // A synthetic 0x34-byte reply: the scene does not look at its fields.
    Client::Network::PacketSerials serials;
    online.socket.reads = {Client::Tests::FromTheGameServer(
        Client::Tests::Bytes({0xC1, 0x34, 0xF3, 0x03, 0x10, 0x20, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                              0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12,
                              0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
                              0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x00, 0x00, 0x26, 0x27, 0x28, 0x29, 0x2A}),
        serials)};
    ASSERT_TRUE(online.session.Poll().HasValue());
    ASSERT_EQ(online.session.State(), SessionState::InWorld);
    tested.Runtime().drawingEnabled = false;
    tested.Runtime().movement.inputBlocked = true;
    tested.Runtime().warpCounter = 31;

    tested.scene.Step(tested.input);
    tested.scene.PrepareFrame();

    EXPECT_TRUE(tested.Runtime().drawingEnabled);
    EXPECT_EQ(tested.Runtime().warpCounter, 30);
    EXPECT_FALSE(tested.scene.HoldsTheLastFrame());
    ASSERT_EQ(tested.scene.Failure(), std::nullopt);
}

// 0x636dc2..0x636ddb: with the world loaded, each step leaves the camera shake at its 0.2 (0x6b8854), in float;
// without the world loaded the step leaves early and does not touch it.
TEST(GameSceneTest, AStepWithTheWorldLeavesAFifthOfTheShake)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    FakeTextureStore store;
    ClothWorld cloth{.random = CrtRandom(Seed)};
    WorldState world;
    ModuleStates states;
    GiveInertTerrain(states.terrain.current);
    Client::Camera::GameCameraInput cameraInput;
    GameScene scene(SceneContext{.dataRoot = Client::Tests::RepoDataDir(),
                                 .textures = store,
                                 .log = log,
                                 .cloth = &cloth,
                                 .cameraInput = &cameraInput,
                                 .world = &world,
                                 .states = &states},
                    nullptr);
    ASSERT_TRUE(scene.Enter().HasValue());
    cameraInput.shake = 3.0f;
    scene.Step(Client::Input::InputState{});
    EXPECT_EQ(cameraInput.shake, 3.0f) << "sin el mundo cargado, 0x636dbc sale antes";

    world.Runtime().drawingEnabled = true;
    world.Runtime().warpCounter = WorldLoadingWait;
    const auto decay = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
    scene.Step(Client::Input::InputState{});
    const auto once = static_cast<float>(3.0 * decay);
    EXPECT_EQ(cameraInput.shake, once);
    scene.Step(Client::Input::InputState{});
    EXPECT_EQ(cameraInput.shake, static_cast<float>(static_cast<double>(once) * decay));
}

// With the frame held the draw neither draws nor stops: only the close of 0x637229 is left.
TEST(GameSceneTest, AHeldFrameDrawsNothing)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Harness tested(log);
    tested.Runtime().drawingEnabled = true;
    tested.Runtime().warpCounter = WaitDrawThreshold + 1;
    ASSERT_TRUE(tested.scene.Enter().HasValue());
    Client::Tests::RecordingFrameTarget target;

    tested.scene.PrepareFrame();
    tested.scene.Render(target.BeginFrame(Client::Core::Duration{}), 0.0f);
    target.Present();

    ASSERT_EQ(target.frames.size(), 1u);
    EXPECT_TRUE(target.frames[0].events.empty());
    EXPECT_EQ(tested.scene.Failure(), std::nullopt);
    EXPECT_FALSE(tested.scene.MouseRay().has_value());
    EXPECT_EQ(tested.cloth.random.State(), RandomAfter(4).State());
}

// The game draw with a map loaded from the repo's Data (0x61a560, MapLoad) and the hero published in its cell,
// without going through the F3 03.
class GameSceneDrawTest : public Client::Tests::GoldenDataTest
{
protected:
    void SetUp() override
    {
        GoldenDataTest::SetUp();
        auto loadedData = Client::Tables::LoadGameData(m_dataRoot);
        ASSERT_TRUE(loadedData.HasValue()) << loadedData.Error();
        data = std::move(loadedData).Value();
        const auto loadedModels = playerModels.Load(m_dataRoot, store, log);
        ASSERT_TRUE(loadedModels.HasValue()) << loadedModels.Error();
    }
    // Map `map` loaded in the game and the hero in slot HeroSlot at cell (x, y), published, with the start camera and
    // the rand() at the seed.
    void Prepare(std::uint8_t map, std::uint8_t x, std::uint8_t y)
    {
        world.Runtime().map = map;
        const auto loaded = Client::Scenes::LoadMap(map, Client::Scenes::MapLoadContext{.dataRoot = m_dataRoot,
                                                                                        .world = world,
                                                                                        .terrain = states.terrain,
                                                                                        .scene = SceneId::Game,
                                                                                        .playerKey = 5,
                                                                                        .random = cloth.random});
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        Client::Kernel::ActorState& hero = world.Actors()[HeroSlot];
        hero.alive = true;
        hero.cellX = x;
        hero.cellY = y;
        hero.position = {(static_cast<float>(x) * 100.0f) + 50.0f, (static_cast<float>(y) * 100.0f) + 50.0f, 165.0f};
        world.Runtime().heroSlot = static_cast<std::int32_t>(HeroSlot);
        world.Runtime().hero = HeroSlot;
        world.Runtime().drawingEnabled = true;
        world.Runtime().warpCounter = WaitDrawThreshold;
        camera = Client::Camera::GameCamera{};
        cameraInput = Client::Camera::GameCameraInput{};
        cloth.random = CrtRandom(Seed);
    }

    // The hero as 0x511290 leaves it: a standing player (action 1) with PK 3 and the safe zone at 1, in Prepare's drawn
    // cell and with the logical cell at (x, y), the one that goes one ahead (06 § Caminar). The own record, with the
    // empty equipment that the F3 03 reply leaves (0x4c47c8).
    void MakeTheHeroAPlayer(std::uint8_t x, std::uint8_t y)
    {
        world.OwnCharacter().ClearEquipmentForEntry();
        Client::Kernel::ActorState& hero = world.Actors()[HeroSlot];
        const glm::vec3 position = hero.position;
        hero = Client::Tests::UnarmedPlayer(1);
        hero.word376 = 0xffff;
        hero.alive = true;
        hero.alpha = hero.alphaTarget = 1.0f;
        hero.head = Client::Kernel::NoItemModel;
        for (auto& part : hero.body)
            part.model = Client::Kernel::NoItemModel;
        hero.byte88 = 1;
        hero.pk = 3;
        hero.animation.action = 1;
        hero.position = position;
        hero.cellX = x;
        hero.cellY = y;
        hero.targetX = x;
        hero.targetY = y;
        hero.safeZone = 1;
    }

    // The TerrainWall word (0x7d0d348) at cell (x, y) of the loaded map.
    [[nodiscard]] std::uint16_t Cell(std::uint8_t x, std::uint8_t y)
    {
        return states.terrain.current.attributes->cells[Client::Data::Terrain::CellIndex(x, y)];
    }

    // The trapezoid the selection draw leaves (0x63475c): the one of its fixed point, with its camera.
    [[nodiscard]] static Client::Terrain::TerrainPolygon SelectionPolygon()
    {
        return Client::Terrain::TerrainPolygonOf(Client::Terrain::CullingScene::CharacterSelect, 54,
                                                 Client::Camera::SelectionCullingPoint,
                                                 Client::Camera::SelectionCamera().angles.z);
    }

    // With `texts`, the Data's Text.bmd (without it, what reads it stops); with faithful pacing, each draw is a frame
    // of the original.
    [[nodiscard]] GameScene Scene(const Client::Tables::TextTable* texts = nullptr,
                                  Client::Kernel::PacingMode pacing = Client::Kernel::PacingMode::Decoupled)
    {
        return GameScene(SceneContext{.dataRoot = m_dataRoot,
                                      .textures = store,
                                      .log = log,
                                      .cloth = &cloth,
                                      .pacing = pacing,
                                      .camera = &camera,
                                      .cameraInput = &cameraInput,
                                      .world = &world,
                                      .states = &states},
                         nullptr, &playerModels, &data.items->table, &playerModels.TextureSlots(), &*data.gates,
                         &*data.skills, texts);
    }

    static constexpr std::size_t HeroSlot = 42;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    FakeTextureStore store;
    ClothWorld cloth{.random = CrtRandom(Seed)};
    WorldState world;
    ModuleStates states;
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput cameraInput;
    Client::Assets::GameModels playerModels;
    Client::Tables::GameData data;
};

// 0x6151fc/0x61520f reserve the two .wav before publishing the WORDs at 0x615232.
class SpearmanRegistrationPlayback final : public Client::Audio::IPlayback
{
public:
    explicit SpearmanRegistrationPlayback(const WorldRuntime& runtime) : m_runtime(runtime) {}
    void PlaySound(std::int32_t, std::optional<glm::vec3>, bool) override {}
    void StopSound(std::int32_t, bool) override {}
    void PlayMusic(std::string_view, bool) override {}
    void StopMusic() override {}
    void SetEffectsVolume(std::uint8_t) override {}
    void SetListener(glm::vec3) override {}
    void RegisterSound(std::int32_t id, std::string_view path) override
    {
        if (id != 0x1bd && id != 0x1be)
            return;
        EXPECT_FALSE(Client::Sound::WrittenSounds(m_runtime.modelSounds, Client::Assets::SpearmanModel));
        registrations.emplace_back(id, path);
    }
    std::vector<std::pair<std::int32_t, std::string>> registrations;

private:
    const WorldRuntime& m_runtime;
};

TEST_F(GameSceneDrawTest, TheCastleSpearmanReservesItsSoundsBeforePublishingTheActionTable)
{
    ASSERT_NO_FATAL_FAILURE(Prepare(30, 80, 80));
    SpearmanRegistrationPlayback audio(world.Runtime());
    world.Runtime().audio = &audio;
    GameScene scene = Scene();
    ASSERT_TRUE(scene.Enter().HasValue());
    Client::Tests::RecordingFrameTarget target;
    scene.PrepareFrame();
    scene.Render(target.BeginFrame(std::chrono::milliseconds{1000}), 0.0f);
    target.Present();
    ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();
    const std::vector<std::pair<std::int32_t, std::string>> expected{
        {0x1bd, "Sound\\BattleCastle\\m_SpearMercAttack.wav"}, {0x1be, "Sound\\BattleCastle\\m_SpearMercDeath.wav"}};
    EXPECT_EQ(audio.registrations, expected);
    const Client::Sound::ModelSounds sounds{-1, -1, 0xeb, 0xeb, 0xec, -1, -1, -1, -1, -1};
    EXPECT_EQ(Client::Sound::WrittenSounds(world.Runtime().modelSounds, Client::Assets::SpearmanModel), sounds);
    scene.PrepareFrame();
    scene.Render(target.BeginFrame(std::chrono::milliseconds{1040}), 0.0f);
    target.Present();
    EXPECT_EQ(audio.registrations, expected);
}

// The first model batch holds terrain, grass and objects; the tail can add other batches.
const Client::Tests::FrameEvent* ModelsOf(const Client::Tests::RecordedFrame& frame)
{
    const Client::Tests::FrameEvent* models = nullptr;
    for (const Client::Tests::FrameEvent& event : frame.events)
    {
        if (event.kind == Client::Tests::FrameEvent::Kind::Models)
        {
            if (models == nullptr)
                models = &event;
        }
    }
    return models;
}

struct GrassHeights
{
    float lowest = std::numeric_limits<float>::max();
    float highest = std::numeric_limits<float>::lowest();
};

GrassHeights GrassHeightsOf(const Client::Tests::FrameEvent& models, std::uint16_t slot)
{
    GrassHeights heights;
    for (const Client::Render::ModelDrawCommand& command : models.modelCommands)
    {
        if (command.state.texture != Client::Render::TextureSlot{slot})
        {
            continue;
        }
        for (std::size_t v = 0; v < command.vertexCount; ++v)
        {
            const float z = models.modelVertices[command.firstVertex + v].position.z;
            heights.lowest = std::min(heights.lowest, z);
            heights.highest = std::max(heights.highest, z);
        }
    }
    return heights;
}

// The objects the draw saw (+0x16c), in the order of 0x6046a0.
std::vector<std::size_t> SeenObjects(const WorldState& world)
{
    std::vector<std::size_t> seen;
    for (const std::size_t index : Client::WorldObjects::DrawOrder(world.Map().objects))
    {
        if (world.Map().objects[index].seen)
        {
            seen.push_back(index);
        }
    }
    return seen;
}

// What the Lorencia draw sees with the hero at (143, 120), the trace's cell: 83 objects of sectors (7, 7), (8, 7),
// (8, 8), (9, 6), (9, 7) and (9, 8), with the 0x69 (Waterspout01, 1720), which with the seed draws an odd
// rand() % 2 and drops no water (*simulated*, with MuRet's trapezoid).
constexpr std::array<std::size_t, 83> LorenciaSeen{
    1366, 1372, 1373, 1374, 1378, 1400, 1432, 1686, 1687, 1688, 1689, 1690, 1691, 1693, 1694, 1695, 1696,
    1697, 1698, 1699, 1700, 1701, 1702, 1703, 1704, 1705, 1706, 1707, 1709, 1710, 1711, 1712, 1713, 1714,
    1715, 1717, 1718, 1719, 1720, 1721, 1740, 1742, 1745, 1748, 1749, 1750, 1751, 1754, 1896, 1899, 1900,
    1901, 1902, 1909, 1910, 1911, 1912, 1913, 1920, 1921, 1922, 1923, 1924, 1926, 1927, 1928, 1929, 1930,
    1931, 1933, 1934, 1935, 1936, 1937, 1940, 1941, 1943, 1950, 1963, 1978, 1986, 1987, 1988};
// And Devias's with the hero at (220, 30): 54 objects of sectors (12, 1) to (14, 2), none with a case of 0x602ed0
// (*simulated*).
constexpr std::array<std::size_t, 54> DeviasSeen{
    1262, 1263, 1264, 1265, 1273, 1276, 1277, 1278, 1282, 1457, 1462, 1463, 1464, 1465, 1466, 1468, 1469, 1470,
    1471, 1472, 1473, 1478, 1480, 1481, 1482, 1483, 1485, 1486, 1713, 1715, 1716, 1717, 1722, 1723, 1724, 1729,
    1733, 1738, 1746, 1747, 1752, 1753, 1754, 1756, 1757, 1759, 1770, 1774, 1777, 1778, 1779, 1788, 1789, 1790};

// The game's terrain textures: tiles 0x23 to 0x30 and 0x122.
bool IsTerrainTile(std::uint16_t slot)
{
    return (slot >= Client::Terrain::FirstTileSlot && slot < Client::Terrain::FirstTileSlot + 14) || slot == 0x122;
}

// 0x63723a and the draw's tail in Lorencia (0) and Devias (2), the reference maps: the camera with the two accesses
// to the hero's cell (8 rand() after the 4 of 0x637229), the mouse ray, the terrain's first pass (the cells, with
// tiles 0x23 to 0x30 and 0x122), the grass pass (0x5fc353, no rand()) with the one grass texture each world has, the
// ground marks (0x5fc365), which with the .bss table draw none and take no rand(), and the objects (0x6374ce,
// 0x6046a0): those the trapezoid sees, sector by sector, each with its rand() of 0x602ef4 and the textures of its
// model (Object1\ and Object3\, from 0x41c), after the terrain; Lorencia's 0x69, with its four meshes and the
// rand() % 2 of 0x603011. Then, with no model effects (the map load turns them off), no birds and no hero seen
// (0x511290 leaves it unseen), the ground effects and the characters neither draw nor take rand(), and 0x42da60
// opens the TerrainWall gates (Q-C276). With the real material tables, the remaining empty passes complete the
// frame and present it.
TEST_F(GameSceneDrawTest, TheReferenceMapsDrawTheTerrainTheGrassTheGroundMarksAndTheObjects)
{
    struct Case
    {
        std::uint8_t map;
        std::uint8_t x;
        std::uint8_t y;
        // World1 has only TileGrass01.OZT and World3 only TileGrass02.OZT: the other two are skipped.
        std::uint16_t grass;
        std::span<const std::size_t> seen;
        // The rand() of 0x602ef4: one per seen object that reaches 0x602ed0; and the rand() % 2 of the 0x69.
        int objectDraws;
        // The cells (x, 115) of the gate (119, 114) and, in Devias, the (x, 205) of the (93, 204), with the 0x04 before
        // the draw.
        std::span<const std::uint8_t> gateRows;
    };
    constexpr std::array<std::uint8_t, 1> LorenciaGates{115};
    constexpr std::array<std::uint8_t, 2> DeviasGates{115, 205};
    for (const Case& probe : {Case{0, 143, 120, 0x32, LorenciaSeen, 83 + 1, LorenciaGates},
                              Case{2, 220, 30, 0x33, DeviasSeen, 54, DeviasGates}})
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        ASSERT_NO_FATAL_FAILURE(Prepare(probe.map, probe.x, probe.y));
        GameScene scene = Scene();
        ASSERT_TRUE(scene.Enter().HasValue());
        Client::Tests::RecordingFrameTarget target;
        for (const std::uint8_t row : probe.gateRows)
        {
            const std::uint8_t gate = row == 115 ? 119 : 93;
            ASSERT_EQ(Cell(gate, row) & 0x04, 0x04) << static_cast<int>(row);
        }
        ASSERT_FALSE(world.Actors()[HeroSlot].seen);

        scene.PrepareFrame();
        ASSERT_FALSE(scene.HoldsTheLastFrame());
        scene.Render(target.BeginFrame(std::chrono::milliseconds{1000}), 0.0f);

        ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();
        ASSERT_EQ(target.frames.size(), 1u);
        const Client::Tests::FrameEvent* models = ModelsOf(target.frames[0]);
        ASSERT_NE(models, nullptr);
        EXPECT_FALSE(models->modelVertices.empty());
        std::size_t grassVertices = 0;
        std::size_t objectVertices = 0;
        for (const Client::Render::ModelDrawCommand& command : models->modelCommands)
        {
            ASSERT_TRUE(command.state.texture.has_value());
            const std::uint16_t slot = command.state.texture->value;
            if (IsTerrainTile(slot))
            {
                EXPECT_EQ(objectVertices, 0u) << "los objetos van después del terreno: " << slot;
                EXPECT_EQ(grassVertices, 0u) << "el pasto va después de todo el terreno";
            }
            else if (slot == probe.grass)
            {
                EXPECT_EQ(objectVertices, 0u) << "los objetos van después del pasto";
                EXPECT_EQ(command.state.blend, Client::Render::ModelBlend::Alpha);
                grassVertices += command.vertexCount;
            }
            else
            {
                // The real table reuses global textures below 0x41c by name.
                objectVertices += command.vertexCount;
            }
        }
        EXPECT_GT(grassVertices, 0u);
        EXPECT_EQ(grassVertices % 6, 0u);
        EXPECT_GT(objectVertices, 0u);
        EXPECT_EQ(objectVertices % 3, 0u);
        const std::vector<std::size_t> seen = SeenObjects(world);
        EXPECT_TRUE(std::ranges::equal(seen, probe.seen));
        EXPECT_TRUE(scene.MouseRay().has_value());
        EXPECT_EQ(cameraInput.hero, world.Actors()[HeroSlot].position);
        EXPECT_EQ(cloth.random.State(),
                  RandomAfter(4 + 8 + probe.objectDraws + FramePickingDraws + HudBaseDraws).State());
        for (const std::uint8_t row : probe.gateRows)
        {
            const std::uint8_t gate = row == 115 ? 119 : 93;
            for (int dx = -1; dx <= 2; ++dx)
            {
                EXPECT_EQ(Cell(static_cast<std::uint8_t>(gate + dx), row) & 0x04, 0) << dx << ", " << int{row};
            }
        }
        EXPECT_NE(std::ranges::find(store.loaded, Client::Terrain::FirstTileSlot), store.loaded.end());
        EXPECT_NE(std::ranges::find(store.loaded, std::uint16_t{0x122}), store.loaded.end());
        EXPECT_NE(std::ranges::find(store.loaded, Client::Assets::WorldModelsFirstSlot.value), store.loaded.end());
        for (std::uint16_t slot = 0x32; slot <= 0x34; ++slot)
        {
            EXPECT_EQ(store.Find(Client::Render::TextureSlot{slot}).has_value(), slot == probe.grass) << slot;
        }
        if (probe.map == 0)
        {
            // The Lorencia trace, with this hero and this camera: 25 quads of TileGrass01, z 165 to 293 (06 § El pasto,
            // *verified*).
            EXPECT_EQ(grassVertices / 6, 25u);
            const auto [lowest, highest] = GrassHeightsOf(*models, probe.grass);
            EXPECT_EQ(lowest, 165.0f);
            EXPECT_EQ(highest, 293.0f);
        }

        target.Present();
        EXPECT_TRUE(target.frames.back().presented);
        scene.Leave();
        EXPECT_FALSE(store.Find(Client::Render::TextureSlot{probe.grass}).has_value());
        EXPECT_FALSE(store.Find(Client::Render::TextureSlot{Client::Terrain::FirstTileSlot}).has_value());
    }
}

// 0x635ead..0x635ed8 and 0x637324: with the camera tour running (0x4033d0: stage 2, without the wait or the cut), the
// camera and the world centre of each frame of the original follow its position (0x4034b0) and the hero's z stays at
// 300 (0x635ec0): the frame is that of a hero standing at that point at 300, with its logical cell. Cut (+0x158) or
// at another stage, the next frame goes back to the hero, who stays at 300 because standing still nobody recomputes
// its z. The level word (0x7d3a0ec) is the tour's +0x150: the trapezoid takes it in the same frame (0x5fb570), D's
// target too (0x6361d0) and the far in the next one (0x635e00); without the tour, the trapezoid keeps the level
// until D goes back to 1000. In Lorencia, Devias and Crywolf.
TEST_F(GameSceneDrawTest, TheCameraFollowsTheTourWhileItRunsAndThenTheHeroAgain)
{
    struct Case
    {
        std::uint8_t map;
        std::uint8_t x;
        std::uint8_t y;
        // true: the BD 00 leaves another stage (0x4033b0, stage 3); false: Esc or the end of the script (+0x158,
        // 0x623edc and 0x404bc6).
        bool otherStage;
    };
    struct Drawn
    {
        Client::Render::Pass3D pass;
        Client::Terrain::TerrainPolygon polygon;
        std::vector<std::size_t> seen;
        std::uint32_t random = 0;
    };
    constexpr Client::Core::Duration FrameTime = Client::Models::OriginalFrameTime;
    constexpr Client::Core::Duration FirstFrame = std::chrono::milliseconds{1000};
    for (const Case& probe :
         {Case{0, 143, 120, false}, Case{2, 220, 30, true}, Case{34, 120, 114, false}, Case{34, 120, 114, true}})
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        SCOPED_TRACE(probe.otherStage);
        const glm::vec3 tourPoint{(static_cast<float>(probe.x + 6) * 100.0f) + 50.0f,
                                  (static_cast<float>(probe.y + 4) * 100.0f) + 50.0f, 400.0f};
        // A frame of the original at `at`: its model pass, the trapezoid, the seen objects and the rand().
        const auto draw = [&](GameScene& scene, Client::Tests::RecordingFrameTarget& target, Client::Core::Duration at)
        {
            scene.PrepareFrame();
            EXPECT_FALSE(scene.HoldsTheLastFrame());
            scene.Render(target.BeginFrame(at), 0.0f);
            EXPECT_FALSE(scene.Failure().has_value()) << scene.Failure().value_or("");
            // The pass is copied before Present(): Sync() adds events to the frame and the vector can move.
            const Client::Tests::FrameEvent* models = ModelsOf(target.frames.back());
            const std::optional<Client::Render::Pass3D> pass =
                models != nullptr ? models->pass : std::optional<Client::Render::Pass3D>{};
            target.Present();
            Drawn drawn{.pass = {},
                        .polygon = states.camera.trapezoid,
                        .seen = SeenObjects(world),
                        .random = cloth.random.State()};
            if (pass)
                drawn.pass = *pass;
            else
                ADD_FAILURE() << "sin el pase de los modelos";
            return drawn;
        };

        // The twin without the tour: the hero drawn at the tour's point at 300 and in the same logical cell.
        ASSERT_NO_FATAL_FAILURE(Prepare(probe.map, probe.x, probe.y));
        world.Actors()[HeroSlot].position = {tourPoint.x, tourPoint.y, Client::Camera::CameraTourHeroHeight};
        Drawn twin;
        {
            GameScene scene = Scene();
            ASSERT_TRUE(scene.Enter().HasValue());
            Client::Tests::RecordingFrameTarget target;
            twin = draw(scene, target, FirstFrame);
            EXPECT_EQ(camera.level, 0);
            scene.Leave();
        }
        // And the hero's in its place, already at 300, as the tour leaves it.
        ASSERT_NO_FATAL_FAILURE(Prepare(probe.map, probe.x, probe.y));
        const glm::vec2 heroPlace{world.Actors()[HeroSlot].position};
        world.Actors()[HeroSlot].position.z = Client::Camera::CameraTourHeroHeight;
        Drawn standing;
        {
            GameScene scene = Scene();
            ASSERT_TRUE(scene.Enter().HasValue());
            Client::Tests::RecordingFrameTarget target;
            standing = draw(scene, target, FirstFrame);
            scene.Leave();
        }

        // The tour running with `level`, in a scene that draws one frame of the original per draw.
        const auto startTheTour = [&](std::int32_t level)
        {
            Client::Events::CameraTour& tour = world.Runtime().events.cameraTour;
            tour = Client::Events::CameraTour{};
            tour.stage = 2;
            tour.waiting = false;
            tour.position = tourPoint;
            tour.level = level;
        };
        const auto stopTheTour = [&]
        {
            Client::Events::CameraTour& tour = world.Runtime().events.cameraTour;
            if (probe.otherStage)
                tour.stage = 3;
            else
                tour.skipped = true;
        };

        // With level 0: the twin's frame and, without the tour, the hero's at 300.
        {
            ASSERT_NO_FATAL_FAILURE(Prepare(probe.map, probe.x, probe.y));
            startTheTour(0);
            GameScene scene = Scene(nullptr, Client::Kernel::PacingMode::Faithful);
            ASSERT_TRUE(scene.Enter().HasValue());
            Client::Tests::RecordingFrameTarget target;
            const Drawn first = draw(scene, target, FirstFrame);
            ASSERT_TRUE(cameraInput.tour.has_value());
            EXPECT_EQ(cameraInput.tour->position, tourPoint);
            EXPECT_EQ(world.Actors()[HeroSlot].position,
                      glm::vec3(heroPlace.x, heroPlace.y, Client::Camera::CameraTourHeroHeight));
            EXPECT_EQ(cameraInput.hero, world.Actors()[HeroSlot].position);
            EXPECT_EQ(first.pass.view, twin.pass.view);
            EXPECT_EQ(first.pass.projection, twin.pass.projection);
            EXPECT_EQ(first.pass.farPlane, twin.pass.farPlane);
            EXPECT_EQ(first.polygon.x, twin.polygon.x);
            EXPECT_EQ(first.polygon.y, twin.polygon.y);
            EXPECT_EQ(first.seen, twin.seen);
            EXPECT_EQ(first.random, twin.random);
            EXPECT_NE(first.pass.view, standing.pass.view);

            stopTheTour();
            const Drawn back = draw(scene, target, FirstFrame + FrameTime);
            EXPECT_FALSE(cameraInput.tour.has_value());
            EXPECT_EQ(back.pass.view, standing.pass.view);
            EXPECT_EQ(back.pass.farPlane, standing.pass.farPlane);
            EXPECT_EQ(back.polygon.x, standing.polygon.x);
            EXPECT_EQ(back.polygon.y, standing.polygon.y);
            EXPECT_EQ(back.seen, standing.seen);
            EXPECT_EQ(world.Actors()[HeroSlot].position.z, Client::Camera::CameraTourHeroHeight)
                << "0x635ec0 no vuelve: el héroe parado se queda a 300";
            scene.Leave();
        }

        // With level 2: the first frame's view is the twin's (the D from before the smoothing), but the trapezoid and
        // D's target are already level 2's; the far, from the second.
        ASSERT_NO_FATAL_FAILURE(Prepare(probe.map, probe.x, probe.y));
        startTheTour(2);
        GameScene scene = Scene(nullptr, Client::Kernel::PacingMode::Faithful);
        ASSERT_TRUE(scene.Enter().HasValue());
        Client::Tests::RecordingFrameTarget target;
        Client::Core::Duration at = FirstFrame;
        const Drawn leveled = draw(scene, target, at);
        EXPECT_EQ(camera.level, 2);
        EXPECT_EQ(camera.targetDistance, 1200.0f);
        EXPECT_EQ(states.camera.trapezoidLevel.effectiveLevel, 2);
        EXPECT_EQ(leveled.pass.farPlane, 2000.0f);
        EXPECT_EQ(leveled.pass.view, twin.pass.view);
        EXPECT_NE(leveled.polygon.x, twin.polygon.x);
        at += FrameTime;
        const Drawn away = draw(scene, target, at);
        EXPECT_EQ(away.pass.farPlane, 2600.0f);
        EXPECT_NE(away.pass.view, twin.pass.view) << "la D ya subió hacia 1.200";

        // Without the tour: the far reads the 2 once more and the target goes back to 1000, but with D above it the
        // trapezoid keeps level 2 (0x5fb70e..0x5fb73d) until D goes back to 1000.
        stopTheTour();
        at += FrameTime;
        const Drawn back = draw(scene, target, at);
        EXPECT_FALSE(cameraInput.tour.has_value());
        EXPECT_EQ(camera.level, 0);
        EXPECT_EQ(camera.targetDistance, 1000.0f);
        EXPECT_EQ(back.pass.farPlane, 2600.0f);
        EXPECT_GT(camera.distance, 1001.0f);
        EXPECT_EQ(states.camera.trapezoidLevel.effectiveLevel, 2);
        int frames = 1;
        while (states.camera.trapezoidLevel.effectiveLevel != 0 && frames < 30)
        {
            at += FrameTime;
            static_cast<void>(draw(scene, target, at));
            ++frames;
        }
        EXPECT_EQ(states.camera.trapezoidLevel.effectiveLevel, 0);
        EXPECT_LT(camera.distance, 1001.0f);
        EXPECT_GT(frames, 5);
        scene.Leave();
    }
}

// 0x63706a: the step after the draw updates the roofs and the blendMesh of the seen objects. The draw uses full
// resources; the step isolates these objects so their three rand() do not mix with fire or door creators. It goes
// on to characters, wind and cloth without stopping.
TEST_F(GameSceneDrawTest, TheStepAfterADrawStepsTheSeenObjects)
{
    struct Case
    {
        std::uint8_t map;
        std::uint8_t x;
        std::uint8_t y;
        std::int32_t layer;
        int drawDraws;
        std::vector<std::size_t> roofs;
        std::vector<std::size_t> flickers;
        int stepDraws;
    };
    for (const Case& probe :
         {Case{0, 143, 120, 4, 4 + 8 + 83 + 1 + HudBaseDraws, {1400}, {1366, 1372, 1690}, 3},
          Case{2, 220, 30, 3, 4 + 8 + 54 + HudBaseDraws, {1282, 1471, 1472, 1473}, {1262, 1462, 1464}, 3}})
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        ASSERT_NO_FATAL_FAILURE(Prepare(probe.map, probe.x, probe.y));
        MakeTheHeroAPlayer(probe.x, probe.y);
        {
            GameScene draw = Scene();
            ASSERT_TRUE(draw.Enter().HasValue());
            Client::Tests::RecordingFrameTarget target;
            draw.PrepareFrame();
            draw.Render(target.BeginFrame(std::chrono::milliseconds{1000}), 0.0f);
            ASSERT_FALSE(draw.Failure().has_value()) << *draw.Failure();
            draw.Leave();
        }
        ASSERT_EQ(cloth.random.State(), RandomAfter(probe.drawDraws).State());
        world.Runtime().heroCellLayer = probe.layer;
        std::vector<Client::Kernel::WorldObject>& objects = world.Map().objects;
        for (const std::size_t roof : probe.roofs)
        {
            ASSERT_TRUE(objects[roof].seen) << roof;
            ASSERT_EQ(objects[roof].targetAlpha, 1.0f) << roof;
        }
        // What each one draws: the step's rand(), in the objects' order.
        CrtRandom flicker = RandomAfter(probe.drawDraws + FirstStepDraws);
        std::vector<float> lights;
        for (std::size_t k = 0; k < probe.flickers.size(); ++k)
        {
            lights.push_back(
                static_cast<float>(static_cast<double>((flicker.Next() & 3) + 4) * static_cast<double>(0.1f)));
        }
        // Contractual isolation of the roof and blendMesh pass: the previous draw checked the whole seen set; this step
        // only publishes the objects under test again. The fire and door creators have their own service tests.
        for (auto& object : objects)
            object.seen = false;
        for (const auto slot : probe.roofs)
            objects[slot].seen = true;
        for (const auto slot : probe.flickers)
            objects[slot].seen = true;
        const auto heroBefore = world.Actors()[HeroSlot];
        const auto weatherBefore = world.Runtime().leafWeather;
        ClothWorld reference = cloth;
        reference.random = Advanced(reference.random, FirstStepDraws + probe.stepDraws);
        GameScene scene = Scene();
        ASSERT_TRUE(scene.Enter().HasValue());

        scene.Step(Client::Input::InputState{});

        ASSERT_EQ(scene.Failure(), std::nullopt);
        ASSERT_TRUE(
            AdvanceEnvironmentPrefix(world, states, heroBefore, weatherBefore, probe.layer, reference).HasValue());
        // MoveHero, 0x57cfc0, TerrainWall, body, seen cell and wind, after the three blendMesh.
        reference.random =
            Advanced(reference.random, 8 + HeroActionPrefixRandoms + 8 + HeroBodyDraws + 8 + 1 + ParticleWindDraws +
                                           GateProbeRandoms(*data.gates, world.Runtime().map, heroBefore));
        Client::Models::StepTheClothWorld(reference);

        for (const std::size_t roof : probe.roofs)
        {
            EXPECT_EQ(objects[roof].targetAlpha, 0.0f) << roof;
            EXPECT_LT(objects[roof].alpha, 1.0f) << roof;
        }
        for (std::size_t k = 0; k < probe.flickers.size(); ++k)
        {
            EXPECT_EQ(objects[probe.flickers[k]].state.light, lights[k]) << probe.flickers[k];
        }
        EXPECT_TRUE(world.Actors()[HeroSlot].seen);
        EXPECT_EQ(world.Runtime().warpCounter, WaitDrawThreshold - 1);
        EXPECT_EQ(cloth.random.State(), reference.random.State());
        scene.Leave();
        EXPECT_FALSE(store.Find(Client::Render::TextureSlot{Client::Terrain::FirstTileSlot}).has_value());
        world.Runtime().heroCellLayer = 0;
    }
}

// 0x606560 on a map without the 0x604ab0 cases ported: with one seen object (those maps stop earlier, in the load or
// in 0x6374ce; here map 18 is loaded and the map changed by hand), the step stops before touching it, with the
// step's UI rand() and no more.
TEST_F(GameSceneDrawTest, TheStepStopsAtTheSeenObjectsOfTheOtherMaps)
{
    ASSERT_NO_FATAL_FAILURE(Prepare(18, 128, 128));
    world.Runtime().map = 35;
    ASSERT_FALSE(world.Map().objects.empty());
    world.Map().objects.front().seen = true;
    const Client::Kernel::WorldObject before = world.Map().objects.front();
    GameScene scene = Scene();
    ASSERT_TRUE(scene.Enter().HasValue());

    scene.Step(Client::Input::InputState{});

    ASSERT_TRUE(scene.Failure().has_value());
    EXPECT_NE(scene.Failure()->find("0x606560"), std::string::npos) << *scene.Failure();
    EXPECT_EQ(world.Map().objects.front().alpha, before.alpha);
    EXPECT_EQ(cloth.random.State(), RandomAfter(FirstStepDraws).State());
    scene.Leave();
}

// 0x5fa973..0x5fa9b1 and 0x5fc33e: Atlans, at (226, 103), with water in layer 2 nearby, draws the whole frame: the
// water of 0x5fa4b0, additive and with the caustic 0x41 + the frame of DAT_07d2d360, and no grass quad
// (0x32..0x34), which map 7 skips even though it has 0x6c9d58.
TEST_F(GameSceneDrawTest, AtlansDrawsItsCausticWaterAndNoGrass)
{
    ASSERT_NO_FATAL_FAILURE(Prepare(7, 226, 103));
    GameScene scene = Scene();
    ASSERT_TRUE(scene.Enter().HasValue());
    Client::Tests::RecordingFrameTarget target;

    scene.PrepareFrame();
    scene.Render(target.BeginFrame(std::chrono::milliseconds{1000}), 0.0f);

    ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();
    const Client::Tests::FrameEvent* models = ModelsOf(target.frames.at(0));
    ASSERT_NE(models, nullptr);
    const auto caustic = Client::Render::TextureSlot{static_cast<std::uint16_t>(0x41 + cloth.waterTextureFrame)};
    std::size_t water = 0;
    for (const Client::Render::ModelDrawCommand& command : models->modelCommands)
    {
        ASSERT_TRUE(command.state.texture.has_value());
        const std::uint16_t slot = command.state.texture->value;
        EXPECT_FALSE(slot >= 0x32 && slot <= 0x34) << slot;
        if (slot >= 0x41 && slot <= 0x60)
        {
            EXPECT_EQ(*command.state.texture, caustic);
            EXPECT_EQ(command.state.blend, Client::Render::ModelBlend::Additive);
            ++water;
        }
    }
    EXPECT_GT(water, 0u);
    scene.Leave();
}

// The HUD's 12 (0x5a9050) reads the projection of the frame's world pass (0x6203c0): the hero's bubble ends up
// measured and in its 640 × 480 place, which at 640 × 480 is where its box goes.
TEST_F(GameSceneDrawTest, TheFrameDrawsTheChatBubblesWithTheWorldProjection)
{
    ASSERT_NO_FATAL_FAILURE(Prepare(7, 226, 103));
    // A player: with another type, 0x50ba30 would draw it as a monster with that number's model.
    MakeTheHeroAPlayer(226, 103);
    GameScene scene = Scene();
    ASSERT_TRUE(scene.Enter().HasValue());
    world.Actors()[HeroSlot].seen = true;
    Client::Chat::ChatBubble& bubble = world.Runtime().chat.bubbles[0];
    bubble.name = "Tero";
    bubble.nameTime = 100;
    bubble.owner = HeroSlot;
    Client::Tests::RecordingFrameTarget target;

    scene.PrepareFrame();
    scene.Render(target.BeginFrame(std::chrono::milliseconds{1000}), 0.0f);

    ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();
    EXPECT_EQ(bubble.width, 4 * 6 + 3);
    // Above the hero, whom the camera leaves near the centre.
    EXPECT_GT(bubble.x, 0);
    EXPECT_GT(bubble.y, 0);
    const std::vector<Client::Tests::RecordedText>& texts = target.Texts();
    const auto named = std::ranges::find(texts, std::string("Tero"), &Client::Tests::RecordedText::text);
    ASSERT_NE(named, texts.end());
    ASSERT_TRUE(named->box.has_value());
    EXPECT_EQ(named->x, bubble.x);
    EXPECT_EQ(named->y, bubble.y);
    scene.Leave();
}

// 0x5a7eb0 (0x637668): with the F3 22 word (0x5767816) at 1 or more and up to 10 minutes (659 s), the time comes out
// after the HUD with texts 1390 and 1391 of the Data's Text.bmd (test_event_drawing looks at the drawing); with 11
// minutes (660 s) or at 0, it leaves without drawing. It never stops.
TEST_F(GameSceneDrawTest, TheEventTimeOfTheF322DrawsUpToTenMinutes)
{
    struct Case
    {
        std::uint8_t map;
        std::uint8_t x;
        std::uint8_t y;
    };
    for (const Case& probe : {Case{0, 143, 120}, Case{2, 220, 30}})
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        ASSERT_NO_FATAL_FAILURE(Prepare(probe.map, probe.x, probe.y));
        for (const std::uint16_t time : {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{659}, std::uint16_t{660}})
        {
            SCOPED_TRACE(time);
            world.Runtime().events.timeValue = time;
            GameScene scene = Scene(&*data.text);
            ASSERT_TRUE(scene.Enter().HasValue());
            Client::Tests::RecordingFrameTarget target;

            scene.PrepareFrame();
            scene.Render(target.BeginFrame(std::chrono::milliseconds{1000}), 0.0f);

            EXPECT_FALSE(scene.Failure().has_value()) << *scene.Failure();
            scene.Leave();
        }
    }
    world.Runtime().events.timeValue = 0;
}

// 0x5fc365 with a blood mark on (as 0x52e810 and 0x505de0 leave it): 0x549130 reads byte +0x28 of slot 0x5e3,
// Effect\blood01.tga, which the game uploads with the effect textures (0x61d7aa): 32 bits, four components, so it
// goes with alpha blending and its decal (0x5fb2f0) comes out with that texture. It takes no rand(): the frame ends
// with the same rand() as without marks (08 P-C49).
TEST_F(GameSceneDrawTest, ALiveBloodMarkIsDrawnWithBlood01)
{
    struct Case
    {
        std::uint8_t map;
        std::uint8_t x;
        std::uint8_t y;
        int objectDraws;
    };
    for (const Case& probe : {Case{0, 143, 120, 83 + 1}, Case{2, 220, 30, 54}})
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        ASSERT_NO_FATAL_FAILURE(Prepare(probe.map, probe.x, probe.y));
        states.terrain.groundMarks = {};
        states.terrain.groundMarks[17] = Client::Terrain::GroundMark{.live = true,
                                                                     .type = 0x5e3,
                                                                     .size = 0.65f,
                                                                     .position = world.Actors()[HeroSlot].position,
                                                                     .color = glm::vec3(0.1f, 0.0f, 0.0f),
                                                                     .alpha = 1.0f,
                                                                     .life = 60};
        GameScene scene = Scene();
        ASSERT_TRUE(scene.Enter().HasValue());
        Client::Tests::RecordingFrameTarget target;

        scene.PrepareFrame();
        scene.Render(target.BeginFrame(std::chrono::milliseconds{1000}), 0.0f);

        ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();
        const Client::Tests::FrameEvent* models = ModelsOf(target.frames.at(0));
        ASSERT_NE(models, nullptr);
        std::size_t decalVertices = 0;
        for (const Client::Render::ModelDrawCommand& command : models->modelCommands)
        {
            if (command.state.texture != Client::Render::TextureSlot{0x5e3})
                continue;
            EXPECT_EQ(command.state.blend, Client::Render::ModelBlend::Alpha);
            decalVertices += command.vertexCount;
        }
        EXPECT_GT(decalVertices, 0u);
        EXPECT_EQ(cloth.random.State(),
                  RandomAfter(4 + 8 + probe.objectDraws + FramePickingDraws + HudBaseDraws).State());
        scene.Leave();
    }
    states.terrain.groundMarks = {};
}

// Maps 18 to 23 have no grass pass (0x6c9d58 at 0), and 0x5fa830 calls 0x405e00 with each first-pass cell without
// bit 0x08 (0x5facb6): one rand() per cell and, with the area at 0xff, nothing more (0x405e1e..0x405e2f). With
// World19's terrain set by hand and every cell hidden but the hero's, the frame ends with the usual 4 + 8 rand() and
// one for that cell, which is drawn.
TEST_F(GameSceneDrawTest, ChaosCastleDrawsRandomForEachShownCellAndNoGrass)
{
    constexpr std::uint8_t ChaosCastle = 18;
    auto terrain = Client::Terrain::LoadWorldTerrain(m_dataRoot, ChaosCastle);
    ASSERT_TRUE(terrain.HasValue()) << terrain.Error();
    states.terrain.current = std::move(terrain).Value();
    for (std::uint16_t& cell : states.terrain.current.attributes->cells)
        cell = static_cast<std::uint16_t>(cell | Client::Terrain::HiddenCellBit);
    const auto heroCell = Client::Data::Terrain::CellIndex(128, 128);
    states.terrain.current.attributes->cells[heroCell] = static_cast<std::uint16_t>(
        states.terrain.current.attributes->cells[heroCell] & ~Client::Terrain::HiddenCellBit);
    world.Runtime().map = ChaosCastle;
    Client::Kernel::ActorState& hero = world.Actors()[HeroSlot];
    hero.alive = true;
    hero.cellX = 128;
    hero.cellY = 128;
    hero.position = {12850.0f, 12850.0f, 165.0f};
    world.Runtime().heroSlot = static_cast<std::int32_t>(HeroSlot);
    world.Runtime().hero = HeroSlot;
    world.Runtime().drawingEnabled = true;
    world.Runtime().warpCounter = WaitDrawThreshold;
    GameScene scene = Scene();
    ASSERT_TRUE(scene.Enter().HasValue());
    Client::Tests::RecordingFrameTarget target;

    scene.PrepareFrame();
    scene.Render(target.BeginFrame({}), 0.0f);

    ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();
    const Client::Tests::FrameEvent* models = ModelsOf(target.frames[0]);
    ASSERT_NE(models, nullptr);
    EXPECT_FALSE(models->modelVertices.empty());
    EXPECT_EQ(cloth.random.State(), RandomAfter(4 + 8 + 1 + FramePickingDraws + HudBaseDraws).State());
    scene.Leave();
}

// 0x63710f and 0x63475c: the first step after the wait reaches the characters (0x508f00) before the first game draw,
// with the trapezoid the last selection draw left: that of its fixed point (SelectionCullingPoint), which does not
// follow the hero (Q-C216). The TerrainWall loses the 0x02 (the one at (221, 11) in Lorencia, Q-C170) and marks the
// logical cell. In Lorencia the hero leaving town westward, from (96, 126) to (95, 126), falls inside: with the
// logical cell it ends up outside the safe zone. In Devias the one leaving northward, from (220, 76) to (220, 77),
// falls outside and +0x3e2 stays as it was. The rand(): MoveHero's 8, the TerrainWall's 8, the body's (0x4fd5f0: the
// own record and the rand() % 6) and, with the hero seen, the cell's 8 and the rand() & 7; then the particle wind
// and the cloth. The draw that follows goes through the ground marks (0x5fc365) and the objects (0x6374ce) with
// their 4 + 8 and one per seen object (20 in Lorencia, 44 in Devias, none with a case), the ground effects with none
// alive, and leaves its own trapezoid. In the characters (0x510d00), Lorencia draws the seen hero without weapons
// outside the safe zone, after the 3848 rand() of the own color and the rand() of 0x50db57; Devias does not see it.
// Both open the gates and present the full frame. The oracle puts leaves/fauna first, probes Gate.bmd and closes
// own/Skill before the body.
TEST_F(GameSceneDrawTest, TheFirstStepAfterTheWaitReadsTheTrapezoidOfTheSelection)
{
    struct Case
    {
        std::uint8_t map;
        std::uint8_t drawnX;
        std::uint8_t drawnY;
        std::uint8_t logicalX;
        std::uint8_t logicalY;
        bool seen;
        std::uint8_t safeZone;
        int draws;
        int objectDraws;
    };
    for (const Case& probe : {Case{0, 96, 126, 95, 126, true, 0, 8 + 8 + HeroBodyDraws + 8 + 1, 20},
                              Case{2, 220, 76, 220, 77, false, 1, 8 + 8 + HeroBodyDraws, 44}})
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        ASSERT_NO_FATAL_FAILURE(Prepare(probe.map, probe.drawnX, probe.drawnY));
        MakeTheHeroAPlayer(probe.logicalX, probe.logicalY);
        const Client::Terrain::TerrainPolygon selection = SelectionPolygon();
        states.camera.trapezoid = selection;
        GameScene scene = Scene();
        ASSERT_TRUE(scene.Enter().HasValue());
        const std::uint16_t before = Cell(221, 11);
        const Client::Kernel::ActorState& hero = world.Actors()[HeroSlot];
        ASSERT_EQ(Cell(probe.drawnX, probe.drawnY) & 1, 1);
        ASSERT_EQ(Cell(probe.logicalX, probe.logicalY) & 1, 0);

        const auto heroBefore = hero;
        const auto weatherBefore = world.Runtime().leafWeather;
        const auto layerBefore = world.Runtime().heroCellLayer;
        ClothWorld reference = cloth;
        reference.random = Advanced(reference.random, FirstStepDraws);
        scene.Step(Client::Input::InputState{});

        ASSERT_EQ(scene.Failure(), std::nullopt);
        ASSERT_TRUE(
            AdvanceEnvironmentPrefix(world, states, heroBefore, weatherBefore, layerBefore, reference).HasValue());
        reference.random =
            Advanced(reference.random, probe.draws + HeroActionPrefixRandoms + ParticleWindDraws +
                                           GateProbeRandoms(*data.gates, world.Runtime().map, heroBefore));
        Client::Models::StepTheClothWorld(reference);
        EXPECT_EQ(world.Runtime().warpCounter, WaitDrawThreshold - 1);
        EXPECT_EQ(Cell(221, 11), before & ~0x02);
        if (probe.map == 0)
        {
            EXPECT_EQ(before, 2);
        }
        EXPECT_EQ(Cell(probe.logicalX, probe.logicalY) & Client::Pathfinding::CharacterWallBit,
                  Client::Pathfinding::CharacterWallBit);
        EXPECT_EQ(hero.seen, probe.seen);
        EXPECT_EQ(hero.safeZone, probe.safeZone);
        EXPECT_EQ(states.camera.trapezoid.x, selection.x) << "el paso no escribe el trapecio";
        EXPECT_EQ(cloth.random.State(), reference.random.State());

        Client::Tests::RecordingFrameTarget target;
        scene.PrepareFrame();
        ASSERT_FALSE(scene.HoldsTheLastFrame());
        scene.Render(target.BeginFrame(std::chrono::milliseconds{1000}), 0.0f);

        ASSERT_EQ(scene.Failure(), std::nullopt);
        target.Present();
        EXPECT_TRUE(target.frames.back().presented);
        EXPECT_EQ(Cell(119, 115) & 0x04, 0) << "las puertas se abren después del jugador sin armas";
        CrtRandom expected = reference.random;
        // 0x50e298..0x50e6af: two byte38e closes, one per empty hand off the back.
        const int characterDraws = probe.seen ? Client::Models::OwnRecordCloseRandoms + 1 + 2 : 0;
        for (int i = 0; i < 4 + 8 + probe.objectDraws + characterDraws + HudBaseDraws; ++i)
        {
            static_cast<void>(expected.Next());
        }
        EXPECT_EQ(cloth.random.State(), expected.State());
        EXPECT_NE(states.camera.trapezoid.x, selection.x) << "el dibujo deja el trapecio del juego";
        scene.Leave();
    }
}

// [0x743a178] and [0x743a17c] are rewritten by 0x505820 from 0x50c91e in each frame of the original that draws a
// player (06, "El conjunto completo de 0x505820"), and the C reads them at 0x550990 (0x65ca1e → 0x550d10) from the
// next step on. With the hero of TheFirstStepAfterTheWaitReadsTheTrapezoidOfTheSelection in Lorencia: the first draw
// is a frame of the original and publishes the hero's scan. One with no new steps, in decoupled mode, is
// intermediate: it neither clears nor publishes it, and what was there stays. With 80 ms of steps the next draw is
// another frame of the original, which writes it again.
TEST_F(GameSceneDrawTest, AnIntermediateDrawKeepsTheScanOfTheLastFrame)
{
    ASSERT_NO_FATAL_FAILURE(Prepare(0, 96, 126));
    MakeTheHeroAPlayer(95, 126);
    states.camera.trapezoid = SelectionPolygon();
    GameScene scene = Scene();
    ASSERT_TRUE(scene.Enter().HasValue());
    scene.Step(Client::Input::InputState{});
    ASSERT_EQ(scene.Failure(), std::nullopt);
    ASSERT_TRUE(world.Actors()[HeroSlot].seen);
    Client::Tests::RecordingFrameTarget target;
    world.Runtime().fullSetScan = std::nullopt;

    scene.PrepareFrame();
    ASSERT_FALSE(scene.HoldsTheLastFrame());
    scene.Render(target.BeginFrame(std::chrono::milliseconds{1000}), 0.0f);
    ASSERT_EQ(scene.Failure(), std::nullopt);
    target.Present();
    ASSERT_TRUE(world.Runtime().fullSetScan.has_value()) << "el frame del original publica el scan del héroe";
    const Client::Actors::Scan505820 heroScan = *world.Runtime().fullSetScan;

    // What another player drawn after the hero in that frame left.
    const Client::Actors::Scan505820 other{.dword743a178 = 123, .byte743a17c = 42};
    world.Runtime().fullSetScan = other;
    scene.PrepareFrame();
    ASSERT_FALSE(scene.HoldsTheLastFrame());
    scene.Render(target.BeginFrame(std::chrono::milliseconds{1010}), 0.0f);
    ASSERT_EQ(scene.Failure(), std::nullopt);
    target.Present();
    ASSERT_TRUE(world.Runtime().fullSetScan.has_value()) << "un dibujo intermedio no lo borra";
    EXPECT_EQ(world.Runtime().fullSetScan->dword743a178, other.dword743a178);
    EXPECT_EQ(world.Runtime().fullSetScan->byte743a17c, other.byte743a17c);

    scene.Step(Client::Input::InputState{});
    scene.Step(Client::Input::InputState{});
    ASSERT_EQ(scene.Failure(), std::nullopt);
    scene.PrepareFrame();
    ASSERT_FALSE(scene.HoldsTheLastFrame());
    EXPECT_FALSE(world.Runtime().fullSetScan.has_value()) << "antes de otro frame del original, sin saber";
    scene.Render(target.BeginFrame(std::chrono::milliseconds{1090}), 0.0f);
    ASSERT_EQ(scene.Failure(), std::nullopt);
    ASSERT_TRUE(world.Runtime().fullSetScan.has_value());
    EXPECT_EQ(world.Runtime().fullSetScan->dword743a178, heroScan.dword743a178);
    EXPECT_EQ(world.Runtime().fullSetScan->byte743a17c, heroScan.byte743a17c);
    scene.Leave();
}

// 0x4eb620 → 0x552f10: at the start of each frame of the original (the first step after its draw), the delays of
// the first +0x6a + 2 slots go down by the clock's ms since the previous frame, not below 0; the extra slot is not
// touched. The scene's first step starts one with 0 ms, the steps that follow in the same frame lower nothing, and
// on entering again what passed outside does not count.
TEST_F(GameSceneDrawTest, EachFrameStartCountsTheSkillDelaysDown)
{
    ASSERT_NO_FATAL_FAILURE(Prepare(0, 96, 126));
    MakeTheHeroAPlayer(95, 126);
    states.camera.trapezoid = SelectionPolygon();
    Client::Tests::FakeClock clock;
    clock.Advance(std::chrono::milliseconds{1000});
    GameScene scene(SceneContext{.dataRoot = m_dataRoot,
                                 .textures = store,
                                 .log = log,
                                 .clock = &clock,
                                 .cloth = &cloth,
                                 .camera = &camera,
                                 .cameraInput = &cameraInput,
                                 .world = &world,
                                 .states = &states},
                    nullptr, &playerModels, &data.items->table, &playerModels.TextureSlots(), &*data.gates,
                    &*data.skills);
    Client::Kernel::OwnCharacterRecord& own = world.OwnCharacter();
    own.byte6a = 1;
    own.skillDelays[0] = 500;
    own.skillDelays[1] = 30;
    own.skillDelays[2] = 0;
    own.skillDelays[3] = 400;
    ASSERT_TRUE(scene.Enter().HasValue());
    Client::Tests::RecordingFrameTarget target;
    const auto drawAFrame = [&]
    {
        scene.PrepareFrame();
        ASSERT_FALSE(scene.HoldsTheLastFrame());
        scene.Render(target.BeginFrame(clock.Now()), 0.0f);
        ASSERT_EQ(scene.Failure(), std::nullopt);
        target.Present();
    };

    scene.Step(Client::Input::InputState{});
    ASSERT_EQ(scene.Failure(), std::nullopt);
    EXPECT_EQ(own.skillDelays[0], 500) << "el primer frame de la escena, con 0 ms";
    ASSERT_NO_FATAL_FAILURE(drawAFrame());
    clock.Advance(std::chrono::milliseconds{80});
    scene.Step(Client::Input::InputState{});
    ASSERT_EQ(scene.Failure(), std::nullopt);
    EXPECT_EQ(own.skillDelays[0], 420);
    EXPECT_EQ(own.skillDelays[1], 0);
    EXPECT_EQ(own.skillDelays[2], 0);
    EXPECT_EQ(own.skillDelays[3], 400) << "+0x6a + 2 lugares";
    clock.Advance(std::chrono::milliseconds{40});
    scene.Step(Client::Input::InputState{});
    EXPECT_EQ(own.skillDelays[0], 420) << "el mismo frame";
    ASSERT_NO_FATAL_FAILURE(drawAFrame());
    scene.Step(Client::Input::InputState{});
    EXPECT_EQ(own.skillDelays[0], 380) << "los 40 ms desde el frame anterior";
    scene.Leave();

    clock.Advance(std::chrono::milliseconds{5000});
    ASSERT_TRUE(scene.Enter().HasValue());
    scene.Step(Client::Input::InputState{});
    ASSERT_EQ(scene.Failure(), std::nullopt);
    EXPECT_EQ(own.skillDelays[0], 380) << "fuera de la escena 5 no baja";
    scene.Leave();
}

// 0x63712d: after the characters, the game step ages the ground marks (0x549090), without rand(): the lit one loses
// one step of life, grows 0.004 and turns red (0.1, 0, 0); the off one does not change. With the hero of
// TheFirstStepAfterTheWaitReadsTheTrapezoidOfTheSelection, in Lorencia and Devias.
TEST_F(GameSceneDrawTest, TheStepAgesTheGroundMarksAfterTheCharacters)
{
    struct Case
    {
        std::uint8_t map;
        std::uint8_t drawnX;
        std::uint8_t drawnY;
        std::uint8_t logicalX;
        std::uint8_t logicalY;
        int draws;
    };
    for (const Case& probe :
         {Case{0, 96, 126, 95, 126, 8 + 8 + HeroBodyDraws + 8 + 1}, Case{2, 220, 76, 220, 77, 8 + 8 + HeroBodyDraws}})
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        ASSERT_NO_FATAL_FAILURE(Prepare(probe.map, probe.drawnX, probe.drawnY));
        MakeTheHeroAPlayer(probe.logicalX, probe.logicalY);
        states.camera.trapezoid = SelectionPolygon();
        Client::Terrain::GroundMarkTable& marks = states.terrain.groundMarks;
        marks = {};
        const Client::Terrain::GroundMark blood{.live = true,
                                                .type = 0x5e3,
                                                .size = 0.65f,
                                                .position = glm::vec3(1000.0f, 2000.0f, 0.0f),
                                                .color = glm::vec3(1.0f, 1.0f, 1.0f),
                                                .alpha = 1.0f,
                                                .life = 60};
        marks[17] = blood;
        marks[18] = blood;
        marks[18].live = false;
        GameScene scene = Scene();
        ASSERT_TRUE(scene.Enter().HasValue());

        const auto heroBefore = world.Actors()[HeroSlot];
        const auto weatherBefore = world.Runtime().leafWeather;
        const auto layerBefore = world.Runtime().heroCellLayer;
        ClothWorld reference = cloth;
        reference.random = Advanced(reference.random, FirstStepDraws);
        scene.Step(Client::Input::InputState{});

        ASSERT_EQ(scene.Failure(), std::nullopt);
        ASSERT_TRUE(
            AdvanceEnvironmentPrefix(world, states, heroBefore, weatherBefore, layerBefore, reference).HasValue());
        reference.random =
            Advanced(reference.random, probe.draws + HeroActionPrefixRandoms + ParticleWindDraws +
                                           GateProbeRandoms(*data.gates, world.Runtime().map, heroBefore));
        Client::Models::StepTheClothWorld(reference);
        EXPECT_TRUE(marks[17].live);
        EXPECT_EQ(marks[17].life, 59);
        EXPECT_EQ(marks[17].size, static_cast<float>(static_cast<double>(0.65f) + static_cast<double>(0.004f)));
        EXPECT_EQ(marks[17].color, glm::vec3(0.1f, 0.0f, 0.0f));
        EXPECT_EQ(marks[17].alpha, 1.0f);
        Client::Terrain::GroundMark off = blood;
        off.live = false;
        EXPECT_EQ(marks[18], off);
        EXPECT_EQ(cloth.random.State(), reference.random.State());
        scene.Leave();
    }
    states.terrain.groundMarks = {};
}

// Q-C217: without a draw before (a fresh WorldState), the trapezoid is the .bss one, all zero, and 0x5fba90 sees
// everyone with the −20 margin. The Devias hero who is not seen with the selection's is seen with this one and takes
// the safe zone of the logical cell.
TEST_F(GameSceneDrawTest, WithoutAPreviousDrawTheFirstStepSeesWithTheTrapezoidOfTheBss)
{
    ASSERT_NO_FATAL_FAILURE(Prepare(2, 220, 76));
    MakeTheHeroAPlayer(220, 77);
    ASSERT_EQ(states.camera.trapezoid.x, (std::array<float, 4>{}));
    ASSERT_EQ(states.camera.trapezoid.y, (std::array<float, 4>{}));
    GameScene scene = Scene();
    ASSERT_TRUE(scene.Enter().HasValue());

    const auto heroBefore = world.Actors()[HeroSlot];
    const auto weatherBefore = world.Runtime().leafWeather;
    const auto layerBefore = world.Runtime().heroCellLayer;
    ClothWorld reference = cloth;
    reference.random = Advanced(reference.random, FirstStepDraws);
    scene.Step(Client::Input::InputState{});

    ASSERT_EQ(scene.Failure(), std::nullopt);
    ASSERT_TRUE(AdvanceEnvironmentPrefix(world, states, heroBefore, weatherBefore, layerBefore, reference).HasValue());
    reference.random =
        Advanced(reference.random, 8 + 8 + HeroBodyDraws + 8 + 1 + HeroActionPrefixRandoms + ParticleWindDraws +
                                       GateProbeRandoms(*data.gates, world.Runtime().map, heroBefore));
    Client::Models::StepTheClothWorld(reference);
    const Client::Kernel::ActorState& hero = world.Actors()[HeroSlot];
    EXPECT_TRUE(hero.seen);
    EXPECT_EQ(hero.safeZone, 0);
    EXPECT_EQ(cloth.random.State(), reference.random.State());
    scene.Leave();
}

// Without Player.bmd (main without the models, or a test that does not pass them), the hero's body stops where
// 0x4f9580 reads its actions, after the own record of 0x4fc450 and before the rand() % 6 of 0x4fd7e4. In Lorencia
// and Devias.
TEST_F(GameSceneDrawTest, WithoutPlayerBmdTheStepStopsAtTheBody)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(static_cast<int>(map));
        ASSERT_NO_FATAL_FAILURE(Prepare(map, 128, 128));
        MakeTheHeroAPlayer(128, 128);
        GameScene scene(SceneContext{.dataRoot = m_dataRoot,
                                     .textures = store,
                                     .log = log,
                                     .cloth = &cloth,
                                     .camera = &camera,
                                     .cameraInput = &cameraInput,
                                     .world = &world,
                                     .states = &states},
                        nullptr, nullptr, &data.items->table, nullptr, &*data.gates, &*data.skills);
        ASSERT_TRUE(scene.Enter().HasValue());

        const auto heroBefore = world.Actors()[HeroSlot];
        const auto weatherBefore = world.Runtime().leafWeather;
        const auto layerBefore = world.Runtime().heroCellLayer;
        ClothWorld reference = cloth;
        reference.random = Advanced(reference.random, FirstStepDraws);
        scene.Step(Client::Input::InputState{});

        ASSERT_TRUE(
            AdvanceEnvironmentPrefix(world, states, heroBefore, weatherBefore, layerBefore, reference).HasValue());
        ASSERT_TRUE(scene.Failure().has_value());
        EXPECT_NE(scene.Failure()->find("0x4f9580"), std::string::npos) << *scene.Failure();
        EXPECT_EQ(cloth.random.State(),
                  Advanced(reference.random, 8 + 8 + Client::Models::OwnRecordCloseRandoms + HeroActionPrefixRandoms +
                                                 GateProbeRandoms(*data.gates, world.Runtime().map, heroBefore))
                      .State());
        scene.Leave();
    }
}

// A draw that stops leaves the scene in Failure: the next step does not run, even though the draw left its
// trapezoid. The mark is of type 0x606, a slot MuRet's game does not upload: 0x549130 stops at 0x549152.
TEST_F(GameSceneDrawTest, AStepAfterACutDrawDoesNothing)
{
    ASSERT_NO_FATAL_FAILURE(Prepare(0, 143, 120));
    states.terrain.groundMarks[0] = Client::Terrain::GroundMark{.live = true,
                                                                .type = 0x606,
                                                                .size = 1.0f,
                                                                .position = world.Actors()[HeroSlot].position,
                                                                .color = glm::vec3(1.0f),
                                                                .alpha = 1.0f,
                                                                .life = 60};
    GameScene scene = Scene();
    ASSERT_TRUE(scene.Enter().HasValue());
    Client::Tests::RecordingFrameTarget target;
    scene.PrepareFrame();
    scene.Render(target.BeginFrame(std::chrono::milliseconds{1000}), 0.0f);
    ASSERT_TRUE(scene.Failure().has_value());
    const std::string cut = *scene.Failure();
    EXPECT_NE(cut.find("0x549152"), std::string::npos);
    const std::uint32_t random = cloth.random.State();

    scene.Step(Client::Input::InputState{});

    EXPECT_EQ(scene.Failure(), cut);
    EXPECT_EQ(world.Runtime().warpCounter, WaitDrawThreshold);
    EXPECT_EQ(cloth.random.State(), random);
    scene.Leave();
}
} // namespace
