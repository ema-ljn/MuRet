#include "Scenes/ActorRemains.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "World/GroundMarks.h"
#include "World/ModelEffects.h"
#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

// Scenes::LeaveTheRemains, 0x52e810 (CL9 hito 3, fase 7): el hueso +0x54 del modelo del tipo, los diez pedazos de
// hielo del 0x161, los tres que no dejan nada y las dos marcas de sangre 0x5e3 de los demás, con sus rand() en orden.
namespace
{
using Client::Render::Models::BoneTransform;
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorRemainsContext;
using Client::Scenes::ActorState;
using Client::Scenes::LeaveTheRemains;

constexpr std::uint32_t Seed = 0x13579bdfu;
// Dos marcas: x, y, el lado, el giro y la vida de 0x548fe0 con lugar.
constexpr int BloodDraws = 2 * 5;
constexpr std::int32_t BloodMark = 0x5e3;
constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));

// Lo que 0x52e810 lee y escribe además del personaje, en el heap (las 200 entradas de 0x518cf0 y las 100 marcas).
struct Remains
{
    Client::Render::Models::Skeleton skeleton;
    Client::World::ModelEffects effects;
    Client::World::GroundMarkTable marks{};
    CrtRandom random{Seed};

    [[nodiscard]] ActorRemainsContext Context(float scale = 0.0f, const glm::vec3& origin = {})
    {
        return ActorRemainsContext{.skeleton = &skeleton,
                                   .modelScale = scale,
                                   .modelOrigin = origin,
                                   .effects = effects,
                                   .marks = marks,
                                   .random = random};
    }

    [[nodiscard]] std::size_t LiveEffects() const
    {
        std::size_t live = 0;
        for (const auto& effect : effects.Effects())
            live += effect.alive ? 1 : 0;
        return live;
    }

    [[nodiscard]] std::size_t LiveMarks() const
    {
        std::size_t live = 0;
        for (const auto& mark : marks)
            live += mark.live ? 1 : 0;
        return live;
    }
};

ActorState Dead(std::uint16_t type)
{
    ActorState actor;
    actor.alive = true;
    actor.type = type;
    actor.position = {12345.0f, 6789.0f, 150.0f};
    actor.angles = {0.0f, 0.0f, 45.0f};
    actor.light = {0.5f, 0.6f, 0.7f};
    return actor;
}

// Lo que 0x52e810 saca para una marca, en su orden, y la vida de 0x548fe0.
struct DrawnMark
{
    int x;
    int y;
    float size;
    float angle;
    std::int32_t life;
};

DrawnMark DrawMark(CrtRandom& draws)
{
    DrawnMark drawn{};
    drawn.x = draws.Next() % 100 - 50;
    drawn.y = draws.Next() % 100 - 50;
    drawn.size = static_cast<float>(static_cast<double>((draws.Next() & 3) + 8) * Tenth);
    drawn.angle = static_cast<float>(draws.Next() % 360);
    drawn.life = draws.Next() % 32 + 50;
    return drawn;
}

// 0x52e834..0x52e83b: con el +0x54 del modelo en −1 no hace nada, ni con el 0x161, y no saca rand().
TEST(ActorRemainsTest, AModelWithoutTheHeadBoneLeavesNothing)
{
    for (const std::uint16_t type : {std::uint16_t{0x161}, std::uint16_t{0x153}, std::uint16_t{0x20a}})
    {
        SCOPED_TRACE(type);
        auto remains = std::make_unique<Remains>();
        ActorState actor = Dead(type);
        ASSERT_TRUE(LeaveTheRemains(actor, remains->Context()).HasValue());
        EXPECT_TRUE(actor.alive);
        EXPECT_EQ(remains->LiveEffects(), 0u);
        EXPECT_EQ(remains->LiveMarks(), 0u);
        EXPECT_EQ(remains->random.State(), Seed);
    }
}

