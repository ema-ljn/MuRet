#include "Scenes/SkullEffects.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Scenes/ActorState.h"
#include "Scenes/HighModelEffects.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldState.h"
#include "World/GameModelEffects.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

// El 0x628, la calavera: su alta (0x51ee23), su paso (0x52b59b) y PutTheSkullOn (0x4e688c, 0x4cd664).
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::CreateHighModelEffect;
using Client::Scenes::HighEffectStepContext;
using Client::Scenes::StepHighModelEffect;
using Client::Scenes::WorldState;
using Client::World::ModelEffect;

constexpr std::uint32_t Seed = 0x2468ace1u;
constexpr std::size_t OwnerSlot = 7;

int DrawsSince(const CrtRandom& random)
{
    CrtRandom probe(Seed);
    for (int draws = 0; draws < 1000; ++draws)
    {
        if (probe.State() == random.State())
            return draws;
        static_cast<void>(probe.Next());
    }
    return -1;
}

struct SkullWorld
{
    SkullWorld()
    {
        services.sprites = sprites.get();
        ActorState& owner = Owner();
        owner.alive = true;
        owner.position = {3000, 4000, 250};
        owner.scale = 1.25f;
    }

    ActorState& Owner()
    {
        return world->Actors()[OwnerSlot];
    }

    ModelEffect& Create(std::int32_t subtype, bool owned = true)
    {
        const auto made = CreateHighModelEffect({.type = 0x628,
                                                 .position = {100, 200, 300},
                                                 .angles = {0, 0, 45},
                                                 .light = {0.3f, 0.4f, 0.5f},
                                                 .subtype = subtype,
                                                 .owner = owned ? &Owner() : nullptr},
                                                {.world = *world, .random = random});
        EXPECT_TRUE(made.HasValue() && made.Value().has_value());
        return world->Map().effects.Effects()[made.Value().value_or(0)];
    }

    Client::Core::Expected<void, std::string> Step(ModelEffect& effect)
    {
        const HighEffectStepContext context{
            .world = *world, .random = random, .services = services, .worldClock = clock};
        return StepHighModelEffect(effect, nullptr, context);
    }

