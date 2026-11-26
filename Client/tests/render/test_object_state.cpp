#include "Models/ObjectState.h"

#include "Math/RoundedMath.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float2.hpp>

#include <chrono>
#include <cstdint>
#include <ios>
#include <optional>

// What the object passes to RenderBody (+0x64, +0x68, +0x6c, +0x70 and +0x58), with the table from emulating 0x607b60
// on main.exe with T = 1000 and 2500 and rand() at 7 (06 § Lo que cuelga).
namespace
{
using Client::Math::RoundedSin;
using Client::Models::EveryMeshBlends;
using Client::Models::HangingObjectState;
using Client::Models::MaskedClassLightAt;
using Client::Models::ModelLightOf;
using Client::Models::NoBlendMesh;
using Client::Models::NoHiddenMesh;
using Client::Models::ObjectRandoms;
using Client::Models::ObjectState;

constexpr ObjectRandoms Sevens{7, 7};

// An emulation row: the type, +0x58, +0x64, +0x68 and the U and V.
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

// 0x607b60 at 1000 ms: the usual object (−1, −1, 1, 0, 0) and the types that change something, those a character
// reaches. Those of 0x60b3d0 by type (Wing05, Wing06 and Staff12) are separate.
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

// What pulses goes with the clock: the same emulation at 2500 ms.
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

// The rand() of 0x607b60: Sword15's U and the U and V of Staff06 and Shield15 go from 0 to 0.9 with rand() % 10, and
// Staff08's light from 0 to 1 with % 11.
TEST(ObjectStateTest, SomeTakeTheirUVOrTheirLightFromRand)
{
    constexpr auto Clock = std::chrono::milliseconds{0};
    const ObjectRandoms randoms{23, 46};

    EXPECT_FLOAT_EQ(HangingObjectState(0x220, Clock, randoms).uv.x, 3.0f * 0.1f);
    EXPECT_EQ(HangingObjectState(0xc17, Clock, randoms).uv, (glm::vec2{3.0f * 0.1f, 6.0f * 0.1f}));
    EXPECT_EQ(HangingObjectState(0xe20, Clock, randoms).uv, (glm::vec2{3.0f * 0.1f, 6.0f * 0.1f}));
    EXPECT_FLOAT_EQ(HangingObjectState(0xc19, Clock, randoms).light, 1.0f * 0.1f);
}

// 0x60b3d0 writes the light by object type before drawing: Wing05 at sin(0.001 · T) + 1.1, Wing06 at
// (sin(0.001 · T) + 1) · 0.25 and Staff12 at sin(0.004 · T) · 0.3 + 0.7.
TEST(ObjectStateTest, SomeTypesGetTheirLightFrom0x60b3d0)
{
    constexpr auto Clock = std::chrono::milliseconds{1000};
    const auto sine = static_cast<float>(RoundedSin(1000.0 * static_cast<double>(0.001f)));

    EXPECT_NEAR(HangingObjectState(0x1a16, Clock, Sevens).light, sine + 1.1f, 1e-6f);
    EXPECT_NEAR(HangingObjectState(0x1a17, Clock, Sevens).light, (sine + 1.0f) * 0.25f, 1e-6f);
    EXPECT_NEAR(HangingObjectState(0xc1d, Clock, Sevens).light, 0.47296f, 1e-5f);
    EXPECT_EQ(HangingObjectState(0x1a16, Clock, Sevens).blendMesh, NoBlendMesh);
}

// −2 is "every mesh" for 0x4ecc30 (below −1); −1, none.
TEST(ObjectStateTest, TheBlendMeshHasTwoSpecialValues)
{
    EXPECT_EQ(EveryMeshBlends, -2);
    EXPECT_EQ(NoBlendMesh, -1);
    EXPECT_EQ(NoHiddenMesh, -1);
    EXPECT_EQ(ObjectState{}, (ObjectState{NoBlendMesh, 1.0f, {}, NoHiddenMesh}));
}

// 0x60b3d0 also writes the light of the character's object by the part's model: the FShoots_R of PantMale25 and
// HelmMale25 pulse at sin(0.001 · T) · 0.4 + 0.6, and the Test20 ones set it back to 1. The rest leave it alone.
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

// The class 4 one, on the helmet part (0x50ba30): sin(0.001 · T) · 0.1 + 0.7.
TEST(ObjectStateTest, TheMaskedClassLightPulses)
{
    EXPECT_FLOAT_EQ(MaskedClassLightAt(std::chrono::milliseconds{250}), RoundedSin(0.25f) * 0.1f + 0.7f);
    EXPECT_FLOAT_EQ(MaskedClassLightAt(std::chrono::milliseconds{0}), 0.7f);
}
} // namespace
