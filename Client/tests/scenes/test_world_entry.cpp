#include "Session/WorldEntry.h"

#include "Session/ServerSession.h"
#include "Session/SessionState.h"
#include "Core/ILogSink.h"
#include "Core/Expected.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/GateRecord.h"
#include "Tables/GateTable.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Tables/TextTable.h"
#include "Input/InputState.h"
#include "Network/PacketSerials.h"
#include "Protocol/FrontEnd/CharacterSelectResult.h"
#include "Render/ModelBatch.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Scenes/CharacterSelectScene.h"
#include "Pets/DarkSpirit.h"
#include "Actors/FullSetScan.h"
#include "Fauna/FaunaStep.h"
#include "Fauna/FaunaState.h"
#include "Weather/GameLeaves.h"
#include "Tables/GameData.h"
#include "Assets/GameModels.h"
#include "Scenes/GameScene.h"
#include "Session/IWorldEntryPublisher.h"
#include "FrontEnd/LoginModels.h"
#include "Hud/MapBanner.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Camera/SceneCameras.h"
#include "Scenes/SceneContext.h"
#include "FrontEnd/SelectionState.h"
#include "Session/SessionEventApplier.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatLog.h"
#include "Ui/MessageArea.h"
#include "Camera/GameCamera.h"
#include "Effects/ModelEffects.h"
#include "Weather/Leaves.h"
#include "Weather/LeafWeather.h"
#include "Particles/Particles.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/WorldTerrain.h"
#include "Session/ModuleStates.h"
#include "Audio/SoundId.h"

#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/ItemSetFiles.h"
#include "support/LetterMeasure.h"
#include "support/PlayerActors.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/SessionHarness.h"
#include "support/TestGameServer.h"
#include "Chat/ChatRuntime.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Scenes::ApplyWorldEntry;
using Client::Scenes::SettleWorldEntry;
using Client::Scenes::WorldEntryResources;
using Client::Scenes::WorldEntryResult;
using Client::Tests::AfterDraws;

constexpr std::uint32_t Seed = 0x1234;
constexpr std::uint16_t PlayerKey = 5;
constexpr std::uint16_t Player = Client::Tables::PlayerSkeletonModel;
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

// The own record closes, the grass, the hero slot, 0x511290 and 0x4f2250's suffix.
constexpr int OwnRecordClose = 0xf08;
constexpr int GrassRandoms = 65536;
constexpr int HeroRandom = 1;
constexpr int ConstructionRandoms = 28;
constexpr int StopRandom = 1;
// Those of 0x550d10's tail, with 0x512a30 whole.
constexpr int TailRandoms = 3;
// After 0x512a30: the counter (4) and 0x657a90's two queries.
constexpr int WaitClose = 4;
constexpr int WindowRandoms = 2;
// 0x637641: the frame picking of the Dark Wizard (class 0, the empty record's) reads its skill from the own record
// (0x59efda), after the world pass.
constexpr int FramePickingRandoms = OwnRecordClose;
// 0x637660: the HUD (0x5a7320) in the base state, after the picking: 19 own record closes (the arrows, the twelve
// durability warnings, the bottom bar, the experience, the AG and the three of the main bar), those of the hero's two
// cells and nine trade and NPC shop queries.
constexpr int HudRandoms = 19 * OwnRecordClose + 2 * Client::Models::SmallLogicalCloseRandoms + 9;
// 0x636ded..0x63705b: the step's UI with everything closed (the skill bar, Q/W/E and IsOpen(0)), before the objects.
constexpr int GameUiStepRandoms = OwnRecordClose + 2 + 2;
constexpr std::size_t PublishedSlot = 42;
// The rand() of the objects pass of the first game frame (0x602ef4), one per seen object that reaches 0x602ed0, with
// the hero in the first cell of each reference map (*simulated*, with the port and the repo's Data). In Lorencia, at
// (133, 127): 84 before the 0x69 (1720) and its own; then its rand() % 2 (0x60303c). Even, the eight of the two
// drops; then the remaining 18 objects follow. The arrival effects do not have their seen published by 0x52d620 yet:
// the floor pass skips them. In Devias, at (220, 30), the whole pass takes 54. Both frames complete their tail.
constexpr int LorenciaRandomsUpToTheFountain = 84 + 1;
constexpr int DropRandoms = 8;
constexpr int LorenciaObjectsAfterTheFountain = 18;
constexpr int DeviasObjectRandoms = 54;

// The objects pass of a first game frame: its rand() and the drops it created.
struct FirstObjectPass
{
    int randoms = 0;
    int drops = 0;
};

// Lorencia's at (133, 127), with `random` on reaching the objects.
FirstObjectPass LorenciaObjectPass(CrtRandom random)
{
    for (int draw = 0; draw < LorenciaRandomsUpToTheFountain; ++draw)
    {
        static_cast<void>(random.Next());
    }
    if (random.Next() % 2 == 0)
    {
        return {.randoms = LorenciaRandomsUpToTheFountain + 1 + DropRandoms + LorenciaObjectsAfterTheFountain,
                .drops = 2};
    }
    return {.randoms = LorenciaRandomsUpToTheFountain + 1 + LorenciaObjectsAfterTheFountain};
}

class RecordingPublisher final : public Client::Scenes::IWorldEntryPublisher
{
public:
    void PublishWorldEntry() override
    {
        ++calls;
        if (onPublish)
        {
            onPublish();
        }
    }

    int calls = 0;
    std::function<void()> onPublish;
};

// What lasts the session, with Player.bmd, an Item.bmd without marked weapons and the own record without equipment.
class Entry
{
private:
    // The simultaneous scenarios keep durable worlds without exhausting the Windows stack.
    std::unique_ptr<Client::Kernel::WorldState> m_world = std::make_unique<Client::Kernel::WorldState>();

public:
    Entry(std::filesystem::path root, std::uint32_t seed) : dataRoot(std::move(root)), cloth{.random = CrtRandom(seed)}
    {
        models.Set(Player, {}, Client::Tests::PlayerActionsSkeleton(), {});
        world.OwnCharacter().ClearEquipmentForEntry();
    }

    Entry(const Entry&) = delete;
    Entry& operator=(const Entry&) = delete;
    Entry(Entry&&) = delete;
    Entry& operator=(Entry&&) = delete;

    [[nodiscard]] WorldEntryResources Resources()
    {
        return WorldEntryResources{.world = world,
                                   .selection = selection,
                                   .dataRoot = dataRoot,
                                   .cloth = cloth,
                                   .camera = camera,
                                   .cameraInput = cameraInput,
                                   .models = models,
                                   .items = &items,
                                   .log = log,
                                   .textMeasure = &letters,
                                   .texts = &texts,
                                   .states = &states};
    }

    [[nodiscard]] ActorState& Hero()
    {
        return world.Actors()[static_cast<std::size_t>(world.Runtime().heroSlot)];
    }

    [[nodiscard]] std::size_t AliveArrivals() const
    {
        const auto effects = world.Map().effects.Effects();
        return static_cast<std::size_t>(std::ranges::count_if(
            effects, [](const auto& effect) { return effect.alive && effect.type == Client::Effects::ArrivalEffect; }));
    }

    // The hero already dressed in `slot`, for what follows 0x512a30.
    ActorState& DressedHero(std::size_t slot)
    {
        world.Runtime().heroSlot = static_cast<std::int32_t>(slot);
        ActorState& hero = Hero();
        hero = Client::Tests::UnarmedPlayer();
        hero.alive = true;
        hero.alpha = 1.0f;
        hero.position = {13350.0f, 12750.0f, 165.0f};
        hero.angles = {0.0f, 0.0f, -45.0f};
        return hero;
    }