    void FillTheParticles()
    {
        for (auto& particle : world->Particles().All())
            particle.alive = true;
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    std::unique_ptr<Client::World::FrameSprites> sprites = std::make_unique<Client::World::FrameSprites>();
    Client::World::ModelEffectStepServices services{};
    CrtRandom random{Seed};
    float clock = 98765.5f;
};

// 0x52b640..0x52b726: el punto de la vuelta `turn` del /0, /1 y /3 con la vida de antes de la cola.
glm::vec3 OrbitPoint(int turn, std::int32_t life, bool reversed, float clock, const ActorState& owner)
{
    using Client::Render::Models::Detail::CosineProductPC53;
    using Client::Render::Models::Detail::SineProductPC53;
    const double index = turn;
    double angle = static_cast<double>(life) * static_cast<double>(std::bit_cast<float>(0x3e2e147bu)) +
                   index * static_cast<double>(std::bit_cast<float>(0x40060a92u));
    if (reversed)
        angle = -angle;
    const auto wobble =
        SineProductPC53(index * static_cast<double>(std::bit_cast<float>(0x4175eb85u)) +
                            static_cast<double>(clock) * static_cast<double>(std::bit_cast<float>(0x3b4b295fu)),
                        20.0f);
    const auto radius = static_cast<float>(wobble.value_or(0.0) + 50.0);
    const auto x = SineProductPC53(angle, radius);
    const auto y = CosineProductPC53(angle, radius);
    return {static_cast<float>(x.value_or(0.0) + static_cast<double>(owner.position.x)),
            static_cast<float>(y.value_or(0.0) + static_cast<double>(owner.position.y)),
            static_cast<float>((reversed ? 250.0 : 200.0) * static_cast<double>(owner.scale) +
                               static_cast<double>(owner.position.z))};
}

// 0x51ee23..0x51ee95: vida 1.000, salvo el /1 (940), el /2 (8, 150 más arriba) y el /4 (20, +0xd8 en 2 y la velocidad
// (0, −10, 0)). Sin rand().
TEST(SkullEffectsTest, TheSkullTakesItsLifeFromItsSubtype)
{
    constexpr std::int32_t Lives[] = {1000, 940, 8, 1000, 20, 1000, 1000};
    for (std::int32_t subtype = 0; subtype < 7; ++subtype)
    {
        SCOPED_TRACE(subtype);
        SkullWorld tested;
        const ModelEffect& skull = tested.Create(subtype);
        EXPECT_EQ(skull.life, Lives[subtype]);
        EXPECT_EQ(skull.transform.position, glm::vec3(100, 200, subtype == 2 ? 450 : 300));
        if (subtype == 4)
        {
            EXPECT_EQ(skull.speed, 2.0f);
        }
        EXPECT_EQ(skull.velocity, subtype == 4 ? glm::vec3(0, -10, 0) : glm::vec3(0.0f));
        EXPECT_EQ(DrawsSince(tested.random), 0);
    }
}

// 0x52b5a3..0x52b5dc: el /4 baja la velocidad y en +0xd8 y la x de sus ángulos en 1 y deja el sprite 0x628 de escala
// 10 con su luz, donde estaba; la cola lo mueve. Sin dueño y sin rand() más que el de la luz local.
TEST(SkullEffectsTest, TheFallingSkullDropsAndLeavesItsSprite)
{
    SkullWorld tested;
    ModelEffect& skull = tested.Create(4, false);
    ASSERT_TRUE(tested.Step(skull).HasValue());
    EXPECT_EQ(skull.velocity, glm::vec3(0, -12, 0));
    EXPECT_EQ(skull.transform.angles.x, -1.0f);
    EXPECT_EQ(skull.life, 19);
    EXPECT_NE(skull.transform.position, glm::vec3(100, 200, 300));
    const auto& sprite = tested.sprites->Sprites()[0];
    ASSERT_TRUE(sprite.alive);
    EXPECT_EQ(sprite.texture, 0x628);
    EXPECT_EQ(sprite.position, glm::vec3(100, 200, 300));
    EXPECT_EQ(sprite.scale, 10.0f);
    EXPECT_EQ(sprite.light, glm::vec3(0.3f, 0.4f, 0.5f));
    EXPECT_EQ(DrawsSince(tested.random), 1);
}

// 0x52b5e9..0x52b5fe: los demás se apagan con su dueño muerto; sin dueño, el original lee un puntero nulo.
TEST(SkullEffectsTest, TheSkullDiesWithItsOwner)
{
    SkullWorld tested;
    ModelEffect& skull = tested.Create(1);
    tested.Owner().alive = false;
    ASSERT_TRUE(tested.Step(skull).HasValue());
    EXPECT_FALSE(skull.alive);
    EXPECT_EQ(skull.life, -1);
    EXPECT_FALSE(tested.sprites->Sprites()[0].alive);
    EXPECT_EQ(DrawsSince(tested.random), 1);

    SkullWorld orphan;
    const auto cut = orphan.Step(orphan.Create(0, false));
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x52b5ef")) << cut.Error();
}

// 0x52b640..0x52b782: el /1 y el /3 (al revés, más alto) dan tres vueltas sobre su dueño; con la tabla de partículas
// llena, la 0x5de/5 no nace y queda el sprite 0x628 de escala 1 con la luz local (1, 1, 1) en cada punto. Con lugar,
// corta en la primera 0x5de/5 (CL11), antes de su sprite.
TEST(SkullEffectsTest, TheSkullCirclesOverItsOwner)
{
    for (const std::int32_t subtype : {1, 3})
    {
        SCOPED_TRACE(subtype);
        SkullWorld tested;
        tested.FillTheParticles();
        ModelEffect& skull = tested.Create(subtype);
        const std::int32_t life = skull.life;
        ASSERT_TRUE(tested.Step(skull).HasValue());
        EXPECT_EQ(skull.life, life - 1);
        for (int turn = 0; turn < 3; ++turn)
        {
            const auto& sprite = tested.sprites->Sprites()[static_cast<std::size_t>(turn)];
            ASSERT_TRUE(sprite.alive);
            EXPECT_EQ(sprite.texture, 0x628);
            EXPECT_EQ(sprite.position, OrbitPoint(turn, life, subtype == 3, tested.clock, tested.Owner()));
            EXPECT_EQ(sprite.scale, 1.0f);
            EXPECT_EQ(sprite.light, glm::vec3(1.0f));
        }
        EXPECT_FALSE(tested.sprites->Sprites()[3].alive);
        EXPECT_EQ(DrawsSince(tested.random), 1);

        SkullWorld roomy;
        const auto cut = roomy.Step(roomy.Create(subtype));
        ASSERT_FALSE(cut.HasValue());
        EXPECT_TRUE(cut.Error().starts_with("0x52b74e")) << cut.Error();
        EXPECT_FALSE(roomy.sprites->Sprites()[0].alive);
    }
}

// 0x52b612..0x52b63b: el /0 mira su propio +0x78. Con 0x40, la vida en 10 (la cola la deja en 9) y da sus vueltas;
// con 0x40 y 0x4000, además +0x16c en 0 y nada más. Sin los bits, gira como el /1. Nadie escribe +0x78 en un efecto
// (Q-C1582): el test lo pone a mano, en un estado al que el original no llega.
TEST(SkullEffectsTest, ThePlainSkullReadsItsOwnFlags)
{
    for (const std::uint32_t flags : {0x0u, 0x40u, 0x4040u, 0x4000u})
    {
        SCOPED_TRACE(flags);
        SkullWorld tested;
        tested.FillTheParticles();
        ModelEffect& skull = tested.Create(0);
        skull.flags78 = flags;
        skull.seen = true;
        ASSERT_TRUE(tested.Step(skull).HasValue());
        const bool held = (flags & 0x40u) != 0;
        EXPECT_EQ(skull.life, held ? 9 : 999);
        const bool hidden = flags == 0x4040u;
        EXPECT_EQ(skull.seen, !hidden);
        EXPECT_EQ(tested.sprites->Sprites()[0].alive, !hidden);
        if (!hidden)
        {
            EXPECT_EQ(tested.sprites->Sprites()[0].position,
                      OrbitPoint(0, held ? 10 : 1000, false, tested.clock, tested.Owner()));
        }
    }
}

// 0x52b85e..0x52b899: el /2 deja el sprite 0x628 de escala 1,5f en su lugar con la luz vida · 0,1f.
TEST(SkullEffectsTest, TheGlowingSkullFadesWithItsLife)
{
    SkullWorld tested;
    ModelEffect& skull = tested.Create(2);
    ASSERT_TRUE(tested.Step(skull).HasValue());
    const auto& sprite = tested.sprites->Sprites()[0];
    ASSERT_TRUE(sprite.alive);
    EXPECT_EQ(sprite.texture, 0x628);
    EXPECT_EQ(sprite.position, glm::vec3(100, 200, 450));
    EXPECT_EQ(sprite.scale, 1.5f);
    const auto glow = static_cast<float>(8.0 * static_cast<double>(std::bit_cast<float>(0x3dcccccdu)));
    EXPECT_EQ(sprite.light, glm::vec3(glow));
    EXPECT_EQ(skull.life, 7);
}

// 0x52b78d..0x52b853: el /5 suelta tres 0x5ac/5; con la tabla llena, nada; con lugar, corta en la primera (CL11). El
// /6 y los demás de más de 5 solo pierden una vida.
TEST(SkullEffectsTest, TheStackedSkullAndTheOthers)
{
    SkullWorld full;
    full.FillTheParticles();
    ModelEffect& stacked = full.Create(5);
    ASSERT_TRUE(full.Step(stacked).HasValue());
    EXPECT_EQ(stacked.life, 999);
    EXPECT_FALSE(full.sprites->Sprites()[0].alive);

    SkullWorld roomy;
    const auto cut = roomy.Step(roomy.Create(5));
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x52b843")) << cut.Error();

    SkullWorld other;
    ModelEffect& sixth = other.Create(6);
    ASSERT_TRUE(other.Step(sixth).HasValue());
    EXPECT_EQ(sixth.life, 999);
    EXPECT_EQ(DrawsSince(other.random), 1);
}

// PutTheSkullOn: apaga solo el 0x628/0 del mismo dueño y crea otro con él de dueño, su posición y sus ángulos y luz 1.
// Con la tabla llena, ninguno y sin error.
TEST(SkullEffectsTest, PuttingASkullReplacesOnlyTheOwnersPlainOne)
{
    SkullWorld tested;
    ModelEffect& old = tested.Create(0);
    ModelEffect& stacked = tested.Create(5);
    ActorState& owner = tested.Owner();
    owner.angles = {0, 0, 60};
    ASSERT_TRUE(Client::Scenes::PutTheSkullOn(owner, *tested.world, tested.random, 0x4cd664).HasValue());
    EXPECT_TRUE(stacked.alive);
    EXPECT_TRUE(old.alive); // el lugar 0 otra vez, con el nuevo
    EXPECT_EQ(old.life, 1000);
    EXPECT_EQ(old.transform.position, owner.position);
    EXPECT_EQ(old.transform.angles, owner.angles);
    EXPECT_EQ(old.light, glm::vec3(1.0f));
    EXPECT_EQ(old.ownerContext, &owner);
    EXPECT_EQ(DrawsSince(tested.random), 0);

    for (auto& effect : tested.world->Map().effects.Effects())
        effect.alive = true;
    old.type = 0;
    ASSERT_TRUE(Client::Scenes::PutTheSkullOn(owner, *tested.world, tested.random, 0x4cd664).HasValue());
}
} // namespace
