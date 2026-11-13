#include "Render/Models/PassColors.h"

#include "Render/Models/Shine.h"
#include "Render/SpriteVertex.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <ratio>

// Client::Render::Models::ShineColorOf, MetalFactorOf, SetColorOf y ExcellentColorAt: el color de
// cada pase de brillo de 0x60b3d0, con las tablas que salen de emular 0x608e00, 0x6093d0 y 0x609560
// sobre el main.exe (06 § Los pases de brillo).
namespace
{
using Client::Render::Color;
using Client::Render::Models::ExcellentColorAt;
using Client::Render::Models::MetalFactorOf;
using Client::Render::Models::PassHiddenMesh;
using Client::Render::Models::SetAlphaAt;
using Client::Render::Models::SetColorOf;
using Client::Render::Models::SetPulseAt;
using Client::Render::Models::SetPulseIsOff;
using Client::Render::Models::ShineColorOf;
using Client::Render::Models::ShinePass;

void ExpectColor(const Color& color, float red, float green, float blue)
{
    EXPECT_FLOAT_EQ(color.red, red);
    EXPECT_FLOAT_EQ(color.green, green);
    EXPECT_FLOAT_EQ(color.blue, blue);
}

// 0x608e00, el de los pases 0x44 y 0x48: un color por ítem, que no mira la luz. El de las Male17 de
// la traza (el caso 10) y el de Sword17, el de siempre.
TEST(PassColorsTest, TheShinePassesHaveTheColorOfTheirItem)
{
    ExpectColor(ShineColorOf(0x1222), 0.75f, 0.65f, 0.5f);
    ExpectColor(ShineColorOf(0x222), 1.0f, 0.5f, 0.0f);
    ExpectColor(ShineColorOf(0x16d), 1.0f, 0.2f, 0.0f);
    ExpectColor(ShineColorOf(0x175), 1.0f, 1.0f, 1.0f);
}

// 0x6093d0, el de los pases 0x240 y 0x1040: multiplica el color base, que es la luz por el factor
// del nivel. Casi todos por 1.
TEST(PassColorsTest, TheMetalPassesKeepTheBase)
{
    EXPECT_EQ(MetalFactorOf(0x1222), glm::vec3(1.0f, 1.0f, 1.0f));
    EXPECT_EQ(MetalFactorOf(0x1016), glm::vec3(1.0f, 0.5f, 0.0f));
    EXPECT_EQ(MetalFactorOf(0x220), glm::vec3(0.0f, 0.5f, 1.0f));
}

// 0x609560, el del pase de set (0x840): azul, salvo 15 partes doradas.
TEST(PassColorsTest, TheSetPassIsBlueOrGold)
{
    ExpectColor(SetColorOf(0x1222), 0.1f, 0.6f, 1.0f);
    ExpectColor(SetColorOf(0x1015), 1.0f, 0.7f, 0.2f);
}

// 0x60b3d0, el 0x42 del excelente: k = (sin(0,002 · T) + 1) · 0,5, y (k, 0,3k, 1 − k).
TEST(PassColorsTest, TheExcellentColorPulses)
{
    ExpectColor(ExcellentColorAt(std::chrono::milliseconds{0}), 0.5f, 0.15f, 0.5f);
    const auto top = std::chrono::duration<double, std::milli>(std::numbers::pi / 0.004);
    const Color brightest = ExcellentColorAt(std::chrono::duration_cast<std::chrono::nanoseconds>(top));
    EXPECT_NEAR(brightest.red, 1.0f, 1e-5f);
    EXPECT_NEAR(brightest.green, 0.3f, 1e-5f);
    EXPECT_NEAR(brightest.blue, 0.0f, 1e-5f);
}

// 0x60b3d0 llama al pase de set con el alpha sin(0,001 · T) · 0,5 + 0,4.
TEST(PassColorsTest, TheSetAlphaPulses)
{
    EXPECT_FLOAT_EQ(SetAlphaAt(std::chrono::milliseconds{0}), 0.4f);
    EXPECT_FLOAT_EQ(SetAlphaAt(std::chrono::milliseconds{250}), std::sin(0.25f) * 0.5f + 0.4f);
}

// 0x60cc19..0x60cc1c: el alpha es el pulso guardado en un float, pero el rand() del set lo decide el pulso de la x87
// sin redondear (FCOMP contra el 0,01f). A los 4.036.258.298 ns el pulso pasa el 0,01f por 2,7e−10 y su float ya es
// 0,01f; dos nanosegundos después queda debajo.
TEST(PassColorsTest, TheSetPulseIsComparedBeforeItBecomesAFloat)
{
    const auto at = [](std::int64_t nanoseconds)
    { return std::chrono::duration<double, std::milli>(std::chrono::nanoseconds{nanoseconds}).count(); };
    const double above = SetPulseAt(at(4036258298));
    EXPECT_GT(above, static_cast<double>(0.01f));
    EXPECT_LE(static_cast<float>(above), 0.01f);
    EXPECT_FALSE(SetPulseIsOff(above));
    EXPECT_TRUE(SetPulseIsOff(SetPulseAt(at(4036258300))));
    EXPECT_EQ(SetAlphaAt(std::chrono::nanoseconds{4036258298}), static_cast<float>(above));
}

// La malla que 0x60b130 (los pases 0x48 y 0x44) y 0x60b300 (0x240, 0x1040 y el set) le pasan a
// RenderBody como la oculta: la 1 de 0xc17 y 0x226 y la 0 de 0x227 en todos, y la 2 de 0x619 solo
// en los de 0x60b130. Las demás, ninguna, aunque su objeto oculte una.
TEST(PassColorsTest, SomeItemsHideAMeshInTheirPasses)
{
    for (const ShinePass pass :
         {ShinePass::Chrome, ShinePass::Shiny, ShinePass::Chrome2, ShinePass::Chrome3, ShinePass::Chrome4})
    {
        EXPECT_EQ(PassHiddenMesh(0xc17, pass, -1), std::optional<std::size_t>{1});
        EXPECT_EQ(PassHiddenMesh(0x226, pass, -1), std::optional<std::size_t>{1});
        EXPECT_EQ(PassHiddenMesh(0x227, pass, -1), std::optional<std::size_t>{0});
        EXPECT_EQ(PassHiddenMesh(0x212, pass, 2), std::nullopt);
    }
    EXPECT_EQ(PassHiddenMesh(0x619, ShinePass::Chrome, -1), std::optional<std::size_t>{2});
    EXPECT_EQ(PassHiddenMesh(0x619, ShinePass::Shiny, -1), std::optional<std::size_t>{2});
    EXPECT_EQ(PassHiddenMesh(0x619, ShinePass::Chrome2, 2), std::nullopt);
    EXPECT_EQ(PassHiddenMesh(0x619, ShinePass::Chrome4, 2), std::nullopt);
}

// 0x60b130 les pasa el +0x58 del objeto a 0x231, 0x81c y 0xe22 y a 0x61a..0x61f; 0x60b300, no.
TEST(PassColorsTest, SomeItemsHideTheMeshOfTheirObjectInTheShinePasses)
{
    for (const std::uint16_t model :
         {std::uint16_t{0x231}, std::uint16_t{0x81c}, std::uint16_t{0xe22}, std::uint16_t{0x61a}, std::uint16_t{0x61f}})
    {
        EXPECT_EQ(PassHiddenMesh(model, ShinePass::Chrome, 1), std::optional<std::size_t>{1}) << model;
        EXPECT_EQ(PassHiddenMesh(model, ShinePass::Shiny, 2), std::optional<std::size_t>{2}) << model;
        EXPECT_EQ(PassHiddenMesh(model, ShinePass::Shiny, -1), std::nullopt) << model;
        EXPECT_EQ(PassHiddenMesh(model, ShinePass::Chrome4, 1), std::nullopt) << model;
    }
    EXPECT_EQ(PassHiddenMesh(0x620, ShinePass::Chrome, 1), std::nullopt);
}
} // namespace
