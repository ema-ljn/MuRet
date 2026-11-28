#include "Assets/WorldModels.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Models/ModelTextureSlots.h"
#include "Render/TextureSlot.h"
#include "Assets/GameModels.h"
#include "FrontEnd/LoginModels.h"
#include "Terrain/WorldTerrain.h"

#include "support/FakeTextureStore.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/SyntheticModels.h"
#include "support/TemporaryFolder.h"
#include "support/TextureFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <ios>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// Client::Assets::WorldModels with the repo's Data\: the Object56\ models 0x616ec0 loads for map 55 (the k of
// Object<k + 1>.bmd) and their textures, which follow the load's table from 0x41c (06 § El mundo del login).
namespace
{
using Client::Assets::WorldModels;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Render::TextureSlot;
using Client::Tests::FakeTextureStore;
using Client::Tests::RecordingSink;

constexpr std::uint8_t Login = 55;
constexpr std::uint8_t Lorencia = 0;
constexpr std::uint8_t Devias = 2;

using GoldenWorldModels = Client::Tests::GoldenDataTest;

// 0x616ec0: the 112 named requests of 0x619a21..0x619ea1 and the two specials. Going back to the Object<k+1> loop
// leaves even Tree01 and Beer03 without a model.
TEST_F(GoldenWorldModels, LorenciaLoadsTheNamedObjects)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    WorldModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, Lorencia, store, log, nullptr).HasValue());

    EXPECT_EQ(models.Count(), 114u);
    constexpr std::array<std::uint16_t, 10> Named{0x00, 0x37, 0x5a, 0x5b, 0x5c, 0x5d, 0x5f, 0x60, 0x85, 0x99};
    for (const std::uint16_t model : Named)
    {
        EXPECT_NE(models.Find(Client::Models::ModelId{model}), nullptr) << std::hex << model;
        EXPECT_NE(models.SkeletonOf(Client::Models::ModelId{model}), nullptr) << std::hex << model;
    }
    // Without an earlier GameModels: Bird/Fish plus 102 object textures. The three global ones the composite case
    // reuses get their own Object1 slots here.
    ASSERT_NE(models.TextureSlots(), nullptr);
    EXPECT_EQ(models.TextureSlots()->ToLoad().size(), 104u);
    EXPECT_EQ(models.TextureSlots()->Next(), TextureSlot{0x482});
    EXPECT_EQ(models.TextureSlots()->NameOf(TextureSlot{0x478}), "bottle.tga");
    EXPECT_EQ(models.TextureSlots()->NameOf(TextureSlot{0x480}), "pot3.tga");
    EXPECT_EQ(models.TextureSlots()->NameOf(TextureSlot{0x481}), "apple.jpg");
}

// The table's real consumer: the 109 types the 2,870 records of World1 ask for.
TEST_F(GoldenWorldModels, EveryLorenciaObjectTypeHasItsNamedModelAndSkeleton)
{
    const auto objects = Client::Terrain::LoadWorldObjects(m_dataRoot, Lorencia);
    ASSERT_TRUE(objects.HasValue()) << objects.Error();
    ASSERT_EQ(objects.Value().size(), 2870u);
    std::set<std::int16_t> types;
    for (const Client::Data::Terrain::TerrainObject& object : objects.Value())
        types.insert(object.type);
    ASSERT_EQ(types.size(), 109u);

    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    WorldModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, Lorencia, store, log, nullptr).HasValue());
    for (const std::int16_t type : types)
    {
        ASSERT_GE(type, 0);
        const auto model = static_cast<std::uint16_t>(type);
        ASSERT_NE(models.Find(Client::Models::ModelId{model}), nullptr) << model;
        EXPECT_NE(models.SkeletonOf(Client::Models::ModelId{model}), nullptr) << model;
    }
}

