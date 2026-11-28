#include "Joints/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Math/OriginalRetainedMath.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// El 0x60f/4 y /0xc (HelixJoints.cpp) que el 0x812 suelta en cada paso (0x52c9da y 0x52c9a7, con él de dueño): el alta
// de 0x533faf..0x5340d7 y el paso de 0x53b08a..0x53b14c, que se repite en el mismo cuadro hasta que la vida que queda
// es múltiplo de 12 (0x53c3c6). El 0x62b/4 y /0xc pasan por los mismos.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::JointOwnerKind;
using Client::Kernel::JointState;
using Client::Math::Detail::CosineDoubleProductPC53;
using Client::Math::Detail::SineDoubleProductPC53;

constexpr std::uint32_t Seed = 0x60fu;
constexpr float Tenth = std::bit_cast<float>(0x3dcccccdu);           // 0x6b87b8
constexpr float NegativeRadians = std::bit_cast<float>(0xbc8efa35u); // 0x6ba2c4

using Client::Math::D;
using Client::Math::F32;

struct Helix
{
    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(std::int32_t type, std::int32_t subtype,
                                                                           bool owned = true)
    {
        glm::vec3 from = start;
        glm::vec3 to = target;
        glm::vec3 turn{0.0f, 0.0f, facing};
        const Client::Kernel::JointRequest request{.type = type,
                                                   .firstPosition = from,
                                                   .secondPosition = to,
                                                   .angles = turn,
                                                   .subtype = subtype,
                                                   .ownerContext = owned ? &owner : nullptr,
                                                   .scale = subtype == 0xc ? 100.0f : 50.0f,
                                                   .key = -1,
                                                   .skill = 0,
                                                   .skillSerial = 0,
                                                   .target = -1};
        return Client::Joints::CreateFaunaJoint(joints, request, random,
                                                owned ? JointOwnerKind::ModelEffect : JointOwnerKind::ByType);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Joints::StepJoints(joints, random, nullptr, nullptr, 0.0f, nullptr);
    }

    std::vector<JointState> joints = std::vector<JointState>(2);
    CrtRandom random{Seed};
    Client::Effects::ModelEffect owner{};
    glm::vec3 start{1000.0f, 2000.0f, 300.0f};
    glm::vec3 target{1100.0f, 2100.0f, 320.0f};
    float facing = 30.0f;
};

float Bits(std::uint32_t bits)
{
    return std::bit_cast<float>(bits);
}

glm::vec3 Bits(std::uint32_t x, std::uint32_t y, std::uint32_t z)
{
    return {Bits(x), Bits(y), Bits(z)};
}

} // namespace

// 0x533677..0x53368e y 0x533faf..0x5340d7: el ancho de la escala, el destino en el segundo punto y vx en rand() % 360
// (un rand()); vy y vz, el seno y el coseno de −z en radianes por −4 (la /0xc) o −2 (la /4). La /0xc con vida 70, la
// luz (0,1f; 0,1f; 1) y 50 puntos; la /4 con vida 110, la luz del dueño (+0xf4) y 200 puntos que el tope de 0x534cb6
// deja en 50.
TEST(HelixJointsTest, EachHelixIsBornAroundItsTarget)
{
    for (const std::int32_t type : {0x60f, 0x62b})
    {
        for (const std::int32_t subtype : {4, 0xc})
        {
            SCOPED_TRACE(type);
            SCOPED_TRACE(subtype);
            Helix helix;
            helix.owner.light = {0.25f, 0.5f, 0.75f};
            const auto created = helix.Create(type, subtype);
            ASSERT_TRUE(created.HasValue()) << created.Error();
            ASSERT_EQ(created.Value(), std::optional<std::size_t>{0});
            CrtRandom expected{Seed};
            const int turn = expected.Next() % 360;
            EXPECT_EQ(helix.random.State(), expected.State());
            const JointState& joint = helix.joints[0];
            const bool wide = subtype == 0xc;
            const double reach = wide ? -4.0 : -2.0;
            const double argument = D(30.0f) * D(NegativeRadians);
            EXPECT_EQ(joint.width, wide ? 100.0f : 50.0f);
            EXPECT_EQ(joint.life, wide ? 70 : 110);
            EXPECT_EQ(joint.maximumCount, 50);
            EXPECT_EQ(joint.light, wide ? glm::vec3(Tenth, Tenth, 1.0f) : glm::vec3(0.25f, 0.5f, 0.75f));
            EXPECT_EQ(joint.velocity.x, static_cast<float>(turn));
            EXPECT_EQ(joint.velocity.y, F32(*SineDoubleProductPC53(argument, reach)));
            EXPECT_EQ(joint.velocity.z, F32(*CosineDoubleProductPC53(argument, reach)));
            EXPECT_EQ(joint.targetPosition, helix.target);
            EXPECT_EQ(joint.ownerContext, &helix.owner);
        }
    }
}

// 0x534070: la /4 lee la luz de su dueño (+0xf4); sin dueño, el original desreferencia el puntero nulo y revienta, y
// MuRet corta antes del rand(). Con la tabla llena, el alta no hace nada ni saca rand().
TEST(HelixJointsTest, TheNarrowHelixNeedsItsOwnerAndAFreePlace)
{
    Helix lonely;
    const auto cut = lonely.Create(0x60f, 4, false);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x534070"), std::string::npos);

    Helix full;
    for (JointState& joint : full.joints)
        joint.alive = true;
    for (const std::int32_t subtype : {4, 0xc})
    {
        const auto created = full.Create(0x60f, subtype);
        ASSERT_TRUE(created.HasValue()) << created.Error();
        EXPECT_FALSE(created.Value().has_value());
    }
    EXPECT_EQ(full.random.State(), CrtRandom{Seed}.State());
}

