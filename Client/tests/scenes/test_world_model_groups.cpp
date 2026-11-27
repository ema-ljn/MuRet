#include "Assets/WorldModels.h"

#include "Core/Log.h"
#include "Core/ILogSink.h"
#include "Data/Models/Model.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Render/ModelBatch.h"
#include "Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Assets/GameModels.h"
#include "Assets/ModelTextureTable.h"

#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/SyntheticModels.h"
#include "support/TemporaryFolder.h"
#include "support/TextureFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using Client::Render::TextureSlot;

struct Special
{
    std::uint16_t model;
    std::string_view folder;
    std::string_view file;
    std::uint16_t slot;
    std::string_view textures{};
};

// 05 § Los especiales de cada mapa. Folders, numbers and order checked against 0x6176f3..0x618c45.
constexpr std::array<Special, 8> Atlans{{{0xb3, "Object8", "Fish02.bmd", 0x69},
                                         {0xb4, "Object8", "Fish03.bmd", 0x6a},
                                         {0xb5, "Object8", "Fish04.bmd", 0x6b},
                                         {0xb6, "Object8", "Fish05.bmd", 0x6c},
                                         {0xb7, "Object8", "Fish06.bmd", 0x6d},
                                         {0xb8, "Object8", "Fish07.bmd", 0x6e},
                                         {0xb9, "Object8", "Fish08.bmd", 0x6f},
                                         {0xba, "Object8", "Fish09.bmd", 0x70}}};
constexpr std::array<Special, 1> Icarus{{{0xbb, "Object11", "cloud.bmd", 0x69}}};
constexpr std::array<Special, 1> BloodCastle{{{0xbc, "Object12", "Crow01.bmd", 0x69}}};
constexpr std::array<Special, 1> ChaosCastle{{{0x1f7, "Player", "Angel.bmd", 0x69, "Npc"}}};
constexpr std::array<Special, 11> Kalima{{{0x11d, "Monster", "cd71a.bmd", 0x69},
                                          {0x11e, "Monster", "cd71b.bmd", 0x6a},
                                          {0x11f, "Monster", "cd71c.bmd", 0x6b},
                                          {0x120, "Monster", "cd71d.bmd", 0x6c},
                                          {0x121, "Monster", "cd71e.bmd", 0x6d},
                                          {0x122, "Monster", "cd71f.bmd", 0x6e},
                                          {0x123, "Monster", "cd71g.bmd", 0x6f},
                                          {0x124, "Monster", "cd71h.bmd", 0x70},
                                          {0x125, "Skill", "dragonhead.bmd", 0x71},
                                          {0x126, "Skill", "phoenix.bmd", 0x72},
                                          {0x127, "Monster", "cundun_gone.bmd", 0x73}}};
constexpr std::array<Special, 1> Trials{{{0xae, "Object1", "Butterfly01.bmd", 0x69}}};
constexpr std::array<Special, 3> Aida{{{0xae, "Object1", "Butterfly01.bmd", 0x69},
                                       {0x141, "Object34", "tree_eff.bmd", 0x6a},
                                       {0xb2, "Object9", "Bug02.bmd", 0x6b}}};
constexpr std::array<Special, 3> Crywolf{{{0x148, "Object35", "scorpion.bmd", 0x69},
                                          {0xed, "Monster", "arrowstusk.bmd", 0x6a},
                                          {0xee, "Monster", "arrowstusk.bmd", 0x6a}}};

struct MapCase
{
    std::uint8_t map;
    std::string_view objects;
    std::span<const Special> specials;
};

constexpr std::array<MapCase, 25> Cases{
    {{7, "Object8", Atlans},        {10, "Object11", Icarus},      {11, "Object12", BloodCastle},
     {12, "Object12", BloodCastle}, {13, "Object12", BloodCastle}, {14, "Object12", BloodCastle},
     {15, "Object12", BloodCastle}, {16, "Object12", BloodCastle}, {17, "Object12", BloodCastle},
     {18, "Object19", ChaosCastle}, {19, "Object19", ChaosCastle}, {20, "Object19", ChaosCastle},
     {21, "Object19", ChaosCastle}, {22, "Object19", ChaosCastle}, {23, "Object19", ChaosCastle},
     {24, "Object25", Kalima},      {25, "Object25", Kalima},      {26, "Object25", Kalima},
     {27, "Object25", Kalima},      {28, "Object25", Kalima},      {29, "Object25", Kalima},
     {31, "Object32", Trials},      {33, "Object34", Aida},        {34, "Object35", Crywolf},
     {36, "Object25", Kalima}}};