// Special requests 0x617356/0x617387, before the cursor goes back to 0x41c. The lor_slots.bin dump already has
// bottle.tga at 0x30f, pot3.tga at 0x2b3 and apple.jpg at 0x2b4: with that global table 99 new textures are asked
// for, not the 102 of an isolated table.
TEST_F(GoldenWorldModels, LorenciaAssignsTheBirdAndFishBeforeTheObjectTextures)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    Client::Assets::GameModels game;
    ASSERT_TRUE(game.Load(m_dataRoot, store, log).HasValue());
    ASSERT_EQ(game.TextureSlots().NameOf(TextureSlot{0x30f}), "bottle.tga");
    ASSERT_EQ(game.TextureSlots().NameOf(TextureSlot{0x2b3}), "pot3.tga");
    ASSERT_EQ(game.TextureSlots().NameOf(TextureSlot{0x2b4}), "apple.jpg");
    ASSERT_EQ(game.TextureSlots().NameOf(TextureSlot{0x69}), "doorc01.jpg");
    ASSERT_EQ(game.TextureSlots().NameOf(TextureSlot{0x6a}), "skR.jpg");
    ASSERT_EQ(game.MeshTextures(Client::Models::ModelId{0x10a}).front(), TextureSlot{0x6a});
    ASSERT_EQ(game.MeshTextures(Client::Models::ModelId{0x10b}).front(), TextureSlot{0x6a});
    ASSERT_TRUE(store.Find(TextureSlot{0x69}).has_value());
    ASSERT_TRUE(store.Find(TextureSlot{0x6a}).has_value());
    WorldModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, Lorencia, store, log, &game.TextureSlots()).HasValue());

    const auto bird = models.MeshTextures(Client::Models::ModelId{0xad});
    const auto fish = models.MeshTextures(Client::Models::ModelId{0xb2});
    ASSERT_EQ(bird.size(), 1u);
    ASSERT_EQ(fish.size(), 1u);
    EXPECT_EQ(bird.front(), TextureSlot{0x69});
    EXPECT_EQ(fish.front(), TextureSlot{0x6a});
    const auto tree = models.MeshTextures(Client::Models::ModelId{0});
    ASSERT_FALSE(tree.empty());
    EXPECT_EQ(tree.front(), TextureSlot{0x41c});
    ASSERT_NE(models.TextureSlots(), nullptr);
    EXPECT_EQ(models.TextureSlots()->ToLoad().size(), 101u);
    EXPECT_EQ(models.TextureSlots()->Next(), TextureSlot{0x47f});
    EXPECT_EQ(models.TextureSlots()->NameOf(TextureSlot{0x69}), "bird.tga");
    EXPECT_EQ(models.TextureSlots()->NameOf(TextureSlot{0x6a}), "fish.tga");
    constexpr std::array<TextureSlot, 3> Sampled{TextureSlot{0x69}, TextureSlot{0x6a}, TextureSlot{0x41c}};
    for (const TextureSlot slot : Sampled)
    {
        const auto texture = store.Find(slot);
        ASSERT_TRUE(texture.has_value());
        EXPECT_EQ(texture->sampling.filter, Client::Data::Textures::Filter::Nearest);
        EXPECT_EQ(texture->sampling.wrap, Client::Data::Textures::Wrap::Repeat);
    }
    models.Release();
    for (const TextureSlot slot : Sampled)
        EXPECT_FALSE(store.Find(slot).has_value());
    constexpr std::array<TextureSlot, 3> Shared{TextureSlot{0x30f}, TextureSlot{0x2b3}, TextureSlot{0x2b4}};
    for (const TextureSlot slot : Shared)
        EXPECT_TRUE(store.Find(slot).has_value());
}

