#include "Scenes/GameModels.h"
#include "Models/TextureMetadata.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/ModelTextureCalls.h"
#include "Data/Models/ModelFiles.h"
#include "Data/Models/Model.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "Render/ModelBatch.h"
#include "Models/Skeleton.h"
#include "Models/ModelTextureSlots.h"
#include "Data/Textures/TextureSet.h"
#include "Render/TextureSlot.h"
#include "Scenes/ArrowEffects.h"
#include "Scenes/CharacterSelectScene.h"
#include "Scenes/CreatePreview.h"
#include "Tables/GameData.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/ModelTextureTable.h"
#include "Scenes/SceneContext.h"
#include "Scenes/TerrainTiles.h"
#include "Scenes/TextureSets.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldState.h"
#include "World/ModelEffects.h"
#include "World/SphereFrustum.h"

#include "support/FakeTextureStore.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/RecordingSink.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ios>
#include <memory>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// Client::Scenes::GameModels contra el Data\ del laboratorio (MU_CLIENT_GOLDEN_DIR): los modelos
// del personaje y de los ítems que lee la carga, con su esqueleto y sus texturas (06 § El modelo en
// el binario).
namespace
{
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Data::Models::ItemModelFiles;
using Client::Data::Models::ModelTextureSampling;
using Client::Data::Models::PlayerFixedTextures;
using Client::Data::Models::PlayerModelFiles;
using Client::Data::Models::SkillModelFiles;
using Client::Render::TextureSlot;
using Client::Scenes::ClothTextureSet;
using Client::Scenes::GameModels;
using Client::Scenes::GlowTextureSet;
using Client::Tests::FakeTextureStore;
using Client::Tests::ReadFileBytes;
using Client::Tests::RecordingSink;

// 615f77..61619f,616370/38d,616953..616e46: replay literal del orden nativo sobre los BMD cargados.
using NativeSkillLoadTest = Client::Tests::GoldenDataTest;
TEST_F(NativeSkillLoadTest, TheLoadedSkillTexturesFollowEveryReachedNativeCallInOrder)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels game;
    ASSERT_TRUE(game.Load(m_dataRoot, store, log).HasValue());
    ASSERT_NE(game.Find(0xc2), nullptr) << "615d4c: Skill/Fire01.bmd abastece fire01.jpg antes del login";
    using Client::Data::Textures::Wrap;
    struct Call
    {
        std::uint16_t model;
        std::string_view folder;
        Wrap wrap = Wrap::Repeat;
        bool bmdAvailable = true;
    };
    Client::Render::Models::ModelTextureSlots native(TextureSlot{0x138});
    std::map<std::uint16_t, std::vector<TextureSlot>> bindings;
    const auto query = [&](const Call& call)
    {
        // 0x61613a y 0x61619f consultan los modelos antes de sus cargas BMD.
        if (!call.bmdAvailable)
            return;
        if (const auto* model = game.Find(call.model); model != nullptr)
            bindings[call.model] =
                native.Assign(*model, call.folder, {Client::Data::Textures::Filter::Nearest, call.wrap});
    };
    for (const auto& call : Client::Data::Models::PlayerTextureCalls())
        query({call.model, call.folder});
    native.SetNext(TextureSlot{0x25a});
    for (const auto& call : Client::Data::Models::ItemTextureCalls())
        query({call.model, call.folder, Wrap::Repeat, call.model != 0xdb});
    ASSERT_EQ(native.Next(), TextureSlot{0x371});
    for (const Call& call : std::to_array<Call>({{0x14a, "Skill\\"},
                                                 {0x14b, "Skill\\"},
                                                 {0x14c, "Skill\\"},
                                                 {0x14d, "Effect\\"},
                                                 {0x109, "Skill\\", Wrap::Repeat, false},
                                                 {0xf9, "Skill\\"},
                                                 {0x10f, "Skill\\", Wrap::Repeat, false}}))
        query(call);
    native.SetNext(TextureSlot{0x69});
    for (const std::uint16_t model :
         {std::uint16_t{0x10d}, std::uint16_t{0x10a}, std::uint16_t{0x10d}, std::uint16_t{0x10b}})
        query({model, "Monster\\"});
    native.SetNext(TextureSlot{0x138});
    query({0x11c, "Item\\", Wrap::Clamp});
    native.SetNext(TextureSlot{0x3b8});
    const auto skill = std::to_array<Call>({{0x145, "Skill\\"},
                                            {0x146, "Skill\\"},
                                            {0x147, "Skill\\"},
                                            {0xbf, "Skill\\"},
                                            {0xc0, "Skill\\"},
                                            {0xc5, "Skill\\"},
                                            {0x110, "Skill\\"},
                                            {0xfb, "Skill\\"},
                                            {0xfc, "Skill\\"},
                                            {0xfd, "Skill\\"},
                                            {0xff, "Skill\\"},
                                            {0x101, "Skill\\"},
                                            {0x102, "Skill\\"},
                                            {0x103, "Skill\\"},
                                            {0x104, "Skill\\"},
                                            {0x107, "Skill\\"},
                                            {0x108, "Skill\\"},
                                            {0x111, "Skill\\"},
                                            {0x112, "Skill\\"},
                                            {0x113, "Skill\\", Wrap::Clamp},
                                            {0x114, "Skill\\"},
                                            {0x115, "Skill\\"},
                                            {0x116, "Skill\\"},
                                            {0x118, "Skill\\"},
                                            {0x119, "Skill\\"},
                                            {0x11a, "Skill\\", Wrap::Clamp},
                                            {0x11b, "Skill\\", Wrap::Clamp},
                                            {0x129, "Skill\\"},
                                            {0x12a, "Skill\\"},
                                            {0x12b, "Npc\\"},
                                            {0x12c, "Skill\\"},
                                            {0x12d, "Skill\\"},
                                            {0x12e, "Skill\\"},
                                            {0x12f, "Object31\\"},
                                            {0x130, "Object31\\"},
                                            {0x131, "Monster\\"},
                                            {0x132, "Monster\\"},
                                            {0x133, "Monster\\"},
                                            {0x134, "Monster\\"},
                                            {0x135, "Skill\\"},
                                            {0x13c, "Monster\\"},
                                            {0x13d, "Skill\\"},
                                            {0x13e, "Skill\\"},
                                            {0x137, "Effect\\"},
                                            {0x140, "Item\\"}});
    for (const Call& call : skill)
        query(call);
    for (std::uint16_t model = 0xbe; model < 0x151; ++model)
        query({model, "Skill\\"});
    ASSERT_EQ(native.Next(), game.TextureSlots().Next());
    const auto expected = native.ToLoad(), actual = game.TextureSlots().ToLoad();
    ASSERT_EQ(expected.size(), actual.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        SCOPED_TRACE(i);
        ASSERT_EQ(actual[i].path, expected[i].path);
        ASSERT_EQ(actual[i].slot, expected[i].slot);
        ASSERT_EQ(actual[i].sampling.filter, expected[i].sampling.filter);
        ASSERT_EQ(actual[i].sampling.wrap, expected[i].sampling.wrap);
    }
    for (const auto& [model, expectedSlots] : bindings)
    {
        const auto actualSlots = game.MeshTextures(model);
        ASSERT_EQ(actualSlots.size(), expectedSlots.size()) << model;
        EXPECT_TRUE(std::equal(actualSlots.begin(), actualSlots.end(), expectedSlots.begin())) << model;
    }
}

