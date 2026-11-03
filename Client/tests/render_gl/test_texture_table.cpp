#include "Render/Gl/TextureTable.h"

#include "Core/Expected.h"
#include "Data/Textures/OztTexture.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Render/ITextureStore.h"
#include "Render/TextureSlot.h"
#include "support/GlTestContext.h"
#include "support/TextureFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// La tabla de texturas de una escena, indexada por el slot del original (pregunta 11 del plan de
// la CL5): lo que se carga se encuentra por su slot, se suelta como con 0x63c150, y lo que no
// entra en los 1.845 slots no se sube.
namespace
{
using Client::Data::Textures::LoadedTexture;
using Client::Data::Textures::LoadOztTexture;
using Client::Data::Textures::Texture;
using Client::Render::ITextureStore;
using Client::Render::OriginalTextureSlotCount;
using Client::Render::TextureSlot;
using Client::Render::Gl::GlTexture;
using Client::Render::Gl::TextureTable;
using Client::Tests::PlainOzt;

using TextureTableTest = Client::Tests::GlTest;

static_assert(std::is_base_of_v<ITextureStore, TextureTable>);
// Las escenas la guardan como ITextureStore&: ni se copia ni se mueve.
static_assert(!std::is_copy_constructible_v<TextureTable>);
static_assert(!std::is_move_constructible_v<TextureTable>);

// Una textura del juego: un OZT sintético de `width` × `height`, con su slot.
LoadedTexture Ozt(std::uint16_t slot, std::int16_t width, std::int16_t height)
{
    Client::Core::Expected<Texture, Client::Data::DataError> texture = LoadOztTexture(PlainOzt(width, height));
    EXPECT_TRUE(texture.HasValue());
    return {slot, {}, texture.HasValue() ? std::move(texture).Value() : Texture{}};
}

// El slot como lo nombra el research, en hexadecimal: 0x1a.
std::string Hex(std::uint16_t slot)
{
    return std::format("{:#x}", slot);
}

// El ancho del buffer de lo que está en `slot`, o 0 si no hay nada.
int WidthAt(const TextureTable& table, std::uint16_t slot)
{
    const GlTexture* texture = table.Find(TextureSlot{slot});
    return texture != nullptr ? texture->Width() : 0;
}

TEST_F(TextureTableTest, FindsWhatItLoaded)
{
    TextureTable table(Gl());
    const std::vector<LoadedTexture> textures{Ozt(0x15, 2, 2), Ozt(0x16, 3, 2)};

    const Client::Core::Expected<void, std::string> loaded = table.Load(textures);

    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    EXPECT_EQ(WidthAt(table, 0x15), 2);
    EXPECT_EQ(WidthAt(table, 0x16), 4); // el buffer de un ancho de 3
    ASSERT_NE(table.Find(TextureSlot{0x15}), nullptr);
    EXPECT_NE(table.Find(TextureSlot{0x15})->Name(), 0u);
    EXPECT_EQ(table.Find(TextureSlot{0x17}), nullptr);
}

// Como 0x63c150(slot): el slot queda libre, y los demás siguen.
TEST_F(TextureTableTest, ReleaseForgetsTheSlot)
{
    TextureTable table(Gl());
    const std::vector<LoadedTexture> textures{Ozt(0x15, 2, 2), Ozt(0x16, 2, 2)};
    ASSERT_TRUE(table.Load(textures).HasValue());

    table.Release(TextureSlot{0x15});

    EXPECT_EQ(table.Find(TextureSlot{0x15}), nullptr);
    EXPECT_EQ(WidthAt(table, 0x16), 2);
    // Soltar uno vacío, o uno que no entra en la tabla, no hace nada.
    table.Release(TextureSlot{0x15});
    table.Release(TextureSlot{static_cast<std::uint16_t>(OriginalTextureSlotCount)});
    EXPECT_EQ(WidthAt(table, 0x16), 2);
}

// Cargar sobre un slot ocupado lo reemplaza: la textura vieja se borra.
TEST_F(TextureTableTest, ALoadReplacesTheSlot)
{
    TextureTable table(Gl());
    ASSERT_TRUE(table.Load(std::vector{Ozt(0x15, 2, 2)}).HasValue());

    ASSERT_TRUE(table.Load(std::vector{Ozt(0x15, 8, 2)}).HasValue());

    EXPECT_EQ(WidthAt(table, 0x15), 8);
}

// El original tiene 1.845 slots (0x64e6 dwords en 0x622e40): uno de afuera no se sube, y el error
// dice cuál. Lo cargado antes queda: la escena que falla suelta la tabla entera.
TEST_F(TextureTableTest, ASlotOutsideTheTableIsRefused)
{
    TextureTable table(Gl());
    const auto outside = static_cast<std::uint16_t>(OriginalTextureSlotCount);
    const std::vector<LoadedTexture> textures{Ozt(0x15, 2, 2), Ozt(outside, 2, 2)};

    const Client::Core::Expected<void, std::string> loaded = table.Load(textures);

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find(Hex(outside)), std::string::npos) << loaded.Error();
    EXPECT_EQ(table.Find(TextureSlot{outside}), nullptr);
    EXPECT_EQ(WidthAt(table, 0x15), 2);
}

// El último slot del original, el 1.844 (0x734), sí está en la tabla: se carga, se encuentra y se
// suelta como cualquiera.
TEST_F(TextureTableTest, TheLastSlotIsInTheTable)
{
    TextureTable table(Gl());
    const auto last = static_cast<std::uint16_t>(OriginalTextureSlotCount - 1);

    const Client::Core::Expected<void, std::string> loaded = table.Load(std::vector{Ozt(last, 4, 2)});

    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    EXPECT_EQ(WidthAt(table, last), 4);
    table.Release(TextureSlot{last});
    EXPECT_EQ(table.Find(TextureSlot{last}), nullptr);
}

// Una textura que GlTexture no sube (un buffer que no mide lo que dice) corta, con su slot.
TEST_F(TextureTableTest, AnUploadThatFailsSaysTheSlot)
{
    TextureTable table(Gl());
    LoadedTexture broken = Ozt(0x1a, 2, 2);
    broken.texture.pixels.pop_back();

    const Client::Core::Expected<void, std::string> loaded = table.Load(std::vector{broken});

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find(Hex(0x1a)), std::string::npos) << loaded.Error();
    EXPECT_EQ(table.Find(TextureSlot{0x1a}), nullptr);
}
} // namespace