// 0x61744d..0x6174d9: in Devias, the donkey (0x201, obj_donkey) and the flag (0x202, obj_flag) from Npc\, with their
// textures from 0x69 (0x60d530) and from Npc\, before the Object3\ objects, which go from 0x41c.
TEST_F(GoldenWorldModels, DeviasLoadsTheDonkeyAndTheFlagBeforeItsObjects)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    WorldModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, Devias, store, log, nullptr).HasValue());

    for (const std::uint16_t special : {std::uint16_t{0x201}, std::uint16_t{0x202}})
    {
        SCOPED_TRACE(special);
        ASSERT_NE(models.Find(Client::Models::ModelId{special}), nullptr);
        ASSERT_NE(models.SkeletonOf(Client::Models::ModelId{special}), nullptr);
        const auto textures = models.MeshTextures(Client::Models::ModelId{special});
        ASSERT_FALSE(textures.empty());
        for (const TextureSlot slot : textures)
        {
            EXPECT_GE(slot.value, 0x69);
            EXPECT_LT(slot.value, 0x41c);
        }
    }
    EXPECT_EQ(models.MeshTextures(Client::Models::ModelId{0x201}).front(), TextureSlot{0x69});
    EXPECT_NE(models.Find(Client::Models::ModelId{0}), nullptr);
    ASSERT_FALSE(models.MeshTextures(Client::Models::ModelId{0}).empty());
    EXPECT_EQ(models.MeshTextures(Client::Models::ModelId{0}).front(), TextureSlot{0x41c});
    EXPECT_EQ(models.Find(Client::Models::ModelId{0xad}), nullptr);
    models.Release();
    EXPECT_EQ(store.Size(), 0u);
}

// Selection map 54 keeps the generic loop: the 77 ObjectNN of Object55.
TEST_F(GoldenWorldModels, World55KeepsTheGenericObjects)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    WorldModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, 54, store, log, nullptr).HasValue());
    EXPECT_EQ(models.Count(), 77u);
    EXPECT_NE(models.Find(Client::Models::ModelId{0}), nullptr);
    EXPECT_NE(models.Find(Client::Models::ModelId{1}), nullptr);
    EXPECT_EQ(models.Find(Client::Models::ModelId{0xad}), nullptr);
    EXPECT_EQ(models.Find(Client::Models::ModelId{0xb2}), nullptr);
    EXPECT_EQ(models.Find(Client::Models::ModelId{Client::Assets::DragonModel}), nullptr);
}

using GoldenLorenciaModelTextureSlots = Client::Tests::MemoryDumpTest;

// The original's texture names: metadata only, without copying the dump into the repo.
TEST_F(GoldenLorenciaModelTextureSlots, TheWorldNamesMatchTheNativeTable)
{
    const auto dump = DumpFile(Client::Tests::LorenciaCapture, "lor_slots.bin");
    ASSERT_TRUE(dump.has_value());
    const std::optional<std::vector<char>> table = Client::Tests::ReadFileBytes(*dump);
    ASSERT_TRUE(table.has_value());
    constexpr std::size_t SlotBytes = 0x38;
    constexpr std::size_t NameBytes = 0x20;
    ASSERT_EQ(table->size(), Client::Render::OriginalTextureSlotCount * SlotBytes);
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    Client::Assets::GameModels game;
    ASSERT_TRUE(game.Load(m_dataRoot, store, log).HasValue());
    WorldModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, Lorencia, store, log, &game.TextureSlots()).HasValue());
    ASSERT_NE(models.TextureSlots(), nullptr);

    std::size_t compared = 0;
    for (std::uint16_t slot = 0x69; slot < 0x47f; ++slot)
    {
        if (slot > 0x6a && slot < 0x41c)
            continue;
        const std::string_view record(table->data() + (slot * SlotBytes), NameBytes);
        const std::string_view name = record.substr(0, record.find('\0'));
        EXPECT_EQ(models.TextureSlots()->NameOf(TextureSlot{slot}), name) << std::hex << slot;
        ++compared;
    }
    EXPECT_EQ(compared, 101u);
}

