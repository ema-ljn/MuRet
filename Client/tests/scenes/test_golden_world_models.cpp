#include "Scenes/WorldModels.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Textures/Sampling.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/TextureSlot.h"
#include "Scenes/GameModels.h"
#include "Scenes/LoginModels.h"

#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/RecordingSink.h"
#include "support/SyntheticModels.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <ios>
#include <string>

// Client::Scenes::WorldModels con el Data\ del repo: los modelos de Object56\ que carga 0x616ec0 para
// el mapa 55 (el k de Object<k + 1>.bmd) y sus texturas, que siguen la tabla de la carga desde 0x41c
// (06 § El mundo del login).
namespace
{
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Render::TextureSlot;
using Client::Scenes::WorldModels;
using Client::Tests::FakeTextureStore;
using Client::Tests::RecordingSink;

constexpr std::uint8_t Login = 55;

using GoldenWorldModels = Client::Tests::GoldenDataTest;

// El modelo k es Object<k + 1>.bmd: en el Data\ del laboratorio están 79 de los 160 (del Object02 al
// Object91 con huecos), y en el mapa 55 también el dragón.
TEST_F(GoldenWorldModels, World56LoadsObjectKPlusOne)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    WorldModels models;
    const auto loaded = models.Load(m_dataRoot, Login, store, log, nullptr);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();

    EXPECT_EQ(models.Count(), 80u) << "79 objetos y el dragón";
    EXPECT_EQ(models.Find(0), nullptr) << "Object01.bmd no está";
    EXPECT_NE(models.Find(1), nullptr) << "Object02.bmd";
    EXPECT_NE(models.Find(33), nullptr) << "Object34.bmd, el dragón de piedra";
    EXPECT_NE(models.Find(90), nullptr) << "Object91.bmd, el remolino 90";
    EXPECT_EQ(models.Find(91), nullptr) << "Object92.bmd no está";
    ASSERT_NE(models.SkeletonOf(83), nullptr) << "las hojas, con su acción de 13 keys";
    EXPECT_EQ(models.SkeletonOf(83)->actions.front().keyCount, 13);
}

// Uno que falta se saltea sin avisar (Q-C23): el tipo 82, que pide el .obj, queda sin modelo.
TEST_F(GoldenWorldModels, AMissingModelIsSkipped)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    WorldModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, Login, store, log, nullptr).HasValue());
    EXPECT_EQ(models.Find(82), nullptr) << "Object83.bmd no está";
    EXPECT_EQ(models.SkeletonOf(82), nullptr);
    EXPECT_TRUE(models.MeshTextures(82).empty());
    for (std::size_t k = 0; k < sink.lines.size(); ++k)
    {
        if (sink.levels[k] == LogLevel::Warning)
        {
            EXPECT_EQ(sink.lines[k].find("Object83"), std::string::npos) << sink.lines[k];
        }
    }
}

// 0x616ec0 en el mapa 55 (0x6188ae): el dragón que vuela es el modelo 0xb4, de Object56\Dragon.bmd
// (dragon.bmd en el Data\, Q-C127), con 3 mallas, 93 huesos y una acción de 41 keys, y sus texturas
// desde 0x60d530(0x69).
TEST_F(GoldenWorldModels, TheDragonIsModel0xb4)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    WorldModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, Login, store, log, nullptr).HasValue());
    const auto* dragon = models.Find(Client::Scenes::DragonModel);
    ASSERT_NE(dragon, nullptr);
    EXPECT_EQ(dragon->meshes.size(), 3u);
    EXPECT_EQ(dragon->bones.size(), 93u);
    ASSERT_NE(models.SkeletonOf(Client::Scenes::DragonModel), nullptr);
    ASSERT_EQ(models.SkeletonOf(Client::Scenes::DragonModel)->actions.size(), 1u);
    EXPECT_EQ(models.SkeletonOf(Client::Scenes::DragonModel)->actions.front().keyCount, 41);
    const auto slots = models.MeshTextures(Client::Scenes::DragonModel);
    ASSERT_EQ(slots.size(), 3u);
    EXPECT_EQ(slots[0], TextureSlot{0x69});
    EXPECT_EQ(slots[1], TextureSlot{0x6a});
    EXPECT_EQ(slots[2], TextureSlot{0x6b});
    // Ningún otro mapa lo tiene.
    WorldModels lorencia;
    ASSERT_TRUE(lorencia.Load(m_dataRoot, 0, store, log, nullptr).HasValue());
    EXPECT_EQ(lorencia.Find(Client::Scenes::DragonModel), nullptr);
}

// 0x60d530(0x41c) y las texturas de cada modelo en el orden de k, con GL_NEAREST y repitiendo
// (0x60d5f0(k, "Object56\\", 0x2901, 0x2600, 1)). Los 65 nombres de Object56\ se buscan primero en la
// tabla: los tres del dragón están en 0x69..0x6b, y en la del original (lor_slots.bin, las de 0x615700)
// están castlegate02, cswalltop y castlegatehide en 0x3e5..0x3e7, y fire02 y fire01 en 0x3ef y 0x3f0. Las
// 57 nuevas van del 0x41c al 0x454 y el logo queda en 0x455 (Q-C92).
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
    Client::Render::Models::ModelTextureSlots table(TextureSlot{0x3e5});
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
    // La malla 0 del Object02 es la primera textura nueva.
    ASSERT_FALSE(models.MeshTextures(1).empty());
    EXPECT_EQ(models.MeshTextures(1).front(), TextureSlot{0x41c});

    models.Release();
    EXPECT_FALSE(store.Find(TextureSlot{0x41c}).has_value());
}

// Con la tabla de GameModels, que todavía no tiene las de 0x615700 más allá de las cuatro primeras
// (Data::Models::SkillTextureCalls), los cinco nombres de las habilidades se piden de Object56\: los tres
// del castillo están y suben, fire02 y fire01 no y se saltean con un aviso, el login sigue y el logo
// queda cinco más allá.
TEST_F(GoldenWorldModels, TwoTexturesFromAnotherFolderAreSkippedWithAWarning)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    Client::Scenes::GameModels game;
    ASSERT_TRUE(game.Load(m_dataRoot, store, log).HasValue());
    WorldModels models;
    const auto loaded = models.Load(m_dataRoot, Login, store, log, &game.TextureSlots());
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    ASSERT_NE(models.TextureSlots(), nullptr);
    EXPECT_EQ(models.TextureSlots()->Next(), TextureSlot{0x45a});
    std::size_t warnings = 0;
    for (std::size_t k = 0; k < sink.lines.size(); ++k)
    {
        if (sink.levels[k] == LogLevel::Warning && sink.lines[k].find("Object56") != std::string::npos)
        {
            ++warnings;
        }
    }
    EXPECT_EQ(warnings, 2u);
    ASSERT_FALSE(models.MeshTextures(89).empty()) << "el fuego, Object90.bmd";
}
} // namespace