void WriteModel(const std::filesystem::path& root, const Special& special, bool actions = true)
{
    Client::Data::Models::Model model = Client::Tests::SampleModel();
    // Both arrowstusk load the same file and share its texture; the id is not the name.
    const std::string texture = "group" + std::to_string(special.slot) + ".tga";
    model.meshes.front().textureName = texture;
    if (!actions)
    {
        model.actions.clear();
        for (auto& bone : model.bones)
            bone.actions.clear();
    }
    constexpr std::uint8_t ClearVersion = 0x0a;
    Client::Tests::WriteSceneFile(root, std::filesystem::path(special.folder) / special.file,
                                  Client::Tests::PlainBmd(Client::Tests::BmdContent(model), ClearVersion));
    const auto textureFolder = special.textures.empty() ? special.folder : special.textures;
    const std::filesystem::path textureFile =
        std::filesystem::path(textureFolder) / ("group" + std::to_string(special.slot) + ".OZT");
    Client::Tests::WriteSceneFile(root, textureFile, Client::Tests::PlainOzt(4, 4));
}

void WriteMap(const std::filesystem::path& root, const MapCase& probe)
{
    for (const Special& special : probe.specials)
        WriteModel(root, special);
    WriteModel(root, Special{0, probe.objects, "Object01.bmd", 0x41c});
    constexpr std::array<Client::Data::Textures::TextureSetEntry, 1> GateTexture{
        {{"Monster\\doorc01.jpg",
          0x6a,
          {Client::Data::Textures::Filter::Nearest, Client::Data::Textures::Wrap::Repeat}}}};
    Client::Tests::WriteTextureSet(root, GateTexture);
}

void WritePlayerSkeleton(const std::filesystem::path& root)
{
    Client::Data::Models::Model player = Client::Tests::SampleModel();
    player.meshes.clear(); // Q-C88: Player.bmd is the skeleton without meshes.
    constexpr std::uint8_t ClearVersion = 0x0a;
    Client::Tests::WriteSceneFile(root, "Player/Player.bmd",
                                  Client::Tests::PlainBmd(Client::Tests::BmdContent(player), ClearVersion));
}

void ExpectOptionalGateBindings(const Client::Assets::WorldModels& models, const Client::Assets::GameModels& globals,
                                const Client::Tests::FakeTextureStore& store, std::uint16_t available)
{
    EXPECT_EQ(models.Count(), 2u);
    ASSERT_NE(models.Find(Client::Models::ModelId{0xbc}), nullptr);
    ASSERT_NE(models.Find(Client::Models::ModelId{0}), nullptr);
    ASSERT_EQ(models.MeshTextures(Client::Models::ModelId{0xbc}).size(), 1u);
    ASSERT_EQ(models.MeshTextures(Client::Models::ModelId{0}).size(), 1u);
    EXPECT_EQ(models.MeshTextures(Client::Models::ModelId{0xbc}).front(), TextureSlot{0x69});
    EXPECT_EQ(models.MeshTextures(Client::Models::ModelId{0}).front(), TextureSlot{0x41c});
    std::vector<std::uint16_t> expectedUploads{0x69};
    if (available != 0)
        expectedUploads.push_back(0x6a);
    expectedUploads.push_back(0x41c);
    EXPECT_EQ(store.loaded, expectedUploads);
    ASSERT_NE(models.TextureSlots(), nullptr);
    EXPECT_EQ(models.TextureSlots()->ToLoad().size(), expectedUploads.size());
    for (const std::uint16_t gate : {std::uint16_t{0x10c}, std::uint16_t{0x10d}})
    {
        EXPECT_EQ(models.Find(Client::Models::ModelId{gate}), globals.Find(Client::Models::ModelId{gate}));
        const auto slots = models.MeshTextures(Client::Models::ModelId{gate});
        EXPECT_EQ(slots.size(), gate == available ? 1u : 0u);
        if (gate == available)
        {
            ASSERT_EQ(slots.size(), 1u);
            EXPECT_EQ(slots.front(), TextureSlot{0x6a});
        }
    }
}