    std::filesystem::path dataRoot;
    Client::Kernel::WorldState& world = *m_world;
    Client::FrontEnd::SelectionState selection{world};
    Client::Session::ModuleStates states;
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput cameraInput;
    Client::Models::ClothWorld cloth;
    Client::FrontEnd::SingleModelLibrary models;
    Client::Tables::ItemTable items = Client::Tests::ItemsMarking({});
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    // The name of map 0 (0x5f0b40: text 30) and the welcome line's 484, synthetic.
    Client::Tests::LetterMeasure letters;
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable({{30, "<mapa 0>"}, {484, " <bienvenida>"}});
};

// Production resources for the tests that go through the whole frame tail. The ApplyWorldEntry unit tests keep their
// minimal library and their deliberate boundaries.
struct CompleteFrameResources
{
    Client::Core::Expected<void, std::string> Load(const std::filesystem::path& root, Client::Core::Log& log)
    {
        auto loadedData = Client::Tables::LoadGameData(root);
        if (!loadedData.HasValue())
        {
            return Client::Core::Unexpected(loadedData.Error());
        }
        data = std::move(loadedData).Value();
        return models.Load(root, textures, log);
    }
    Client::Tests::FakeTextureStore textures;
    Client::Assets::GameModels models;
    Client::Tables::GameData data;
};

// The wire reply: cell (133, 127) of map 0 with direction 0, PK 4 and CtlCode 0x20.
Client::Protocol::FrontEnd::CharacterSelectResult CableReply()
{
    Client::Protocol::FrontEnd::CharacterSelectResult reply;
    reply.x = 133;
    reply.y = 127;
    reply.map = 0;
    reply.direction = 0;
    reply.pk = 4;
    reply.controlCode = 0x20;
    return reply;
}

class WorldEntryTest : public Client::Tests::GoldenDataTest
{
};

// 06, row 3: the hero slot is the rand() after the first record close and the grass.
TEST_F(WorldEntryTest, TheHeroSlotIsTheRandAfterTheGrassOffsets)
{
    Entry tested(m_dataRoot, Seed);
    RecordingPublisher publisher;

    static_cast<void>(ApplyWorldEntry(CableReply(), PlayerKey, tested.Resources(), publisher));

    CrtRandom replay = AfterDraws(Seed, OwnRecordClose + GrassRandoms);
    EXPECT_EQ(tested.world.Runtime().heroSlot, replay.Next() % 400);
}

// Q-C175: direction 0 gives −45°, and the hero is at the center of the cell, with the key, the record's class, the
// skin at 0 and the reply's PK and CtlCode.
TEST_F(WorldEntryTest, TheCableReplyGives13350_12750AndMinus45)
{
    Entry tested(m_dataRoot, Seed);
    tested.world.OwnCharacter().characterClass = 0x20;
    RecordingPublisher publisher;

    static_cast<void>(ApplyWorldEntry(CableReply(), PlayerKey, tested.Resources(), publisher));

    const ActorState& hero = tested.Hero();
    EXPECT_TRUE(hero.alive);
    EXPECT_EQ(hero.position.x, 13350.0f);
    EXPECT_EQ(hero.position.y, 12750.0f);
    EXPECT_EQ(hero.angles.z, -45.0f);
    EXPECT_EQ(hero.key, PlayerKey);
    EXPECT_EQ(hero.characterClass, 0x20);
    EXPECT_EQ(hero.skin, 0);
    EXPECT_EQ(hero.pk, 4);
    EXPECT_EQ(hero.controlCode, 0x20);
    EXPECT_EQ(hero.byte88, 1);
    EXPECT_EQ(tested.cameraInput.map, 0);
}

// 06, rows 5 and 7: 0x512a30 dresses with the old record, and only then is its equipment emptied.
TEST_F(WorldEntryTest, TheOldRecordDressesTheHeroBeforeItIsEmptied)
{
    Entry tested(m_dataRoot, Seed);
    tested.world.OwnCharacter().equipment[0].id = 5;
    RecordingPublisher publisher;

    static_cast<void>(ApplyWorldEntry(CableReply(), PlayerKey, tested.Resources(), publisher));

    constexpr std::uint16_t Sword = 5 + Client::Tables::FirstItemModel;
    EXPECT_EQ(tested.Hero().hands[0].model, Sword);
    EXPECT_EQ(tested.world.OwnCharacter().equipment[0].id, 5);

    // With 0x512a30 whole, what follows empties the record and the hero keeps what it wears.
    ASSERT_TRUE(SettleWorldEntry({}, tested.Resources(), publisher).HasValue());
    EXPECT_EQ(tested.Hero().hands[0].model, Sword);
    for (const auto& item : tested.world.OwnCharacter().equipment)
    {
        EXPECT_EQ(item.id, Client::Kernel::EmptyOwnItemId);
    }
}

// The GS's encrypted F3 03 reply: a C1 of 0x34 bytes with cell (x, y) of map 0, direction 0, PK 4 (pkt[0x2c]) and
// CtlCode 0x20 (pkt[0x2d]).
std::vector<std::byte> EntryPacket(std::uint8_t x, std::uint8_t y, Client::Network::PacketSerials& serials)
{
    constexpr std::size_t Size = 0x34;
    std::vector<std::byte> reply(Size);
    reply[0] = std::byte{0xC1};
    reply[1] = std::byte{Size};
    reply[2] = std::byte{0xF3};
    reply[3] = std::byte{0x03};
    reply[4] = std::byte{x};
    reply[5] = std::byte{y};
    reply[0x2c] = std::byte{4};
    reply[0x2d] = std::byte{0x20};
    return Client::Tests::FromTheGameServer(reply, serials);
}

// Through the session: 0x512a30 stops (currently in 0x550d10, on its first call, 0x40e1b0) and the session fails
// with the hero unpublished, the counter untouched and the rand() the original took up to there.
TEST_F(WorldEntryTest, AnEquipmentFailureLeavesTheHeroUnpublished)
{
    Entry tested(m_dataRoot, Seed);
    Client::Scenes::SessionEventApplier applier(tested.Resources());
    Client::Tests::LoggedInSession online(&applier);
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    tested.world.Runtime().warpCounter = Client::Kernel::WorldLoadingWait;
    Client::Network::PacketSerials serials;
    online.socket.reads = {EntryPacket(133, 127, serials)};

    static_cast<void>(online.session.Poll());

    EXPECT_EQ(online.session.State(), Client::Session::SessionState::Failed);
    EXPECT_EQ(tested.world.Runtime().hero, 0u);
    EXPECT_EQ(tested.world.Runtime().warpCounter, Client::Kernel::WorldLoadingWait);
    EXPECT_EQ(tested.AliveArrivals(), 0u);
    EXPECT_EQ(tested.Hero().key, PlayerKey);
    EXPECT_EQ(tested.Hero().name, ActorState{}.name);
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(
        tested.cloth.random.State(),
        AfterDraws(Seed, 3 * OwnRecordClose + GrassRandoms + HeroRandom + ConstructionRandoms + StopRandom).State());
}

// The set tables with no set: 0x40e1b0 walks the equipment and applies nothing.
Client::Tables::ItemSetTables NoSets()
{
    return Client::Tests::ItemSetTablesFrom(
        std::vector<std::byte>(Client::Tables::ItemSetTypeTableSize, std::byte{0xff}),
        std::vector<std::byte>(Client::Tables::ItemSetOptionTableSize, std::byte{0xff}));
}

// The hero slot with `Seed`, the first test's.
std::size_t HeroSlotOfSeed()
{
    CrtRandom replay = AfterDraws(Seed, OwnRecordClose + GrassRandoms);
    return static_cast<std::size_t>(replay.Next() % 400);
}

