#include "Scenes/FaunaModels.h"
#include "Scenes/MonsterSounds.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "support/FakeTextureStore.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/SyntheticModels.h"
#include "support/TemporaryFolder.h"
#include "support/TextureFiles.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include "Scenes/WorldModels.h"
#include "support/GoldenDir.h"
#include <cstdint>

namespace
{
void WriteMonster(const std::filesystem::path& root, int number)
{
    auto model = Client::Tests::SampleModel();
    const auto action = model.actions.front();
    model.actions.assign(8, action);
    for (auto& bone : model.bones)
        if (!bone.dummy)
        {
            const auto frames = bone.actions.front();
            bone.actions.assign(8, frames);
        }
    model.meshes.front().textureName = "fauna.tga";
    Client::Tests::WriteSceneFile(root, "Monster/Monster" + std::to_string(number) + ".bmd",
                                  Client::Tests::PlainBmd(Client::Tests::BmdContent(model), 0x0a));
    Client::Tests::WriteSceneFile(root, "Monster/fauna.OZT", Client::Tests::PlainOzt(2, 2));
}
TEST(FaunaModelsTest, PrepareLoadsMonster32AndItsExactActionStoresOnce)
{
    Client::Tests::TemporaryFolder folder{"fauna-model32"};
    WriteMonster(folder.Path(), 32);
    Client::Tests::FakeTextureStore store;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    Client::Render::Models::ModelTextureSlots slots{Client::Render::TextureSlot{0x500}};
    Client::Scenes::FaunaModels models;
    Client::Scenes::FaunaModelLoadContext context{folder.Path(), store, log, slots};
    const auto result = models.Prepare(0x1f, context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_NE(models.Find(0x171), nullptr);
    const auto* skeleton = models.SkeletonOf(0x171);
    ASSERT_NE(skeleton, nullptr);
    ASSERT_EQ(skeleton->actions.size(), 8U);
    EXPECT_FLOAT_EQ(skeleton->actions[0].speed, .25f);
    EXPECT_FLOAT_EQ(skeleton->actions[1].speed, .8f);
    EXPECT_FLOAT_EQ(skeleton->actions[2].speed, .34f);
    EXPECT_FLOAT_EQ(skeleton->actions[3].speed, .5f);
    EXPECT_FLOAT_EQ(skeleton->actions[4].speed, .7f);
    EXPECT_FLOAT_EQ(skeleton->actions[5].speed, .5f);
    EXPECT_FLOAT_EQ(skeleton->actions[6].speed, .55f);
    EXPECT_FLOAT_EQ(skeleton->actions[7].speed, .8f);
    EXPECT_FALSE(skeleton->actions[1].repeats);
    EXPECT_FALSE(skeleton->actions[6].repeats);
    EXPECT_EQ(models.MeshTextures(0x171).front(), Client::Render::TextureSlot{0x500});
    EXPECT_EQ(store.loadCalls, 1);
    EXPECT_TRUE(models.Prepare(0x1f, context).HasValue());
    EXPECT_EQ(store.loadCalls, 1);
    EXPECT_EQ(models.SoundFiles().size(), 3U);
}
// 0x613127: con `sounds`, la carga del 0x1f (con mallas) escribe la tabla de sonidos del 0x171 como el alta de un
// monstruo (0x612c80); la segunda, que reutiliza el modelo, no la vuelve a escribir.
TEST(FaunaModelsTest, PrepareWritesTheSoundTableOfTheLoadedMonster)
{
    Client::Tests::TemporaryFolder folder{"fauna-model32-sounds"};
    WriteMonster(folder.Path(), 32);
    Client::Tests::FakeTextureStore store;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    Client::Render::Models::ModelTextureSlots slots{Client::Render::TextureSlot{0x500}};
    Client::Scenes::FaunaModels models;
    Client::Scenes::ModelSoundTable sounds;
    Client::Scenes::FaunaModelLoadContext context{folder.Path(), store, log, slots, nullptr, &sounds};
    ASSERT_TRUE(models.Prepare(0x1f, context).HasValue());
    Client::Scenes::ModelSoundTable expected;
    Client::Scenes::WriteMonsterSounds(0x1f, expected);
    EXPECT_EQ(sounds, expected);
    ASSERT_TRUE(Client::Scenes::WrittenSounds(sounds, 0x171).has_value());
    sounds.clear();
    ASSERT_TRUE(models.Prepare(0x1f, context).HasValue());
    EXPECT_TRUE(sounds.empty());
}

TEST(FaunaModelsTest, PrepareLoadsMonster34AndKeepsTheGenericActionSettings)
{
    Client::Tests::TemporaryFolder folder{"fauna-model34"};
    WriteMonster(folder.Path(), 34);
    Client::Tests::FakeTextureStore store;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    Client::Render::Models::ModelTextureSlots slots{Client::Render::TextureSlot{0x500}};
    Client::Scenes::FaunaModels models;
    Client::Scenes::FaunaModelLoadContext context{folder.Path(), store, log, slots};
    const auto result = models.Prepare(0x21, context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto* skeleton = models.SkeletonOf(0x173);
    ASSERT_NE(skeleton, nullptr);
    EXPECT_FLOAT_EQ(skeleton->actions[1].speed, .2f);
    EXPECT_FLOAT_EQ(skeleton->actions[3].speed, .33f);
    EXPECT_TRUE(skeleton->actions[1].repeats);
    EXPECT_FALSE(skeleton->actions[6].repeats);
    EXPECT_EQ(models.SoundFiles().size(), 2U);
}
TEST(FaunaModelsTest, MissingRequiredMonsterStopsBeforeTexturesOrFalsePreparation)
{
    Client::Tests::TemporaryFolder folder{"fauna-missing"};
    Client::Tests::FakeTextureStore store;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    Client::Render::Models::ModelTextureSlots slots{Client::Render::TextureSlot{0x500}};
    Client::Scenes::FaunaModels models;
    Client::Scenes::FaunaModelLoadContext context{folder.Path(), store, log, slots};
    const auto result = models.Prepare(0x1f, context);
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(models.Find(0x171), nullptr);
    EXPECT_EQ(store.loadCalls, 0);
    EXPECT_EQ(slots.Next().value, 0x500);
}
} // namespace

namespace
{
TEST(FaunaModelsTest, AMapTransitionSharesMonsterReferencesAndACachedMonsterSeesAReusedSlot)
{
    Client::Tests::TemporaryFolder folder{"fauna-map-textures"};
    WriteMonster(folder.Path(), 32);
    auto object = Client::Tests::SampleModel();
    object.meshes.front().textureName = "fauna.tga";
    Client::Tests::WriteSceneFile(folder.Path(), "Object2/Object41.bmd",
                                  Client::Tests::PlainBmd(Client::Tests::BmdContent(object), 0x0a));
    Client::Tests::WriteSceneFile(folder.Path(), "Object2/fauna.OZT", Client::Tests::PlainOzt(2, 2));
    Client::Tests::FakeTextureStore store;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    Client::Render::Models::ModelTextureSlots initial{{0x69}};
    Client::Scenes::FaunaModels fauna;
    auto& table = fauna.TextureTable();
    Client::Scenes::WorldModels world;
    ASSERT_TRUE(world.Load(folder.Path(), 1, store, log, &initial, nullptr, &table).HasValue());
    fauna.Bind(&world);
    Client::Scenes::FaunaModelLoadContext context{folder.Path(), store, log, *world.TextureSlots(), &table};
    ASSERT_TRUE(fauna.Prepare(0x1f, context).HasValue());
    const auto slot = fauna.MeshTextures(0x171).front();
    EXPECT_EQ(slot.value, 0x41c);
    EXPECT_EQ(table.References(slot), 2);
    world.Release();
    EXPECT_EQ(table.References(slot), 1);
    EXPECT_TRUE(store.Find(slot));
    ASSERT_TRUE(world.Load(folder.Path(), 1, store, log, &initial, nullptr, &table).HasValue());
    EXPECT_EQ(world.MeshTextures(40).front(), slot);
    // 60d550 busca [0,next): el nombre en next=41c queda fuera y se vuelve a subir.
    // 63bfcf pierde esa referencia vieja antes de sobrescribir, aun con BMD cacheado.
    EXPECT_EQ(table.References(slot), 1);
    EXPECT_EQ(store.loadCalls, 2);
    ASSERT_TRUE(fauna.TextureMetadataOf(slot.value));
    fauna.Release();
    EXPECT_EQ(table.References(slot), 0);
    EXPECT_FALSE(store.Find(slot));
    world.Release();
    EXPECT_FALSE(store.Find(slot));
}
} // namespace

namespace
{
using GoldenFaunaModels = Client::Tests::GoldenDataTest;
TEST_F(GoldenFaunaModels, BothRealMonsterResourcesPrepareTheirMeshesTexturesAndActionTables)
{
    Client::Tests::FakeTextureStore store;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    Client::Render::Models::ModelTextureSlots slots{{0x41c}};
    Client::Scenes::FaunaModels fauna;
    Client::Scenes::FaunaModelLoadContext context{m_dataRoot, store, log, slots};
    ASSERT_TRUE(fauna.Prepare(0x1f, context).HasValue());
    ASSERT_TRUE(fauna.Prepare(0x21, context).HasValue());
    for (const std::uint16_t model : {std::uint16_t{0x171}, std::uint16_t{0x173}})
    {
        ASSERT_NE(fauna.Find(model), nullptr);
        EXPECT_FALSE(fauna.Find(model)->meshes.empty());
        ASSERT_NE(fauna.SkeletonOf(model), nullptr);
        EXPECT_GE(fauna.SkeletonOf(model)->actions.size(), 7U);
        EXPECT_EQ(fauna.MeshTextures(model).size(), fauna.Find(model)->meshes.size());
    }
    EXPECT_GT(store.Size(), 0U);
}
} // namespace
