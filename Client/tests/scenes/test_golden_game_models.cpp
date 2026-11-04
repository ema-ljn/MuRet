#include "Scenes/GameModels.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/ModelTextureCalls.h"
#include "Data/Models/PlayerModels.h"
#include "Render/TextureSlot.h"
#include "Scenes/TextureSets.h"

#include "support/FakeTextureStore.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
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
using Client::Scenes::GameModels;
using Client::Scenes::GlowTextureSet;
using Client::Tests::FakeTextureStore;
using Client::Tests::ReadFileBytes;
using Client::Tests::RecordingSink;

// Los tres giftbox de 0x60ff50, que el laboratorio no tiene (06).
constexpr std::size_t MissingInTheLab = 3;

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
              models.TextureSlots().ToLoad().size() + PlayerFixedTextures().size() + GlowTextureSet.size());
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

class ModelTextureSlotsGoldenTest : public Client::Tests::MemoryDumpTest
{
};

// El nombre de cada slot de 0x12e a 0x370, como lo dejan 0x60f7d0 y 0x611850, y de 0x3b8 a 0x3c6, como
// los deja 0x615700 desde su 0x60d530(0x3b8) hasta el caballo, contra la tabla de 0x7d3a198 volcada
// en Lorencia (0x38 bytes por slot, el nombre en +0). 0x138 y 0x139 son de LaceArrow, que 0x615700
// carga encima (Q-C89); de 0x371 a 0x3b7 van las habilidades y los efectos que 0x615700 pide antes,
// que MuRet todavía no carga.
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

    ASSERT_EQ(models.TextureSlots().Next(), TextureSlot{0x3c7});
    std::size_t compared = 0;
    for (std::uint16_t slot = 0x12e; slot < 0x3c7; ++slot)
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
    EXPECT_EQ(compared, 489u + 15u);
}
} // namespace
