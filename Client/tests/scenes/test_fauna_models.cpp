#include "Scenes/FaunaModels.h"
#include "Scenes/MonsterKinds.h"
#include "Scenes/MonsterModelTable.h"
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
#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
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

// 0x612d90 y 0x612270 con el Data del laboratorio (CL9 hito 2, P-C176): cada número que piden los casos de 0x5141e0 y
// sus creadores de evento carga su Monster{n + 1}.bmd con mallas, texturas y las acciones que escriben sus tablas; el
// 0x31 (Monster50) no está y corta (P-C173). Cada NPC con caso carga, salvo BloodCastle01 (Q-C108, P-C240).
namespace
{
using GoldenMonsterModels = Client::Tests::GoldenDataTest;

struct GoldenLoad
{
    Client::Tests::FakeTextureStore store;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    Client::Render::Models::ModelTextureSlots slots{{0x41c}};
};

TEST_F(GoldenMonsterModels, EveryMonsterOfTheCasesLoadsWithItsActionTables)
{
    auto load = std::make_unique<GoldenLoad>();
    Client::Scenes::FaunaModels models;
    Client::Scenes::FaunaModelLoadContext context{m_dataRoot, load->store, load->log, load->slots};
    std::set<std::int32_t> numbers;
    const auto collect = [&numbers](const Client::Scenes::MonsterKind& kind)
    {
        for (const Client::Scenes::MonsterStep& step : kind.steps)
            if (step.kind == Client::Scenes::MonsterStepKind::LoadMonster)
            {
                numbers.insert(static_cast<std::int32_t>(step.value));
                // El 51443f suma rand() % 2.
                if (step.plusRandom)
                    numbers.insert(static_cast<std::int32_t>(step.value) + 1);
            }
    };
    for (const auto& kind : Client::Scenes::MainMonsterKinds())
        collect(kind);
    collect(Client::Scenes::DefaultMonsterKind());
    for (const auto& event : Client::Scenes::EventMonsterKinds())
        collect(event.kind);
    ASSERT_FALSE(numbers.empty());
    for (const std::int32_t number : numbers)
    {
        SCOPED_TRACE(number);
        const auto loaded = models.LoadMonster(number, 0, context);
        if (number == 0x31)
        {
            ASSERT_FALSE(loaded.HasValue());
            EXPECT_NE(loaded.Error().find("Monster50.bmd"), std::string::npos) << loaded.Error();
            continue;
        }
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        const auto model = static_cast<std::uint16_t>(Client::Scenes::FirstMonsterModel + number);
        ASSERT_NE(models.Find(model), nullptr);
        ASSERT_NE(models.SkeletonOf(model), nullptr);
        if (models.Find(model)->meshes.empty())
            continue;
        EXPECT_EQ(models.MeshTextures(model).size(), models.Find(model)->meshes.size());
        EXPECT_FALSE(models.SkeletonOf(model)->actions[6].repeats);
    }
    // 0x613137: Monster01 con los cinco .wav del toro, sus sonidos y el hueso de la cabeza en 0x14.
    EXPECT_EQ(models.SkeletonOf(0x152)->headBone, 0x14);
    EXPECT_EQ(models.SoundActions(0x152)[4], 4);
    // 0x612ea3: el 3 multiplica por 1,2 las acciones 0 a 5 (0,25 · 1,2).
    EXPECT_EQ(models.SkeletonOf(0x155)->actions[0].speed, 0.25f * 1.2f);
    EXPECT_EQ(models.SkeletonOf(0x155)->actions[6].speed, 0.55f);
    EXPECT_EQ(models.SkeletonOf(0x155)->headBone, 0x10);
    // 0x613f97: el 0x1b deja +0x88 en 1.
    EXPECT_EQ(models.ModelByte(0x16d, 0x88), std::optional<std::uint8_t>{std::uint8_t{1}});
    // 0x50c21c..0x50c281: el +0x376 0x4d copia el hueso 24 de Monster56 (0x189) y el 23 y el 14 de Monster57 (0x18a).
    EXPECT_GT(models.SkeletonOf(0x189)->bones.size(), 24u);
    EXPECT_GT(models.SkeletonOf(0x18a)->bones.size(), 23u);
}

TEST_F(GoldenMonsterModels, EveryNpcOfTheCasesLoadsButBloodCastle01)
{
    auto load = std::make_unique<GoldenLoad>();
    Client::Scenes::FaunaModels models;
    Client::Scenes::FaunaModelLoadContext context{m_dataRoot, load->store, load->log, load->slots};
    for (std::uint16_t model = 0x1cb; model <= 0x208; model = static_cast<std::uint16_t>(model + 1))
    {
        SCOPED_TRACE(model);
        const auto loaded = models.LoadNpc(model, context);
        if (model == 0x1f3)
        {
            ASSERT_FALSE(loaded.HasValue());
            EXPECT_NE(loaded.Error().find("Q-C108"), std::string::npos) << loaded.Error();
            continue;
        }
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        if (Client::Scenes::NpcModelCaseOf(model).ops.empty())
            continue;
        const auto* skeleton = models.SkeletonOf(model);
        ASSERT_NE(skeleton, nullptr);
        for (const auto& action : skeleton->actions)
            EXPECT_EQ(action.speed, Client::Scenes::NpcActionSpeed);
    }
    // 0x6122c4: Female01 y sus partes de 0x1da a 0x1e3, cada una con sus texturas.
    for (const int part : {0x1da, 0x1db, 0x1dc, 0x1dd, 0x1de, 0x1df, 0x1e2, 0x1e3})
    {
        const auto model = static_cast<std::uint16_t>(part);
        ASSERT_NE(models.Find(model), nullptr) << part;
        EXPECT_EQ(models.MeshTextures(model).size(), models.Find(model)->meshes.size());
    }
    // 0x612590: el herrero pide nBlackSmith.wav con un búfer y sin banderas.
    const auto sounds = models.SoundFiles();
    EXPECT_TRUE(
        std::any_of(sounds.begin(), sounds.end(), [](const Client::Scenes::FaunaResourceSound& sound)
                    { return sound.id == 0xa0 && sound.path == "Sound\\nBlackSmith.wav" && sound.buffers == 1; }));
}

// Q-C857: el Breeder (0x1f6) llama a 0x60d5f0 en su caso (0x612b47) y otra vez en la cola (0x612b8b), que encuentra
// cada nombre y le suma una referencia; el herrero (0x1ce) solo pasa por la cola.
TEST_F(GoldenMonsterModels, TheNpcTailAddsASecondReferenceToTheTexturesOfItsCase)
{
    auto load = std::make_unique<GoldenLoad>();
    Client::Scenes::FaunaModels models;
    models.TextureTable().Bind(load->store);
    auto context = models.LoadContextOf(m_dataRoot, load->log);
    ASSERT_TRUE(context.has_value());
    ASSERT_TRUE(models.LoadNpc(0x1f6, *context).HasValue());
    ASSERT_TRUE(models.LoadNpc(0x1ce, *context).HasValue());
    ASSERT_FALSE(models.MeshTextures(0x1f6).empty());
    for (const auto slot : models.MeshTextures(0x1f6))
        EXPECT_EQ(models.TextureTable().References(slot), 2) << slot.value;
    ASSERT_FALSE(models.MeshTextures(0x1ce).empty());
    for (const auto slot : models.MeshTextures(0x1ce))
        EXPECT_EQ(models.TextureTable().References(slot), 1) << slot.value;
}
} // namespace