// With the set tables, 0x550d10 reaches the end and the entry goes on until it settles. The hero slot is not the
// published one (WinMain's 0): 0x505820 does not run in 0x512a30, and 0x550990 reads the 0 of the .bss, that of a
// process that drew nobody. The inner accesses nest in 0x512a30's: the extra rand() are the tail's three, and then
// SettleWorldEntry's with its close.
TEST_F(WorldEntryTest, WithTheSetTablesTheEntryRunsTheWholeRecalculation)
{
    Entry tested(m_dataRoot, Seed);
    const Client::Tables::ItemSetTables sets = NoSets();
    WorldEntryResources resources = tested.Resources();
    resources.itemSetTables = &sets;
    RecordingPublisher publisher;
    ASSERT_NE(HeroSlotOfSeed(), tested.world.Runtime().hero);

    const auto entered = ApplyWorldEntry(CableReply(), PlayerKey, resources, publisher);

    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    EXPECT_EQ(publisher.calls, 1);
    EXPECT_EQ(tested.world.Runtime().hero, HeroSlotOfSeed());
    EXPECT_EQ(tested.world.Runtime().warpCounter, Client::Kernel::WaitDrawThreshold);
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
    ASSERT_TRUE(tested.world.Runtime().fullSetScan.has_value());
    EXPECT_EQ(tested.world.Runtime().fullSetScan->dword743a178, 0);
    EXPECT_EQ(tested.world.Runtime().fullSetScan->byte743a17c, 0);
    constexpr int BeforeTheSettling = GrassRandoms + HeroRandom + ConstructionRandoms + StopRandom + TailRandoms;
    constexpr int TheSettling = OwnRecordClose + WaitClose + WindowRandoms + StopRandom;
    EXPECT_EQ(tested.cloth.random.State(),
              AfterDraws(Seed, 3 * OwnRecordClose + BeforeTheSettling + TheSettling).State());
}

// 0x4c4b22..0x4c4b60, at the end: 0x65dc30 (the banner enters with alpha at 0.2 and asks for Interface\lorencia.tga
// for slot 0x66a) and the system line with the empty name: sprintf("%s%s", 0x5f0b40(map), text 484), the map name
// first.
TEST_F(WorldEntryTest, TheEntryShowsTheMapBannerAndTheWelcomeLine)
{
    Entry tested(m_dataRoot, Seed);
    const Client::Tables::ItemSetTables sets = NoSets();
    WorldEntryResources resources = tested.Resources();
    resources.itemSetTables = &sets;
    RecordingPublisher publisher;

    const auto entered = ApplyWorldEntry(CableReply(), PlayerKey, resources, publisher);

    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    const Client::Kernel::WorldRuntime& runtime = tested.world.Runtime();
    const Client::Hud::MapBanner& banner = runtime.hud.mapBanner;
    EXPECT_EQ(banner.state, Client::Hud::MapBannerState::FadingIn);
    EXPECT_EQ(banner.shownMap, 0);
    ASSERT_TRUE(banner.pendingImage.has_value());
    EXPECT_EQ(*banner.pendingImage, "Interface\\lorencia.tga");
    ASSERT_FALSE(runtime.chat.log.entries.empty());
    const Client::Chat::ChatEntry& welcome = runtime.chat.log.entries.back();
    EXPECT_EQ(welcome.name, "");
    EXPECT_EQ(welcome.text, "<mapa 0> <bienvenida>");
    EXPECT_EQ(welcome.type, Client::Ui::SystemMessage);
}

// The applied reply and, without any step, the first game frame: the counter at 30 does not hold, the camera looks
// at the published hero and reads its cell (8 rand() after the 4 of 0x637229), Lorencia's terrain comes out with its
// tiles and its grass (0x5fc353, without rand()), the floor marks draw none (0x5fc365: the .bss table, empty) and
// the objects take their rand() up to the 0x69; with the seed, its rand() % 2 is even and creates the two drops.
// Effects not yet seen are skipped and the complete tail presents the frame.
TEST_F(WorldEntryTest, AZeroStepFrameAfterTheEntryDrawsTheTerrainUpToTheObjects)
{
    Entry tested(m_dataRoot, Seed);
    const Client::Tables::ItemSetTables sets = NoSets();
    WorldEntryResources resources = tested.Resources();
    resources.itemSetTables = &sets;
    RecordingPublisher publisher;
    ASSERT_TRUE(ApplyWorldEntry(CableReply(), PlayerKey, resources, publisher).HasValue());
    // What loading the selection's 54 left (0x633f30): the world loaded.
    tested.world.Runtime().drawingEnabled = true;
    CompleteFrameResources frameResources;
    const auto loadedFrame = frameResources.Load(m_dataRoot, tested.log);
    ASSERT_TRUE(loadedFrame.HasValue()) << loadedFrame.Error();
    Client::Scenes::GameScene scene(Client::Scenes::SceneContext{.dataRoot = m_dataRoot,
                                                                 .textures = frameResources.textures,
                                                                 .log = tested.log,
                                                                 .cloth = &tested.cloth,
                                                                 .camera = &tested.camera,
                                                                 .cameraInput = &tested.cameraInput,
                                                                 .world = &tested.world,
                                                                 .states = &tested.states},
                                    nullptr, &frameResources.models, &frameResources.data.items->table,
                                    &frameResources.models.TextureSlots(), &*frameResources.data.gates,
                                    &*frameResources.data.skills);
    ASSERT_TRUE(scene.Enter().HasValue());
    CrtRandom expected = tested.cloth.random;
    for (int draw = 0; draw < 4 + 8; ++draw)
    {
        static_cast<void>(expected.Next());
    }
    const FirstObjectPass objects = LorenciaObjectPass(expected);
    ASSERT_EQ(objects.drops, 2);
    for (int draw = 0; draw < objects.randoms + FramePickingRandoms + HudRandoms; ++draw)
    {
        static_cast<void>(expected.Next());
    }
    Client::Tests::RecordingFrameTarget target;

    scene.PrepareFrame();
    ASSERT_FALSE(scene.HoldsTheLastFrame());
    scene.Render(target.BeginFrame({}), 0.0f);

    ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();
    target.Present();
    ASSERT_EQ(target.frames.size(), 1u);
    EXPECT_TRUE(target.frames[0].presented);
    std::size_t vertices = 0;
    std::size_t grass = 0;
    for (const auto& event : target.frames[0].events)
    {
        vertices += event.modelVertices.size();
        for (const Client::Render::ModelDrawCommand& command : event.modelCommands)
        {
            grass += command.state.texture == Client::Render::TextureSlot{0x32} ? command.vertexCount : 0;
        }
    }
    EXPECT_GT(vertices, 0u);
    EXPECT_GT(grass, 0u) << "el pasto de World1 (TileGrass01) en 0x32";
    EXPECT_TRUE(scene.MouseRay().has_value());
    EXPECT_EQ(
        std::ranges::count_if(tested.world.Particles().All(), [](const auto& particle) { return particle.alive; }),
        objects.drops);
    EXPECT_EQ(tested.cameraInput.hero, tested.Hero().position);
    EXPECT_EQ(tested.cloth.random.State(), expected.State());
    scene.Leave();
}

// 0x550990 reads what the last 0x505820 left, here the selection's: [0x743a17c] at 1 and [0x743a178] at 12 add
// _ftol(+0x62 · 0.15f) to +0x62, which with class 0 is the agility >> 2 (0x550990).
TEST_F(WorldEntryTest, TheEntryReadsWhatTheSelectionDrawingLeft)
{
    Entry tested(m_dataRoot, Seed);
    tested.world.Runtime().fullSetScan = Client::Actors::Scan505820{.dword743a178 = 12, .byte743a17c = 1};
    const Client::Tables::ItemSetTables sets = NoSets();
    WorldEntryResources resources = tested.Resources();
    resources.itemSetTables = &sets;
    Client::Protocol::FrontEnd::CharacterSelectResult reply = CableReply();
    reply.agility = 400;
    RecordingPublisher publisher;

    const auto entered = ApplyWorldEntry(reply, PlayerKey, resources, publisher);

    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    EXPECT_EQ(tested.world.OwnCharacter().word62, 100 + 15);
    ASSERT_TRUE(tested.world.Runtime().fullSetScan.has_value());
    EXPECT_EQ(tested.world.Runtime().fullSetScan->dword743a178, 12);
}