class WorldModelGroupsTest : public Client::Tests::GoldenDataTest
{
protected:
    void SetUp() override
    {
        Client::Tests::GoldenDataTest::SetUp();
        if (HasFatalFailure())
            return;
        ASSERT_TRUE(m_globals.Load(m_dataRoot, m_store, m_log).HasValue());
    }

    Client::Tests::FakeTextureStore m_store;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    Client::Assets::GameModels m_globals;
};

TEST_F(WorldModelGroupsTest, EveryRemainingNominalMapLoadsItsSpecialFilesAndTextureFolders)
{
    for (const MapCase& probe : Cases)
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        Client::Tests::TemporaryFolder folder{"world-special-group"};
        WriteMap(folder.Path(), probe);
        Client::Assets::WorldModels models;

        const auto loaded =
            models.Load(folder.Path(), probe.map, m_store, m_log, &m_globals.TextureSlots(), &m_globals);

        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        EXPECT_EQ(models.Count(), probe.specials.size() + 1);
        for (const Special& special : probe.specials)
        {
            SCOPED_TRACE(special.model);
            ASSERT_NE(models.Find(Client::Models::ModelId{special.model}), nullptr);
            ASSERT_NE(models.SkeletonOf(Client::Models::ModelId{special.model}), nullptr);
            const auto slots = models.MeshTextures(Client::Models::ModelId{special.model});
            ASSERT_EQ(slots.size(), 1u);
            EXPECT_EQ(slots.front(), TextureSlot{special.slot});
            EXPECT_TRUE(m_store.Find(slots.front()).has_value());
        }
        ASSERT_EQ(models.MeshTextures(Client::Models::ModelId{0}).size(), 1u);
        EXPECT_EQ(models.MeshTextures(Client::Models::ModelId{0}).front(), TextureSlot{0x41c});
        models.Release();
    }
}

TEST_F(WorldModelGroupsTest, EveryRemainingFamilyFindsItsSpecialModelsInTheRealData)
{
    for (const MapCase& probe : Cases)
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        Client::Assets::WorldModels models;

        const auto loaded = models.Load(m_dataRoot, probe.map, m_store, m_log, &m_globals.TextureSlots(), &m_globals);

        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        for (const Special& special : probe.specials)
        {
            SCOPED_TRACE(special.model);
            ASSERT_NE(models.Find(Client::Models::ModelId{special.model}), nullptr);
            EXPECT_NE(models.SkeletonOf(Client::Models::ModelId{special.model}), nullptr);
            EXPECT_FALSE(models.MeshTextures(Client::Models::ModelId{special.model}).empty());
        }
        models.Release();
    }
}