// 0x53c393 resta la vida y agrega el tramo, y 0x53c3c6 repite el paso mientras la vida que queda no sea múltiplo de 12.
// Con vida 110 son dos vueltas (110 y 109); con 108, doce (108..97). Sin rand().
TEST(HelixJointsTest, TheHelixTurnsUntilItsLifeIsAMultipleOfTwelve)
{
    for (const std::int32_t subtype : {4, 0xc})
    {
        SCOPED_TRACE(subtype);
        Helix helix;
        ASSERT_TRUE(helix.Create(0x60f, subtype).HasValue());
        JointState& joint = helix.joints[0];
        joint.life = 110;
        for (const int turns : {2, 12})
        {
            SCOPED_TRACE(turns);
            const std::int32_t life = joint.life;
            const auto state = helix.random.State();
            const std::int32_t count = joint.count;

            const auto stepped = helix.Step();
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();

            EXPECT_EQ(joint.life, life - turns);
            EXPECT_EQ(joint.life % 12, 0);
            EXPECT_EQ(joint.count, count + turns);
            EXPECT_EQ(helix.random.State(), state);
        }
    }
}

// Los bits de una corrida aparte de 0x53b08a..0x53b14c, 0x53b318..0x53b363 y 0x53c3c6 con la x87 de 53 bits (FSIN y
// FCOS con el giro sin redondear o guardado según el ASM), desde el segundo punto (1100,5; 2100,25; 320,125) y la
// luz del dueño (0,25; 0,5; 0,75). La /4 con el radio (vida + 40, no menos de 10) · 0,65f: con 0x7ffffff0 la suma da
// la vuelta y el radio queda en 10. La 0x62b/4 con 8 de vida se apaga en cada una de sus ocho vueltas.
TEST(HelixJointsTest, TheHelixLandsOnTheBitsOfTheOriginal)
{
    struct Case
    {
        std::int32_t type;
        std::int32_t subtype;
        std::int32_t life;
        glm::vec3 velocity;
        float facing;
        std::int32_t turns;
        glm::vec3 position;
        glm::vec3 target;
        glm::vec3 light;
    };
    const glm::vec3 owned{0.25f, 0.5f, 0.75f};
    const glm::vec3 dim{Tenth, Tenth, 1.0f};
    const std::array<Case, 5> cases{{
        {0x60f,
         4,
         110,
         {123.0f, Bits(0x3f9e3779u), Bits(0xbfb504f3u)},
         30.0f,
         2,
         Bits(0x448d957du, 0x45042918u, 0x43658cc6u),
         Bits(0x4489df1cu, 0x450316beu, 0x43a01000u),
         owned},
        {0x60f,
         0xc,
         70,
         {359.0f, Bits(0x40400001u), Bits(0xc0000003u)},
         77.7f,
         10,
         Bits(0x448d96e5u, 0x4502a693u, 0x438fcc37u),
         Bits(0x448d5000u, 0x45020400u, 0x43a01000u),
         dim},
        {0x62b,
         4,
         8,
         {5.0f, 1.5f, -2.25f},
         200.0f,
         8,
         Bits(0x448da566u, 0x45029c5du, 0x43a7961cu),
         Bits(0x448b1000u, 0x45022400u, 0x43a01000u),
         Bits(0x3cfb1033u, 0x3d7b1033u, 0x3dbc4c26u)},
        {0x62b,
         0xc,
         13,
         {200.0f, 0.1f, 0.2f},
         -45.0f,
         1,
         Bits(0x448b5881u, 0x4502648cu, 0x439ccc8au),
         Bits(0x44899333u, 0x45034733u, 0x43a01000u),
         dim},
        {0x60f,
         4,
         0x7ffffff0,
         {1.0f, 2.0f, 3.0f},
         10.0f,
         4,
         Bits(0x448ac097u, 0x45040849u, 0x43a33841u),
         Bits(0x448a9000u, 0x45040400u, 0x43a01000u),
         owned},
    }};
    for (const Case& tested : cases)
    {
        SCOPED_TRACE(tested.type);
        SCOPED_TRACE(tested.subtype);
        SCOPED_TRACE(tested.life);
        Helix helix;
        helix.owner.light = owned;
        helix.target = {1100.5f, 2100.25f, 320.125f};
        helix.facing = tested.facing;
        ASSERT_TRUE(helix.Create(tested.type, tested.subtype).HasValue());
        JointState& joint = helix.joints[0];
        joint.life = tested.life;
        joint.velocity = tested.velocity;
        const std::int32_t count = joint.count;

        const auto stepped = helix.Step();
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();

        EXPECT_EQ(joint.life, tested.life - tested.turns);
        EXPECT_EQ(joint.count, count + tested.turns);
        EXPECT_EQ(joint.position, tested.position);
        EXPECT_EQ(joint.targetPosition, tested.target);
        EXPECT_EQ(joint.light, tested.light);
        EXPECT_TRUE(joint.alive);
    }
}

// 0x53c3bd: con 9 de vida, el cuadro da nueve vueltas hasta 0; el siguiente da una y el joint muere.
TEST(HelixJointsTest, TheHelixDiesAfterItsLastTurn)
{
    Helix helix;
    ASSERT_TRUE(helix.Create(0x60f, 4).HasValue());
    JointState& joint = helix.joints[0];
    joint.life = 9;

    ASSERT_TRUE(helix.Step().HasValue());

    EXPECT_EQ(joint.life, 0);
    EXPECT_TRUE(joint.alive);

    ASSERT_TRUE(helix.Step().HasValue());

    EXPECT_FALSE(joint.alive);
}