// 0x52e841..0x52e889: el monstruo de hielo de Devias (0x161, Monster16) apaga su byte vivo y suelta diez pedazos 0xca
// en su posición, con sus ángulos y su luz, cinco rand() cada uno (ModelEffects::CreateIceShard); sin marcas.
TEST(ActorRemainsTest, TheIceMonsterOfDeviasBreaksIntoTenIceShards)
{
    auto remains = std::make_unique<Remains>();
    remains->skeleton.headBone = 0x13;
    ActorState actor = Dead(0x161);
    ASSERT_TRUE(LeaveTheRemains(actor, remains->Context(1.0f, {1.0f, 2.0f, 3.0f})).HasValue());
    EXPECT_FALSE(actor.alive);
    EXPECT_EQ(remains->LiveEffects(), 10u);
    EXPECT_EQ(remains->LiveMarks(), 0u);
    auto expected = std::make_unique<Client::World::ModelEffects>();
    CrtRandom draws(Seed);
    for (int shard = 0; shard < 10; ++shard)
        ASSERT_TRUE(expected->CreateIceShard(actor.position, actor.angles, actor.light, draws).has_value());
    EXPECT_EQ(remains->random.State(), draws.State());
    EXPECT_EQ(draws.State(), Client::Tests::AfterDraws(Seed, 50).State());
    for (std::size_t slot = 0; slot < 10; ++slot)
    {
        SCOPED_TRACE(slot);
        const auto& shard = remains->effects.Effects()[slot];
        const auto& wanted = expected->Effects()[slot];
        EXPECT_EQ(shard.type, Client::World::IceShardEffect);
        EXPECT_EQ(shard.subtype, 0);
        EXPECT_EQ(shard.transform.position, (glm::vec3{12345.0f, 6789.0f, 200.0f}));
        EXPECT_EQ(shard.light, actor.light);
        EXPECT_EQ(shard.life, wanted.life);
        EXPECT_EQ(shard.transform.scale, wanted.transform.scale);
        EXPECT_EQ(shard.transform.angles, wanted.transform.angles);
        EXPECT_EQ(shard.velocity, wanted.velocity);
        EXPECT_EQ(shard.turnDirection, wanted.turnDirection);
    }
}

// 0x52e88a..0x52e8a5: el 0x159, el 0x160 y el 0x164 no dejan nada, sin rand().
TEST(ActorRemainsTest, ThreeMonstersLeaveNothing)
{
    for (const std::uint16_t type : {std::uint16_t{0x159}, std::uint16_t{0x160}, std::uint16_t{0x164}})
    {
        SCOPED_TRACE(type);
        auto remains = std::make_unique<Remains>();
        remains->skeleton.headBone = 0x14;
        ActorState actor = Dead(type);
        ASSERT_TRUE(LeaveTheRemains(actor, remains->Context()).HasValue());
        EXPECT_TRUE(actor.alive);
        EXPECT_EQ(remains->LiveEffects(), 0u);
        EXPECT_EQ(remains->LiveMarks(), 0u);
        EXPECT_EQ(remains->random.State(), Seed);
    }
}

// 0x52e8ab..0x52e975: dos veces, el corrimiento (rand() % 100 − 50, rand() % 100 − 50, 0) desde el hueso +0x54 de los
// huesos propios, por la escala y más la posición del modelo compartido (0x4ec110), el lado ((rand() & 3) + 8) · 0,1f,
// el giro rand() % 360 y la marca 0x5e3 con la luz del personaje; 0x548fe0 le sortea la vida. El jugador de Lorencia
// con su hueso 20 posado.
TEST(ActorRemainsTest, ThePlayerLeavesTwoBloodMarksAroundItsHeadBone)
{
    auto remains = std::make_unique<Remains>();
    remains->skeleton.headBone = 20;
    ActorState actor = Dead(0x20a);
    actor.bodyPose.assign(21, BoneTransform(1.0f));
    actor.bodyPose[20][3] = {10.0f, 20.0f, 30.0f};
    const glm::vec3 origin{13000.0f, 13500.0f, 100.0f};
    ASSERT_TRUE(LeaveTheRemains(actor, remains->Context(1.25f, origin)).HasValue());
    EXPECT_TRUE(actor.alive);
    EXPECT_EQ(remains->LiveEffects(), 0u);
    EXPECT_EQ(remains->LiveMarks(), 2u);
    CrtRandom draws(Seed);
    for (std::size_t slot = 0; slot < 2; ++slot)
    {
        SCOPED_TRACE(slot);
        const DrawnMark drawn = DrawMark(draws);
        const auto& mark = remains->marks[slot];
        EXPECT_EQ(mark.type, BloodMark);
        EXPECT_EQ(mark.position,
                  (glm::vec3{(static_cast<float>(drawn.x) + 10.0f) * 1.25f + origin.x,
                             (static_cast<float>(drawn.y) + 20.0f) * 1.25f + origin.y, 30.0f * 1.25f + origin.z}));
        EXPECT_EQ(mark.size, drawn.size);
        EXPECT_EQ(mark.angle, drawn.angle);
        EXPECT_EQ(mark.color, actor.light);
        EXPECT_EQ(mark.alpha, 1.0f);
        EXPECT_EQ(mark.life, drawn.life);
    }
    EXPECT_EQ(remains->random.State(), draws.State());
    EXPECT_EQ(draws.State(), Client::Tests::AfterDraws(Seed, BloodDraws).State());
}