// After a game frame that draws the characters, MuRet does not know what 0x505820 left: with another slot, the
// entry stops at 0x550990 with the hero unpublished and the rand() up to there, without the tail.
TEST_F(WorldEntryTest, WithoutKnowingTheLastScanTheEntryStopsAt0x550990)
{
    Entry tested(m_dataRoot, Seed);
    tested.world.Runtime().fullSetScan = std::nullopt;
    const Client::Tables::ItemSetTables sets = NoSets();
    WorldEntryResources resources = tested.Resources();
    resources.itemSetTables = &sets;
    RecordingPublisher publisher;

    const auto entered = ApplyWorldEntry(CableReply(), PlayerKey, resources, publisher);

    ASSERT_FALSE(entered.HasValue());
    EXPECT_NE(entered.Error().find("0x550990"), std::string::npos) << entered.Error();
    EXPECT_EQ(publisher.calls, 0);
    EXPECT_EQ(tested.world.Runtime().hero, 0u);
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(
        tested.cloth.random.State(),
        AfterDraws(Seed, 3 * OwnRecordClose + GrassRandoms + HeroRandom + ConstructionRandoms + StopRandom).State());
}

// If rand() % 400 gives the published hero's slot (on a second entry, or WinMain's 0), 0x512a30 dresses that same
// one and 0x505820 runs with it (0x512ee7): what came before does not matter. The empty record leaves the class's
// parts, which are not a set: [0x743a178] at 0 and [0x743a17c] at 1.
TEST_F(WorldEntryTest, WithThePublishedHeroSlotTheEntryScansTheHero)
{
    Entry tested(m_dataRoot, Seed);
    tested.world.Runtime().hero = HeroSlotOfSeed();
    tested.world.Runtime().fullSetScan = std::nullopt;
    const Client::Tables::ItemSetTables sets = NoSets();
    WorldEntryResources resources = tested.Resources();
    resources.itemSetTables = &sets;
    RecordingPublisher publisher;

    const auto entered = ApplyWorldEntry(CableReply(), PlayerKey, resources, publisher);

    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    EXPECT_EQ(publisher.calls, 1);
    ASSERT_TRUE(tested.world.Runtime().fullSetScan.has_value());
    EXPECT_EQ(tested.world.Runtime().fullSetScan->dword743a178, 0);
    EXPECT_EQ(tested.world.Runtime().fullSetScan->byte743a17c, 1);
}

