#include "Render/Models/ModelTextureSlots.h"

#include "Data/Models/Model.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/TextureSet.h"
#include "Render/TextureSlot.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <format>
#include <ostream>
#include <span>
#include <string_view>
#include <vector>

// Client::Render::Models::ModelTextureSlots: 0x60d530, 0x60d550 y 0x60d5f0 como datos.
namespace Client::Render
{
// Para los mensajes de GoogleTest: el slot en hexadecimal, como el research.
void PrintTo(const TextureSlot& slot, std::ostream* out)
{
    *out << std::format("{:#x}", slot.value);
}
} // namespace Client::Render

namespace
{
using Client::Data::Models::Mesh;
using Client::Data::Models::Model;
using Client::Data::Textures::Filter;
using Client::Data::Textures::Sampling;
using Client::Data::Textures::TextureSetEntry;
using Client::Data::Textures::Wrap;
using Client::Render::TextureSlot;
using Client::Render::Models::ModelTextureSlots;

// El primer slot de las partes del personaje (0x60f7d0) y el de los ítems (0x611850).
constexpr TextureSlot PlayerParts{0x138};
constexpr TextureSlot Items{0x25a};
// El muestreo de los cargadores de los modelos: GL_LINEAR y GL_REPEAT.
constexpr Sampling ModelSampling{Filter::Linear, Wrap::Repeat};

// Un modelo con una malla por textura, sin vértices: el asignador solo mira los nombres.
Model ModelWith(std::vector<std::string_view> textures)
{
    Model model;
    for (const std::string_view texture : textures)
    {
        Mesh mesh;
        mesh.textureName = texture;
        model.meshes.push_back(mesh);
    }
    return model;
}

std::vector<TextureSlot> Slots(std::vector<std::uint16_t> values)
{
    std::vector<TextureSlot> slots;
    for (const std::uint16_t value : values)
    {
        slots.push_back(TextureSlot{value});
    }
    return slots;
}

// Un nombre nuevo va al próximo libre, que se corre.
TEST(ModelTextureSlotsTest, ANewNameTakesTheNextSlot)
{
    ModelTextureSlots slots(PlayerParts);

    const std::vector<TextureSlot> assigned =
        slots.Assign(ModelWith({"armor_a.jpg", "armor_b.jpg"}), "Player\\", ModelSampling);

    EXPECT_EQ(assigned, Slots({0x138, 0x139}));
    EXPECT_EQ(slots.Next(), TextureSlot{0x13a});
    ASSERT_EQ(slots.ToLoad().size(), 2u);
    EXPECT_EQ(slots.ToLoad()[0].path, "Player\\armor_a.jpg");
    EXPECT_EQ(slots.ToLoad()[0].slot, 0x138);
    EXPECT_EQ(slots.ToLoad()[1].path, "Player\\armor_b.jpg");
}

// Uno que ya tiene slot lo reusa, en el mismo modelo o en otro, y no pide nada.
TEST(ModelTextureSlotsTest, ARepeatedNameReusesItsSlot)
{
    ModelTextureSlots slots(PlayerParts);
    (void)slots.Assign(ModelWith({"armor_a.jpg"}), "Player\\", ModelSampling);

    const std::vector<TextureSlot> assigned =
        slots.Assign(ModelWith({"armor_a.jpg", "armor_a.jpg"}), "Player\\", ModelSampling);

    EXPECT_EQ(assigned, Slots({0x138, 0x138}));
    EXPECT_EQ(slots.ToLoad().size(), 1u);
    EXPECT_EQ(slots.Next(), TextureSlot{0x139});
}

// Q-C90: se busca el nombre desnudo, sin la carpeta: el de otra carpeta se reusa.
TEST(ModelTextureSlotsTest, ANameIsFoundInAnyFolder)
{
    ModelTextureSlots slots(PlayerParts);
    (void)slots.Assign(ModelWith({"flag.jpg"}), "Skill\\", ModelSampling);

    const std::vector<TextureSlot> assigned = slots.Assign(ModelWith({"flag.jpg"}), "Item\\", ModelSampling);

    EXPECT_EQ(assigned, Slots({0x138}));
    ASSERT_EQ(slots.ToLoad().size(), 1u);
    EXPECT_EQ(slots.ToLoad()[0].path, "Skill\\flag.jpg");
}

// Q-C90: con strcmp, distinguiendo mayúsculas.
TEST(ModelTextureSlotsTest, TheSearchIsCaseSensitive)
{
    ModelTextureSlots slots(PlayerParts);

    const std::vector<TextureSlot> assigned =
        slots.Assign(ModelWith({"Flag.jpg", "flag.jpg"}), "Item\\", ModelSampling);

    EXPECT_EQ(assigned, Slots({0x138, 0x139}));
}

// 0x60d530 pone la búsqueda en el 0: lo que pidió otro cargador antes, en otro primer slot, se reusa.
TEST(ModelTextureSlotsTest, TheSearchStartsAtSlotZero)
{
    ModelTextureSlots slots(PlayerParts);
    (void)slots.Assign(ModelWith({"armor_a.jpg"}), "Player\\", ModelSampling);
    slots.SetNext(Items);

    const std::vector<TextureSlot> assigned =
        slots.Assign(ModelWith({"armor_a.jpg", "sword.jpg"}), "Item\\", ModelSampling);

    EXPECT_EQ(assigned, Slots({0x138, 0x25a}));
}

// La búsqueda llega hasta el próximo libre: con él para atrás (0x615700 vuelve al 0x138, Q-C89), un
// nombre de más arriba ya no se encuentra y se pide otra vez, encima.
TEST(ModelTextureSlotsTest, TheSearchStopsAtNext)
{
    ModelTextureSlots slots(Items);
    (void)slots.Assign(ModelWith({"sword.jpg"}), "Item\\", ModelSampling);
    slots.SetNext(PlayerParts);

    const std::vector<TextureSlot> assigned = slots.Assign(ModelWith({"sword.jpg"}), "Item\\", ModelSampling);

    EXPECT_EQ(assigned, Slots({0x138}));
    EXPECT_EQ(slots.ToLoad().size(), 2u);
}

// Las OZT van siempre con GL_NEAREST; el wrap es el de quien pide. La ruta lleva la extensión del
// cargador que eligió la letra, para que LoadTextureSet abra el mismo archivo.
TEST(ModelTextureSlotsTest, OztIsAlwaysNearest)
{
    ModelTextureSlots slots(PlayerParts);

    (void)slots.Assign(ModelWith({"wing.tga", "body.jpg", "x.tga.jpg"}), "Item\\", ModelSampling);

    const std::span<const TextureSetEntry> toLoad = slots.ToLoad();
    ASSERT_EQ(toLoad.size(), 3u);
    EXPECT_EQ(toLoad[0].path, "Item\\wing.tga");
    EXPECT_EQ(toLoad[0].sampling.filter, Filter::Nearest);
    EXPECT_EQ(toLoad[0].sampling.wrap, Wrap::Repeat);
    EXPECT_EQ(toLoad[1].path, "Item\\body.jpg");
    EXPECT_EQ(toLoad[1].sampling.filter, Filter::Linear);
    EXPECT_EQ(toLoad[2].path, "Item\\x.tga");
    EXPECT_EQ(toLoad[2].sampling.filter, Filter::Nearest);
}

// Las de la piel y el pelo van a sus slots fijos después de cargarse: el que se pidió queda ocupado.
TEST(ModelTextureSlotsTest, TheSkinMeshesPointAtTheSkinSlots)
{
    ModelTextureSlots slots(PlayerParts);

    const std::vector<TextureSlot> assigned =
        slots.Assign(ModelWith({"skin_barbarian_01.jpg", "hair_r.tga", "hide.jpg", "level_man01.jpg", "armor.jpg"}),
                     "Player\\", ModelSampling);

    EXPECT_EQ(assigned, Slots({0x12e, 0x137, 0x12d, 0x12e, 0x13c}));
    EXPECT_EQ(slots.ToLoad().size(), 5u);
}

// Una de la piel que ya se pidió no se pide otra vez, pero va igual a su slot fijo.
TEST(ModelTextureSlotsTest, ASkinNameStillTakesASlot)
{
    ModelTextureSlots slots(PlayerParts);
    (void)slots.Assign(ModelWith({"skin_barbarian_01.jpg"}), "Player\\", ModelSampling);

    const std::vector<TextureSlot> assigned =
        slots.Assign(ModelWith({"skin_barbarian_01.jpg"}), "Player\\", ModelSampling);

    EXPECT_EQ(assigned, Slots({0x12e}));
    EXPECT_EQ(slots.ToLoad().size(), 1u);
    EXPECT_EQ(slots.Next(), TextureSlot{0x139});
}
} // namespace
