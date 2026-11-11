#include "Scenes/GameModels.h"
#include "Scenes/TextureMetadata.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/ModelTextureCalls.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/CreatePreview.h"
#include "Scenes/TextureSets.h"

#include "support/FakeTextureStore.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ios>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// Client::Scenes::GameModels contra el Data\ del laboratorio (MURET_CLIENT_GOLDEN_DIR): los modelos
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

// Los tres giftbox de 0x60ff50, que el laboratorio no tiene (06).
constexpr std::size_t MissingInTheLab = 3;
// 6161b5 carga RidingSpear01/10f y 0x6166fe MuSign/12a aparte de las tablas de jugador, ítems y skills.
constexpr std::size_t AdditionalTailModels = 2;

class GoldenGameModels : public Client::Tests::GoldenDataTest
{
};

// Todas las filas de los dos cargadores, salvo las tres que faltan; Player.bmd con su esqueleto de
// personaje (la cabeza en el 20), y una malla con su slot por cada malla de cada modelo.
TEST_F(GoldenGameModels, LoadsEveryModelOfThePlayerLoader)
{
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;

    const auto loaded = models.Load(m_dataRoot, textures, log);

    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    EXPECT_EQ(models.Count(), PlayerModelFiles().size() + ItemModelFiles().size() + SkillModelFiles().size() +
                                  AdditionalTailModels - MissingInTheLab);
    ASSERT_NE(models.SkeletonOf(0x20a), nullptr);
    EXPECT_EQ(models.SkeletonOf(0x20a)->headBone, 20);
    for (const auto& file : PlayerModelFiles())
    {
        const auto* model = models.Find(file.model);
        ASSERT_NE(model, nullptr) << file.name << " " << file.number;
        EXPECT_EQ(models.MeshTextures(file.model).size(), model->meshes.size()) << file.name << " " << file.number;
    }
}

// 0x616304/0x61631a cargan Gate01/02 de Object12; 0x61634e..0x616370 asigna solo Gate02 desde
// Monster, en 0x69, antes de volver a 0x3b8. Los dos BMD reales usan doorc01.jpg.
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
    EXPECT_TRUE(models.MeshTextures(0x10c).empty());
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
    // 0x2901 y 0x2600 en todas las de los modelos.
    for (const auto& entry : models.TextureSlots().ToLoad())
    {
        EXPECT_EQ(entry.sampling.filter, ModelTextureSampling.filter) << entry.path;
        EXPECT_EQ(entry.sampling.wrap, ModelTextureSampling.wrap) << entry.path;
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
                  Client::Scenes::DamageTextureSet.size() + Client::Scenes::FaunaJointTextureSet.size() +
                  Client::Scenes::GameTailTextureSet.size());
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

// El nombre de cada slot de 0x12e a 0x370, como lo dejan 0x60f7d0 y 0x611850, y de 0x3b8 a 0x3ca, como
// los deja 0x615700 desde su 0x60d530(0x3b8) hasta el cuervo (0xc0, la quinta llamada, CL7.6 C), contra
// la tabla de 0x7d3a198 volcada en Lorencia (0x38 bytes por slot, el nombre en +0). 0x138 y 0x139 son de
// LaceArrow, que 0x615700 carga encima (Q-C89); de 0x371 a 0x3b7 van las habilidades y los efectos que
// 0x615700 pide antes, que MuRet todavía no carga.
TEST_F(ModelTextureSlotsGoldenTest, StartupMatchesTheDump)
{
    const auto dump = DumpFile(Client::Tests::LorenciaCapture, "lor_slots.bin");
    ASSERT_TRUE(dump.has_value());
    const std::optional<std::vector<char>> table = ReadFileBytes(*dump);
    ASSERT_TRUE(table.has_value());
    constexpr std::size_t SlotBytes = 0x38;
    constexpr std::size_t NameBytes = 0x20;
    ASSERT_EQ(table->size(), Client::Render::OriginalTextureSlotCount * SlotBytes);
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    GameModels models;

    ASSERT_TRUE(models.Load(m_dataRoot, textures, log).HasValue());

    ASSERT_EQ(models.TextureSlots().Next(), TextureSlot{0x3cb});
    std::size_t compared = 0;
    for (std::uint16_t slot = 0x12e; slot < 0x3cb; ++slot)
    {
        if (slot == 0x138 || slot == 0x139 || (slot >= 0x371 && slot < 0x3b8))
        {
            continue;
        }
        const std::string_view record(table->data() + (slot * SlotBytes), NameBytes);
        const std::string_view name = record.substr(0, record.find('\0'));
        EXPECT_EQ(models.TextureSlots().NameOf(TextureSlot{slot}), name) << std::hex << slot;
        compared += name.empty() ? 0u : 1u;
    }
    EXPECT_EQ(compared, 489u + 15u + 4u);
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