// Model k is Object<k + 1>.bmd: the lab's Data\ has 79 of the 160 (Object02 to Object91 with gaps), and on map 55
// the dragon too.
TEST_F(GoldenWorldModels, World56LoadsObjectKPlusOne)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    WorldModels models;
    const auto loaded = models.Load(m_dataRoot, Login, store, log, nullptr);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();

    EXPECT_EQ(models.Count(), 80u) << "79 objetos y el dragón";
    EXPECT_EQ(models.Find(Client::Models::ModelId{0}), nullptr) << "Object01.bmd no está";
    EXPECT_NE(models.Find(Client::Models::ModelId{1}), nullptr) << "Object02.bmd";
    EXPECT_NE(models.Find(Client::Models::ModelId{33}), nullptr) << "Object34.bmd, el dragón de piedra";
    EXPECT_NE(models.Find(Client::Models::ModelId{90}), nullptr) << "Object91.bmd, el remolino 90";
    EXPECT_EQ(models.Find(Client::Models::ModelId{91}), nullptr) << "Object92.bmd no está";
    ASSERT_NE(models.SkeletonOf(Client::Models::ModelId{83}), nullptr) << "las hojas, con su acción de 13 keys";
    EXPECT_EQ(models.SkeletonOf(Client::Models::ModelId{83})->actions.front().keyCount, 13);
}

// A missing one is skipped silently (Q-C23): type 82, which asks for the .obj, stays without a model.
TEST_F(GoldenWorldModels, AMissingModelIsSkipped)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    WorldModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, Login, store, log, nullptr).HasValue());
    EXPECT_EQ(models.Find(Client::Models::ModelId{82}), nullptr) << "Object83.bmd no está";
    EXPECT_EQ(models.SkeletonOf(Client::Models::ModelId{82}), nullptr);
    EXPECT_TRUE(models.MeshTextures(Client::Models::ModelId{82}).empty());
    for (std::size_t k = 0; k < sink.lines.size(); ++k)
    {
        if (sink.levels[k] == LogLevel::Warning)
        {
            EXPECT_EQ(sink.lines[k].find("Object83"), std::string::npos) << sink.lines[k];
        }
    }
}

// 0x616ec0 on map 55 (0x6188ae): the flying dragon is model 0xb4, from Object56\Dragon.bmd (dragon.bmd in the Data\,
// Q-C127), with 3 meshes, 93 bones and one action of 41 keys, and its textures from 0x60d530(0x69).
TEST_F(GoldenWorldModels, TheDragonIsModel0xb4)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    WorldModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, Login, store, log, nullptr).HasValue());
    const auto* dragon = models.Find(Client::Models::ModelId{Client::Assets::DragonModel});
    ASSERT_NE(dragon, nullptr);
    EXPECT_EQ(dragon->meshes.size(), 3u);
    EXPECT_EQ(dragon->bones.size(), 93u);
    ASSERT_NE(models.SkeletonOf(Client::Models::ModelId{Client::Assets::DragonModel}), nullptr);
    ASSERT_EQ(models.SkeletonOf(Client::Models::ModelId{Client::Assets::DragonModel})->actions.size(), 1u);
    EXPECT_EQ(models.SkeletonOf(Client::Models::ModelId{Client::Assets::DragonModel})->actions.front().keyCount, 41);
    const auto slots = models.MeshTextures(Client::Models::ModelId{Client::Assets::DragonModel});
    ASSERT_EQ(slots.size(), 3u);
    EXPECT_EQ(slots[0], TextureSlot{0x69});
    EXPECT_EQ(slots[1], TextureSlot{0x6a});
    EXPECT_EQ(slots[2], TextureSlot{0x6b});
    // No other map has it.
    WorldModels lorencia;
    ASSERT_TRUE(lorencia.Load(m_dataRoot, 0, store, log, nullptr).HasValue());
    EXPECT_EQ(lorencia.Find(Client::Models::ModelId{Client::Assets::DragonModel}), nullptr);
}