// Los tres giftbox de 0x60ff50 y ManyFlag de 0x6166e8 no están en el laboratorio.
constexpr std::size_t MissingInTheLab = 4;

class GoldenGameModels : public Client::Tests::GoldenDataTest
{
};

TEST_F(GoldenGameModels, TheEffectTextureRegistryPublishesTheFire05Slot)
{
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, textures, log).HasValue());

    // 0x61ddac fija el slot. Las dimensiones y componentes salen de Fire05.OZJ (fixture JPEG 6b).
    constexpr TextureSlot fire{0x5dc};
    const auto slot = models.TextureSlotOf(fire.value);
    ASSERT_TRUE(slot.has_value());
    EXPECT_EQ(*slot, fire);
    const auto metadata = models.TextureMetadataOf(fire.value);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->size.x, 256.0f);
    EXPECT_EQ(metadata->size.y, 64.0f);
    EXPECT_EQ(metadata->components, 3);
    const auto uploaded = textures.Find(fire);
    ASSERT_TRUE(uploaded.has_value());
    EXPECT_EQ(uploaded->sampling.filter, Client::Data::Textures::Filter::Linear);
    EXPECT_EQ(uploaded->sampling.wrap, Client::Data::Textures::Wrap::Clamp);
}

// 0x616095 carga la flecha común. 0x52d68c lee su cuenta de mallas en el segundo pase de Kalima.
TEST_F(GoldenGameModels, TheBowArrowDrawsInTheSecondPassOfKalima)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;
    const auto loaded = models.Load(m_dataRoot, store, log);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();

    auto world = std::make_unique<Client::Scenes::WorldState>();
    Client::Crt::CrtRandom random(1);
    const Client::World::ModelEffectArguments arguments{.type = 0xdb, .position = {100, 100, 100}, .light = {1, 1, 1}};
    const auto created = Client::Scenes::CreateTheArrowEffect(arguments, *world, random);
    ASSERT_TRUE(created.HasValue()) << created.Error();
    const auto& arrow = world->Map().effects.Effects().front();
    ASSERT_TRUE(arrow.alive);
    ASSERT_EQ(arrow.objectState.blendMesh, 1);
    Client::Render::ModelBatch batch;
    const auto drawn = Client::Scenes::DrawGameModelEffects(batch, models, world->Map().effects.Effects(),
                                                            Client::World::SphereFrustum{}, nullptr, {}, 0, 1);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_FALSE(batch.Vertices().empty());

    constexpr std::array<std::uint16_t, 14> projectiles{0xdb, 0xdc, 0xdd, 0xde, 0xdf,  0xe0,  0xe1,
                                                        0xe2, 0xe3, 0xf7, 0xf9, 0x10e, 0x111, 0x11c};
    for (const auto type : projectiles)
    {
        SCOPED_TRACE(type);
        const auto* model = models.Find(type);
        ASSERT_NE(model, nullptr);
        EXPECT_FALSE(model->meshes.empty());
        EXPECT_EQ(models.MeshTextures(type).size(), model->meshes.size());
    }
}

// Todas las filas de los cargadores de arranque, salvo los cuatro archivos ausentes.
TEST_F(GoldenGameModels, LoadsEveryModelOfThePlayerLoader)
{
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;

    const auto loaded = models.Load(m_dataRoot, textures, log);

    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    EXPECT_EQ(SkillModelFiles().size(), 106u);
    EXPECT_EQ(models.Count(),
              PlayerModelFiles().size() + ItemModelFiles().size() + SkillModelFiles().size() - MissingInTheLab);
    ASSERT_NE(models.SkeletonOf(0x20a), nullptr);
    EXPECT_EQ(models.SkeletonOf(0x20a)->headBone, 20);
    for (const auto& file : PlayerModelFiles())
    {
        const auto* model = models.Find(file.model);
        ASSERT_NE(model, nullptr) << file.name << " " << file.number;
        EXPECT_EQ(models.MeshTextures(file.model).size(), model->meshes.size()) << file.name << " " << file.number;
    }
    EXPECT_EQ(models.Find(0x129), nullptr);
    for (const auto& file : SkillModelFiles())
    {
        SCOPED_TRACE(file.model);
        if (file.model == 0x129)
            continue;
        const auto* model = models.Find(file.model);
        ASSERT_NE(model, nullptr) << file.name << " " << file.number;
        const auto slots = models.MeshTextures(file.model);
        ASSERT_EQ(slots.size(), model->meshes.size());
        for (const TextureSlot slot : slots)
        {
            EXPECT_TRUE(textures.Find(slot).has_value()) << slot.value;
        }
    }
}

TEST_F(GoldenGameModels, StartupActionsKeepTheOverridesWrittenAfterTheBmdLoad)
{
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, textures, log).HasValue());

    const auto* gate = models.SkeletonOf(0xc5);
    ASSERT_NE(gate, nullptr);
    ASSERT_FALSE(gate->actions.empty());
    EXPECT_FALSE(gate->actions.front().lockPositions); // 0x61606f.
    EXPECT_EQ(gate->actions.front().speed, 0.15f);     // 0x61607f.
    const auto* projectile = models.SkeletonOf(0xf1);
    ASSERT_NE(projectile, nullptr);
    ASSERT_FALSE(projectile->actions.empty());
    EXPECT_EQ(projectile->actions.front().speed, 0.5f); // 0x616276.
}

TEST_F(GoldenGameModels, StartupPublishesTheFourFixedSkillTextureSlots)
{
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, textures, log).HasValue());

    // 0x616043 y 0x616e6f..0x616ea9 suben estos slots fuera del lazo de mallas.
    for (const TextureSlot slot : {TextureSlot{0x608}, TextureSlot{0x609}, TextureSlot{0x60a}, TextureSlot{0x6f7}})
    {
        SCOPED_TRACE(slot.value);
        EXPECT_EQ(models.TextureSlotOf(slot.value), slot);
        const auto uploaded = textures.Find(slot);
        ASSERT_TRUE(uploaded.has_value());
        const bool darkSpirit = slot.value == 0x6f7;
        EXPECT_EQ(uploaded->sampling.filter,
                  darkSpirit ? Client::Data::Textures::Filter::Linear : Client::Data::Textures::Filter::Nearest);
        EXPECT_EQ(uploaded->sampling.wrap,
                  darkSpirit ? Client::Data::Textures::Wrap::Repeat : Client::Data::Textures::Wrap::Clamp);
    }
}