// Un monstruo de Devias sin huesos propios (su dibujo no está portado, o antes del primero): la matriz en cero
// (Q-C318), y las dos marcas caen en la posición del modelo compartido, con los mismos rand().
TEST(ActorRemainsTest, AMonsterWithoutItsBonesBleedsOnItsModelPlacement)
{
    auto remains = std::make_unique<Remains>();
    remains->skeleton.headBone = 0x10;
    ActorState actor = Dead(0x15a);
    const glm::vec3 origin{22050.0f, 3050.0f, 120.0f};
    ASSERT_TRUE(LeaveTheRemains(actor, remains->Context(1.1f, origin)).HasValue());
    ASSERT_EQ(remains->LiveMarks(), 2u);
    CrtRandom draws(Seed);
    for (std::size_t slot = 0; slot < 2; ++slot)
    {
        const DrawnMark drawn = DrawMark(draws);
        EXPECT_EQ(remains->marks[slot].position, origin);
        EXPECT_EQ(remains->marks[slot].size, drawn.size);
        EXPECT_EQ(remains->marks[slot].angle, drawn.angle);
    }
    EXPECT_EQ(remains->random.State(), draws.State());
}

// Con la tabla de las marcas llena, 0x548fe0 apaga la de menos vida sin poner la nueva ni sacar la vida (Q-C231): la
// segunda vuelta de 0x52e810 encuentra ese lugar apagado y la pone ahí, con la vida.
TEST(ActorRemainsTest, AFullTableOfMarksMakesRoomForTheSecondOne)
{
    auto remains = std::make_unique<Remains>();
    remains->skeleton.headBone = 5;
    for (auto& mark : remains->marks)
        mark = Client::World::GroundMark{.live = true, .type = BloodMark, .alpha = 1.0f, .life = 90};
    remains->marks[40].life = 55;
    ActorState actor = Dead(0x153);
    ASSERT_TRUE(LeaveTheRemains(actor, remains->Context(1.0f, {500.0f, 600.0f, 0.0f})).HasValue());
    CrtRandom draws(Seed);
    for (int draw = 0; draw < 4; ++draw)
        static_cast<void>(draws.Next());
    const DrawnMark second = DrawMark(draws);
    const auto& mark = remains->marks[40];
    EXPECT_TRUE(mark.live);
    EXPECT_EQ(mark.position, (glm::vec3{500.0f, 600.0f, 0.0f}));
    EXPECT_EQ(mark.size, second.size);
    EXPECT_EQ(mark.angle, second.angle);
    EXPECT_EQ(mark.life, second.life);
    EXPECT_EQ(remains->LiveMarks(), Client::World::GroundMarkCount);
    EXPECT_EQ(remains->random.State(), Client::Tests::AfterDraws(Seed, 9).State());
}

// Los cortes: sin el modelo cargado (0x52e834) y con un hueso +0x54 fuera de los huesos propios ya posados (0x4ec110
// leería otra memoria), antes de cualquier rand().
TEST(ActorRemainsTest, WithoutTheModelOrWithTheBoneOutsideTheOwnBonesItCuts)
{
    auto remains = std::make_unique<Remains>();
    ActorState actor = Dead(0x153);
    ActorRemainsContext missing = remains->Context();
    missing.skeleton = nullptr;
    const auto unloaded = LeaveTheRemains(actor, missing);
    ASSERT_FALSE(unloaded.HasValue());
    EXPECT_NE(unloaded.Error().find("0x52e834"), std::string::npos) << unloaded.Error();

    remains->skeleton.headBone = 20;
    actor.bodyPose.assign(5, BoneTransform(1.0f));
    const auto outside = LeaveTheRemains(actor, remains->Context());
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("0x52e91c"), std::string::npos) << outside.Error();
    EXPECT_EQ(remains->LiveMarks(), 0u);
    EXPECT_EQ(remains->random.State(), Seed);
}
} // namespace