// 0x60d530(0x41c) and each model's textures in k order, with GL_NEAREST and repeating (0x60d5f0(k, "Object56\\",
// 0x2901, 0x2600, 1)). The 65 names of Object56\ are looked up in the table first: the dragon's three are at
// 0x69..0x6b, and in the original's (lor_slots.bin, those of 0x615700) castlegate02, cswalltop and castlegatehide
// are at 0x3e5..0x3e7, and fire02 and fire01 at 0x3ef and 0x3f0. The 57 new ones go from 0x41c to 0x454 and the logo
// lands at 0x455 (Q-C92).
TEST_F(GoldenWorldModels, TheLogoTexturesFollowTheWorld)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    const auto meshesNamed = [](std::initializer_list<const char*> names)
    {
        Client::Data::Models::Model model = Client::Tests::SampleModel();
        model.meshes.clear();
        for (const char* name : names)
        {
            model.meshes.push_back(Client::Tests::SampleMesh());
            model.meshes.back().textureName = name;
        }
        return model;
    };
    Client::Models::ModelTextureSlots table(TextureSlot{0x3e5});
    table.Assign(meshesNamed({"castlegate02.jpg", "cswalltop.JPG", "castlegatehide.jpg"}), "Skill\\", {});
    table.SetNext(TextureSlot{0x3ef});
    table.Assign(meshesNamed({"fire02.jpg", "fire01.jpg"}), "Skill\\", {});
    WorldModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, Login, store, log, &table).HasValue());

    ASSERT_NE(models.TextureSlots(), nullptr);
    EXPECT_EQ(models.TextureSlots()->Next(), Client::Scenes::LoginLogoFirstSlot);
    for (std::uint16_t slot = 0x41c; slot < 0x455; ++slot)
    {
        const auto texture = store.Find(TextureSlot{slot});
        ASSERT_TRUE(texture.has_value()) << std::hex << slot;
        EXPECT_EQ(texture->sampling.filter, Client::Data::Textures::Filter::Nearest) << std::hex << slot;
        EXPECT_EQ(texture->sampling.wrap, Client::Data::Textures::Wrap::Repeat) << std::hex << slot;
    }
    EXPECT_FALSE(store.Find(TextureSlot{0x455}).has_value());
    // Mesh 0 of Object02 is the first new texture.
    ASSERT_FALSE(models.MeshTextures(Client::Models::ModelId{1}).empty());
    EXPECT_EQ(models.MeshTextures(Client::Models::ModelId{1}).front(), TextureSlot{0x41c});

    models.Release();
    EXPECT_FALSE(store.Find(TextureSlot{0x41c}).has_value());
}