TEST_F(WorldModelGroupsTest, BloodCastleBorrowsTheModelsAndOverlaysTheirTextureBindings)
{
    Client::Tests::TemporaryFolder folder{"world-gate-overlay"};
    WriteMap(folder.Path(), Cases[2]);
    Client::Assets::WorldModels models;

    ASSERT_TRUE(models.Load(folder.Path(), 11, m_store, m_log, &m_globals.TextureSlots(), &m_globals).HasValue());

    for (const std::uint16_t gate : {std::uint16_t{0x10c}, std::uint16_t{0x10d}})
    {
        EXPECT_EQ(models.Find(Client::Models::ModelId{gate}), m_globals.Find(Client::Models::ModelId{gate}));
        EXPECT_EQ(models.SkeletonOf(Client::Models::ModelId{gate}),
                  m_globals.SkeletonOf(Client::Models::ModelId{gate}));
        ASSERT_EQ(models.MeshTextures(Client::Models::ModelId{gate}).size(), 1u);
        EXPECT_EQ(models.MeshTextures(Client::Models::ModelId{gate}).front(), TextureSlot{0x6a});
        // 0x616370 and the sweep of 0x616e46 leave both global models on doorc01.jpg.
        ASSERT_EQ(m_globals.MeshTextures(Client::Models::ModelId{gate}).size(), 1u);
        EXPECT_EQ(m_globals.MeshTextures(Client::Models::ModelId{gate}).front(), TextureSlot{0x69});
    }
    EXPECT_EQ(models.Count(), 2u) << "solo Crow y Object01 son propios del mapa";
    EXPECT_EQ(models.TextureSlotOf(0x69), TextureSlot{0x69});
    EXPECT_EQ(models.TextureSlotOf(0x6a), TextureSlot{0x6a});
    EXPECT_EQ(models.TextureSlotOf(0x622), m_globals.TextureSlotOf(0x622));
    EXPECT_FALSE(models.TextureSlotOf(0x10069).has_value());
    const auto crow = models.TextureMetadataOf(0x69);
    ASSERT_TRUE(crow.has_value());
    EXPECT_EQ(crow->components, 4);
    const auto initialGate = m_globals.TextureMetadataOf(0x69);
    ASSERT_TRUE(initialGate.has_value());
    EXPECT_EQ(initialGate->components, 3);
    models.Release();
    EXPECT_EQ(models.Find(Client::Models::ModelId{0x10c}), nullptr);
    EXPECT_FALSE(models.TextureSlotOf(0x69).has_value());
}

// 0x6179ce loads Crow's textures before 0x6179f0. At 0x60d617..0x60d61b an optional Gate without meshes does not
// enter the loop or reserve a slot; that is not the same as having no library.
TEST(WorldOptionalGateModelsTest, BloodCastleSkipsAbsentGlobalGateFilesAndContinuesItsOwnModels)
{
    constexpr std::array<std::uint16_t, 3> AvailableGate{0, 0x10c, 0x10d};
    for (const std::uint16_t available : AvailableGate)
    {
        SCOPED_TRACE(available);
        Client::Tests::TemporaryFolder folder{"world-optional-global-gate"};
        WriteMap(folder.Path(), Cases[2]);
        WritePlayerSkeleton(folder.Path());
        if (available != 0)
        {
            const std::string_view file = available == 0x10c ? "Gate01.bmd" : "Gate02.bmd";
            WriteModel(folder.Path(), Special{available, "Object12", file, 0x6a, "Monster"});
        }
        Client::Tests::FakeTextureStore store;
        Client::Tests::RecordingSink sink;
        Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
        Client::Assets::GameModels globals;
        const auto globalsLoaded = globals.Load(folder.Path(), store, log);
        ASSERT_TRUE(globalsLoaded.HasValue()) << globalsLoaded.Error();
        store.loaded.clear();
        Client::Assets::WorldModels models;

        const auto loaded = models.Load(folder.Path(), 11, store, log, &globals.TextureSlots(), &globals);

        EXPECT_TRUE(loaded.HasValue()) << loaded.Error();
        if (!loaded.HasValue())
            continue;
        ExpectOptionalGateBindings(models, globals, store, available);
        models.Release();
    }
}

TEST_F(WorldModelGroupsTest, AFaultedUploadKeepsOnlyTheActualPhysicalTextureMetadata)
{
    Client::Tests::TemporaryFolder folder{"world-gate-upload-failure"};
    WriteMap(folder.Path(), Cases[2]);
    Client::Assets::WorldModels models;
    m_store.failNextLoad = true;

    ASSERT_TRUE(models.Load(folder.Path(), 11, m_store, m_log, &m_globals.TextureSlots(), &m_globals).HasValue());

    const auto metadata = models.TextureMetadataOf(0x69);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->components, 3) << "quedó Gate02, no se publicó el Crow RGBA";
    EXPECT_EQ(models.TextureSlotOf(0x69), TextureSlot{0x69});
    const auto physical = m_store.Find(TextureSlot{0x69});
    ASSERT_TRUE(physical.has_value());
    EXPECT_EQ(physical->texture.format, Client::Data::Textures::PixelFormat::Rgb);
    models.Release();
    EXPECT_TRUE(m_store.Find(TextureSlot{0x69}).has_value());
}

