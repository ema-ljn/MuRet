#include "Render/Models/PassColors.h"

#include "Render/SpriteVertex.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <chrono>
#include <cmath>
#include <numbers>
#include <ratio>

// Client::Render::Models::ShineColorOf, MetalFactorOf, SetColorOf y ExcellentColorAt: el color de
// cada pase de brillo de 0x60b3d0, con las tablas que salen de emular 0x608e00, 0x6093d0 y 0x609560
// sobre el main.exe (06 § Los pases de brillo).
namespace
{
using Client::Render::Color;
using Client::Render::Models::ExcellentColorAt;
using Client::Render::Models::MetalFactorOf;
using Client::Render::Models::SetColorOf;
using Client::Render::Models::ShineColorOf;

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
} // namespace