// 0x616c95..0x616ce9 and the sweep of 0x616e46 provide the five names before 0x61a115/0x61c7c0. No global slot is
// forced: the native cache reuses by name and leaves 57 new names, up to 0x455.
TEST_F(GoldenWorldModels, TheLoginReusesTheFiveNativeGlobalNamesBeforeLoadingTheLogo)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    Client::Assets::GameModels game;
    ASSERT_TRUE(game.Load(m_dataRoot, store, log).HasValue());
    struct Global
    {
        std::string_view name;
        std::string_view path;
    };
    const std::array<Global, 5> globals{{{"castlegate02.jpg", "Object31\\castlegate02.jpg"},
                                         {"cswalltop.JPG", "Object31\\cswalltop.jpg"},
                                         {"castlegatehide.jpg", "Object31\\castlegatehide.jpg"},
                                         {"fire02.jpg", "Skill\\fire02.jpg"},
                                         {"fire01.jpg", "Skill\\fire01.jpg"}}};
    std::array<TextureSlot, 5> cached{};
    for (std::size_t index = 0; index < globals.size(); ++index)
    {
        const auto entry = std::ranges::find(game.TextureSlots().ToLoad(), globals[index].path,
                                             &Client::Data::Textures::TextureSetEntry::path);
        ASSERT_NE(entry, game.TextureSlots().ToLoad().end());
        const TextureSlot slot{entry->slot};
        cached[index] = slot;
        EXPECT_EQ(game.TextureSlots().NameOf(slot), globals[index].name);
        const auto image = store.Find(slot);
        ASSERT_TRUE(image.has_value());
        EXPECT_EQ(image->texture.format, Client::Data::Textures::PixelFormat::Rgb);
        EXPECT_EQ(image->sampling.filter, Client::Data::Textures::Filter::Nearest);
        EXPECT_EQ(image->sampling.wrap, Client::Data::Textures::Wrap::Repeat);
    }
    // Fire01.bmd brings fire02.jpg on mesh 0 and fire01.jpg on mesh 1; Object90 reuses that same pair.
    ASSERT_EQ(game.MeshTextures(Client::Models::ModelId{0xc2}).size(), 2u);
    EXPECT_EQ(game.MeshTextures(Client::Models::ModelId{0xc2})[0], cached[3]);
    EXPECT_EQ(game.MeshTextures(Client::Models::ModelId{0xc2})[1], cached[4]);
    const auto prefix = store.loaded.size();
    WorldModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, Login, store, log, &game.TextureSlots()).HasValue());
    ASSERT_NE(models.TextureSlots(), nullptr);
    EXPECT_EQ(models.TextureSlots()->Next(), TextureSlot{0x455});
    for (const auto& entry : models.TextureSlots()->ToLoad())
        for (const auto& global : globals)
            EXPECT_NE(entry.path.substr(entry.path.find('\\') + 1), global.path.substr(global.path.find('\\') + 1));
    for (const std::uint16_t id : {std::uint16_t{8}, std::uint16_t{9}, std::uint16_t{21}, std::uint16_t{22}})
    {
        ASSERT_FALSE(models.MeshTextures(Client::Models::ModelId{id}).empty());
        EXPECT_EQ(models.MeshTextures(Client::Models::ModelId{id}).front(), cached[0]);
    }
    // Object24.bmd: column2.JPG on mesh 0; cswalltop.JPG on mesh 1.
    ASSERT_EQ(models.MeshTextures(Client::Models::ModelId{23}).size(), 2u);
    EXPECT_EQ(models.MeshTextures(Client::Models::ModelId{23})[1], cached[1]);
    const auto fire = models.MeshTextures(Client::Models::ModelId{89});
    ASSERT_EQ(fire.size(), 2u);
    EXPECT_EQ(fire[0], cached[3]);
    EXPECT_EQ(fire[1], cached[4]);
    std::vector<std::uint16_t> uploads{0x69, 0x6a, 0x6b};
    for (std::uint16_t slot = 0x41c; slot < 0x455; ++slot)
        uploads.push_back(slot);
    EXPECT_EQ(
        (std::vector<std::uint16_t>(store.loaded.begin() + static_cast<std::ptrdiff_t>(prefix), store.loaded.end())),
        uploads);
    for (std::size_t index = 0; index < sink.lines.size(); ++index)
        EXPECT_FALSE(sink.levels[index] == LogLevel::Warning && sink.lines[index].find("Object56") != std::string::npos)
            << sink.lines[index];
    models.Release();
    EXPECT_FALSE(store.Find(TextureSlot{0x41c}).has_value());
    EXPECT_EQ(models.TextureSlots(), nullptr);
    for (const auto slot : cached)
        EXPECT_TRUE(store.Find(slot).has_value());
}

