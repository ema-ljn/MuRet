#include "Scenes/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "World/IFishStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// El 0x10f con un personaje de dueño (AuraJoints.cpp): el alta 0x530edd..0x531214 y el paso 0x535d4f..0x5366a7. 06,
// "Los joints de las familias (CL11)".
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::JointOwnerKind;
using Client::Scenes::JointState;

constexpr std::int32_t Aura = 0x10f;

double D(float value)
{
    return static_cast<double>(value);
}

float F32(double value)
{
    return static_cast<float>(value);
}

struct Pool
{
    Pool()
    {
        actors[0].alive = true;
        actors[0].position = {1000, 2000, 300};
        cloth->logicSteps = 77;
        cloth->openCounterAccesses = 1; // sin los cuatro rand() del cierre.
    }

    Client::Core::Expected<std::optional<std::size_t>, std::string>
    Create(std::int32_t subtype, float width, std::int32_t target = -1, glm::vec3 to = {0, 0, 0})
    {
        glm::vec3 from = actors[0].position;
        glm::vec3 angles = startAngles;
        const Client::World::FishEffectRequest request{.type = Aura,
                                                       .firstPosition = from,
                                                       .secondPosition = to,
                                                       .angles = angles,
                                                       .subtype = subtype,
                                                       .ownerContext = actors.data(),
                                                       .scale = width,
                                                       .key = -1,
                                                       .skill = 0,
                                                       .skillSerial = 0,
                                                       .target = target};
        return Client::Scenes::CreateFaunaJoint(joints, request, cloth->random, JointOwnerKind::Actor);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Scenes::StepJoints(joints, cloth->random, cloth.get(), nullptr, 0, nullptr, nullptr, {}, {},
                                          actors);
    }

    std::vector<JointState> joints = std::vector<JointState>(4);
    std::vector<ActorState> actors = std::vector<ActorState>(3);
    std::unique_ptr<Client::Render::Models::ClothWorld> cloth = std::make_unique<Client::Render::Models::ClothWorld>();
    glm::vec3 startAngles{};
};

// 0x5363bf..0x536450 con la frecuencia entera, calculado aparte del código de MuRet (Fraction y Decimal de Python):
// FSIN y FCOS a 64 bits, los productos en PC53, el seno de z directo a float (0x53642f) y los spills a float. La fase
// −77 es la del lugar 0 con 77 pasos; 53.808, la del lugar 1.
glm::vec3 GoldenDirection(std::int32_t phase)
{
    if (phase == -77)
        return {std::bit_cast<float>(0x3f3ffc03u), std::bit_cast<float>(0xbf29561bu),
                std::bit_cast<float>(0xbbe42472u)};
    EXPECT_EQ(phase, 53808);
    return {std::bit_cast<float>(0xbe5d83c5u), std::bit_cast<float>(0xbf7610b0u), std::bit_cast<float>(0x3e2f529eu)};
}

// 0x536388..0x5364d2: el punto del lugar `index` alrededor de `target`, con la frecuencia entera.
glm::vec3 RingPoint(std::size_t index, std::int32_t steps, const glm::vec3& target, double lift)
{
    const auto slot = static_cast<std::uint32_t>(index);
    const auto phase =
        std::bit_cast<std::int32_t>(slot * 53731u + ((slot & 1u) != 0 ? std::bit_cast<std::uint32_t>(steps)
                                                                      : 0u - std::bit_cast<std::uint32_t>(steps)));
    const glm::vec3 direction = GoldenDirection(phase);
    return {F32(D(direction[0]) * 80 + D(target.x)), F32(D(direction[1]) * 80 + D(target.y)),
            F32(D(direction[2]) * 120 + D(target.z) + lift)};
}
} // namespace

// 0x530f05 y 0x536073: el 10f/0 nace con la luz entera, 999.999 de vida y 30 puntos, y gira alrededor del dueño, 110
// por encima de su +0x10 + 10. Al moverse el dueño, los tramos se corren con él (0x5360dc..0x536185).
TEST(AuraJointsTest, TheRingsCircleTheirOwner)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0, 50).HasValue());
    const JointState& ring = pool.joints[0];
    EXPECT_EQ(ring.texture, 0x62b);
    EXPECT_EQ(ring.life, 999999);
    EXPECT_EQ(ring.maximumCount, 30);
    EXPECT_EQ(ring.light, glm::vec3(1));
    EXPECT_EQ(ring.width, 50.0f);
    EXPECT_EQ(ring.targetPosition, pool.actors[0].position);
    ASSERT_TRUE(pool.Step().HasValue());
    glm::vec3 target = pool.actors[0].position;
    target.z = 310;
    EXPECT_EQ(ring.targetPosition, target);
    EXPECT_EQ(ring.position, RingPoint(0, 77, target, 110));
    EXPECT_EQ(ring.count, 1);
    EXPECT_EQ(ring.life, 999998);
    const glm::vec3 before = ring.points[0][0];
    pool.actors[0].position.x = 1100;
    ASSERT_TRUE(pool.Step().HasValue());
    // El viejo tramo 0 (ya corrido) es ahora el 1.
    EXPECT_EQ(ring.points[1][0].x, F32(D(F32(D(before.x) - 1000)) + 1100));
    // Muerto el dueño: vida en 0, sin dibujo, y la cola común le resta uno.
    pool.actors[0].alive = false;
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_FALSE(ring.alive);
    EXPECT_EQ(ring.life, -1);
}