TEST_F(WorldModelGroupsTest, KalimaAppliesOnlyTheFourConcreteFirstActionStores)
{
    Client::Tests::TemporaryFolder folder{"world-kalima-actions"};
    WriteMap(folder.Path(), Cases[15]);
    Client::Assets::WorldModels models;

    ASSERT_TRUE(models.Load(folder.Path(), 24, m_store, m_log, nullptr).HasValue());

    for (const std::uint16_t model : {std::uint16_t{0x122}, std::uint16_t{0x123}})
    {
        const auto* skeleton = models.SkeletonOf(Client::Models::ModelId{model});
        ASSERT_NE(skeleton, nullptr);
        ASSERT_GE(skeleton->actions.size(), 2u);
        EXPECT_FLOAT_EQ(skeleton->actions[0].speed, 0.13f);
        EXPECT_TRUE(skeleton->actions[0].repeats);
        EXPECT_FLOAT_EQ(skeleton->actions[1].speed, 0.0f);
    }
    ASSERT_NE(models.SkeletonOf(Client::Models::ModelId{0x126}), nullptr);
    EXPECT_FALSE(models.SkeletonOf(Client::Models::ModelId{0x126})->actions[0].repeats);
    ASSERT_NE(models.SkeletonOf(Client::Models::ModelId{0x127}), nullptr);
    EXPECT_TRUE(models.SkeletonOf(Client::Models::ModelId{0x127})->actions[0].repeats);
    ASSERT_NE(models.SkeletonOf(Client::Models::ModelId{0x11d}), nullptr);
    EXPECT_FLOAT_EQ(models.SkeletonOf(Client::Models::ModelId{0x11d})->actions[0].speed, 0.0f);
    models.Release();
}

TEST_F(WorldModelGroupsTest, DungeonWritesTheSecondActionSpeedOnlyOnMapOne)
{
    Client::Tests::TemporaryFolder folder{"world-dungeon-action"};
    WriteModel(folder.Path(), Special{40, "Object2", "Object41.bmd", 0x41c});
    WriteModel(folder.Path(), Special{40, "Object5", "Object41.bmd", 0x41c});
    Client::Assets::WorldModels models;
    for (const std::uint8_t map : {std::uint8_t{1}, std::uint8_t{4}})
    {
        SCOPED_TRACE(static_cast<int>(map));
        ASSERT_TRUE(models.Load(folder.Path(), map, m_store, m_log, nullptr).HasValue());
        const auto* skeleton = models.SkeletonOf(Client::Models::ModelId{40});
        ASSERT_NE(skeleton, nullptr);
        ASSERT_EQ(skeleton->actions.size(), 2u);
        EXPECT_FLOAT_EQ(skeleton->actions[0].speed, 0.0f);
        EXPECT_FLOAT_EQ(skeleton->actions[1].speed, map == 1 ? 0.4f : 0.0f);
        models.Release();
    }
}

TEST_F(WorldModelGroupsTest, AKalimaActionMissingFromTheBmdStopsAfterTheEarlierStores)
{
    Client::Tests::TemporaryFolder folder{"world-kalima-missing-action"};
    WriteMap(folder.Path(), Cases[15]);
    WriteModel(folder.Path(), Kalima.back(), false);
    Client::Assets::WorldModels models;
    const auto before = m_store.loadCalls;

    const auto loaded = models.Load(folder.Path(), 24, m_store, m_log, nullptr);

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find("0x618ca3"), std::string::npos) << loaded.Error();
    ASSERT_NE(models.SkeletonOf(Client::Models::ModelId{0x122}), nullptr);
    EXPECT_FLOAT_EQ(models.SkeletonOf(Client::Models::ModelId{0x122})->actions[0].speed, 0.13f);
    ASSERT_NE(models.SkeletonOf(Client::Models::ModelId{0x126}), nullptr);
    EXPECT_FALSE(models.SkeletonOf(Client::Models::ModelId{0x126})->actions[0].repeats);
    EXPECT_EQ(m_store.loadCalls, before);
    models.Release();
}

