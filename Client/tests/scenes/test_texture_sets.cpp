#include "Scenes/TextureSets.h"

#include "Data/Textures/Sampling.h"
#include "Data/Textures/TextureSet.h"
#include "Render/TextureSlot.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <span>

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
using Client::Scenes::LoadingTextureSet;
using Client::Scenes::LoginTextureSet;
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

// Las tres de 0x61ccb0 para las ventanas del login (CL5.5), sin filtro y sin repetir, en slots que
// el login no usa: la escena sube los dos juegos a la vez.
TEST(TextureSetsTest, TheWindowSetIsTheOneOf0x61ccb0)
{
    ExpectAValidSet(WindowTextureSet);
    EXPECT_EQ(WindowTextureSet.size(), 3u);
    EXPECT_EQ(CountWith(WindowTextureSet, {Filter::Nearest, Wrap::Clamp}), 3u);
    for (const TextureSetEntry& window : WindowTextureSet)
    {
        for (const TextureSetEntry& login : LoginTextureSet)
        {
            EXPECT_NE(window.slot, login.slot);
        }
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