// FaunaDirectionPC53 (0x53d1a0 con escala 1, el mismo bloque que 0x5363bf) da los valores dorados.
TEST(AuraJointsTest, TheDirectionMatchesTheGoldenValues)
{
    for (const std::int32_t phase : {-77, 53808})
    {
        SCOPED_TRACE(phase);
        const auto direction = Client::Render::Models::Detail::FaunaDirectionPC53(phase);
        ASSERT_TRUE(direction.has_value());
        const glm::vec3 golden = GoldenDirection(phase);
        for (int axis = 0; axis < 3; ++axis)
            EXPECT_EQ(std::bit_cast<std::uint32_t>((*direction)[static_cast<std::size_t>(axis)]),
                      std::bit_cast<std::uint32_t>(golden[axis]));
    }
}

// 0x536187..0x536383: sin otro acceso abierto al contador de pasos, cada anillo lo abre y lo cierra, y el cierre saca
// cuatro rand() (0x536383 → 0x412890), por anillo y por paso.
TEST(AuraJointsTest, EachRingClosesTheCounterWithFourRandoms)
{
    Pool pool;
    pool.cloth->openCounterAccesses = 0;
    ASSERT_TRUE(pool.Create(0, 50).HasValue());
    ASSERT_TRUE(pool.Create(0, 50).HasValue());
    CrtRandom expected = pool.cloth->random;

    ASSERT_TRUE(pool.Step().HasValue());

    for (int draw = 0; draw < 2 * 4; ++draw)
        static_cast<void>(expected.Next());
    EXPECT_EQ(pool.cloth->random.State(), expected.State());
    glm::vec3 target = pool.actors[0].position;
    target.z = 310;
    EXPECT_EQ(pool.joints[0].position, RingPoint(0, 77, target, 110));
    EXPECT_EQ(pool.joints[1].position, RingPoint(1, 77, target, 110));
    EXPECT_EQ(pool.cloth->openCounterAccesses, 0);
}

// 0x53120e y 0x536073..0x536452: un subtipo sin caso (0xa, que ningún llamador crea) nace sólo con el ancho y en su
// paso lee al dueño, pone el destino 10 más arriba, abre y cierra el acceso al contador (cuatro rand() sin otro
// abierto) y va a la cola, sin correr los tramos ni moverse.
TEST(AuraJointsTest, TheUnhandledSubtypesOnlyReadTheirOwner)
{
    Pool pool;
    pool.cloth->openCounterAccesses = 0;
    ASSERT_TRUE(pool.Create(0xa, 20).HasValue());
    JointState& joint = pool.joints[0];
    joint.life = 5; // el alta no la escribe.
    const glm::vec3 before = joint.position;
    CrtRandom expected = pool.cloth->random;

    ASSERT_TRUE(pool.Step().HasValue());

    for (int draw = 0; draw < 4; ++draw)
        static_cast<void>(expected.Next());
    EXPECT_EQ(pool.cloth->random.State(), expected.State());
    EXPECT_EQ(joint.targetPosition, glm::vec3(1000, 2000, 310));
    EXPECT_EQ(joint.position, before);
    EXPECT_EQ(joint.count, -1); // sin puntos: +0x6c quedó en 0 y la cuenta llega a −1 (0x535520).
    pool.actors[0].alive = false;
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_FALSE(joint.alive);
    EXPECT_EQ(joint.life, -1);
}

// 0x530f35 y 0x535d4f: el 10f/4 vive mientras su dueño tenga 8 (o 0x2000000) en +0x78, con la vida en 100; con el
// personaje de su +0xa0c sobre Fenrir fuera de la zona segura pasa a /9 (140 de alto) y vuelve a /4 al bajarse.
TEST(AuraJointsTest, TheBindingFollowsItsTargetOnFenrir)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(4, 20, 2).HasValue());
    JointState& binding = pool.joints[0];
    EXPECT_EQ(binding.target, 2);
    EXPECT_EQ(binding.life, 10000);
    EXPECT_EQ(binding.light, glm::vec3(std::bit_cast<float>(0x3ecccccdu), std::bit_cast<float>(0x3f4ccccdu),
                                       std::bit_cast<float>(0x3e4ccccdu)));
    pool.actors[0].flags78 = 8;
    pool.actors[2].pet.model = 0x1c37;
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_EQ(binding.subtype, 9);
    EXPECT_EQ(binding.life, 99);
    EXPECT_EQ(binding.position, RingPoint(0, 77, {1000, 2000, 310}, 140));
    pool.actors[2].safeZone = 1;
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_EQ(binding.subtype, 4);
    // Sin el bit, se apaga.
    pool.actors[0].flags78 = 0;
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_FALSE(binding.alive);
    // Otro /4 con el blanco −1 se queda con el +0xa0c del uso anterior del lugar.
    ASSERT_TRUE(pool.Create(4, 20).HasValue());
    EXPECT_EQ(pool.joints[0].target, 2);
    // Un blanco fuera de los personajes corta.
    pool.joints[0].target = 7;
    pool.actors[0].flags78 = 8;
    const auto cut = pool.Step();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x535d63"), std::string::npos) << cut.Error();
}