// Three inline closes (0x4c41c4, 0x4c46f0 and 0x4c481e) and 0x512a30's: each takes 3848 rand() only if it leaves
// the record without accesses.
TEST_F(WorldEntryTest, TheEntryClosesTheOwnRecordAtItsThreeSites)
{
    constexpr int BeforeTheSettling = GrassRandoms + HeroRandom + ConstructionRandoms + StopRandom;
    Entry closing(m_dataRoot, Seed);
    Entry held(m_dataRoot, Seed);
    held.world.Runtime().ownRecordDepth = 1;
    RecordingPublisher publisher;

    static_cast<void>(ApplyWorldEntry(CableReply(), PlayerKey, closing.Resources(), publisher));
    static_cast<void>(ApplyWorldEntry(CableReply(), PlayerKey, held.Resources(), publisher));

    EXPECT_EQ(closing.cloth.random.State(), AfterDraws(Seed, 3 * OwnRecordClose + BeforeTheSettling).State());
    EXPECT_EQ(held.cloth.random.State(), AfterDraws(Seed, BeforeTheSettling).State());
    EXPECT_EQ(closing.world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(held.world.Runtime().ownRecordDepth, 1);

    Entry settling(m_dataRoot, Seed);
    Entry settlingHeld(m_dataRoot, Seed);
    settling.DressedHero(PublishedSlot);
    settlingHeld.DressedHero(PublishedSlot);
    settlingHeld.world.Runtime().ownRecordDepth = 1;
    ASSERT_TRUE(SettleWorldEntry({}, settling.Resources(), publisher).HasValue());
    ASSERT_TRUE(SettleWorldEntry({}, settlingHeld.Resources(), publisher).HasValue());
    constexpr int AfterTheThirdClose = WaitClose + WindowRandoms + StopRandom;
    EXPECT_EQ(settling.cloth.random.State(), AfterDraws(Seed, OwnRecordClose + AfterTheThirdClose).State());
    EXPECT_EQ(settlingHeld.cloth.random.State(), AfterDraws(Seed, AfterTheThirdClose).State());
}

// 06, rows 6 to 17 and "El héroe": what the session sees when it publishes the 0x3d and what is left at the end.
TEST_F(WorldEntryTest, TheSettlingFollowsTheNativeOrder)
{
    Entry tested(m_dataRoot, Seed);
    tested.world.Runtime().map = 0;
    tested.world.Runtime().warpCounter = Client::Kernel::WorldLoadingWait;
    tested.world.OwnCharacter().name[0] = std::byte{'h'};
    tested.world.OwnCharacter().equipment[3].id = 7;
    ActorState& hero = tested.DressedHero(PublishedSlot);
    RecordingPublisher publisher;
    bool checked = false;
    publisher.onPublish = [&]
    {
        checked = true;
        EXPECT_EQ(tested.world.Runtime().hero, PublishedSlot);
        EXPECT_EQ(hero.name[0], std::byte{'h'});
        EXPECT_EQ(tested.world.OwnCharacter().equipment[3].id, Client::Kernel::EmptyOwnItemId);
        EXPECT_EQ(tested.AliveArrivals(), 1u);
        EXPECT_EQ(tested.world.Runtime().warpCounter, Client::Kernel::WorldLoadingWait);
        EXPECT_EQ(hero.alpha, 1.0f);
        EXPECT_EQ(tested.cameraInput.hero, hero.position);
    };

    const auto settled = SettleWorldEntry({}, tested.Resources(), publisher);

    ASSERT_TRUE(settled.HasValue()) << settled.Error();
    EXPECT_TRUE(checked);
    EXPECT_EQ(publisher.calls, 1);
    EXPECT_EQ(tested.world.Runtime().warpCounter, Client::Kernel::WaitDrawThreshold);
    EXPECT_EQ(tested.AliveArrivals(), 2u);
    EXPECT_EQ(hero.alpha, 0.0f);
    EXPECT_EQ(hero.movement, 0);
    EXPECT_EQ(settled.Value().sounds,
              (std::vector<Client::Audio::SoundId>{Client::Audio::SoundId{0x8b}, Client::Audio::SoundId{0x99},
                                                   Client::Audio::SoundId{0x9e}}));
    EXPECT_EQ(tested.cloth.random.State(),
              AfterDraws(Seed, OwnRecordClose + WaitClose + WindowRandoms + StopRandom).State());
    for (const auto& effect : tested.world.Map().effects.Effects())
    {
        if (effect.alive)
        {
            EXPECT_EQ(effect.transform.position, hero.position);
            EXPECT_EQ(effect.transform.angles, hero.angles);
            EXPECT_EQ(effect.life, 20);
            EXPECT_TRUE(effect.hasOwner);
            EXPECT_EQ(effect.ownerContext, &hero);
            EXPECT_EQ(effect.disturbanceState, 0xffff);
        }
    }
}

// The sitting C1 18 service is missing: the counter is already at 30 and the two arrival effects are created.
TEST_F(WorldEntryTest, AStopFailureKeepsCounterThirtyAndTwoArrivals)
{
    Entry tested(m_dataRoot, Seed);
    tested.world.Runtime().warpCounter = Client::Kernel::WorldLoadingWait;
    ActorState& hero = tested.DressedHero(PublishedSlot);
    constexpr std::uint32_t Sitting = 0x200000;
    hero.flags7c |= Sitting;
    RecordingPublisher publisher;

    const auto settled = SettleWorldEntry({}, tested.Resources(), publisher);

    ASSERT_FALSE(settled.HasValue());
    EXPECT_NE(settled.Error().find("0x4f2416"), std::string::npos) << settled.Error();
    EXPECT_EQ(publisher.calls, 1);
    EXPECT_EQ(tested.world.Runtime().warpCounter, Client::Kernel::WaitDrawThreshold);
    EXPECT_EQ(tested.AliveArrivals(), 2u);
    EXPECT_EQ(tested.world.Runtime().hero, PublishedSlot);
    EXPECT_EQ(hero.alpha, 0.0f);
    EXPECT_EQ(hero.animation.action, 0xca);
}

// 0x4c49d6..0x4c4a11: without the 0x8b in Blood Castle and without the 0x99 or the 0x9e in Chaos Castle.
TEST_F(WorldEntryTest, TheSoundsSkipTheEventMaps)
{
    Entry tested(m_dataRoot, Seed);
    tested.DressedHero(PublishedSlot);
    RecordingPublisher publisher;

    tested.world.Runtime().map = 12;
    const auto bloodCastle = SettleWorldEntry({}, tested.Resources(), publisher);
    tested.world.Runtime().map = 20;
    const auto chaosCastle = SettleWorldEntry({}, tested.Resources(), publisher);

    ASSERT_TRUE(bloodCastle.HasValue()) << bloodCastle.Error();
    ASSERT_TRUE(chaosCastle.HasValue()) << chaosCastle.Error();
    EXPECT_EQ(bloodCastle.Value().sounds,
              (std::vector<Client::Audio::SoundId>{Client::Audio::SoundId{0x99}, Client::Audio::SoundId{0x9e}}));
    EXPECT_EQ(chaosCastle.Value().sounds, (std::vector<Client::Audio::SoundId>{Client::Audio::SoundId{0x8b}}));
}

// 0x4c4954: 0x420d60 with the published hero, between the wait and the second arrival effect. With the 0x1c17 in the
// left hand it releases the raven it had and creates another (0x4167b0) at the center of its cell, 300 above the
// terrain, with the 12 rand() of its cells' closes and the counter's; without the 0x1c17 it touches nothing.
TEST_F(WorldEntryTest, ARavenIsCreatedAt420d60AfterTheWait)
{
    constexpr int RavenRandoms = 3 * Client::Models::SmallLogicalCloseRandoms;
    Entry tested(m_dataRoot, Seed);
    tested.states.terrain.current.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    tested.states.terrain.current.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    ActorState& hero = tested.DressedHero(PublishedSlot);
    hero.hands[1].model = 0x1c17;
    hero.cellX = 133;
    hero.cellY = 127;
    hero.darkSpirit.emplace().state = 4;
    RecordingPublisher publisher;
    publisher.onPublish = [&] { EXPECT_EQ(hero.darkSpirit->state, 4); };

    const auto settled = SettleWorldEntry({}, tested.Resources(), publisher);

    ASSERT_TRUE(settled.HasValue()) << settled.Error();
    ASSERT_TRUE(hero.darkSpirit.has_value());
    EXPECT_EQ(hero.darkSpirit->state, 0);
    EXPECT_EQ(hero.darkSpirit->position, glm::vec3(13350.0f, 12750.0f, 300.0f));
    EXPECT_EQ(tested.world.Runtime().warpCounter, Client::Kernel::WaitDrawThreshold);
    EXPECT_EQ(tested.AliveArrivals(), 2u);
    EXPECT_EQ(tested.cloth.random.State(),
              AfterDraws(Seed, OwnRecordClose + WaitClose + WindowRandoms + RavenRandoms + StopRandom).State());

    Entry unarmed(m_dataRoot, Seed);
    ActorState& walker = unarmed.DressedHero(PublishedSlot);
    walker.darkSpirit.emplace().state = 4;
    RecordingPublisher quiet;
    ASSERT_TRUE(SettleWorldEntry({}, unarmed.Resources(), quiet).HasValue());
    ASSERT_TRUE(walker.darkSpirit.has_value());
    EXPECT_EQ(walker.darkSpirit->state, 4);
}

// ---- A second reply (T13; 06, "La entrada al mundo", "Una segunda respuesta") ----

// A whole entry with the set tables and the empty record, in 0 or in 2: the four record closes, the grass, the slot,
// 0x511290, 0x4f2250's two suffixes, 0x550d10's tail, the counter and the windows.
constexpr int WholeEntryRandoms = (4 * OwnRecordClose) + GrassRandoms + HeroRandom + ConstructionRandoms +
                                  (2 * StopRandom) + TailRandoms + WaitClose + WindowRandoms;
// Up to the stop at 0x550990, without knowing what 0x505820 left: the first and the second inline close, the grass,
// the slot, 0x511290, 0x4f2250's suffix and 0x512a30's close, without the tail.
constexpr int EntryUpTo550990 = (3 * OwnRecordClose) + GrassRandoms + HeroRandom + ConstructionRandoms + StopRandom;
// A game frame without steps: the counter (0x637229) and the two accesses to the hero's cell (0x635ae0).
constexpr int FrameRandoms = 4 + 8;
// From an entry's slot to the Lorencia game frame after it; from the frame, to the next one's slot.
constexpr int FromTheSlotToTheFrame = WholeEntryRandoms - (OwnRecordClose + GrassRandoms + HeroRandom);
constexpr int FromTheFrameToTheSlot = OwnRecordClose + GrassRandoms;

// The reference maps, each with two cells.
struct ReferenceMap
{
    std::uint8_t map = 0;
    std::uint8_t firstX = 0;
    std::uint8_t firstY = 0;
    std::uint8_t secondX = 0;
    std::uint8_t secondY = 0;
};
constexpr ReferenceMap Lorencia{.map = 0, .firstX = 133, .firstY = 127, .secondX = 143, .secondY = 120};
constexpr ReferenceMap Devias{.map = 2, .firstX = 220, .firstY = 30, .secondX = 210, .secondY = 40};

// The first game frame of `map` with the hero in its first cell, with `random` on reaching the objects.
FirstObjectPass FirstObjectPassOf(const ReferenceMap& map, const CrtRandom& random)
{
    if (map.map == 0)
    {
        return LorenciaObjectPass(random);
    }
    return {.randoms = DeviasObjectRandoms};
}

Client::Protocol::FrontEnd::CharacterSelectResult ReplyAt(std::uint8_t map, std::uint8_t x, std::uint8_t y,
                                                          std::uint8_t direction)
{
    Client::Protocol::FrontEnd::CharacterSelectResult reply = CableReply();
    reply.map = map;
    reply.x = x;
    reply.y = y;
    reply.direction = direction;
    return reply;
}

CrtRandom Advanced(CrtRandom random, int draws)
{
    for (int draw = 0; draw < draws; ++draw)
    {
        static_cast<void>(random.Next());
    }
    return random;
}

// The slot the next reply draws from `random`: the rand() after the first close and the grass.
std::size_t NextHeroSlot(const CrtRandom& random)
{
    CrtRandom replay = Advanced(random, OwnRecordClose + GrassRandoms);
    return static_cast<std::size_t>(replay.Next() % 400);
}

// The first seed with which both Lorencia entries, with a game frame in between, draw the same slot.
std::uint32_t SeedOfTheSameSlotAfterAFrame()
{
    return Client::Tests::FirstSeedWhere(
        [](CrtRandom& random)
        {
            random = Advanced(random, OwnRecordClose + GrassRandoms);
            const int first = random.Next() % 400;
            random = Advanced(random, FromTheSlotToTheFrame + FrameRandoms);
            random = Advanced(random, LorenciaObjectPass(random).randoms + FramePickingRandoms + HudRandoms +
                                          FromTheFrameToTheSlot);
            return random.Next() % 400 == first;
        });
}

std::size_t AliveActors(const Entry& tested)
{
    return static_cast<std::size_t>(
        std::ranges::count_if(tested.world.Actors(), [](const ActorState& actor) { return actor.alive; }));
}

// A complete frame without steps, like AZeroStepFrameAfterTheEntryDrawsTheTerrainUpToTheObjects: it reaches
// 0x510d00 without a seen actor that publishes 0x505820; it keeps the unknown scan.
void DrawAGameFrame(Entry& tested)
{
    tested.world.Runtime().drawingEnabled = true;
    CompleteFrameResources frameResources;
    const auto loadedFrame = frameResources.Load(tested.dataRoot, tested.log);
    ASSERT_TRUE(loadedFrame.HasValue()) << loadedFrame.Error();
    Client::Scenes::GameScene scene(Client::Scenes::SceneContext{.dataRoot = tested.dataRoot,
                                                                 .textures = frameResources.textures,
                                                                 .log = tested.log,
                                                                 .cloth = &tested.cloth,
                                                                 .camera = &tested.camera,
                                                                 .cameraInput = &tested.cameraInput,
                                                                 .world = &tested.world,
                                                                 .states = &tested.states},
                                    nullptr, &frameResources.models, &frameResources.data.items->table,
                                    &frameResources.models.TextureSlots(), &*frameResources.data.gates,
                                    &*frameResources.data.skills);
    ASSERT_TRUE(scene.Enter().HasValue());
    Client::Tests::RecordingFrameTarget target;
    const CrtRandom before = tested.cloth.random;
    const FirstObjectPass objects = LorenciaObjectPass(Advanced(before, FrameRandoms));

    scene.PrepareFrame();
    ASSERT_FALSE(scene.HoldsTheLastFrame());
    scene.Render(target.BeginFrame({}), 0.0f);

    ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();
    target.Present();
    ASSERT_TRUE(target.frames.back().presented);
    ASSERT_FALSE(tested.world.Runtime().fullSetScan.has_value());
    ASSERT_EQ(tested.cloth.random.State(),
              Advanced(before, FrameRandoms + objects.randoms + FramePickingRandoms + HudRandoms).State());
    scene.Leave();
}

// With the hero in the world, another F3 03 to the same map, in Lorencia and in Devias: 0x61a560 loads it again
// without comparing it with the loaded one (the objects again, not in addition; the grass with 65536 new rand(); of
// the arrival effects, only this one's two), the slot is drawn again after the grass and the new hero is built and
// published there. The previous one has the player's key: 0x510f70 does not kill it and it stays as it was, alive in
// its slot (Q-C196). The rand() are those of the first.
TEST_F(WorldEntryTest, ASecondEntryReloadsAndRedrawsTheHeroSlot)
{
    for (const ReferenceMap& probe : {Lorencia, Devias})
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        Entry tested(m_dataRoot, Seed);
        const Client::Tables::ItemSetTables sets = NoSets();
        WorldEntryResources resources = tested.Resources();
        resources.itemSetTables = &sets;
        RecordingPublisher publisher;
        const auto first =
            ApplyWorldEntry(ReplyAt(probe.map, probe.firstX, probe.firstY, 0), PlayerKey, resources, publisher);
        ASSERT_TRUE(first.HasValue()) << first.Error();
        const std::size_t firstSlot = tested.world.Runtime().hero;
        const ActorState previousBefore = tested.world.Actors()[firstSlot];
        const std::size_t objects = tested.world.Map().objects.size();
        const CrtRandom afterFirst = tested.cloth.random;
        const std::size_t secondSlot = NextHeroSlot(afterFirst);
        ASSERT_NE(secondSlot, firstSlot);

        const auto second =
            ApplyWorldEntry(ReplyAt(probe.map, probe.secondX, probe.secondY, 3), PlayerKey, resources, publisher);

        ASSERT_TRUE(second.HasValue()) << second.Error();
        EXPECT_EQ(publisher.calls, 2);
        EXPECT_EQ(tested.world.Runtime().map, probe.map);
        EXPECT_EQ(tested.world.Runtime().heroSlot, static_cast<std::int32_t>(secondSlot));
        EXPECT_EQ(tested.world.Runtime().hero, secondSlot);
        const ActorState& hero = tested.world.Actors()[secondSlot];
        EXPECT_TRUE(hero.alive);
        EXPECT_EQ(hero.key, PlayerKey);
        EXPECT_EQ(hero.position.x, (static_cast<float>(probe.secondX) * 100.0f) + 50.0f);
        EXPECT_EQ(hero.position.y, (static_cast<float>(probe.secondY) * 100.0f) + 50.0f);
        EXPECT_EQ(hero.angles.z, 90.0f);
        EXPECT_EQ(hero.alpha, 0.0f);
        EXPECT_EQ(hero.revision, 1u);
        EXPECT_EQ(tested.cameraInput.hero, hero.position);
        const ActorState& previous = tested.world.Actors()[firstSlot];
        EXPECT_TRUE(previous.alive);
        EXPECT_EQ(previous.key, PlayerKey);
        EXPECT_EQ(previous.cellX, probe.firstX);
        EXPECT_EQ(previous.cellY, probe.firstY);
        EXPECT_EQ(previous.position, previousBefore.position);
        EXPECT_EQ(previous.revision, previousBefore.revision);
        EXPECT_EQ(AliveActors(tested), 2u);
        EXPECT_EQ(tested.world.Map().objects.size(), objects);
        EXPECT_EQ(tested.AliveArrivals(), 2u);
        CrtRandom grass = Advanced(afterFirst, OwnRecordClose);
        EXPECT_EQ(tested.world.Map().grassOffsets.front(), static_cast<float>(grass.Next() % 4) * 0.25f);
        EXPECT_EQ(tested.world.Runtime().warpCounter, Client::Kernel::WaitDrawThreshold);
        EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
        EXPECT_EQ(tested.cloth.random.State(), Advanced(afterFirst, WholeEntryRandoms).State());
    }
}