TEST_F(GoldenGameModels, ReloadKeepsTheSameModelTextureOrder)
{
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, textures, log).HasValue());
    const auto expectedRequests = models.TextureSlots().ToLoad().size();
    const auto expectedNext = models.TextureSlots().Next();
    ASSERT_FALSE(models.MeshTextures(0xdb).empty());
    const auto expectedArrow = models.MeshTextures(0xdb).front();

    ASSERT_TRUE(models.Load(m_dataRoot, textures, log).HasValue());
    EXPECT_EQ(models.TextureSlots().ToLoad().size(), expectedRequests);
    EXPECT_EQ(models.TextureSlots().Next(), expectedNext);
    ASSERT_FALSE(models.MeshTextures(0xdb).empty());
    EXPECT_EQ(models.MeshTextures(0xdb).front(), expectedArrow);
    for (const auto& entry : models.TextureSlots().ToLoad())
        EXPECT_FALSE(entry.path.starts_with("Item\\Bow\\")) << entry.path;
}

TEST_F(GoldenGameModels, SelectionReplacesAndReleasesTheTemporaryStartupTexture)
{
    constexpr TextureSlot temporary{0x716};
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, textures, log).HasValue());
    const auto startup = textures.Find(temporary);
    ASSERT_TRUE(startup.has_value());
    EXPECT_EQ(models.TextureSlotOf(temporary.value), temporary);
    FakeTextureStore unrelated;
    EXPECT_FALSE(models.TakeStartupSelectionTexture(unrelated).has_value());
    EXPECT_EQ(startup->texture.textureWidth, 64);
    EXPECT_EQ(startup->texture.textureHeight, 64);
    EXPECT_EQ(startup->sampling.filter, Client::Data::Textures::Filter::Linear);
    EXPECT_EQ(startup->sampling.wrap, Client::Data::Textures::Wrap::Clamp);

    Client::Scenes::SelectionModels selection;
    ASSERT_TRUE(
        selection.Load(m_dataRoot, textures, log, &models.TextureSlots(), models.TakeStartupSelectionTexture(textures))
            .HasValue());
    const auto preview = textures.Find(temporary);
    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(preview->texture.textureWidth, 256);
    EXPECT_EQ(preview->texture.textureHeight, 256);
    EXPECT_EQ(preview->sampling.filter, Client::Data::Textures::Filter::Nearest);
    EXPECT_EQ(preview->sampling.wrap, Client::Data::Textures::Wrap::Repeat);
    EXPECT_EQ(selection.TextureSlotOf(temporary.value), temporary);
    const auto selected = selection.TextureMetadataOf(temporary.value);
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected->size.x, 256.0f);
    EXPECT_FALSE(models.TextureSlotOf(temporary.value).has_value());

    selection.Release();
    selection.Release();
    EXPECT_FALSE(textures.Find(temporary).has_value());
    EXPECT_FALSE(selection.TextureSlotOf(temporary.value).has_value());
    EXPECT_FALSE(models.TextureSlotOf(temporary.value).has_value());
    EXPECT_FALSE(models.TextureMetadataOf(temporary.value).has_value());
    EXPECT_EQ(std::ranges::count(textures.released, temporary.value), 1);

    // 0x61784b: Tarkan vuelve a publicar el slot desde la carpeta del mapa.
    Client::Scenes::ModelTextureTable mapTextures;
    Client::Scenes::TerrainTiles terrain(textures);
    terrain.BindTextureTable(mapTextures);
    std::vector<std::string> skipped;
    const auto loaded = terrain.Load(m_dataRoot, 8, skipped);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    const auto mapped = mapTextures.Metadata(temporary.value);
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->size.x, 64.0f);
    EXPECT_EQ(mapped->size.y, 64.0f);
    EXPECT_FALSE(models.TextureMetadataOf(temporary.value).has_value());
    terrain.Release();
    EXPECT_FALSE(textures.Find(temporary).has_value());
    EXPECT_FALSE(mapTextures.Metadata(temporary.value).has_value());
}

TEST_F(GoldenGameModels, SelectionReleasesTheTemporaryStartupSlotWithoutAReplacementModel)
{
    constexpr TextureSlot temporary{0x716};
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, textures, log).HasValue());

    const Client::Tests::TemporaryFolder noPreviewModels("selection-without-preview-models");
    Client::Scenes::SelectionModels selection;
    ASSERT_TRUE(selection
                    .Load(noPreviewModels.Path(), textures, log, &models.TextureSlots(),
                          models.TakeStartupSelectionTexture(textures))
                    .HasValue());
    EXPECT_EQ(selection.TextureSlotOf(temporary.value), temporary);
    const auto inherited = selection.TextureMetadataOf(temporary.value);
    ASSERT_TRUE(inherited.has_value());
    EXPECT_EQ(inherited->size.x, 64.0f);
    EXPECT_FALSE(models.TextureMetadataOf(temporary.value).has_value());
    selection.Release();
    EXPECT_FALSE(textures.Find(temporary).has_value());
    EXPECT_FALSE(selection.TextureMetadataOf(temporary.value).has_value());
    EXPECT_EQ(std::ranges::count(textures.released, temporary.value), 1);
}

TEST_F(GoldenGameModels, AnEnteredOrFailedSelectionReleasesTheStartupTexture)
{
    constexpr TextureSlot temporary{0x716};
    const Client::Tests::TemporaryFolder missingUi("selection-without-ui");
    const Client::Scenes::GameData data;
    for (const bool success : {true, false})
    {
        SCOPED_TRACE(success);
        FakeTextureStore textures;
        RecordingSink sink;
        Log log(sink, LogLevel::Debug);
        GameModels models;
        ASSERT_TRUE(models.Load(m_dataRoot, textures, log).HasValue());
        EXPECT_EQ(models.TextureSlotOf(temporary.value), temporary);

        const Client::Scenes::SceneContext context{success ? m_dataRoot : missingUi.Path(), textures, log};
        auto scene = std::make_unique<Client::Scenes::CharacterSelectScene>(context, data, models, nullptr,
                                                                            &models.TextureSlots());
        const auto entered = scene->Enter();
        ASSERT_EQ(entered.HasValue(), success);
        scene->Leave();
        scene->Leave();

        EXPECT_FALSE(models.TextureMetadataOf(temporary.value).has_value());
        EXPECT_FALSE(textures.Find(temporary).has_value());
        EXPECT_EQ(std::ranges::count(textures.released, temporary.value), 1);
    }
}