TEST_F(WorldModelGroupsTest, MissingExileSpecialsAreOptionalButTheirInvalidWrapIsExplicit)
{
    Client::Tests::TemporaryFolder folder{"world-exile-specials"};
    Client::Assets::WorldModels models;
    const auto before = m_store.loadCalls;
    const auto absent = models.Load(folder.Path(), 5, m_store, m_log, nullptr);
    ASSERT_TRUE(absent.HasValue()) << absent.Error();
    EXPECT_EQ(models.Count(), 0u);
    EXPECT_EQ(m_store.loadCalls, before);
    WriteModel(folder.Path(), Special{0xe7, "Object6", "Meteo01.bmd", 0x69});

    const auto present = models.Load(folder.Path(), 5, m_store, m_log, nullptr);

    ASSERT_FALSE(present.HasValue());
    EXPECT_NE(present.Error().find("0x617699"), std::string::npos) << present.Error();
    EXPECT_NE(models.Find(Client::Models::ModelId{0xe7}), nullptr);
    EXPECT_EQ(m_store.loadCalls, before);
    models.Release();
}
// A provisional assignment does not own the reference of the model that already left the texture resident.
TEST(WorldModelOwnershipTest, FailedActionsKeepResidentReferencesAcrossReleaseAndRetry)
{
    constexpr TextureSlot SharedSlot{0x68};
    constexpr std::array<Client::Data::Textures::TextureSetEntry, 1> SharedTexture{
        {{"Object2\\group104.tga", SharedSlot.value, {}}}};
    for (const std::uint8_t map : {std::uint8_t{1}, std::uint8_t{30}})
    {
        SCOPED_TRACE(static_cast<int>(map));
        Client::Tests::TemporaryFolder folder{"world-texture-ownership"};
        WriteModel(folder.Path(), Special{0, "Object2", "Object01.bmd", SharedSlot.value});
        WriteModel(folder.Path(), Special{0, "Object31", "Object01.bmd", SharedSlot.value});
        if (map == 30)
            WriteModel(folder.Path(),
                       Special{Client::Assets::SpearmanModel, "Monster", "Monster78.bmd", SharedSlot.value});
        WriteModel(folder.Path(), Special{1, map == 1 ? "Object2" : "Object31", "Object02.bmd", 0x41c});
        const auto decoded = Client::Data::Textures::LoadTextureSet(folder.Path(), SharedTexture);
        ASSERT_TRUE(decoded.HasValue());
        Client::Tests::FakeTextureStore store;
        Client::Assets::ModelTextureTable table;
        table.Bind(store);
        table.Slots().SetName(SharedSlot, "group104.tga");
        table.Slots().SetNext(TextureSlot{0x700});
        ASSERT_TRUE(table.Upload(decoded.Value().front()).HasValue());
        Client::Tests::RecordingSink sink;
        Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
        Client::Assets::WorldModels models;

        const auto failed = models.Load(folder.Path(), map, store, log, nullptr, nullptr, &table);

        ASSERT_FALSE(failed.HasValue());
        EXPECT_NE(failed.Error().find(map == 1 ? "0x61a13c" : "0x612e5a"), std::string::npos);
        ASSERT_EQ(models.MeshTextures(Client::Models::ModelId{0}).size(), 1u);
        EXPECT_EQ(models.MeshTextures(Client::Models::ModelId{0}).front(), SharedSlot);
        EXPECT_EQ(store.loadCalls, 1);
        EXPECT_EQ(table.Slots().Next(), TextureSlot{0x700});
        EXPECT_TRUE(table.Slots().ToLoad().empty());
        EXPECT_TRUE(table.Slots().NameOf(TextureSlot{0x41c}).empty());
        models.Release();
        models.Release();
        EXPECT_EQ(table.References(SharedSlot), 1);
        EXPECT_EQ(table.Slots().NameOf(SharedSlot), "group104.tga");
        EXPECT_TRUE(table.Metadata(SharedSlot.value).has_value());
        EXPECT_TRUE(store.Find(SharedSlot).has_value());
        EXPECT_TRUE(store.released.empty());

        // Load also releases an earlier failed load before retrying.
        ASSERT_FALSE(models.Load(folder.Path(), map, store, log, nullptr, nullptr, &table).HasValue());
        std::filesystem::remove(folder.Path() / "Object2/Object02.bmd");
        WriteModel(folder.Path(), Special{40, "Object2", "Object41.bmd", SharedSlot.value});
        const auto retried = models.Load(folder.Path(), 1, store, log, nullptr, nullptr, &table);
        ASSERT_TRUE(retried.HasValue()) << retried.Error();
        EXPECT_EQ(table.References(SharedSlot), 3);
        EXPECT_EQ(store.loadCalls, 1);
        models.Release();
        EXPECT_EQ(table.References(SharedSlot), 1);
        EXPECT_TRUE(table.Metadata(SharedSlot.value).has_value());
        EXPECT_TRUE(store.Find(SharedSlot).has_value());
        table.Release(SharedSlot);
        EXPECT_EQ(table.References(SharedSlot), 0);
        EXPECT_FALSE(store.Find(SharedSlot).has_value());
    }
}