// 0x619bc2 loads SteelWall after StoneWall, but 0x619eb7 assigns textures by ID. These own BMDs tell both orders
// apart without the original Data or a memory dump.
TEST(WorldModelsTest, LorenciaAssignsNamedTexturesByIdAfterLoadingTheSpecials)
{
    Client::Tests::TemporaryFolder folder{"lorencia-texture-id-order"};
    struct Request
    {
        std::string_view file;
        std::string_view textureStem;
    };
    constexpr std::array<Request, 4> Requests{{{"Bird01.bmd", "test-bird"},
                                               {"Fish01.bmd", "test-fish"},
                                               {"StoneWall01.bmd", "test-stone"},
                                               {"SteelWall01.bmd", "test-steel"}}};
    constexpr std::uint8_t ClearVersion = 0x0a;
    for (const Request& request : Requests)
    {
        Client::Data::Models::Model model = Client::Tests::SampleModel();
        const std::string texture(request.textureStem);
        model.meshes.front().textureName = texture + ".tga";
        const std::filesystem::path objects("Object1");
        Client::Tests::WriteSceneFile(folder.Path(), objects / request.file,
                                      Client::Tests::PlainBmd(Client::Tests::BmdContent(model), ClearVersion));
        Client::Tests::WriteSceneFile(folder.Path(), objects / (texture + ".OZT"), Client::Tests::PlainOzt(2, 2));
    }
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    WorldModels models;
    ASSERT_TRUE(models.Load(folder.Path(), Lorencia, store, log, nullptr).HasValue());
    const auto expectSlot = [&](std::uint16_t model, TextureSlot expected)
    {
        const auto slots = models.MeshTextures(Client::Models::ModelId{model});
        ASSERT_EQ(slots.size(), 1u) << model;
        EXPECT_EQ(slots.front(), expected) << model;
    };
    expectSlot(0xad, TextureSlot{0x69});
    expectSlot(0xb2, TextureSlot{0x6a});
    expectSlot(0x41, TextureSlot{0x41c});
    expectSlot(0x45, TextureSlot{0x41d});
    ASSERT_NE(models.TextureSlots(), nullptr);
    EXPECT_EQ(models.TextureSlots()->Next(), TextureSlot{0x41e});
    EXPECT_EQ(models.TextureSlots()->ToLoad().size(), 4u);
}

// The truncated BMDs make each request observable through its immediate warning. This does not record how Load and
// Assign interleave when the models are valid.
TEST(WorldModelsTest, LorenciaRequestsTheSpecialsAndNamedModelsInCallOrder)
{
    Client::Tests::TemporaryFolder folder{"lorencia-model-call-order"};
    const std::filesystem::path objects = folder.Path() / "Object1";
    ASSERT_TRUE(std::filesystem::create_directory(objects));
    constexpr std::array<std::string_view, 7> Files{"Bird01.bmd",      "Fish01.bmd",   "StoneWall01.bmd",
                                                    "SteelWall01.bmd", "Cannon01.bmd", "Bridge01.bmd",
                                                    "Object01.bmd"};
    for (const std::string_view name : Files)
    {
        std::ofstream file(objects / name, std::ios::binary);
        file << "BMD";
        file.close();
        ASSERT_TRUE(file.good()) << name;
    }
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    WorldModels models;
    ASSERT_TRUE(models.Load(folder.Path(), Lorencia, store, log, nullptr).HasValue());

    constexpr std::string_view UnreadableModel = "un objeto del mundo no se puede leer y no se dibuja: ";
    std::vector<std::string> requests;
    for (std::size_t index = 0; index < sink.lines.size(); ++index)
    {
        const std::size_t message = sink.lines[index].find(UnreadableModel);
        if (sink.levels[index] == LogLevel::Warning && message != std::string::npos)
            requests.push_back(sink.lines[index].substr(message + UnreadableModel.size()));
    }
    const std::vector<std::string> Expected{"Data\\Object1\\Bird01.bmd",      "Data\\Object1\\Fish01.bmd",
                                            "Data\\Object1\\StoneWall01.bmd", "Data\\Object1\\SteelWall01.bmd",
                                            "Data\\Object1\\Cannon01.bmd",    "Data\\Object1\\Bridge01.bmd"};
    EXPECT_EQ(requests, Expected);
}

// Without any model (no Object56 folder, or an empty one) the login goes without objects, and says so in the log.
TEST(WorldModelsTest, WithoutAnyModelThereIsAWarning)
{
    Client::Tests::TemporaryFolder empty{"world-models-empty"};
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    WorldModels models;
    ASSERT_TRUE(models.Load(empty.Path(), Login, store, log, nullptr).HasValue());
    EXPECT_EQ(models.Count(), 0u);
    bool warned = false;
    for (std::size_t k = 0; k < sink.lines.size(); ++k)
    {
        warned = warned || (sink.levels[k] == LogLevel::Warning && sink.lines[k].find("Object56") != std::string::npos);
    }
    EXPECT_TRUE(warned);
}
} // namespace