// 0x61a560: without the terrain's state the load has nowhere to put the map, and the entry stops there.
TEST_F(WorldEntryTest, WithoutTheTerrainsStateTheLoadStops)
{
    Entry tested(m_dataRoot, Seed);
    const Client::Tables::ItemSetTables sets = NoSets();
    WorldEntryResources resources = tested.Resources();
    resources.itemSetTables = &sets;
    resources.states = nullptr;
    RecordingPublisher publisher;

    const auto entered =
        ApplyWorldEntry(ReplyAt(Lorencia.map, Lorencia.firstX, Lorencia.firstY, 0), PlayerKey, resources, publisher);

    ASSERT_FALSE(entered.HasValue());
    EXPECT_NE(entered.Error().find("0x61a560"), std::string::npos) << entered.Error();
}

// The second reply on the other reference map, from Lorencia to Devias and back: the terrain, the objects (in Devias,
// the special 0x201 and 0x202 first) and the camera are the new map's, and the previous hero stays alive with the old
// one's position. The objects of 0 and 2 take no rand(): the count is that of an entry to the same map.
TEST_F(WorldEntryTest, ASecondEntryToTheOtherReferenceMapLoadsItAndKeepsThePreviousHero)
{
    struct MapChange
    {
        ReferenceMap from;
        ReferenceMap to;
    };
    for (const MapChange& change : {MapChange{Lorencia, Devias}, MapChange{Devias, Lorencia}})
    {
        const ReferenceMap& from = change.from;
        const ReferenceMap& to = change.to;
        SCOPED_TRACE(static_cast<int>(to.map));
        Entry tested(m_dataRoot, Seed);
        const Client::Tables::ItemSetTables sets = NoSets();
        WorldEntryResources resources = tested.Resources();
        resources.itemSetTables = &sets;
        RecordingPublisher publisher;
        ASSERT_TRUE(ApplyWorldEntry(ReplyAt(from.map, from.firstX, from.firstY, 0), PlayerKey, resources, publisher)
                        .HasValue());
        const std::size_t firstSlot = tested.world.Runtime().hero;
        const ActorState previousBefore = tested.world.Actors()[firstSlot];
        const CrtRandom afterFirst = tested.cloth.random;
        const std::size_t secondSlot = NextHeroSlot(afterFirst);
        ASSERT_NE(secondSlot, firstSlot);

        const auto second = ApplyWorldEntry(ReplyAt(to.map, to.firstX, to.firstY, 0), PlayerKey, resources, publisher);

        ASSERT_TRUE(second.HasValue()) << second.Error();
        EXPECT_EQ(tested.world.Runtime().map, to.map);
        EXPECT_EQ(tested.cameraInput.map, to.map);
        const auto terrain = Client::Terrain::LoadWorldTerrain(m_dataRoot, to.map);
        ASSERT_TRUE(terrain.HasValue()) << terrain.Error();
        ASSERT_TRUE(tested.states.terrain.current.attributes);
        EXPECT_EQ(tested.states.terrain.current.attributes->cells, terrain.Value().attributes->cells);
        ASSERT_FALSE(tested.world.Map().objects.empty());
        EXPECT_EQ(tested.world.Map().objects.front().type == 0x201, to.map == Devias.map);
        EXPECT_EQ(tested.world.Runtime().hero, secondSlot);
        const ActorState& hero = tested.world.Actors()[secondSlot];
        EXPECT_EQ(hero.position.x, (static_cast<float>(to.firstX) * 100.0f) + 50.0f);
        EXPECT_EQ(hero.position.y, (static_cast<float>(to.firstY) * 100.0f) + 50.0f);
        const ActorState& previous = tested.world.Actors()[firstSlot];
        EXPECT_TRUE(previous.alive);
        EXPECT_EQ(previous.position, previousBefore.position);
        EXPECT_EQ(tested.AliveArrivals(), 2u);
        EXPECT_EQ(tested.cloth.random.State(), Advanced(afterFirst, WholeEntryRandoms).State());
    }
}