// 0x616304/0x61631a cargan Gate01/02 de Object12; 0x61634e..0x616370 asigna solo Gate02 desde
// Monster, en 0x69, antes de volver a 0x3b8. El barrido616e46 también asigna Gate01: los dos BMD reales
// usan doorc01.jpg y reutilizan el mismo recurso sin pedirlo desde Skill.
TEST_F(GoldenGameModels, TheWorldGatesAreGlobalAndTheSecondAssignsItsInitialTexture)
{
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;

    ASSERT_TRUE(models.Load(m_dataRoot, textures, log).HasValue());

    for (const std::uint16_t gate : {std::uint16_t{0x10c}, std::uint16_t{0x10d}})
    {
        SCOPED_TRACE(gate);
        ASSERT_NE(models.Find(gate), nullptr);
        EXPECT_EQ(models.Find(gate)->meshes.size(), 1u);
        ASSERT_NE(models.SkeletonOf(gate), nullptr);
        EXPECT_EQ(models.SkeletonOf(gate)->bones.size(), 1u);
    }
    ASSERT_EQ(models.MeshTextures(0x10c).size(), 1u);
    EXPECT_EQ(models.MeshTextures(0x10c).front(), TextureSlot{0x69});
    ASSERT_EQ(models.MeshTextures(0x10d).size(), 1u);
    EXPECT_EQ(models.MeshTextures(0x10d).front(), TextureSlot{0x69});
    EXPECT_EQ(models.TextureSlots().NameOf(TextureSlot{0x69}), "doorc01.jpg");
    const auto texture = textures.Find(TextureSlot{0x69});
    ASSERT_TRUE(texture.has_value());
    EXPECT_EQ(texture->sampling.filter, Client::Data::Textures::Filter::Nearest);
    EXPECT_EQ(texture->sampling.wrap, Client::Data::Textures::Wrap::Repeat);
    EXPECT_EQ(models.TextureSlotOf(0x69), TextureSlot{0x69});
    EXPECT_FALSE(models.TextureSlotOf(0x10069).has_value());
    const auto metadata = models.TextureMetadataOf(0x69);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->size.x, static_cast<float>(texture->texture.textureWidth));
    EXPECT_EQ(metadata->size.y, static_cast<float>(texture->texture.textureHeight));
    EXPECT_EQ(metadata->components, 3);
}

// La piel y el pelo de 0x60f7d0 quedan en sus nueve slots fijos, y las texturas del personaje
// empiezan en 0x138 y las de los ítems en 0x25a.
TEST_F(GoldenGameModels, TheTexturesGoWhereTheLoadersPutThem)
{
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;

    ASSERT_TRUE(models.Load(m_dataRoot, textures, log).HasValue());

    const std::set<std::uint16_t> loaded(textures.loaded.begin(), textures.loaded.end());
    for (const int slot : {0x12e, 0x12f, 0x130, 0x131, 0x132, 0x133, 0x134, 0x136, 0x137})
    {
        EXPECT_TRUE(loaded.contains(static_cast<std::uint16_t>(slot))) << std::hex << slot;
    }
    EXPECT_FALSE(loaded.contains(0x12d));
    EXPECT_FALSE(loaded.contains(0x135));
    EXPECT_TRUE(loaded.contains(0x138));
    EXPECT_TRUE(loaded.contains(0x25a));
    // Y las de los pases de brillo (0x61ccb0, avance 0), que el dibujo de las partes pide.
    for (const int slot : {0x5c0, 0x5fc, 0x620})
    {
        EXPECT_TRUE(loaded.contains(static_cast<std::uint16_t>(slot))) << std::hex << slot;
    }
    // LaceArrow (0x6164bb), AirForce (0x616b63), seamanFX (0x616c0b) y Skull (0x616c26) piden CLAMP.
    for (const auto& entry : models.TextureSlots().ToLoad())
    {
        const bool laceArrow = (entry.slot == 0x138 || entry.slot == 0x139) &&
                               (entry.path == "Item\\nfb_R.jpg" || entry.path == "Item\\nfb.jpg");
        const bool skillClamps = entry.slot == 0x3d8 || (entry.slot >= 0x3df && entry.slot <= 0x3e1);
        EXPECT_EQ(entry.sampling.filter, ModelTextureSampling.filter) << entry.path;
        EXPECT_EQ(entry.sampling.wrap,
                  laceArrow || skillClamps ? Client::Data::Textures::Wrap::Clamp : ModelTextureSampling.wrap)
            << entry.path;
    }
    for (const TextureSlot slot : {TextureSlot{0x138}, TextureSlot{0x139}})
    {
        const auto uploaded = textures.Find(slot);
        ASSERT_TRUE(uploaded.has_value());
        EXPECT_EQ(uploaded->sampling.wrap, Client::Data::Textures::Wrap::Clamp);
    }
}

// 0x6166fe y 0x616c5c: Skill\MuSign.bmd, el 0x12a de la capa de los GM, con sus texturas de Skill\. Van
// al final de la carga, en slots que ninguna textura fija vuelve a subir.
TEST_F(GoldenGameModels, TheGameMasterSignIsLoadedWithItsOwnTextures)
{
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, textures, log).HasValue());

    const auto* sign = models.Find(0x12a);
    ASSERT_NE(sign, nullptr);
    ASSERT_NE(models.SkeletonOf(0x12a), nullptr);
    EXPECT_FALSE(models.SkeletonOf(0x12a)->actions.empty());
    const auto slots = models.MeshTextures(0x12a);
    ASSERT_EQ(slots.size(), sign->meshes.size());
    for (const TextureSlot slot : slots)
    {
        SCOPED_TRACE(slot.value);
        EXPECT_EQ(std::ranges::count(textures.loaded, slot.value), 1);
        EXPECT_TRUE(textures.Find(slot).has_value());
    }
}

// 0x61641d y 0x616442: Skill\groundStone.bmd y groundStone2.bmd, las piedras 0x118 y 0x119 del pisotón del caballo,
// con su esqueleto (la acción 0 que repite, como la deja 0x4f13d0 y la vuelve a escribir 0x615700) y las texturas de
// Skill\ del lazo 0x616e20.
TEST_F(GoldenGameModels, TheGroundStonesAreLoadedWithTheirSkillTextures)
{
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, textures, log).HasValue());

    for (const std::uint16_t stone : {std::uint16_t{0x118}, std::uint16_t{0x119}})
    {
        SCOPED_TRACE(stone);
        const auto* model = models.Find(stone);
        ASSERT_NE(model, nullptr);
        EXPECT_FALSE(model->meshes.empty());
        const auto* skeleton = models.SkeletonOf(stone);
        ASSERT_NE(skeleton, nullptr);
        ASSERT_FALSE(skeleton->actions.empty());
        EXPECT_TRUE(skeleton->actions.front().repeats);
        const auto slots = models.MeshTextures(stone);
        ASSERT_EQ(slots.size(), model->meshes.size());
        for (const TextureSlot slot : slots)
        {
            SCOPED_TRACE(slot.value);
            EXPECT_TRUE(textures.Find(slot).has_value());
        }
    }
}

