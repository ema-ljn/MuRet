#include "Scenes/TextureSets.h"

#include "Data/Textures/Sampling.h"
#include "Data/Textures/TextureSet.h"
#include "Render/TextureSlot.h"
#include "Scenes/SceneTextures.h"
#include "support/FakeTextureStore.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <span>
#include <string_view>

// Los juegos de texturas de la carga, del login y de sus ventanas como datos (06 § La carga y § El
// login): cada entrada tiene un cargador, un slot de la tabla del original y uno solo. Lo que se
// lee del disco lo prueba test_golden_texture_sets.cpp.
namespace
{
using Client::Data::Textures::Filter;
using Client::Data::Textures::Sampling;
using Client::Data::Textures::TextureLoaderOf;
using Client::Data::Textures::TextureSetEntry;
using Client::Data::Textures::Wrap;
using Client::Render::OriginalTextureSlotCount;
using Client::Scenes::EffectTextureSet;
using Client::Scenes::GlowTextureSet;
using Client::Scenes::LoadingTextureSet;
using Client::Scenes::LoginTextureSet;
using Client::Scenes::SelectionTextureSet;
using Client::Scenes::WindowTextureSet;

// Que cada entrada tenga cargador y un slot de la tabla, sin repetir.
void ExpectAValidSet(std::span<const TextureSetEntry> set)
{
    std::set<std::uint16_t> slots;
    for (const TextureSetEntry& entry : set)
    {
        EXPECT_NE(TextureLoaderOf(entry.path), std::nullopt) << "el slot " << entry.slot << " no es .jpg ni .tga";
        EXPECT_LT(entry.slot, OriginalTextureSlotCount);
        EXPECT_TRUE(slots.insert(entry.slot).second) << "el slot " << entry.slot << " está dos veces";
    }
}

// Cuántas entradas tienen `sampling`.
std::size_t CountWith(std::span<const TextureSetEntry> set, Sampling sampling)
{
    std::size_t count = 0;
    for (const TextureSetEntry& entry : set)
    {
        count += entry.sampling.filter == sampling.filter && entry.sampling.wrap == sampling.wrap ? 1 : 0;
    }
    return count;
}

// 0x61c7c0 sube 18 texturas (el 0xa2 es un modelo, no entra): 12 sin filtro y sin repetir, las dos
// de la arena con filtro y repetidas, FireSnuff y cr_mu_lo con filtro, y server_ex03 y server_ex02
// repetidas sin filtro.
TEST(TextureSetsTest, TheLoginSetIsTheOneOf0x61c7c0)
{
    ExpectAValidSet(LoginTextureSet);
    EXPECT_EQ(LoginTextureSet.size(), 18u);
    EXPECT_EQ(CountWith(LoginTextureSet, {Filter::Nearest, Wrap::Clamp}), 12u);
    EXPECT_EQ(CountWith(LoginTextureSet, {Filter::Linear, Wrap::Repeat}), 2u);
    EXPECT_EQ(CountWith(LoginTextureSet, {Filter::Linear, Wrap::Clamp}), 2u);
    EXPECT_EQ(CountWith(LoginTextureSet, {Filter::Nearest, Wrap::Repeat}), 2u);
}

// Las de 0x61ccb0 que usan el login y la selección: los dos cursores (los slots 2 y 3, con filtro y sin
// repetir, 0x61ccf9 y 0x61cd12), las cuatro de las ventanas (CL5.5 y CL6.5), sin filtro y sin repetir, y
// las seis de la ventana de Esc (CL8), sin filtro y con la piedra y los dos lados repetidos. Ninguna en un
// slot de las dos escenas: cada una sube los dos juegos a la vez. La sexta es la caja del código del
// borrado (0x728), y la última, los botones de la de Esc (0x72f).
TEST(TextureSetsTest, TheWindowSetIsTheOneOf0x61ccb0)
{
    ExpectAValidSet(WindowTextureSet);
    ASSERT_EQ(WindowTextureSet.size(), 12u);
    EXPECT_EQ(WindowTextureSet[0].path, "Interface\\Cursor.tga");
    EXPECT_EQ(WindowTextureSet[0].slot, 0x02);
    EXPECT_EQ(WindowTextureSet[1].path, "Interface\\CursorPush.tga");
    EXPECT_EQ(WindowTextureSet[1].slot, 0x03);
    EXPECT_EQ(WindowTextureSet[5].slot, 0x728);
    EXPECT_EQ(WindowTextureSet[11].slot, 0x72f);
    EXPECT_EQ(CountWith(WindowTextureSet, {Filter::Linear, Wrap::Clamp}), 2u);
    EXPECT_EQ(CountWith(WindowTextureSet, {Filter::Nearest, Wrap::Clamp}), 7u);
    EXPECT_EQ(CountWith(WindowTextureSet, {Filter::Nearest, Wrap::Repeat}), 3u);
    for (const TextureSetEntry& window : WindowTextureSet)
    {
        for (const TextureSetEntry& scene : LoginTextureSet)
        {
            EXPECT_NE(window.slot, scene.slot);
        }
        for (const TextureSetEntry& scene : SelectionTextureSet)
        {
            EXPECT_NE(window.slot, scene.slot);
        }
    }
}

// Las de los pases de brillo, que 0x61ccb0 sube en su avance 0 (0x61cd9b, 0x61cdc5 y 0x61cf1f):
// Shiny01 y Chrome01 con filtro, Chrome01 repitiendo, y Chrome02 sin filtro ni repetir.
TEST(TextureSetsTest, TheGlowSetIsTheOneOf0x61ccb0)
{
    ExpectAValidSet(GlowTextureSet);
    ASSERT_EQ(GlowTextureSet.size(), 3u);
    EXPECT_EQ(GlowTextureSet[0].path, "Effect\\Shiny01.jpg");
    EXPECT_EQ(GlowTextureSet[0].slot, 0x5fc);
    EXPECT_EQ(GlowTextureSet[0].sampling.filter, Filter::Linear);
    EXPECT_EQ(GlowTextureSet[0].sampling.wrap, Wrap::Clamp);
    EXPECT_EQ(GlowTextureSet[1].path, "Effect\\Chrome01.jpg");
    EXPECT_EQ(GlowTextureSet[1].slot, 0x5c0);
    EXPECT_EQ(GlowTextureSet[1].sampling.filter, Filter::Linear);
    EXPECT_EQ(GlowTextureSet[1].sampling.wrap, Wrap::Repeat);
    EXPECT_EQ(GlowTextureSet[2].path, "Effect\\Chrome02.jpg");
    EXPECT_EQ(GlowTextureSet[2].slot, 0x620);
    EXPECT_EQ(GlowTextureSet[2].sampling.filter, Filter::Nearest);
    EXPECT_EQ(GlowTextureSet[2].sampling.wrap, Wrap::Clamp);
}

// 0x61d801 sube clouds.jpg a 0x622, con GL_LINEAR y GL_CLAMP, para la neblina del login.
TEST(TextureSetsTest, TheEffectSetLoadsTheCloudsWithLinearClamp)
{
    ExpectAValidSet(EffectTextureSet);
    const TextureSetEntry* cloud = nullptr;
    for (const TextureSetEntry& entry : EffectTextureSet)
    {
        if (entry.slot == 0x622)
        {
            cloud = &entry;
        }
    }
    ASSERT_NE(cloud, nullptr);
    EXPECT_EQ(cloud->path, "Effect\\clouds.jpg");
    EXPECT_EQ(cloud->sampling.filter, Filter::Linear);
    EXPECT_EQ(cloud->sampling.wrap, Wrap::Clamp);
}

// 0x61d7e5 sube Explotion01.jpg a 0x5ed, con GL_LINEAR y GL_CLAMP: la explosión 5ed/0 del terremoto 0xfa.
TEST(TextureSetsTest, TheEffectSetLoadsTheEarthquakeExplosionWithLinearClamp)
{
    const TextureSetEntry* explosion = nullptr;
    for (const TextureSetEntry& entry : EffectTextureSet)
    {
        if (entry.slot == 0x5ed)
            explosion = &entry;
    }
    ASSERT_NE(explosion, nullptr);
    EXPECT_EQ(explosion->path, "Effect\\Explotion01.jpg");
    EXPECT_EQ(explosion->sampling.filter, Filter::Linear);
    EXPECT_EQ(explosion->sampling.wrap, Wrap::Clamp);
}

// 0x61d6e0 y 0x61d6fc: el calco y los anillos de 0x61f deben subir a los slots originales.
// Lee los OZJ versionados del repo: también fija el nombre real, no solo dos filas de la tabla.
TEST(TextureSetsTest, TheArrivalGroundAndRingTexturesUploadAtTheirOriginalSlots)
{
    Client::Tests::FakeTextureStore store;
    Client::Scenes::SceneTextures textures(store, EffectTextureSet);
    const std::filesystem::path dataRoot{MURET_CLIENT_REPO_DATA_DIR};
    const auto loaded = textures.Load(dataRoot);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    for (const std::uint16_t slot : {std::uint16_t{0x61e}, std::uint16_t{0x61f}})
    {
        for (const TextureSetEntry& entry : EffectTextureSet)
        {
            if (entry.slot == slot)
            {
                EXPECT_EQ(entry.path, slot == 0x61e ? "Effect\\Magic_Ground2.jpg" : "Effect\\Magic_Circle1.jpg");
            }
        }
        const auto texture = store.Find(Client::Render::TextureSlot{slot});
        ASSERT_TRUE(texture.has_value()) << slot;
        EXPECT_EQ(texture->sampling.filter, Filter::Linear);
        EXPECT_EQ(texture->sampling.wrap, Wrap::Clamp);
        EXPECT_GT(texture->texture.width, 0);
        EXPECT_GT(texture->texture.height, 0);
    }
}

// 0x61d5a0 y 0x61d893: la chispa 6ec/5 de Crywolf (fantaF) y la burbuja 698/0 de su agua (coll), de la Data del repo.
TEST(TextureSetsTest, TheCrywolfSparkAndBubbleTexturesUploadAtTheirOriginalSlots)
{
    Client::Tests::FakeTextureStore store;
    Client::Scenes::SceneTextures textures(store, EffectTextureSet);
    const auto loaded = textures.Load(std::filesystem::path{MURET_CLIENT_REPO_DATA_DIR});
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    for (const std::uint16_t slot : {std::uint16_t{0x6ec}, std::uint16_t{0x698}})
    {
        for (const TextureSetEntry& entry : EffectTextureSet)
        {
            if (entry.slot == slot)
            {
                EXPECT_EQ(entry.path, slot == 0x6ec ? "Effect\\fantaF.jpg" : "Effect\\coll.jpg");
            }
        }
        const auto texture = store.Find(Client::Render::TextureSlot{slot});
        ASSERT_TRUE(texture.has_value()) << slot;
        EXPECT_EQ(texture->sampling.filter, Filter::Linear);
        EXPECT_EQ(texture->sampling.wrap, Wrap::Clamp);
        EXPECT_GT(texture->texture.width, 0);
        EXPECT_GT(texture->texture.height, 0);
    }
}

// 0x61d663 y 0x61d768: la onda 0x6eb del galope en Icarus (ShockWave, 128 × 128) y el polvo 5f3 del galope en los
// demás mapas (smoke02, 64 × 64, el alto que lee su paso), de la Data del repo.
TEST(TextureSetsTest, TheGallopTexturesUploadAtTheirOriginalSlots)
{
    Client::Tests::FakeTextureStore store;
    Client::Scenes::SceneTextures textures(store, EffectTextureSet);
    const auto loaded = textures.Load(std::filesystem::path{MURET_CLIENT_REPO_DATA_DIR});
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    struct Case
    {
        std::uint16_t slot;
        std::string_view path;
        int side;
    };
    for (const Case& expected : {Case{0x6eb, "Effect\\ShockWave.jpg", 128}, Case{0x5f3, "Effect\\smoke02.tga", 64}})
    {
        SCOPED_TRACE(expected.slot);
        int entries = 0;
        for (const TextureSetEntry& entry : EffectTextureSet)
        {
            if (entry.slot == expected.slot)
            {
                ++entries;
                EXPECT_EQ(entry.path, expected.path);
            }
        }
        EXPECT_EQ(entries, 1);
        const auto texture = store.Find(Client::Render::TextureSlot{expected.slot});
        ASSERT_TRUE(texture.has_value());
        EXPECT_EQ(texture->sampling.filter, Filter::Linear);
        EXPECT_EQ(texture->sampling.wrap, Wrap::Clamp);
        EXPECT_EQ(texture->texture.width, expected.side);
        EXPECT_EQ(texture->texture.height, expected.side);
    }
}

// 0x61d4db: la llama que emiten los objetos usa Fire01, también en la carga durable.
TEST(TextureSetsTest, TheObjectFireTextureUploadsAtItsOriginalSlot)
{
    Client::Tests::FakeTextureStore store;
    Client::Scenes::SceneTextures textures(store, EffectTextureSet);
    const auto loaded = textures.Load(std::filesystem::path{MURET_CLIENT_REPO_DATA_DIR});
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    const auto texture = store.Find(Client::Render::TextureSlot{0x5d9});
    ASSERT_TRUE(texture.has_value());
    EXPECT_EQ(texture->sampling.filter, Filter::Linear);
    EXPECT_EQ(texture->sampling.wrap, Wrap::Clamp);
    EXPECT_GT(texture->texture.width, 0);
    EXPECT_GT(texture->texture.height, 0);
}

TEST(TextureSetsTest, TheNoriaSparkAndLightningTexturesUploadAtTheirOriginalSlots)
{
    Client::Tests::FakeTextureStore store;
    Client::Scenes::SceneTextures textures(store, EffectTextureSet);
    const auto loaded = textures.Load(std::filesystem::path{MURET_CLIENT_REPO_DATA_DIR});
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    for (const std::uint16_t slot : {std::uint16_t{0x5c5}, std::uint16_t{0x5d5}})
    {
        const auto texture = store.Find(Client::Render::TextureSlot{slot});
        ASSERT_TRUE(texture.has_value()) << slot;
        EXPECT_EQ(texture->sampling.filter, Filter::Linear);
        EXPECT_EQ(texture->sampling.wrap, Wrap::Clamp);
        EXPECT_GT(texture->texture.width, 0);
    }
}

// 0x622e40 sube seis, todas con filtro y sin repetir salvo la barra.
TEST(TextureSetsTest, TheLoadingSetIsTheOneOf0x622e40)
{
    ExpectAValidSet(LoadingTextureSet);
    EXPECT_EQ(LoadingTextureSet.size(), 6u);
    EXPECT_EQ(CountWith(LoadingTextureSet, {Filter::Linear, Wrap::Clamp}), 5u);
    EXPECT_EQ(CountWith(LoadingTextureSet, {Filter::Linear, Wrap::Repeat}), 1u);
}
} // namespace

// 61da58/61da62: Thunder01 usa GL_LINEAR/GL_REPEAT, a diferencia de los sprites cercanos.
TEST(TextureSetsTest, HelperThunderUploadsItsRealImageWithLinearRepeat)
{
    Client::Tests::FakeTextureStore store;
    Client::Scenes::SceneTextures textures(store, EffectTextureSet);
    const auto loaded = textures.Load(std::filesystem::path{MURET_CLIENT_REPO_DATA_DIR});
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    const auto texture = store.Find(Client::Render::TextureSlot{0x5ca});
    ASSERT_TRUE(texture.has_value());
    EXPECT_EQ(texture->sampling.filter, Filter::Linear);
    EXPECT_EQ(texture->sampling.wrap, Wrap::Repeat);
    EXPECT_GT(texture->texture.width, 0);
    EXPECT_GT(texture->texture.height, 0);
}