// 0x53106d y 0x536526: el 10f/5 nace sin el primer tramo, a 800 de su +0x24, y en cada paso da tres vueltas que lo
// frenan (0,95) y lo hacen caer; en la tanda 40 suelta el efecto 0x6eb (0x518cf0, otro lote) y corta. La /7 no.
TEST(AuraJointsTest, TheRiseFallsUntilItsEffect)
{
    Pool pool;
    pool.startAngles = {0, 0, 30};
    const glm::vec3 base{500, 600, 700};
    // 0x53115b va antes de 0x531166: el primer tramo usa el ancho del uso anterior del lugar.
    constexpr float PreviousWidth = 12.0f;
    pool.joints[0].width = PreviousWidth;
    ASSERT_TRUE(pool.Create(5, 25, -1, base).HasValue());
    const JointState& rise = pool.joints[0];
    for (const auto& [first, second] : {std::pair{0, 1}, std::pair{2, 3}})
    {
        const glm::vec3 across =
            rise.points[0][static_cast<std::size_t>(second)] - rise.points[0][static_cast<std::size_t>(first)];
        EXPECT_NEAR(std::sqrt(across.x * across.x + across.y * across.y + across.z * across.z), PreviousWidth, 1e-3);
    }
    EXPECT_EQ(rise.texture, 0x610);
    EXPECT_EQ(rise.drawEnabled, 1);
    EXPECT_FALSE(rise.tailEnabled);
    EXPECT_EQ(rise.life, 60);
    EXPECT_EQ(rise.light, glm::vec3(1, 1, std::bit_cast<float>(0x3f4ccccdu)));
    EXPECT_EQ(rise.count, 0);
    EXPECT_EQ(rise.width, 25.0f);
    const auto matrix = Client::Render::Models::OriginalAngleMatrix(pool.startAngles);
    const auto turned = Client::Render::Models::OriginalVectorRotate({0, 800, 0}, matrix);
    EXPECT_EQ(rise.position, glm::vec3(F32(D(turned.x) + 500), F32(D(turned.y) + 600), F32(D(turned.z) + 700)));
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_EQ(rise.angles.z, 60.0f);
    EXPECT_EQ(rise.count, 3);
    EXPECT_EQ(rise.width, 20.0f);
    Client::Core::Expected<void, std::string> stepped{};
    for (int step = 0; step < 60 && stepped.HasValue(); ++step)
        stepped = pool.Step();
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x536626"), std::string::npos) << stepped.Error();
    EXPECT_EQ(rise.volley, 40);

    Pool other;
    ASSERT_TRUE(other.Create(7, 25, -1, base).HasValue());
    for (int step = 0; step < 61; ++step)
        ASSERT_TRUE(other.Step().HasValue()) << step;
    EXPECT_FALSE(other.joints[0].alive);
    EXPECT_GT(other.joints[0].volley, 40);
}

// 0x53116e y 0x536005: el 10f/8 gira 25 en z alrededor de su +0x24, agrega la cola y sube 15.
TEST(AuraJointsTest, TheSpinTurnsAndRises)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(8, 30, -1, {10, 20, 30}).HasValue());
    const JointState& spin = pool.joints[0];
    EXPECT_EQ(spin.texture, 0x5ac);
    EXPECT_EQ(spin.life, 40);
    EXPECT_EQ(spin.light, glm::vec3(0.5f));
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_EQ(spin.angles.z, 25.0f);
    EXPECT_EQ(spin.pathIncrement.z, 45.0f);
    EXPECT_EQ(spin.count, 1);
    const auto matrix = Client::Render::Models::OriginalAngleMatrix({0, 0, 25});
    const auto turned = Client::Render::Models::OriginalVectorRotate({0, -40, 0}, matrix);
    EXPECT_EQ(spin.position, glm::vec3(F32(D(turned.x) + 10), F32(D(turned.y) + 20), F32(D(turned.z) + 30)));
}

// 0x530f7e y 0x535db2: el 10f/2 toma el segundo punto; sin dueño vivo lo suelta, y con él corta en 0x53d1a0.
TEST(AuraJointsTest, TheBeamCutsOnThePlayerBone)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(2, 10, -1, {7, 8, 9}).HasValue());
    EXPECT_EQ(pool.joints[0].targetPosition, glm::vec3(7, 8, 9));
    EXPECT_EQ(pool.joints[0].texture, 0x62c);
    EXPECT_EQ(pool.joints[0].maximumCount, 5);
    const auto cut = pool.Step();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x535e66"), std::string::npos) << cut.Error();
    pool.actors[0].alive = false;
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_EQ(pool.joints[0].ownerContext, nullptr);
}