// 0x6165bc..0x616688 cargan Skill\EarthQuake01..08 en 0xfb..0x102 (el 04 en 0xfe y el 06 en 0x100, al final) y
// 0x616a11..0x616a9b sus texturas de Skill\: las grietas del terremoto del caballo se dibujan con su malla.
TEST_F(GoldenGameModels, TheEarthquakeCracksAreLoadedWithTheirOwnTextures)
{
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, textures, log).HasValue());

    for (std::uint16_t crack = 0xfb; crack <= 0x102; ++crack)
    {
        SCOPED_TRACE(crack);
        const auto* model = models.Find(crack);
        ASSERT_NE(model, nullptr);
        ASSERT_FALSE(model->meshes.empty());
        const auto slots = models.MeshTextures(crack);
        ASSERT_EQ(slots.size(), model->meshes.size());
        for (const TextureSlot slot : slots)
            EXPECT_TRUE(textures.Find(slot).has_value()) << slot.value;
    }
}

// La carga común de GameModels conserva clouds.jpg para las escenas; LoginScene no la sube.
TEST_F(GoldenGameModels, TheCloudTextureIsLoadedWithTheGameModels)
{
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, textures, log).HasValue());
    const auto cloud = textures.Find(TextureSlot{0x622});
    ASSERT_TRUE(cloud.has_value());
    EXPECT_EQ(cloud->sampling.filter, Client::Data::Textures::Filter::Linear);
    EXPECT_EQ(cloud->sampling.wrap, Client::Data::Textures::Wrap::Clamp);
    EXPECT_TRUE(models.TextureSize(TextureSlot{0x622}).has_value());
}
TEST_F(GoldenGameModels, JointAndPortalResourcesPublishTheirRealPhysicalTextures)
{
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;
    const auto loaded = models.Load(m_dataRoot, textures, log);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    for (const std::uint32_t selector :
         {std::uint32_t{0x5c5}, std::uint32_t{0x5d5}, std::uint32_t{0x5fc}, std::uint32_t{0x617}, std::uint32_t{0x62b}})
    {
        SCOPED_TRACE(selector);
        const auto metadata = models.TextureMetadataOf(selector);
        ASSERT_TRUE(metadata.has_value());
        const auto handle = models.TextureSlotOf(selector);
        ASSERT_TRUE(handle.has_value());
        EXPECT_EQ(handle->value, selector);
        const auto physical = textures.Find(*handle);
        ASSERT_TRUE(physical.has_value());
        EXPECT_EQ(physical->sampling.filter, Client::Data::Textures::Filter::Linear);
        EXPECT_EQ(physical->sampling.wrap, Client::Data::Textures::Wrap::Clamp);
        EXPECT_GT(metadata->size.x, 0.0f);
        EXPECT_GT(metadata->size.y, 0.0f);
        EXPECT_EQ(metadata->size.x, static_cast<float>(physical->texture.textureWidth));
        EXPECT_EQ(metadata->size.y, static_cast<float>(physical->texture.textureHeight));
        EXPECT_EQ(metadata->components, 3);
    }
}

// Lo que piden los dos cargadores está en el laboratorio: el original se cierra con una textura de
// modelo que falta (Q-C23), así que con estas carpetas ninguna falta. Con la carpeta del .bmd
// faltaban 14, entre ellas las de las mascotas.
TEST_F(GoldenGameModels, EveryTextureTheLoadersAskForIsThere)
{
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;

    ASSERT_TRUE(models.Load(m_dataRoot, textures, log).HasValue());

    EXPECT_EQ(textures.loaded.size(),
              models.TextureSlots().ToLoad().size() + PlayerFixedTextures().size() + GlowTextureSet.size() +
                  ClothTextureSet.size() + Client::Scenes::EffectTextureSet.size() +
                  Client::Scenes::StartupSelectionTextureSet.size() + Client::Scenes::DamageTextureSet.size() +
                  Client::Scenes::HudTextureSet.size() + Client::Scenes::CrywolfTextureSet.size() +
                  Client::Scenes::FaunaJointTextureSet.size() + Client::Scenes::GameTailTextureSet.size());
    for (const std::string& line : sink.lines)
    {
        EXPECT_EQ(line.find("una textura de un modelo"), std::string::npos) << line;
    }
}

// El caballo oscuro (0xbf, Skill\DarkHorse.bmd): sus 13 mallas, su esqueleto y sus texturas, 11 desde
// 0x3bc, después de las cuatro de 0x145, 0x146 y 0x147, y dos que ya estaban: dkhead2 y dkheadwing,
// que subió 0x1c16 (DarkHorseHorn, también de Skill\) en 0x26b y 0x26c.
TEST_F(GoldenGameModels, TheDarkHorseGetsItsTexturesAfterTheThreeBeforeIt)
{
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;

    ASSERT_TRUE(models.Load(m_dataRoot, textures, log).HasValue());

    const auto* horse = models.Find(0xbf);
    ASSERT_NE(horse, nullptr);
    EXPECT_EQ(horse->meshes.size(), 13u);
    ASSERT_NE(models.SkeletonOf(0xbf), nullptr);
    std::vector<std::uint16_t> slots;
    for (const TextureSlot slot : models.MeshTextures(0xbf))
    {
        slots.push_back(slot.value);
    }
    EXPECT_EQ(slots, (std::vector<std::uint16_t>{0x3bc, 0x3bd, 0x26b, 0x3be, 0x3bf, 0x3c0, 0x3c1, 0x3c2, 0x3c3, 0x3c4,
                                                 0x3c5, 0x26c, 0x3c6}));
    EXPECT_EQ(models.TextureSlots().NameOf(TextureSlot{0x26b}), "dkhead2.JPG");
}

// La mascota que se dibuja (0x1c13, Helper02 de Player\) con las texturas de Item\, todas subidas.
TEST_F(GoldenGameModels, ThePetDrawsWithItsTextures)
{
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;

    ASSERT_TRUE(models.Load(m_dataRoot, textures, log).HasValue());

    const std::set<std::uint16_t> loaded(textures.loaded.begin(), textures.loaded.end());
    ASSERT_FALSE(models.MeshTextures(0x1c13).empty());
    for (const TextureSlot slot : models.MeshTextures(0x1c13))
    {
        EXPECT_TRUE(loaded.contains(slot.value)) << std::hex << slot.value;
    }
}