TEST(WorldModelOwnershipTest, AFailedEmptySlotDoesNotOwnALaterUpload)
{
    constexpr TextureSlot slot{0x41c};
    Client::Tests::TemporaryFolder folder("world-empty-texture-slot");
    auto model = Client::Tests::SampleModel();
    model.meshes.front().textureName = "missing.tga";
    Client::Tests::WriteSceneFile(folder.Path(), "Object3/Object01.bmd",
                                  Client::Tests::PlainBmd(Client::Tests::BmdContent(model), 0x0a));
    Client::Tests::WriteSceneFile(folder.Path(), "Object3/later.OZT", Client::Tests::PlainOzt(8, 8));
    Client::Tests::FakeTextureStore store;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Client::Assets::ModelTextureTable table;
    Client::Assets::WorldModels models;
    ASSERT_TRUE(models.Load(folder.Path(), 2, store, log, nullptr, nullptr, &table).HasValue());
    EXPECT_TRUE(table.Slots().NameOf(slot).empty());
    EXPECT_EQ(table.Slots().Next(), TextureSlot{0x41d});
    EXPECT_EQ(models.MeshTextures(Client::Models::ModelId{0}).front(), Client::Render::UnboundModelTextureSlot);

    const Client::Data::Textures::TextureSetEntry entry{"Object3/later.tga", slot.value, {}};
    const auto loaded = Client::Data::Textures::LoadTextureSet(folder.Path(), std::span(&entry, 1));
    ASSERT_TRUE(loaded.HasValue());
    table.Slots().SetName(slot, "later.tga");
    ASSERT_TRUE(table.Upload(loaded.Value().front()).HasValue());

    models.Release();
    EXPECT_EQ(table.References(slot), 1);
    EXPECT_EQ(table.Slots().NameOf(slot), "later.tga");
    EXPECT_TRUE(store.Find(slot));
}

