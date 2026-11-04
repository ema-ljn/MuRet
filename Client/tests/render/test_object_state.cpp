#include "Render/Models/ObjectState.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float2.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <ios>
#include <optional>

// Client::Render::Models::HangingObjectState, ModelLightOf y MaskedClassLightAt: lo que el objeto le
// pasa a RenderBody (+0x64, +0x68, +0x6c, +0x70 y +0x58), con la tabla que sale de emular 0x607b60
// sobre el main.exe con T = 1000 y 2500 y rand() en 7 (06 § Lo que cuelga).
namespace
{
using Client::Render::Models::EveryMeshBlends;
using Client::Render::Models::HangingObjectState;
using Client::Render::Models::MaskedClassLightAt;
using Client::Render::Models::ModelLightOf;
using Client::Render::Models::NoBlendMesh;
using Client::Render::Models::NoHiddenMesh;
using Client::Render::Models::ObjectRandoms;
using Client::Render::Models::ObjectState;

constexpr ObjectRandoms Sevens{7, 7};

// Una fila de la emulación: el tipo, el +0x58, el +0x64, el +0x68 y la U y la V.
struct Emulated
{
    std::uint16_t type;
    std::int16_t hidden;
    std::int16_t blend;
    float light;
    float u;
    float v;
};

void ExpectEmulated(const Emulated& row, int milliseconds)
{
    constexpr float Rounding = 1e-5f;
    const ObjectState state = HangingObjectState(row.type, std::chrono::milliseconds{milliseconds}, Sevens);
    EXPECT_EQ(state.hiddenMesh, row.hidden) << std::hex << row.type;
    EXPECT_EQ(state.blendMesh, row.blend) << std::hex << row.type;
    EXPECT_NEAR(state.light, row.light, Rounding) << std::hex << row.type;
    EXPECT_NEAR(state.uv.x, row.u, Rounding) << std::hex << row.type;
    EXPECT_NEAR(state.uv.y, row.v, Rounding) << std::hex << row.type;
}

// 0x607b60 a los 1000 ms: el objeto de siempre (−1, −1, 1, 0, 0) y los tipos que cambian algo, con
// los que alcanza un personaje. Los de 0x60b3d0 por tipo (Wing05, Wing06 y Staff12) van aparte.
TEST(ObjectStateTest, WhatHangsHasTheObjectOf0x607b60)
{
    for (const Emulated& row : {
             Emulated{0x212, -1, -1, 1.0f, 0.0f, 0.0f},     Emulated{0x217, -1, 1, 1.0f, 0.0f, 0.0f},
             Emulated{0x21c, -1, 1, 0.47296f, 0.0f, 0.0f},  Emulated{0x21f, -1, 1, 1.0f, 0.0f, 0.0f},
             Emulated{0x220, -1, 1, 0.47296f, 0.7f, 0.0f},  Emulated{0x231, 2, -1, 1.0f, 0.0f, 0.0f},
             Emulated{0x616, -1, 1, 0.64864f, 0.0f, 0.0f},  Emulated{0x617, -1, 0, 1.0f, 0.0f, 0.0f},
             Emulated{0x618, -1, 1, 0.47296f, 0.0f, 0.0f},  Emulated{0x619, 2, -1, 1.0f, 0.0f, 0.0f},
             Emulated{0x812, -1, 1, 0.47296f, 0.0f, 0.0f},  Emulated{0x81b, -1, -1, 1.0f, 0.0f, -0.5f},
             Emulated{0x81c, 1, -1, 1.0f, 0.0f, 0.0f},      Emulated{0xa18, -1, -2, 0.47296f, 0.0f, 0.0f},
             Emulated{0xa1f, -1, -2, 0.47296f, 0.0f, 0.0f}, Emulated{0xa20, -1, -2, 0.47296f, 0.0f, 0.0f},
             Emulated{0xa22, -1, -2, 0.74864f, 0.0f, 0.0f}, Emulated{0xc12, -1, 2, 1.0f, 0.0f, 0.0f},
             Emulated{0xc17, -1, 2, 1.0f, 0.7f, 0.7f},      Emulated{0xc18, -1, -2, 0.47296f, 0.0f, 0.0f},
             Emulated{0xc19, -1, 1, 0.7f, 0.0f, 0.0f},      Emulated{0xc1a, -1, -2, 0.74864f, 0.0f, 0.0f},
             Emulated{0xc1b, -1, 1, 1.0f, 0.0f, 0.0f},      Emulated{0xc1d, -1, 2, 0.47296f, 0.0f, 0.0f},
             Emulated{0xe1d, -1, 1, 0.47296f, 0.0f, 0.0f},  Emulated{0xe1e, -1, 1, 0.47296f, 0.0f, 0.0f},
             Emulated{0xe1f, -1, 1, 0.47296f, 0.0f, 0.0f},  Emulated{0xe20, -1, 1, 1.0f, 0.7f, 0.7f},
             Emulated{0xe22, 2, -1, 1.0f, 0.0f, 0.0f},      Emulated{0x1a12, -1, 0, 1.0f, 0.0f, 0.0f},
             Emulated{0x1a15, -1, 0, 1.0f, 0.0f, 0.0f},     Emulated{0x1c12, -1, 1, 1.0f, 0.0f, 0.0f},
         })
    {
        ExpectEmulated(row, 1000);
    }
}

// Lo que late va con el reloj: la misma emulación a los 2500 ms.
TEST(ObjectStateTest, ItsLightAndItsVGoWithTheClock)
{
    for (const Emulated& row : {
             Emulated{0x21c, -1, 1, 0.53679f, 0.0f, 0.0f},
             Emulated{0x616, -1, 1, 0.6912f, 0.0f, 0.0f},
             Emulated{0x81b, -1, -1, 1.0f, 0.0f, -0.25f},
             Emulated{0xa22, -1, -2, 0.7912f, 0.0f, 0.0f},
         })
    {
        ExpectEmulated(row, 2500);
    }
}

// Los rand() de 0x607b60: la U de Sword15, la U y la V de Staff06 y Shield15 van de 0 a 0,9 con el
// rand() % 10, y la luz de Staff08 de 0 a 1 con el % 11.
TEST(ObjectStateTest, SomeTakeTheirUVOrTheirLightFromRand)
{
    constexpr auto Clock = std::chrono::milliseconds{0};
    const ObjectRandoms randoms{23, 46};

    EXPECT_FLOAT_EQ(HangingObjectState(0x220, Clock, randoms).uv.x, 3.0f * 0.1f);
    EXPECT_EQ(HangingObjectState(0xc17, Clock, randoms).uv, (glm::vec2{3.0f * 0.1f, 6.0f * 0.1f}));
    EXPECT_EQ(HangingObjectState(0xe20, Clock, randoms).uv, (glm::vec2{3.0f * 0.1f, 6.0f * 0.1f}));
    EXPECT_FLOAT_EQ(HangingObjectState(0xc19, Clock, randoms).light, 1.0f * 0.1f);
}

// 0x60b3d0 escribe la luz por el tipo del objeto antes de dibujar: Wing05 en sin(0,001 · T) + 1,1,
// Wing06 en (sin(0,001 · T) + 1) · 0,25 y Staff12 en sin(0,004 · T) · 0,3 + 0,7.
TEST(ObjectStateTest, SomeTypesGetTheirLightFrom0x60b3d0)
{
    constexpr auto Clock = std::chrono::milliseconds{1000};
    const auto sine = static_cast<float>(std::sin(1000.0 * static_cast<double>(0.001f)));

    EXPECT_NEAR(HangingObjectState(0x1a16, Clock, Sevens).light, sine + 1.1f, 1e-6f);
    EXPECT_NEAR(HangingObjectState(0x1a17, Clock, Sevens).light, (sine + 1.0f) * 0.25f, 1e-6f);
    EXPECT_NEAR(HangingObjectState(0xc1d, Clock, Sevens).light, 0.47296f, 1e-5f);
    EXPECT_EQ(HangingObjectState(0x1a16, Clock, Sevens).blendMesh, NoBlendMesh);
}

// −2 es "todas las mallas" para 0x4ecc30 (menor que −1); −1, ninguna.
TEST(ObjectStateTest, TheBlendMeshHasTwoSpecialValues)
{
    EXPECT_EQ(EveryMeshBlends, -2);
    EXPECT_EQ(NoBlendMesh, -1);
    EXPECT_EQ(NoHiddenMesh, -1);
    EXPECT_EQ(ObjectState{}, (ObjectState{NoBlendMesh, 1.0f, {}, NoHiddenMesh}));
}

// 0x60b3d0 escribe también la luz del objeto del personaje con el modelo de la parte: los FShoots_R
// de PantMale25 y HelmMale25 laten en sin(0,001 · T) · 0,4 + 0,6, y las Test20 la vuelven a 1. Las
// demás no la tocan.
TEST(ObjectStateTest, SomePartsWriteTheLightOfTheCharacter)
{
    constexpr auto Clock = std::chrono::milliseconds{1000};

    for (const std::uint16_t model : {std::uint16_t{0x142a}, std::uint16_t{0x102a}})
    {
        const std::optional<float> light = ModelLightOf(model, Clock);
        ASSERT_TRUE(light.has_value()) << std::hex << model;
        EXPECT_NEAR(*light, 0.93659f, 1e-5f) << std::hex << model;
    }
    for (const std::uint16_t model : {std::uint16_t{0x1025}, std::uint16_t{0x1625}, std::uint16_t{0x1825}})
    {
        EXPECT_EQ(ModelLightOf(model, Clock), std::optional<float>{1.0f}) << std::hex << model;
    }
    EXPECT_EQ(ModelLightOf(0x1425, Clock), std::nullopt);
    EXPECT_EQ(ModelLightOf(0x1a16, Clock), std::nullopt);
}

// La de la clase 4 en la parte del casco (0x50ba30): sin(0,001 · T) · 0,1 + 0,7.
TEST(ObjectStateTest, TheMaskedClassLightPulses)
{
    EXPECT_FLOAT_EQ(MaskedClassLightAt(std::chrono::milliseconds{250}), std::sin(0.25f) * 0.1f + 0.7f);
    EXPECT_FLOAT_EQ(MaskedClassLightAt(std::chrono::milliseconds{0}), 0.7f);
}
} // namespace