// Las monturas de las mascotas de 0x615700, con su esqueleto y sus texturas subidas: el Uniria (Rider01, 0xc6) con
// las de Helper03 (unicon.jpg y unicon01.tga de Item\, que el lazo 0x616e20 encuentra por el nombre), el Dinorant
// (Rider02, 0x110, 0x6169fb) después de HellGate (0x6169e0) y los tres Fenrir desde 0x371, con fenril02.jpg en común.
TEST_F(GoldenGameModels, ThePetMountsLoadWithTheirTextures)
{
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;

    ASSERT_TRUE(models.Load(m_dataRoot, textures, log).HasValue());

    const auto slotsOf = [&models](std::uint16_t model)
    {
        std::vector<std::uint16_t> slots;
        for (const TextureSlot slot : models.MeshTextures(model))
        {
            slots.push_back(slot.value);
        }
        return slots;
    };
    EXPECT_EQ(slotsOf(0xc6), (std::vector<std::uint16_t>{0x2cb, 0x2cc}));
    EXPECT_EQ(models.TextureSlots().NameOf(TextureSlot{0x2cb}), "unicon.jpg");
    EXPECT_EQ(slotsOf(0xc5), (std::vector<std::uint16_t>{0x3cb, 0x3cc}));
    EXPECT_EQ(slotsOf(0x110), (std::vector<std::uint16_t>{0x3cd, 0x3ce}));
    EXPECT_EQ(models.TextureSlots().NameOf(TextureSlot{0x3cd}), "RDgon.jpg");
    EXPECT_EQ(slotsOf(0x14a), (std::vector<std::uint16_t>{0x371, 0x372}));
    EXPECT_EQ(slotsOf(0x14b), (std::vector<std::uint16_t>{0x371, 0x373}));
    EXPECT_EQ(slotsOf(0x14c), (std::vector<std::uint16_t>{0x371, 0x374}));
    EXPECT_EQ(models.TextureSlots().NameOf(TextureSlot{0x371}), "fenril02.jpg");
    const std::set<std::uint16_t> loaded(textures.loaded.begin(), textures.loaded.end());
    for (const std::uint16_t model : std::array<std::uint16_t, 5>{0xc6, 0x110, 0x14a, 0x14b, 0x14c})
    {
        const auto* skeleton = models.SkeletonOf(model);
        ASSERT_NE(skeleton, nullptr) << std::hex << model;
        EXPECT_FALSE(skeleton->actions.empty()) << std::hex << model;
        for (const TextureSlot slot : models.MeshTextures(model))
        {
            EXPECT_TRUE(loaded.contains(slot.value)) << std::hex << model << " " << slot.value;
        }
    }
    EXPECT_EQ(models.SkeletonOf(0xc6)->actions.size(), 4u);
    EXPECT_EQ(models.SkeletonOf(0x110)->actions.size(), 8u);
    EXPECT_EQ(models.SkeletonOf(0x14a)->actions.size(), 6u);
}

// 0x61ca30 lee Logo05 y los cinco NewFace de Data\Logo (en la Data del repo), y les pone 0,3 a las
// acciones 0 y 1 de los NewFace; sin la tabla de la carga, sus texturas empiezan en 0x6f8.
TEST_F(GoldenGameModels, TheSelectionModelsLoadWithTheirSpeeds)
{
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    Client::Scenes::SelectionModels selection;

    ASSERT_TRUE(selection.Load(m_dataRoot, textures, log, nullptr).HasValue());

    EXPECT_NE(selection.Find(Client::Scenes::SelectionLogoModel), nullptr);
    for (std::uint16_t model = 0xa8; model <= 0xac; ++model)
    {
        const auto* skeleton = selection.SkeletonOf(model);
        ASSERT_NE(skeleton, nullptr) << std::hex << model;
        ASSERT_GE(skeleton->actions.size(), 2u) << std::hex << model;
        EXPECT_FLOAT_EQ(skeleton->actions[0].speed, 0.3f) << std::hex << model;
        EXPECT_FLOAT_EQ(skeleton->actions[1].speed, 0.3f) << std::hex << model;
        EXPECT_FALSE(selection.MeshTextures(model).empty()) << std::hex << model;
    }
    ASSERT_NE(selection.TextureSlots(), nullptr);
    EXPECT_EQ(selection.MeshTextures(Client::Scenes::SelectionLogoModel).front(), TextureSlot{0x6f8});
    // Las del 0xa4 con GL_LINEAR (0x2601) y las de los NewFace con GL_NEAREST (0x2600), repitiendo.
    const auto logo = textures.Find(TextureSlot{0x6f8});
    ASSERT_TRUE(logo.has_value());
    EXPECT_EQ(logo->sampling.filter, Client::Data::Textures::Filter::Linear);
    EXPECT_EQ(logo->sampling.wrap, Client::Data::Textures::Wrap::Repeat);
    const auto face = textures.Find(selection.MeshTextures(0xa8).front());
    ASSERT_TRUE(face.has_value());
    if (face->texture.format == Client::Data::Textures::PixelFormat::Rgb)
    {
        EXPECT_EQ(face->sampling.filter, Client::Data::Textures::Filter::Nearest);
    }
    EXPECT_EQ(face->sampling.wrap, Client::Data::Textures::Wrap::Repeat);
}

class ModelTextureSlotsGoldenTest : public Client::Tests::MemoryDumpTest
{
};

TEST_F(ModelTextureSlotsGoldenTest, CommonTextureDimensionsAndComponentsMatchLorencia)
{
    const auto dump = DumpFile(Client::Tests::LorenciaCapture, "lor_slots.bin");
    ASSERT_TRUE(dump.has_value());
    const auto table = ReadFileBytes(*dump);
    ASSERT_TRUE(table.has_value());
    constexpr std::size_t SlotBytes = 0x38;
    constexpr std::size_t DimensionsOffset = 0x20;
    constexpr std::size_t ComponentsOffset = 0x28;
    ASSERT_EQ(table->size(), Client::Render::OriginalTextureSlotCount * SlotBytes);
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, textures, log).HasValue());

    // Entradas fijas nuevas de 0x61ccb0. La selección reutiliza y libera 0x716 (0x61cc65..0x61cc7a).
    constexpr std::array<std::uint16_t, 32> slots{
        0xf6,  0x102, 0x103, 0x5c1, 0x5dc, 0x5ee, 0x5ff, 0x600, 0x61b, 0x61d, 0x624, 0x628, 0x629, 0x62a, 0x62d, 0x62e,
        0x62f, 0x630, 0x631, 0x632, 0x633, 0x634, 0x699, 0x69a, 0x6ae, 0x6b2, 0x6b3, 0x6b4, 0x6b5, 0x6e8, 0x6e9, 0x6ef};
    for (const auto slot : slots)
    {
        SCOPED_TRACE(slot);
        const auto uploaded = textures.Find(TextureSlot{slot});
        ASSERT_TRUE(uploaded.has_value());
        const char* record = table->data() + (slot * SlotBytes);
        float width = 0.0f;
        float height = 0.0f;
        std::memcpy(&width, record + DimensionsOffset, sizeof width);
        std::memcpy(&height, record + DimensionsOffset + sizeof width, sizeof height);
        const auto components = static_cast<std::uint8_t>(record[ComponentsOffset]);
        EXPECT_EQ(static_cast<float>(uploaded->texture.textureWidth), width);
        EXPECT_EQ(static_cast<float>(uploaded->texture.textureHeight), height);
        EXPECT_EQ(uploaded->texture.format == Client::Data::Textures::PixelFormat::Rgba ? 4u : 3u, components);
    }
}