// Q-C197: while 0x512a30 dresses the new hero, the published one (0x743a170) is the previous, and 0x40d010 sets its
// +0x358 to 0 (0x40d07d). The new one has it at 0 from 0x511290 (0x511c99).
TEST_F(WorldEntryTest, TheSecondEntryRecalculatesThe358OfThePreviousHero)
{
    Entry tested(m_dataRoot, Seed);
    const Client::Tables::ItemSetTables sets = NoSets();
    WorldEntryResources resources = tested.Resources();
    resources.itemSetTables = &sets;
    RecordingPublisher publisher;
    ASSERT_TRUE(ApplyWorldEntry(CableReply(), PlayerKey, resources, publisher).HasValue());
    ActorState& previous = tested.world.Hero();
    previous.byte358 = 1;
    const std::size_t secondSlot = NextHeroSlot(tested.cloth.random);
    ASSERT_NE(secondSlot, tested.world.Runtime().hero);
    tested.world.Actors()[secondSlot].byte358 = 1;

    ASSERT_TRUE(ApplyWorldEntry(CableReply(), PlayerKey, resources, publisher).HasValue());

    EXPECT_EQ(previous.byte358, 0);
    EXPECT_EQ(tested.world.Actors()[secondSlot].byte358, 0);
}

// After a game frame, a second reply elsewhere dresses the new one with the previous one published: 0x505820 does
// not run and 0x550990 reads what the frame's character drawing left, which MuRet does not have. It stops there
// (P-C40) with the previous one still published and the new one built, both alive, and the counter as it was.
TEST_F(WorldEntryTest, AfterAGameFrameASecondEntryElsewhereStopsAt0x550990)
{
    Entry tested(m_dataRoot, Seed);
    const Client::Tables::ItemSetTables sets = NoSets();
    WorldEntryResources resources = tested.Resources();
    resources.itemSetTables = &sets;
    RecordingPublisher publisher;
    ASSERT_TRUE(ApplyWorldEntry(CableReply(), PlayerKey, resources, publisher).HasValue());
    const std::size_t firstSlot = tested.world.Runtime().hero;
    ASSERT_NO_FATAL_FAILURE(DrawAGameFrame(tested));
    // As if steps had run (0x59af01).
    tested.world.Runtime().warpCounter = 7;
    const CrtRandom afterTheFrame = tested.cloth.random;
    const std::size_t secondSlot = NextHeroSlot(afterTheFrame);
    ASSERT_NE(secondSlot, firstSlot);

    const auto second = ApplyWorldEntry(ReplyAt(0, 143, 120, 3), PlayerKey, resources, publisher);

    ASSERT_FALSE(second.HasValue());
    EXPECT_NE(second.Error().find("0x550990"), std::string::npos) << second.Error();
    EXPECT_EQ(publisher.calls, 1);
    EXPECT_EQ(tested.world.Runtime().hero, firstSlot);
    EXPECT_EQ(tested.world.Runtime().heroSlot, static_cast<std::int32_t>(secondSlot));
    EXPECT_TRUE(tested.world.Actors()[firstSlot].alive);
    EXPECT_TRUE(tested.world.Actors()[secondSlot].alive);
    EXPECT_EQ(tested.world.Actors()[secondSlot].key, PlayerKey);
    EXPECT_EQ(tested.world.Runtime().warpCounter, 7);
    EXPECT_FALSE(tested.world.Runtime().fullSetScan.has_value());
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(tested.cloth.random.State(), Advanced(afterTheFrame, EntryUpTo550990).State());
}

// With the seed that draws the same slot twice, the second reply rebuilds the published hero in its record:
// 0x512a30 dresses the published one, 0x505820 runs with it (0x512ee7) and leaves its own, and 0x550990 reads it
// even though a game frame erased what came before. The class's parts are not a set: [0x743a178] at 0 and
// [0x743a17c] at 1.
TEST_F(WorldEntryTest, AfterAGameFrameASecondEntryOnTheSameSlotScansTheRebuiltHero)
{
    const std::uint32_t seed = SeedOfTheSameSlotAfterAFrame();
    Entry tested(m_dataRoot, seed);
    const Client::Tables::ItemSetTables sets = NoSets();
    WorldEntryResources resources = tested.Resources();
    resources.itemSetTables = &sets;
    RecordingPublisher publisher;
    ASSERT_TRUE(ApplyWorldEntry(CableReply(), PlayerKey, resources, publisher).HasValue());
    const std::size_t slot = tested.world.Runtime().hero;
    ASSERT_NO_FATAL_FAILURE(DrawAGameFrame(tested));
    const CrtRandom afterTheFrame = tested.cloth.random;
    ASSERT_EQ(NextHeroSlot(afterTheFrame), slot);

    const auto second = ApplyWorldEntry(ReplyAt(0, 143, 120, 3), PlayerKey, resources, publisher);

    ASSERT_TRUE(second.HasValue()) << second.Error();
    EXPECT_EQ(publisher.calls, 2);
    EXPECT_EQ(tested.world.Runtime().hero, slot);
    const ActorState& hero = tested.world.Actors()[slot];
    EXPECT_EQ(hero.revision, 2u);
    EXPECT_EQ(hero.cellX, 143);
    EXPECT_EQ(hero.cellY, 120);
    EXPECT_EQ(AliveActors(tested), 1u);
    ASSERT_TRUE(tested.world.Runtime().fullSetScan.has_value());
    EXPECT_EQ(tested.world.Runtime().fullSetScan->dword743a178, 0);
    EXPECT_EQ(tested.world.Runtime().fullSetScan->byte743a17c, 1);
    EXPECT_EQ(tested.cloth.random.State(), Advanced(afterTheFrame, WholeEntryRandoms).State());
}