TEST(WorldModelOwnershipTest, FailedReplacementPreservesTheResidentAndDoesNotDrawItOnTheNewMesh)
{
    constexpr TextureSlot resident{0x41c};
    constexpr TextureSlot shared{0x41d};
    constexpr TextureSlot neighbor{0x41e};
    for (const bool failUpload : {false, true})
    {
        SCOPED_TRACE(failUpload);
        Client::Tests::TemporaryFolder folder("world-texture-replacement");
        const auto writeModel = [&](std::string_view file, std::span<const std::string_view> names)
        {
            Client::Data::Models::Model model = Client::Tests::SampleModel();
            const auto mesh = model.meshes.front();
            model.meshes.clear();
            for (const auto name : names)
            {
                auto copy = mesh;
                copy.textureName = name;
                model.meshes.push_back(copy);
            }
            Client::Tests::WriteSceneFile(folder.Path(), file,
                                          Client::Tests::PlainBmd(Client::Tests::BmdContent(model), 0x0a));
        };
        constexpr std::array<std::string_view, 3> first{"new.tga", "new.tga", "shared.tga.extra"};
        constexpr std::array<std::string_view, 2> second{"shared.tga.extra", "neighbor.tga"};
        writeModel("Object3/Object01.bmd", first);
        writeModel("Object3/Object02.bmd", second);
        Client::Tests::WriteSceneFile(folder.Path(), "Object3/shared.OZT", Client::Tests::PlainOzt(16, 16));
        Client::Tests::WriteSceneFile(folder.Path(), "Object3/neighbor.OZT", Client::Tests::PlainOzt(4, 4));
        const std::string_view residentName = failUpload ? "resident.tga" : "new.tga";
        Client::Tests::WriteSceneFile(folder.Path(),
                                      "Object2/" + std::string(residentName.substr(0, residentName.find('.'))) + ".OZT",
                                      Client::Tests::PlainOzt(8, 8));
        if (failUpload)
            Client::Tests::WriteSceneFile(folder.Path(), "Object3/new.OZT", Client::Tests::PlainOzt(32, 32));

        Client::Tests::FakeTextureStore store;
        Client::Assets::ModelTextureTable table;
        table.Bind(store);
        table.Slots().SetName(resident, residentName);
        const auto oldPath = "Object2\\" + std::string(residentName);
        const Client::Data::Textures::TextureSetEntry old{oldPath, resident.value, {}};
        const auto loaded = Client::Data::Textures::LoadTextureSet(folder.Path(), std::span(&old, 1));
        ASSERT_TRUE(loaded.HasValue());
        ASSERT_TRUE(table.Upload(loaded.Value().front()).HasValue());
        store.failNextLoad = failUpload;
        Client::Tests::RecordingSink sink;
        Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
        Client::Assets::WorldModels models;

        ASSERT_TRUE(models.Load(folder.Path(), 2, store, log, nullptr, nullptr, &table).HasValue());
        EXPECT_EQ(table.Slots().Next(), TextureSlot{0x41f});
        EXPECT_EQ(table.Slots().NameOf(resident), residentName);
        EXPECT_EQ(table.Slots().NameOf(shared), "shared.tga.extra");
        EXPECT_EQ(table.References(resident), 1);
        EXPECT_EQ(table.References(shared), 2);
        EXPECT_EQ(table.References(neighbor), 1);
        EXPECT_EQ(store.loaded, (std::vector<std::uint16_t>{0x41c, 0x41d, 0x41e}));
        ASSERT_TRUE(store.Find(resident).has_value());
        EXPECT_EQ(store.Find(resident)->texture.textureWidth, 8);

        Client::Render::ModelBatch batch;
        Client::Actors::Drawing::DrawnObject object;
        object.transform.scale = 1;
        Client::Actors::Drawing::DrawObject(batch, models, object, {});
        ASSERT_EQ(batch.Commands().size(), 2u);
        EXPECT_FALSE(batch.Commands().front().state.texture.has_value());
        EXPECT_EQ(batch.Commands().back().state.texture, shared);

        models.Release();
        models.Release();
        EXPECT_EQ(table.References(resident), 1);
        ASSERT_TRUE(table.Metadata(resident.value).has_value());
        EXPECT_TRUE(store.Find(resident).has_value());
        EXPECT_FALSE(store.Find(shared).has_value());
        EXPECT_FALSE(store.Find(neighbor).has_value());

        Client::Tests::WriteSceneFile(folder.Path(), "Object3/new.OZT", Client::Tests::PlainOzt(32, 32));
        ASSERT_TRUE(models.Load(folder.Path(), 2, store, log, nullptr, nullptr, &table).HasValue());
        EXPECT_EQ(models.MeshTextures(Client::Models::ModelId{0}).front(), resident);
        EXPECT_EQ(table.Slots().NameOf(resident), "new.tga");
        ASSERT_TRUE(store.Find(resident).has_value());
        EXPECT_EQ(store.Find(resident)->texture.textureWidth, 32);
        models.Release();
    }
}
} // namespace