// Oráculo nativo independiente: metadatos residuales del slot 0x622 en la captura de Lorencia.
// Su nombre ya está vacío; no demuestra residencia, visibilidad ni movimiento en el login.
TEST_F(ModelTextureSlotsGoldenTest, TheCloudTextureMatchesTheNativeSlot)
{
    const auto dump = DumpFile(Client::Tests::LorenciaCapture, "lor_slots.bin");
    ASSERT_TRUE(dump.has_value());
    const auto table = ReadFileBytes(*dump);
    ASSERT_TRUE(table.has_value());
    constexpr std::size_t SlotBytes = 0x38;
    constexpr std::size_t NameBytes = 0x20;
    constexpr TextureSlot CloudSlot{0x622};
    ASSERT_EQ(table->size(), Client::Render::OriginalTextureSlotCount * SlotBytes);
    const char* record = table->data() + (CloudSlot.value * SlotBytes);
    const std::string_view name(record, NameBytes);
    EXPECT_TRUE(name.substr(0, name.find('\0')).empty());
    float width = 0.0f;
    float height = 0.0f;
    std::uint32_t components = 0;
    std::memcpy(&width, record + 0x20, sizeof width);
    std::memcpy(&height, record + 0x24, sizeof height);
    std::memcpy(&components, record + 0x28, sizeof components);
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, textures, log).HasValue());
    const auto uploaded = textures.Find(CloudSlot);
    ASSERT_TRUE(uploaded.has_value());
    EXPECT_EQ(static_cast<float>(uploaded->texture.textureWidth), width);
    EXPECT_EQ(static_cast<float>(uploaded->texture.textureHeight), height);
    EXPECT_EQ(uploaded->texture.format == Client::Data::Textures::PixelFormat::Rgba ? 4u : 3u, components);
}

// Los nombres de 0x60f7d0, 0x611850 y 0x615700, incluidos los slots vacíos y los de LaceArrow (Q-C89).
// El volcado de 0x7d3a198 en Lorencia tiene registros de 0x38 bytes, con el nombre en +0.
TEST_F(ModelTextureSlotsGoldenTest, StartupMatchesTheDump)
{
    const auto dump = DumpFile(Client::Tests::LorenciaCapture, "lor_slots.bin");
    ASSERT_TRUE(dump.has_value());
    const std::optional<std::vector<char>> table = ReadFileBytes(*dump);
    ASSERT_TRUE(table.has_value());
    constexpr std::size_t SlotBytes = 0x38;
    constexpr std::size_t NameBytes = 0x20;
    constexpr std::uint16_t WorldObjectStart = 0x41c;
    ASSERT_EQ(table->size(), Client::Render::OriginalTextureSlotCount * SlotBytes);
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;

    ASSERT_TRUE(models.Load(m_dataRoot, textures, log).HasValue());

    ASSERT_FALSE(models.MeshTextures(0x110).empty());
    EXPECT_EQ(models.MeshTextures(0x110).back(), TextureSlot{0x3ce});
    EXPECT_EQ(models.TextureSlots().Next(), TextureSlot{0x41b});
    for (std::uint16_t slot = 0x12e; slot < WorldObjectStart; ++slot)
    {
        const std::string_view record(table->data() + (slot * SlotBytes), NameBytes);
        const std::string_view name = record.substr(0, record.find('\0'));
        EXPECT_EQ(models.TextureSlots().NameOf(TextureSlot{slot}), name) << std::hex << slot;
    }
}

// El cuervo (0xc0, Skill\DarkSpirit.bmd): sus mallas con las texturas de la quinta llamada de 0x615700
// (0x3c7 a 0x3ca) y la malla 3 con dksbody.tga, que encuentra en 0x26f, donde la subió el 0x1c17
// (06 § El cuervo).
TEST_F(ModelTextureSlotsGoldenTest, TheRavenFindsItsBodyInTheBillsSlot)
{
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, textures, log).HasValue());

    const auto meshes = models.MeshTextures(0xc0);
    ASSERT_EQ(meshes.size(), 5u);
    EXPECT_EQ(meshes[2], TextureSlot{0x3c9});
    EXPECT_EQ(meshes[3], TextureSlot{0x26f});
    EXPECT_EQ(models.TextureSlots().NameOf(TextureSlot{0x26f}), "dksbody.tga");
    EXPECT_EQ(models.TextureSlots().NameOf(TextureSlot{0x3c9}), "dkonebody.jpg");
    const auto* skeleton = models.SkeletonOf(0xc0);
    ASSERT_NE(skeleton, nullptr);
    EXPECT_EQ(skeleton->bones.size(), 77u);
    EXPECT_EQ(skeleton->actions.size(), 4u);
}
// 0x61ca30 con 0x60d530(0x6f8): Logo05 y los cinco NewFace, siguiendo la tabla de la carga, dejan
// sus 31 texturas de 0x6f8 a 0x716 (Q-C91). En el volcado de Lorencia, 0x61cc50 ya soltó los slots y
// borró los nombres, pero cada uno guarda el tamaño del buffer (los floats de +0x20 y +0x24) y los
// componentes (+0x28: 3 o 4) de lo que tuvo: los de lo que sube MuRet en cada slot.
TEST_F(ModelTextureSlotsGoldenTest, TheSelectionTexturesEndAt0x716)
{
    const auto dump = DumpFile(Client::Tests::LorenciaCapture, "lor_slots.bin");
    ASSERT_TRUE(dump.has_value());
    const std::optional<std::vector<char>> table = ReadFileBytes(*dump);
    ASSERT_TRUE(table.has_value());
    constexpr std::size_t SlotBytes = 0x38;
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, textures, log).HasValue());
    Client::Scenes::SelectionModels selection;

    ASSERT_TRUE(selection.Load(m_dataRoot, textures, log, &models.TextureSlots()).HasValue());

    ASSERT_NE(selection.TextureSlots(), nullptr);
    EXPECT_EQ(selection.TextureSlots()->Next(), TextureSlot{0x717});
    for (std::uint16_t slot = 0x6f8; slot < 0x717; ++slot)
    {
        const char* record = table->data() + (slot * SlotBytes);
        float width = 0.0f;
        float height = 0.0f;
        std::uint32_t components = 0;
        std::memcpy(&width, record + 0x20, sizeof width);
        std::memcpy(&height, record + 0x24, sizeof height);
        std::memcpy(&components, record + 0x28, sizeof components);
        // Los nombres de GL de 0x6f8 a 0x716 van seguidos: los de 0x61ca30, que sube estas 31 después
        // de sus cuatro del 2D.
        std::uint32_t glName = 0;
        std::uint32_t firstGlName = 0;
        std::memcpy(&glName, record + 0x2c, sizeof glName);
        std::memcpy(&firstGlName, table->data() + (0x6f8 * SlotBytes) + 0x2c, sizeof firstGlName);
        EXPECT_EQ(glName, firstGlName + (slot - 0x6f8u)) << std::hex << slot;
        const auto uploaded = textures.Find(TextureSlot{slot});
        ASSERT_TRUE(uploaded.has_value()) << std::hex << slot;
        EXPECT_EQ(static_cast<float>(uploaded->texture.textureWidth), width) << std::hex << slot;
        EXPECT_EQ(static_cast<float>(uploaded->texture.textureHeight), height) << std::hex << slot;
        EXPECT_EQ(uploaded->texture.format == Client::Data::Textures::PixelFormat::Rgba ? 4u : 3u, components)
            << std::hex << slot;
    }
}