// Through the session: with the 0x3d already published, another encrypted F3 03 still goes to 0x4c3940 (0x4e306b
// checks neither the state nor the scene) and publishes the 0x3d again, with the new hero.
TEST_F(WorldEntryTest, AnEntryInTheWorldIsAppliedAgainByTheSession)
{
    Entry tested(m_dataRoot, Seed);
    const Client::Tables::ItemSetTables sets = NoSets();
    WorldEntryResources resources = tested.Resources();
    resources.itemSetTables = &sets;
    Client::Scenes::SessionEventApplier applier(resources);
    Client::Tests::LoggedInSession online(&applier);
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    Client::Network::PacketSerials serials;
    online.socket.reads = {EntryPacket(133, 127, serials)};
    static_cast<void>(online.session.Poll());
    ASSERT_EQ(online.session.State(), Client::Session::SessionState::InWorld);
    const std::size_t firstSlot = tested.world.Runtime().hero;
    const std::size_t secondSlot = NextHeroSlot(tested.cloth.random);
    ASSERT_NE(secondSlot, firstSlot);

    online.socket.reads = {EntryPacket(143, 120, serials)};
    static_cast<void>(online.session.Poll());

    EXPECT_EQ(online.session.State(), Client::Session::SessionState::InWorld);
    EXPECT_EQ(tested.world.Runtime().hero, secondSlot);
    EXPECT_EQ(tested.Hero().cellX, 143);
    EXPECT_TRUE(tested.world.Actors()[firstSlot].alive);
    EXPECT_EQ(tested.world.Runtime().warpCounter, Client::Kernel::WaitDrawThreshold);
}

// T11b, end to end: the selection drawing (0x63475c) leaves in the globals the trapezoid of its fixed point; the
// world entry (F3 03) does not touch it, and the first game step after the wait reads it in 0x508f00 (0x63710f). In
// Lorencia the hero at (133, 127) falls inside and is seen; in Devias the one at (220, 30) falls outside (Q-C216).
// The step's rand(): MoveHero's 8, the TerrainWall's 8, the hero's body (0x4fd5f0: the own record close in
// 0x4fc450 and the rand() % 6 of 0x4fd7e4) and, with the hero seen, the 8 of its logical cell and the rand() & 7;
// then the particle wind's two and the cloth. The next frame passes the floor marks (0x5fc365), which take no
// rand(), and the objects, with their 4 + 8 and one per seen object; Lorencia, with the odd rand() % 2 of the 0x69,
// goes on. Both skip the arrival effects not yet seen and present the complete frame. The new prefix includes
// leaves/fauna, Gate.bmd, own/Skill and two live Arrival.
TEST_F(WorldEntryTest, TheFirstGameStepReadsTheTrapezoidOfTheLastSelectionDraw)
{
    const Client::Terrain::TerrainPolygon selected = Client::Terrain::TerrainPolygonOf(
        Client::Terrain::CullingScene::CharacterSelect, 54, Client::Camera::SelectionCullingPoint,
        Client::Camera::SelectionCamera().angles.z);
    for (const ReferenceMap& probe : {Lorencia, Devias})
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        const bool seen = probe.map == Lorencia.map;
        Entry tested(m_dataRoot, Seed);
        Client::Tests::FakeTextureStore store;
        const Client::Scenes::SceneContext context{.dataRoot = m_dataRoot,
                                                   .textures = store,
                                                   .log = tested.log,
                                                   .cloth = &tested.cloth,
                                                   .camera = &tested.camera,
                                                   .cameraInput = &tested.cameraInput,
                                                   .world = &tested.world,
                                                   .states = &tested.states};
        const Client::Tables::GameData data;
        Client::Scenes::CharacterSelectScene selection(context, data, tested.models, nullptr);
        ASSERT_TRUE(selection.Enter().HasValue());
        Client::Tests::RecordingFrameTarget target;
        selection.Render(target.BeginFrame({}), 0.0f);
        target.Present();
        ASSERT_EQ(selection.Failure(), std::nullopt);
        selection.Leave();
        ASSERT_EQ(tested.states.camera.trapezoid.x, selected.x);
        ASSERT_EQ(tested.states.camera.trapezoid.y, selected.y);
        const Client::Tables::ItemSetTables sets = NoSets();
        WorldEntryResources resources = tested.Resources();
        resources.itemSetTables = &sets;
        RecordingPublisher publisher;
        const auto entered =
            ApplyWorldEntry(ReplyAt(probe.map, probe.firstX, probe.firstY, 0), PlayerKey, resources, publisher);
        ASSERT_TRUE(entered.HasValue()) << entered.Error();
        EXPECT_EQ(tested.states.camera.trapezoid.x, selected.x) << "la entrada no toca el trapecio";
        EXPECT_EQ(tested.states.camera.trapezoid.y, selected.y);
        // What loading the selection's 54 left (0x633f30): the world loaded.
        tested.world.Runtime().drawingEnabled = true;
        CompleteFrameResources frameResources;
        const auto loadedFrame = frameResources.Load(m_dataRoot, tested.log);
        ASSERT_TRUE(loadedFrame.HasValue()) << loadedFrame.Error();
        const Client::Scenes::SceneContext gameContext{.dataRoot = m_dataRoot,
                                                       .textures = frameResources.textures,
                                                       .log = tested.log,
                                                       .cloth = &tested.cloth,
                                                       .camera = &tested.camera,
                                                       .cameraInput = &tested.cameraInput,
                                                       .world = &tested.world,
                                                       .states = &tested.states};
        Client::Scenes::GameScene game(gameContext, nullptr, &frameResources.models, &frameResources.data.items->table,
                                       &frameResources.models.TextureSlots(), &*frameResources.data.gates,
                                       &*frameResources.data.skills);
        ASSERT_TRUE(game.Enter().HasValue());
        Client::Models::ClothWorld reference = tested.cloth;
        // 0x4eb620, at the start of the frame: 0x552f10 (the skill wait) opens and closes the own record. 0x636920: the
        // first step's banner (0x46b010 and 0x46b0e0, which opens and closes the own record; without Slide.bmd it makes
        // no random pick), and the step's UI.
        reference.random = Advanced(reference.random, OwnRecordClose + OwnRecordClose + GameUiStepRandoms);
        const auto heroBefore = tested.Hero();
        const auto weatherBefore = tested.world.Runtime().leafWeather;
        const auto layerBefore = tested.world.Runtime().heroCellLayer;

        game.Step(Client::Input::InputState{});

        ASSERT_EQ(game.Failure(), std::nullopt);
        ASSERT_TRUE(
            AdvanceEnvironmentPrefix(tested.world, tested.states, heroBefore, weatherBefore, layerBefore, reference)
                .HasValue());
        const int body = OwnRecordClose + 1;
        // 0x57cfc0 closes own (3848) and Skill.bmd (60); the two live Arrival add one each.
        reference.random = Advanced(
            reference.random, (seen ? 8 + 8 + body + 8 + 1 : 8 + 8 + body) + OwnRecordClose + 60 + 2 + 2 +
                                  GateProbeRandoms(*frameResources.data.gates, tested.world.Runtime().map, heroBefore));
        Client::Models::StepTheClothWorld(reference);
        EXPECT_EQ(tested.world.Runtime().warpCounter, Client::Kernel::WaitDrawThreshold - 1);
        EXPECT_EQ(tested.Hero().seen, seen);
        EXPECT_EQ(tested.cloth.random.State(), reference.random.State());

        game.PrepareFrame();
        ASSERT_FALSE(game.HoldsTheLastFrame());
        game.Render(target.BeginFrame({}), 0.0f);

        const FirstObjectPass objects = FirstObjectPassOf(probe, Advanced(reference.random, FrameRandoms));
        ASSERT_FALSE(game.Failure().has_value()) << *game.Failure();
        target.Present();
        EXPECT_TRUE(target.frames.back().presented);
        // The seen hero reaches its own color (a 3848 close) and the common rand() of 0x50db57.
        const int characterDraws = seen ? OwnRecordClose + 1 : 0;
        EXPECT_EQ(tested.cloth.random.State(),
                  Advanced(reference.random,
                           FrameRandoms + objects.randoms + characterDraws + FramePickingRandoms + HudRandoms)
                      .State());
        // The wire's hero is a GM (CtlCode 0x20): drawn, 0x413fe0 sets its +0x360 to 5 and the MuSign layer.
        EXPECT_EQ(tested.Hero().byte360, seen ? 5 : 0);
        EXPECT_EQ(tested.Hero().layer.has_value(), seen);
        game.Leave();
    }
}
} // namespace