// 0x61ccb0 sube las texturas de los sprites y las partículas del login (0x63b870 con GL_LINEAR y
// GL_CLAMP) y nadie las suelta: Effect\flare01.jpg en 0x5ac, Effect\Shiny02.jpg en 0x5fd, Effect\Spark03.jpg
// en 0x5c6 (32 × 32) y Effect\smoke01.jpg en 0x5f2. El sprite y la partícula miden su tamaño por la escala
// (0x549290, 0x548550), así que GameModels lo da.
TEST_F(GoldenGameModels, TheSpriteTexturesStayWithTheirSize)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    Client::Scenes::GameModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, store, log).HasValue());
    for (const std::uint16_t slot :
         {std::uint16_t{0x5ac}, std::uint16_t{0x5fd}, std::uint16_t{0x5c6}, std::uint16_t{0x5f2}})
    {
        const auto texture = store.Find(TextureSlot{slot});
        ASSERT_TRUE(texture.has_value()) << std::hex << slot;
        EXPECT_EQ(texture->sampling.filter, Client::Data::Textures::Filter::Linear) << std::hex << slot;
        EXPECT_EQ(texture->sampling.wrap, Client::Data::Textures::Wrap::Clamp) << std::hex << slot;
        const auto size = models.TextureSize(TextureSlot{slot});
        ASSERT_TRUE(size.has_value()) << std::hex << slot;
        EXPECT_EQ(size->x, static_cast<float>(texture->texture.textureWidth));
        EXPECT_EQ(size->y, static_cast<float>(texture->texture.textureHeight));
    }
    // Spark03.OZJ mide 32 × 32 (otra chispa, como Effect\spark.jpg, mide 112).
    const auto spark = models.TextureSize(TextureSlot{0x5c6});
    ASSERT_TRUE(spark.has_value());
    EXPECT_EQ(spark->x, 32.0f);
    EXPECT_EQ(spark->y, 32.0f);
    EXPECT_FALSE(models.TextureSize(TextureSlot{0x123}).has_value());
}
} // namespace

// 0x6161ea/0x616333/0x616349/0x6163c2/0x6163d8/0x61679e..0x616854: efectos alcanzados por el asedio y Crywolf.
TEST_F(GoldenGameModels, EveryReachedSiegeAndCrywolfEffectHasItsRealModelSkeletonAndUploadedMeshTextures)
{
    using Client::Data::Models::ModelFile;
    const std::array<ModelFile, 17> expected{{{0xe5, "Skill\\", "BigStone", 1},
                                              {0xe6, "Skill\\", "BigStone", 2},
                                              {0x10a, "Object12\\", "StoneCoffin", 1},
                                              {0x10b, "Object12\\", "StoneCoffin", 2},
                                              {0x114, "Skill\\", "m_Waves", -1},
                                              {0x115, "Skill\\", "m_Piercing", -1},
                                              {0x12b, "Skill\\", "FlyBigStone1", -1},
                                              {0x12c, "Skill\\", "FlyBigStone2", -1},
                                              {0x12d, "Skill\\", "FlySmallStone1", -1},
                                              {0x12e, "Skill\\", "FlySmallStone2", -1},
                                              {0x12f, "Skill\\", "WallStone1", -1},
                                              {0x130, "Skill\\", "WallStone2", -1},
                                              {0x131, "Skill\\", "GatePart1", -1},
                                              {0x132, "Skill\\", "GatePart2", -1},
                                              {0x133, "Skill\\", "GatePart3", -1},
                                              {0x145, "Skill\\", "elf_skill", -1},
                                              {0x147, "Skill\\", "deathsp_eff", -1}}};
    auto textures = std::make_unique<FakeTextureStore>();
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    auto models = std::make_unique<GameModels>();
    const auto loaded = models->Load(m_dataRoot, *textures, log);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    for (const auto& file : expected)
    {
        const auto original = Client::Data::Models::LoadModelFile(m_dataRoot, file);
        ASSERT_TRUE(original.HasValue()) << file.name;
        const auto* model = models->Find(file.model);
        EXPECT_NE(model, nullptr) << std::hex << file.model << " " << file.name;
        if (model == nullptr)
            continue;
        EXPECT_EQ(model->meshes.size(), original.Value().meshes.size()) << file.name;
        EXPECT_EQ(model->bones.size(), original.Value().bones.size()) << file.name;
        EXPECT_NE(models->SkeletonOf(file.model), nullptr) << file.name;
        const auto slots = models->MeshTextures(file.model);
        ASSERT_EQ(slots.size(), model->meshes.size()) << file.name;
        for (std::size_t mesh = 0; mesh < slots.size(); ++mesh)
        {
            EXPECT_EQ(model->meshes[mesh].textureName, original.Value().meshes[mesh].textureName) << file.name;
            EXPECT_EQ(models->TextureSlots().NameOf(slots[mesh]), model->meshes[mesh].textureName) << file.name;
            const auto uploaded = textures->Find(slots[mesh]);
            ASSERT_TRUE(uploaded.has_value()) << file.name << " " << model->meshes[mesh].textureName;
            EXPECT_GT(uploaded->texture.width, 0u);
            EXPECT_GT(uploaded->texture.height, 0u);
            EXPECT_TRUE(models->TextureMetadataOf(slots[mesh].value).has_value());
        }
    }
}
