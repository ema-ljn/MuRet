#include "Scenes/HighModelEffects.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalAngles.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameModelEffects.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/TerrainLighting.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// Client::Scenes::CreateHighModelEffect y StepHighModelEffect: las altas de 0x518cf0 y los pasos de 0x5208a0 de los
// tipos altos de la CL11 (0x134, 0x13b, 0x141, 0x19f, 0x61c y 0xc1a). 06, "Los efectos con modelo de los tipos altos".
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::CreateHighModelEffect;
using Client::Scenes::HighEffectContext;
using Client::Scenes::HighEffectStepContext;
using Client::Scenes::StepHighModelEffect;
using Client::Scenes::WorldState;
using Client::World::ModelEffect;
using Client::World::ModelEffectArguments;

constexpr std::uint32_t Seed = 0x13579bdfu;

// Cuántos rand() sacó `random` desde la semilla.
int DrawsSince(std::uint32_t seed, const CrtRandom& random)
{
    CrtRandom probe(seed);
    for (int draws = 0; draws < 100000; ++draws)
    {
        if (probe.State() == random.State())
            return draws;
        static_cast<void>(probe.Next());
    }
    return -1;
}

// Un BMD con `actions` acciones de 10 keys, para 0x4ea030 y 0x4ec200.
class ActionModels final : public Client::Scenes::IModelLibrary
{
public:
    explicit ActionModels(std::size_t actions)
    {
        skeleton.actions.assign(actions, Client::Render::Models::ActionInfo{.repeats = true, .keyCount = 10});
    }
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return &skeleton;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
    Client::Render::Models::Skeleton skeleton;
};

struct HighWorld
{
    HighWorld()
    {
        world->Runtime().map = 30;
        world->Runtime().hero = HeroSlot;
        world->Actors()[HeroSlot].alive = true;
        world->Actors()[HeroSlot].position = {5000, 5000, 0};
    }

    HighEffectContext Context(const Client::Scenes::IModelLibrary* models = nullptr)
    {
        return {.world = *world, .random = random, .worldClock = clock, .models = models};
    }

    HighEffectStepContext StepContext(const Client::Scenes::IModelLibrary* models = nullptr)
    {
        return {.world = *world, .random = random, .services = services, .worldClock = clock, .models = models};
    }

    ModelEffect& Effect(std::size_t slot)
    {
        return world->Map().effects.Effects()[slot];
    }

    void FillTheEffects()
    {
        for (auto& effect : world->Map().effects.Effects())
            effect.alive = true;
    }

    void FillTheJoints()
    {
        for (auto& joint : world->DrawTail().joints)
            joint.alive = true;
    }

    void FillTheParticles()
    {
        for (auto& particle : world->Particles().All())
            particle.alive = true;
    }

    [[nodiscard]] int Draws() const
    {
        return DrawsSince(Seed, random);
    }

    static constexpr std::size_t HeroSlot = 12;
    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    std::unique_ptr<Client::World::TerrainLighting> lighting = std::make_unique<Client::World::TerrainLighting>();
    CrtRandom random{Seed};
    float clock = 1234.5f;
    Client::World::ModelEffectStepServices services{.lighting = lighting.get()};
};

bool HasError(const Client::Core::Expected<void, std::string>& result, std::string_view address)
{
    return !result.HasValue() && result.Error().starts_with(address);
}

bool HasError(const Client::Core::Expected<std::optional<std::size_t>, std::string>& result, std::string_view address)
{
    return !result.HasValue() && result.Error().starts_with(address);
}

// Los rand() de una partícula: la crea en un pool aparte con `replay`, que avanza como el original.
void ReplayParticle(CrtRandom& replay, std::int32_t type, std::int32_t subtype, float scale = 1.0f)
{
    auto twin = std::make_unique<Client::World::Particles>();
    const auto made =
        twin->CreateChecked(type, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f), subtype, scale, 0.0f, replay);
    ASSERT_TRUE(made.HasValue());
}

void Skip(CrtRandom& random, int draws)
{
    for (int draw = 0; draw < draws; ++draw)
        static_cast<void>(random.Next());
}

std::ptrdiff_t AliveParticles(WorldState& world, std::int32_t type, std::int32_t subtype)
{
    return std::ranges::count_if(world.Particles().All(), [type, subtype](const Client::World::Particle& particle)
                                 { return particle.alive && particle.type == type && particle.subtype == subtype; });
}

// 0x51d714: el aura 0x134 de la llegada del asedio, con vida 100, la escala de la clave (MOVSX) por 0,01f y +0x64 y
// +0x68 en 0, sin rand(); con la tabla llena, nada.
TEST(HighModelEffectsTest, TheArrivalAuraTakesItsScaleFromTheKeyWithoutRandoms)
{
    HighWorld tested;
    ActorState& owner = tested.world->Actors()[3];
    const ModelEffectArguments arguments{.type = 0x134,
                                         .position = {100, 200, 300},
                                         .angles = {1, 2, 3},
                                         .light = {0.5f, 0.6f, 0.7f},
                                         .owner = &owner,
                                         .key = 120};
    const auto made = CreateHighModelEffect(arguments, tested.Context());
    ASSERT_TRUE(made.HasValue()) << made.Error();
    ASSERT_EQ(made.Value(), std::optional<std::size_t>{0});
    const ModelEffect& aura = tested.Effect(0);
    EXPECT_TRUE(aura.alive);
    EXPECT_EQ(aura.type, 0x134);
    EXPECT_EQ(aura.life, 100);
    EXPECT_EQ(aura.transform.scale, static_cast<float>(120.0 * static_cast<double>(0.01f)));
    EXPECT_EQ(aura.objectState.blendMesh, 0);
    EXPECT_EQ(aura.objectState.light, 0.0f);
    EXPECT_EQ(aura.ownerContext, &owner);
    EXPECT_EQ(tested.Draws(), 0);

    // La clave se lee con signo: 0xff9c es −100.
    const auto negative = CreateHighModelEffect({.type = 0x134, .key = 0xff9c}, tested.Context());
    ASSERT_TRUE(negative.HasValue()) << negative.Error();
    EXPECT_EQ(tested.Effect(1).transform.scale, static_cast<float>(-100.0 * static_cast<double>(0.01f)));

    tested.FillTheEffects();
    const auto full = CreateHighModelEffect(arguments, tested.Context());
    ASSERT_TRUE(full.HasValue()) << full.Error();
    EXPECT_FALSE(full.Value().has_value());
    EXPECT_EQ(tested.Draws(), 0);
}

// 0x52952d: con el dueño vivo, +0x58 en −1 con el asedio o −2 sin él, la vida en 2 (y la cola la deja en 1), +0x68 en
// FSIN(T · 0,001f) · 0,1f + 0,2f y +0x6c en T · 0,0005f; con el dueño muerto, el lugar libre y sólo +0x6c. El rand()
// de la luz local sale siempre.
TEST(HighModelEffectsTest, TheArrivalAuraHoldsWhileItsActorLivesAndHidesWithoutTheSiege)
{
    for (const bool siege : {false, true})
    {
        SCOPED_TRACE(siege);
        HighWorld tested;
        tested.world->Runtime().siegeActive = siege;
        ActorState& owner = tested.world->Actors()[3];
        owner.alive = true;
        ASSERT_TRUE(CreateHighModelEffect({.type = 0x134, .position = {100, 200, 300}, .owner = &owner, .key = 120},
                                          tested.Context())
                        .HasValue());
        ModelEffect& aura = tested.Effect(0);
        auto stepped = StepHighModelEffect(aura, nullptr, tested.StepContext());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_TRUE(aura.alive);
        EXPECT_EQ(aura.life, 1);
        EXPECT_EQ(aura.objectState.hiddenMesh, siege ? -1 : -2);
        const double argument = static_cast<double>(tested.clock) * static_cast<double>(0.001f);
        const auto wave = Client::Render::Models::Detail::SineProductPC53(argument, 0.1f);
        ASSERT_TRUE(wave.has_value());
        EXPECT_EQ(aura.objectState.light, static_cast<float>(*wave + static_cast<double>(0.2f)));
        EXPECT_EQ(aura.objectState.uv.x,
                  static_cast<float>(static_cast<double>(tested.clock) * static_cast<double>(0.0005f)));
        EXPECT_EQ(aura.transform.position, (glm::vec3{100, 200, 300}));
        EXPECT_EQ(tested.Draws(), 1);

        owner.alive = false;
        tested.clock = 2000.0f;
        stepped = StepHighModelEffect(aura, nullptr, tested.StepContext());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_FALSE(aura.alive);
        EXPECT_EQ(aura.life, 0);
        EXPECT_EQ(aura.objectState.uv.x, static_cast<float>(2000.0 * static_cast<double>(0.0005f)));
        EXPECT_EQ(tested.Draws(), 2);
    }
}

// 0x51e4ba: el lancero 0x19f/0 con vida 20, +0x58 en 3, +0xd8 en 0,33f, la acción 2 (0x4ea030, si el BMD la tiene: el
// de FaunaResources o, si no, el de la biblioteca) y los ángulos en 0. Sin el BMD, sin acción. Con otro subtipo, sólo
// el prefijo.
TEST(HighModelEffectsTest, TheSiegeSpearmanTakesActionTwoWhenItsModelHasIt)
{
    for (const std::size_t actions : {std::size_t{0}, std::size_t{2}, std::size_t{3}})
    {
        SCOPED_TRACE(actions);
        HighWorld tested;
        const ActionModels models(actions);
        const auto made = CreateHighModelEffect({.type = 0x19f, .position = {10, 20, 30}, .angles = {4, 5, 6}},
                                                tested.Context(actions == 0 ? nullptr : &models));
        ASSERT_TRUE(made.HasValue()) << made.Error();
        const ModelEffect& spearman = tested.Effect(0);
        EXPECT_EQ(spearman.life, 20);
        EXPECT_EQ(spearman.objectState.hiddenMesh, 3);
        EXPECT_EQ(spearman.speed, std::bit_cast<float>(0x3ea8f5c3u));
        EXPECT_EQ(spearman.animation.action, actions > 2 ? 2 : 0);
        EXPECT_EQ(spearman.transform.angles, glm::vec3(0.0f));
        EXPECT_EQ(tested.Draws(), 0);
    }
    HighWorld tested;
    const auto other = CreateHighModelEffect({.type = 0x19f, .angles = {4, 5, 6}, .subtype = 1}, tested.Context());
    ASSERT_TRUE(other.HasValue()) << other.Error();
    EXPECT_EQ(tested.Effect(0).transform.angles, (glm::vec3{4, 5, 6}));
}

// 0x52aa6a: con la vida múltiplo de 5, dos rand() para el punto y el fragmento 0x12b/2 (que con la tabla llena no
// saca más); con menos de 5, el alpha por 0,769f. La cola lo mueve con su velocidad (0) y le quita una vida.
TEST(HighModelEffectsTest, TheSiegeSpearmanDropsAStoneEveryFiveSteps)
{
    HighWorld tested;
    tested.FillTheEffects();
    ModelEffect& spearman = tested.Effect(0);
    spearman = ModelEffect{};
    spearman.alive = true;
    spearman.type = 0x19f;
    spearman.life = 10;
    spearman.transform.position = {1000, 2000, 300};
    auto stepped = StepHighModelEffect(spearman, nullptr, tested.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(spearman.life, 9);
    EXPECT_EQ(tested.Draws(), 3);

    spearman.life = 4;
    spearman.alpha = 1.0f;
    stepped = StepHighModelEffect(spearman, nullptr, tested.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(spearman.alpha, std::bit_cast<float>(0x3f44ec4fu));
    EXPECT_EQ(spearman.life, 3);
    EXPECT_EQ(tested.Draws(), 4);

    // Con lugar: el 0x12b/2 en la posición corrida.
    HighWorld roomy;
    ModelEffect& dropper = roomy.Effect(0);
    dropper.alive = true;
    dropper.type = 0x19f;
    dropper.life = 5;
    dropper.transform.position = {1000, 2000, 300};
    CrtRandom expected(Seed);
    static_cast<void>(expected.Next());
    const auto side = static_cast<float>(static_cast<double>(expected.Next() % 100) - 50.0 + 1000.0);
    const auto fall = static_cast<float>(2000.0 - (static_cast<double>(expected.Next() % 30) + 15.0));
    stepped = StepHighModelEffect(dropper, nullptr, roomy.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const ModelEffect& stone = roomy.Effect(1);
    EXPECT_TRUE(stone.alive);
    EXPECT_EQ(stone.type, 0x12b);
    EXPECT_EQ(stone.subtype, 2);
    EXPECT_EQ(stone.transform.position.x, side);
    EXPECT_EQ(stone.transform.position.y, fall);
}

// 0x51f154: el pulso 0x61c/0 con vida 20, la velocidad (0, −550, 0), los ángulos (−90, 0, z), +0x28 y +0x2c en 0 y
// +0x30 en rand() % 360; su joint 0x61c/3 (0x51f1c5) es del lote de los joints: con lugar corta ahí, antes de la
// partida. El /1, (0, −250, 0) sin joint. Otro subtipo, sólo el prefijo y sin rand().
TEST(HighModelEffectsTest, TheSiegePulseDrawsItsPhaseAndCutsAtItsJoint)
{
    {
        HighWorld tested;
        const ModelEffectArguments arguments{.type = 0x61c, .position = {10, 20, 30}, .angles = {4, 5, 6}};
        EXPECT_TRUE(HasError(CreateHighModelEffect(arguments, tested.Context()), "0x51f1c5"));
        const ModelEffect& pulse = tested.Effect(0);
        EXPECT_EQ(pulse.life, 20);
        EXPECT_EQ(pulse.velocity, (glm::vec3{0, -550, 0}));
        EXPECT_EQ(pulse.transform.angles, (glm::vec3{-90, 0, 6}));
        EXPECT_EQ(pulse.start, glm::vec3(0.0f));
        EXPECT_EQ(tested.Draws(), 1);

        tested.FillTheJoints();
        const auto made = CreateHighModelEffect(arguments, tested.Context());
        ASSERT_TRUE(made.HasValue()) << made.Error();
        const ModelEffect& joined = tested.Effect(1);
        CrtRandom expected(Seed);
        static_cast<void>(expected.Next());
        EXPECT_EQ(joined.copiedAngles, (glm::vec3{0, 0, static_cast<float>(expected.Next() % 360)}));
        EXPECT_EQ(joined.start, (glm::vec3{10, 20, 30}));
        EXPECT_EQ(tested.Draws(), 2);
    }
    HighWorld tested;
    const auto second = CreateHighModelEffect({.type = 0x61c, .position = {1, 2, 3}, .subtype = 1}, tested.Context());
    ASSERT_TRUE(second.HasValue()) << second.Error();
    EXPECT_EQ(tested.Effect(0).velocity, (glm::vec3{0, -250, 0}));
    EXPECT_EQ(tested.Effect(0).start, (glm::vec3{1, 2, 3}));
    EXPECT_EQ(tested.Draws(), 1);
    const auto other = CreateHighModelEffect({.type = 0x61c, .angles = {7, 8, 9}, .subtype = 2}, tested.Context());
    ASSERT_TRUE(other.HasValue()) << other.Error();
    EXPECT_EQ(tested.Effect(1).transform.angles, (glm::vec3{7, 8, 9}));
    EXPECT_EQ(tested.Draws(), 1);
}

// 0x52c19a: con la vida en 10 o menos y par, el pulso gira +0x30 72 más, pone los ángulos en (90, 0, 0) y, con el /0,
// saca rand() % 400 para el joint 0x632/5 (0x52c266, del lote de los joints; con la tabla llena no se crea); después
// sigue por el paso del 0x13b/0 (0x52c34e): en 12 o menos, cuatro chispas 0x617/0 y 0x5d9/2 con su rand() % 60.
TEST(HighModelEffectsTest, TheSiegePulseTurnsAndFallsIntoTheFlareStep)
{
    HighWorld tested;
    ModelEffect& pulse = tested.Effect(0);
    pulse.alive = true;
    pulse.type = 0x61c;
    pulse.life = 10;
    pulse.transform.position = {1000, 2000, 300};
    pulse.velocity = {0, -550, 0};
    pulse.copiedAngles = {0, 0, 10};
    pulse.start = {1000, 2000, 300};
    EXPECT_TRUE(HasError(StepHighModelEffect(pulse, nullptr, tested.StepContext()), "0x52c266"));
    EXPECT_EQ(pulse.transform.angles, (glm::vec3{90, 0, 0}));
    EXPECT_EQ(pulse.copiedAngles.z, 82.0f);
    EXPECT_EQ(tested.Draws(), 2);

    HighWorld full;
    full.FillTheJoints();
    ModelEffect& joined = full.Effect(0);
    joined = pulse;
    joined.copiedAngles = {0, 0, 10};
    joined.transform.scale = 1.0f;
    const auto stepped = StepHighModelEffect(joined, nullptr, full.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(joined.life, 9);
    EXPECT_EQ(joined.transform.scale, static_cast<float>(1.0 - static_cast<double>(0.05f)));
    const float light = static_cast<float>(10.0 * static_cast<double>(std::bit_cast<float>(0x3d638e39u)));
    EXPECT_EQ(joined.alpha, light);
    EXPECT_EQ(joined.light, glm::vec3(light));
    EXPECT_EQ(joined.velocity.y, -552.0f);
    // La luz, el rand() % 400, los dos del punto y, por chispa, su ángulo y los dos del fuego 0x5d9/2.
    EXPECT_EQ(full.Draws(), 1 + 1 + 2 + 4 * 3);
    EXPECT_EQ(std::ranges::count_if(full.world->Particles().All(), [](const auto& particle)
                                    { return particle.alive && particle.type == Client::World::FireParticle; }),
              4);
}

// 0x51e059 y 0x52c34e: el 0x13b/0 con vida 15, la escala en 0, la z 100 más, la velocidad (0, −10, 0) y +0xd8 en
// 0,25f; con más de 12 de vida crece 0,9f, baja la y de la velocidad y suelta el 0x13b/1 con él de dueño (vida 5,
// escala 3,5), que en su paso pone la luz y el alpha en vida · 0,1f. Los dos con la luz dinámica sólo el /0.
TEST(HighModelEffectsTest, TheSiegeFlareGrowsAndLeavesSparksThatFade)
{
    HighWorld tested;
    const auto made =
        CreateHighModelEffect({.type = 0x13b, .position = {1000, 2000, 300}, .light = {1, 1, 1}}, tested.Context());
    ASSERT_TRUE(made.HasValue()) << made.Error();
    ModelEffect& flare = tested.Effect(0);
    EXPECT_EQ(flare.life, 15);
    EXPECT_EQ(flare.transform.scale, 0.0f);
    EXPECT_EQ(flare.transform.position.z, 400.0f);
    EXPECT_EQ(flare.velocity, (glm::vec3{0, -10, 0}));
    EXPECT_EQ(flare.speed, 0.25f);
    EXPECT_EQ(flare.objectState.blendMesh, 0);
    EXPECT_EQ(flare.objectState.light, 1.0f);

    const auto stepped = StepHighModelEffect(flare, nullptr, tested.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(flare.life, 14);
    EXPECT_EQ(flare.transform.scale, std::bit_cast<float>(0x3f666666u));
    EXPECT_EQ(flare.velocity.y, -12.0f);
    EXPECT_EQ(flare.transform.position, (glm::vec3{1000, 1988, 400}));
    const ModelEffect& spark = tested.Effect(1);
    ASSERT_TRUE(spark.alive);
    EXPECT_EQ(spark.type, 0x13b);
    EXPECT_EQ(spark.subtype, 1);
    EXPECT_EQ(spark.ownerContext, &flare);
    EXPECT_EQ(spark.life, 5);
    EXPECT_EQ(spark.transform.scale, 3.5f);
    EXPECT_EQ(spark.transform.position, (glm::vec3{1000, 2000, 400}));
    EXPECT_EQ(tested.Draws(), 1);
    EXPECT_TRUE(std::ranges::any_of(tested.lighting->cells, [](const auto& cell) { return cell[0] > 0; }));

    ModelEffect& owned = tested.Effect(1);
    const auto faded = StepHighModelEffect(owned, nullptr, tested.StepContext());
    ASSERT_TRUE(faded.HasValue()) << faded.Error();
    const float light = static_cast<float>(5.0 * static_cast<double>(0.1f));
    EXPECT_EQ(owned.alpha, light);
    EXPECT_EQ(owned.objectState.light, light);
    EXPECT_EQ(owned.light, glm::vec3(light));
    EXPECT_EQ(owned.life, 4);
    EXPECT_EQ(tested.Draws(), 2);
}

// 0x52cd41..0x52cd84: el 0x13b anima dos veces por paso; cuando la primera termina la acción (0xff), la segunda lee
// la acción 255 del BMD, fuera de su tabla, y corta.
TEST(HighModelEffectsTest, TheSiegeFlareCutsWhenItsSecondAnimationReadsAFinishedAction)
{
    HighWorld tested;
    tested.FillTheEffects();
    ModelEffect& flare = tested.Effect(0);
    flare = ModelEffect{};
    flare.alive = true;
    flare.type = 0x13b;
    flare.subtype = 1;
    flare.life = 5;
    flare.speed = 0.25f;
    const ActionModels models(1);
    const auto first = StepHighModelEffect(flare, &models.skeleton, tested.StepContext());
    ASSERT_TRUE(first.HasValue()) << first.Error();
    EXPECT_EQ(flare.animation.frame, 0.5f);
    flare.animation.action = 0xff;
    EXPECT_TRUE(HasError(StepHighModelEffect(flare, &models.skeleton, tested.StepContext()), "0x52cd84"));
}

// 0x51c410 y 0x525f1a: el 0x141 nace 20 más arriba con vida 20, +0xe8 en 0 y escala 0,05f; cada paso crece 0,1f y,
// desde 1, se queda en 1 y pierde 0,07f de alpha.
TEST(HighModelEffectsTest, TheGrowingFlashStopsAtOneAndFades)
{
    HighWorld tested;
    const auto made =
        CreateHighModelEffect({.type = 0x141, .position = {10, 20, 30}, .light = {1, 1, 1}}, tested.Context());
    ASSERT_TRUE(made.HasValue()) << made.Error();
    ModelEffect& flash = tested.Effect(0);
    EXPECT_EQ(flash.transform.position, (glm::vec3{10, 20, 50}));
    EXPECT_EQ(flash.life, 20);
    EXPECT_FALSE(flash.vertexLight);
    EXPECT_EQ(flash.transform.scale, std::bit_cast<float>(0x3d4ccccdu));
    float scale = flash.transform.scale;
    float alpha = 1.0f;
    for (int step = 0; step < 15; ++step)
    {
        const auto stepped = StepHighModelEffect(flash, nullptr, tested.StepContext());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        const double grown = static_cast<double>(scale) + static_cast<double>(0.1f);
        scale = static_cast<float>(grown);
        if (grown >= 1.0)
        {
            alpha = static_cast<float>(static_cast<double>(alpha) - static_cast<double>(0.07f));
            scale = 1.0f;
        }
        EXPECT_EQ(flash.transform.scale, scale);
        EXPECT_EQ(flash.alpha, alpha);
    }
    EXPECT_EQ(flash.life, 5);
    EXPECT_LT(flash.alpha, 1.0f);
    EXPECT_EQ(tested.Draws(), 15);
}

// 0x51f85a y 0x52c9fd: el fragmento 0xc1a nace 280 más arriba con vida 30, +0x64 en −2 y la velocidad (0, −80, −10);
// con el terreno por encima de su z, la explosión 0x5ed/0 (y su sonido 0x40), seis piedritas 0xc8 o 0xc9 (rand() & 1)
// y el lugar libre, aunque la cola lo mueve igual; siempre, la luz dinámica.
TEST(HighModelEffectsTest, TheRadialShardBurstsIntoChipsBelowTheTerrain)
{
    HighWorld tested;
    tested.services.terrainHeight = [](float, float) { return 300.0; };
    const auto made = CreateHighModelEffect(
        {.type = 0xc1a, .position = {1000, 2000, 40}, .angles = {3, 0, 0}, .light = {1, 1, 1}}, tested.Context());
    ASSERT_TRUE(made.HasValue()) << made.Error();
    ModelEffect& shard = tested.Effect(0);
    EXPECT_EQ(shard.life, 30);
    EXPECT_EQ(shard.objectState.blendMesh, -2);
    EXPECT_EQ(shard.transform.position.z, 320.0f);
    EXPECT_EQ(shard.transform.angles.x, 23.0f);
    EXPECT_EQ(shard.velocity, (glm::vec3{0, -80, -10}));
    EXPECT_EQ(tested.Draws(), 0);

    auto stepped = StepHighModelEffect(shard, nullptr, tested.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(shard.alive);
    EXPECT_EQ(shard.life, 29);
    EXPECT_EQ(tested.Draws(), 1);

    shard.transform.position.z = 200.0f;
    stepped = StepHighModelEffect(shard, nullptr, tested.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FALSE(shard.alive);
    EXPECT_EQ(shard.life, 28);
    const auto chips = std::ranges::count_if(tested.world->Map().effects.Effects(), [](const ModelEffect& effect)
                                             { return effect.alive && (effect.type == 0xc8 || effect.type == 0xc9); });
    EXPECT_EQ(chips, 6);
    EXPECT_EQ(tested.world->Runtime().lastSoundId, 0x40);
    EXPECT_TRUE(std::ranges::any_of(tested.world->Particles().All(), [](const auto& particle)
                                    { return particle.alive && particle.type == Client::World::ExplosionParticle; }));

    // Con las tablas llenas, sólo la luz y los seis rand() & 1.
    HighWorld full;
    full.services.terrainHeight = [](float, float) { return 300.0; };
    full.FillTheEffects();
    full.FillTheParticles();
    ModelEffect& lone = full.Effect(0);
    lone = ModelEffect{};
    lone.alive = true;
    lone.type = 0xc1a;
    lone.life = 30;
    stepped = StepHighModelEffect(lone, nullptr, full.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FALSE(lone.alive);
    EXPECT_EQ(full.Draws(), 1 + 6);
    EXPECT_NE(full.world->Runtime().lastSoundId, 0x40);

    HighWorld blind;
    ModelEffect& unseen = blind.Effect(0);
    unseen.alive = true;
    unseen.type = 0xc1a;
    unseen.life = 30;
    EXPECT_TRUE(HasError(StepHighModelEffect(unseen, nullptr, blind.StepContext()), "0x52ca08"));
}
} // namespace

namespace
{
// 0x51eebb..0x51efa4: el caso del 0x61e en el alta, con sus rand() y la tabla llena sin ninguno.
TEST(HighModelEffectsTest, TheColoredDecalTakesItsLifeAndScaleBySubtype)
{
    constexpr double ScaleStep = static_cast<double>(std::bit_cast<float>(0x3d23d70au)); // 0,04f
    struct Probe
    {
        std::int32_t subtype;
        std::int32_t life;
        int draws;
    };
    for (const Probe& probe : {Probe{2, 20, 0}, Probe{4, 40, 1}, Probe{5, 20, 0}, Probe{6, 60, 1}, Probe{7, 40, 1},
                               Probe{8, 20, 0}, Probe{9, 10, 0}, Probe{0xa, 40, 1}})
    {
        SCOPED_TRACE(probe.subtype);
        HighWorld tested;
        ActorState& owner = tested.world->Actors()[3];
        const ModelEffectArguments arguments{.type = 0x61e,
                                             .position = {700, 800, 90},
                                             .angles = {0, 0, 45},
                                             .light = glm::vec3(0.5f),
                                             .subtype = probe.subtype,
                                             .owner = &owner};
        const auto made = CreateHighModelEffect(arguments, tested.Context());
        ASSERT_TRUE(made.HasValue()) << made.Error();
        ASSERT_EQ(made.Value(), std::optional<std::size_t>{0});
        const ModelEffect& decal = tested.Effect(0);
        EXPECT_EQ(decal.life, probe.life);
        EXPECT_EQ(decal.ownerContext, &owner);
        EXPECT_EQ(tested.Draws(), probe.draws);
        CrtRandom expected{Seed};
        if (probe.subtype == 4 || probe.subtype == 6 || probe.subtype == 0xa)
        {
            const int grown = expected.Next() % 50 + 50;
            EXPECT_EQ(decal.transform.scale, static_cast<float>(static_cast<double>(grown) * ScaleStep));
        }
        if (probe.subtype == 6)
        {
            EXPECT_EQ(decal.start, arguments.position);
        }
        if (probe.subtype == 7)
        {
            EXPECT_EQ(decal.transform.angles.z, static_cast<float>(expected.Next() % 360));
        }
        if (probe.subtype == 9)
        {
            EXPECT_EQ(decal.transform.scale, std::bit_cast<float>(0x3dcccccdu));
        }
    }
    HighWorld full;
    full.FillTheEffects();
    const auto none = CreateHighModelEffect({.type = 0x61e, .subtype = 6}, full.Context());
    ASSERT_TRUE(none.HasValue()) << none.Error();
    EXPECT_FALSE(none.Value().has_value());
    EXPECT_EQ(full.Draws(), 0);
}

// 0x52b9c1..0x52bb24: el 0x61e/6 pone su partida como origen del modelo de su dueño, sortea +0x68 y, por cada vida, un
// hueso; los dummy no sacan más; los otros, tres rand() del corrimiento y la partícula 0x5ac/8, que MuRet no tiene:
// con lugar corta ahí (0x52baf5); llena, sigue. Después, el alpha del dueño en vida · 0,0125f, y la cola: una vida
// menos sin moverse.
TEST(HighModelEffectsTest, TheBindingDecalSparksOverTheBonesOfItsOwner)
{
    HighWorld tested;
    ActionModels models(1);
    models.skeleton.bones.resize(2);
    models.skeleton.bones[1].dummy = true;
    tested.world->FaunaResources().Bind(&models);
    ActorState& owner = tested.world->Actors()[3];
    owner.alive = true;
    owner.type = 0x20a;
    owner.position = {1000, 1000, 0};
    owner.angles = {0, 0, 90};
    owner.alpha = 1.0f;
    const ModelEffectArguments arguments{
        .type = 0x61e, .position = {1000, 1000, 0}, .light = glm::vec3(1.0f), .subtype = 6, .owner = &owner};
    ASSERT_TRUE(CreateHighModelEffect(arguments, tested.Context()).HasValue());
    ModelEffect& decal = tested.Effect(0);
    decal.life = 3;
    const int before = tested.Draws();
    const ModelEffect kept = decal;
    const auto cut = StepHighModelEffect(decal, nullptr, tested.StepContext());
    // Hasta el corte (0x52baf5): la luz local (0x5208c5), +0x68 (0x52ba0f) y, por vuelta, el hueso (0x52ba3d); el
    // primero que no es un dummy saca los tres del corrimiento (0x52ba66..0x52ba98) y la partícula corta sin sacar más.
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x52baf5")) << cut.Error();
    {
        CrtRandom untilCut{Seed};
        for (int skipped = 0; skipped < before; ++skipped)
            static_cast<void>(untilCut.Next());
        int cutDraws = before + 2;
        static_cast<void>(untilCut.Next());
        static_cast<void>(untilCut.Next());
        for (int turn = 0; turn < 3; ++turn)
        {
            ++cutDraws;
            if (untilCut.Next() % 2 == 0)
            {
                cutDraws += 3;
                break;
            }
        }
        EXPECT_EQ(tested.Draws(), cutDraws);
    }
    decal = kept;
    tested.random = CrtRandom{Seed};
    for (int skipped = 0; skipped < before; ++skipped)
        static_cast<void>(tested.random.Next());
    tested.FillTheParticles();

    const auto stepped = StepHighModelEffect(decal, nullptr, tested.StepContext());

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    CrtRandom expected{Seed};
    for (int skipped = 0; skipped < before; ++skipped)
        static_cast<void>(expected.Next());
    static_cast<void>(expected.Next()); // la luz local (0x5208c5)
    const int glow = expected.Next() % 10;
    int sparks = 0;
    int draws = before + 2;
    for (int turn = 0; turn < 3; ++turn)
    {
        const int bone = expected.Next() % 2;
        ++draws;
        if (bone == 1)
            continue;
        for (int axis = 0; axis < 3; ++axis)
            static_cast<void>(expected.Next());
        draws += 3;
        ++sparks;
    }
    EXPECT_EQ(decal.objectState.light,
              static_cast<float>(static_cast<double>(glow) * static_cast<double>(std::bit_cast<float>(0x3dcccccdu))));
    EXPECT_EQ(owner.alpha, static_cast<float>(3.0 * static_cast<double>(std::bit_cast<float>(0x3c4ccccdu))));
    EXPECT_EQ(tested.world->ModelPlacement(0x20a).origin, decal.start);
    EXPECT_EQ(decal.life, 2);
    EXPECT_EQ(decal.transform.position, arguments.position);
    EXPECT_GT(sparks, 0);
    EXPECT_EQ(tested.Draws(), draws);
    tested.world->FaunaResources().Bind(nullptr);
}

// El 0x61e/6 sin huesos en el modelo de su dueño: el original divide por cero (0x52ba47); MuRet corta, después del
// rand() de +0x68.
TEST(HighModelEffectsTest, TheBindingDecalCutsWhenItsOwnerHasNoBones)
{
    HighWorld tested;
    ActorState& owner = tested.world->Actors()[3];
    owner.type = 0x20a;
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x61e, .subtype = 6, .owner = &owner}, tested.Context()).HasValue());
    const auto stepped = StepHighModelEffect(tested.Effect(0), nullptr, tested.StepContext());
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x52ba47"), std::string::npos) << stepped.Error();
    EXPECT_EQ(tested.Draws(), 1 + 2);
}

// 0x52ba1d: el BMD del dueño sale de la tabla global, como en el alta del 0x19f: sin el de FaunaResources (un jugador),
// el de `models`. Con un solo hueso dummy, cada vida saca solo el hueso y la partícula no corre.
TEST(HighModelEffectsTest, TheBindingDecalFindsItsOwnerModelLikeTheSpearman)
{
    HighWorld tested;
    ActionModels models(1);
    models.skeleton.bones.resize(1);
    models.skeleton.bones[0].dummy = true;
    ActorState& owner = tested.world->Actors()[3];
    owner.alive = true;
    owner.type = 0x20a;
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x61e, .subtype = 6, .owner = &owner}, tested.Context()).HasValue());
    ModelEffect& decal = tested.Effect(0);
    decal.life = 3;
    const int before = tested.Draws();

    const auto stepped = StepHighModelEffect(decal, nullptr, tested.StepContext(&models));

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(tested.Draws(), before + 2 + 3);
    EXPECT_EQ(decal.life, 2);
}

// 0x51c884 y 0x5266ea: el seamanFX 0x11a nace con +0x64 en −2, +0x68 en 0,2f, escala 0,4f, vida 20, +0xe4 en 120 y la
// velocidad (0, −40, 0) en +0xd8, con luz 1 y la partida en su lugar. Cada paso crece 0,02f, sube a la partida más
// +0xe4 (que baja de a 20 hasta 60) y acelera 10; con la tabla de partículas llena, sin las 0x6f4/1 ni rand() de más.
TEST(HighModelEffectsTest, TheSeamanEffectRisesAndSpeedsUp)
{
    HighWorld tested;
    tested.FillTheParticles();
    ASSERT_TRUE(
        CreateHighModelEffect({.type = 0x11a, .position = {100, 200, 300}, .light = glm::vec3(0.5f)}, tested.Context())
            .HasValue());
    ModelEffect& seaman = tested.Effect(0);
    EXPECT_EQ(seaman.objectState.blendMesh, -2);
    EXPECT_EQ(seaman.objectState.light, std::bit_cast<float>(0x3e4ccccdu));
    EXPECT_EQ(seaman.transform.scale, std::bit_cast<float>(0x3ecccccdu));
    EXPECT_EQ(seaman.life, 20);
    EXPECT_EQ(seaman.turnDirection, 120.0f);
    EXPECT_EQ(seaman.velocity, glm::vec3(0, -40, 0));
    EXPECT_EQ(seaman.light, glm::vec3(1.0f));
    EXPECT_EQ(seaman.start, glm::vec3(100, 200, 300));
    float scale = seaman.transform.scale;
    float before = seaman.turnDirection;
    for (const float lift : {100.0f, 80.0f, 60.0f, 60.0f})
    {
        ASSERT_TRUE(StepHighModelEffect(seaman, nullptr, tested.StepContext()).HasValue());
        scale = static_cast<float>(static_cast<double>(scale) + static_cast<double>(std::bit_cast<float>(0x3ca3d70au)));
        EXPECT_EQ(seaman.transform.scale, scale);
        EXPECT_EQ(seaman.turnDirection, lift);
        EXPECT_EQ(seaman.transform.position.z, 300.0f + before);
        before = lift;
    }
    EXPECT_EQ(seaman.speed, -80.0f);
    EXPECT_EQ(seaman.velocity, glm::vec3(0, -80, 0));
    EXPECT_EQ(seaman.life, 16);
    EXPECT_EQ(tested.Draws(), 4);

    // Con lugar, las cuatro 0x6f4/1 (las de la cascada de Kalima) en su lugar, con la luz local (0x5208c5, sin la resta
    // de las últimas 5 vidas) por (0,1f; 0,3f; 0,6f) (0x526740..0x526783) y un rand() cada una (0x543d30); después, la
    // luz dinámica de radio 3 con esa misma luz (0x5267ba).
    HighWorld roomy;
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x11a, .position = {100, 200, 300}}, roomy.Context()).HasValue());
    const auto stepped = StepHighModelEffect(roomy.Effect(0), nullptr, roomy.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    CrtRandom expected{Seed};
    const auto luminosity =
        static_cast<float>(static_cast<double>((expected.Next() & 3) + 7) * static_cast<double>(0.1f));
    const glm::vec3 color{static_cast<float>(static_cast<double>(luminosity) * static_cast<double>(0.1f)),
                          static_cast<float>(static_cast<double>(luminosity) * static_cast<double>(0.3f)),
                          static_cast<float>(static_cast<double>(luminosity) * static_cast<double>(0.6f))};
    int sprays = 0;
    for (const auto& particle : roomy.world->Particles().All())
    {
        if (!particle.alive || particle.type != 0x6f4 || particle.subtype != 1)
            continue;
        ++sprays;
        EXPECT_EQ(particle.light, color);
    }
    EXPECT_EQ(sprays, 4);
    EXPECT_EQ(roomy.Draws(), 1 + 4);
    EXPECT_TRUE(std::ranges::any_of(roomy.lighting->cells, [](const auto& cell) { return cell[0] > 0; }));
}

// 0x52bb29..0x52bb74: el 0x61e/9 crece 0,3f y su luz baja a 0,909f; los /4, /7, /8 y /0xa solo pierden una vida.
TEST(HighModelEffectsTest, TheGrowingDecalWidensAndDims)
{
    HighWorld tested;
    ASSERT_TRUE(
        CreateHighModelEffect({.type = 0x61e, .light = glm::vec3(0.8f), .subtype = 9}, tested.Context()).HasValue());
    ModelEffect& decal = tested.Effect(0);
    ASSERT_TRUE(StepHighModelEffect(decal, nullptr, tested.StepContext()).HasValue());
    const double decay = static_cast<double>(std::bit_cast<float>(0x3f68ba2eu));
    EXPECT_EQ(decal.transform.scale, static_cast<float>(static_cast<double>(std::bit_cast<float>(0x3dcccccdu)) +
                                                        static_cast<double>(std::bit_cast<float>(0x3e99999au))));
    EXPECT_EQ(decal.light.x, static_cast<float>(static_cast<double>(0.8f) * decay));
    EXPECT_EQ(decal.life, 9);
    EXPECT_EQ(tested.Draws(), 1);
    for (const std::int32_t subtype : {4, 7, 8, 0xa})
    {
        HighWorld plain;
        ASSERT_TRUE(CreateHighModelEffect({.type = 0x61e, .subtype = subtype}, plain.Context()).HasValue());
        const int life = plain.Effect(0).life;
        const float scale = plain.Effect(0).transform.scale;
        const int before = plain.Draws();
        ASSERT_TRUE(StepHighModelEffect(plain.Effect(0), nullptr, plain.StepContext()).HasValue());
        EXPECT_EQ(plain.Effect(0).life, life - 1);
        EXPECT_EQ(plain.Effect(0).transform.scale, scale);
        EXPECT_EQ(plain.Draws(), before + 1);
    }
}
} // namespace

namespace
{
// 0x51e596..0x51e69e: los rayos 0x5e8 (azul; el /1 blanco y de escala 3), 0x5e9 y 0x5ea (naranjas): vida 20, la luz,
// la escala y la velocidad girada. Sin rand() sin el héroe de dueño; la tabla llena, nada. El paso solo les quita vida:
// no se mueven (0x52cd1d).
TEST(HighModelEffectsTest, TheMovingLightsTakeTheirColorScaleAndRush)
{
    struct Probe
    {
        std::int16_t type;
        std::int32_t subtype;
        glm::vec3 light;
        float scale;
        float rush;
    };
    const float orange = std::bit_cast<float>(0x3ecccccdu), ember = std::bit_cast<float>(0x3e4ccccdu);
    for (const Probe& probe :
         {Probe{0x5e8, 0, {0.5f, std::bit_cast<float>(0x3f333333u), 1.0f}, 16.0f, -50.0f},
          Probe{0x5e8, 1, glm::vec3(1.0f), 3.0f, -50.0f}, Probe{0x5e9, 0, {1.0f, orange, ember}, 16.0f, -50.0f},
          Probe{0x5ea, 1, {1.0f, orange, ember}, 5.0f, -15.0f}})
    {
        SCOPED_TRACE(probe.type);
        HighWorld tested;
        const ModelEffectArguments arguments{.type = probe.type,
                                             .position = {300, 400, 50},
                                             .angles = {0, 0, 90},
                                             .light = glm::vec3(0.25f),
                                             .subtype = probe.subtype,
                                             .owner = &tested.world->Actors()[2]};
        ASSERT_TRUE(CreateHighModelEffect(arguments, tested.Context()).HasValue());
        ModelEffect& ray = tested.Effect(0);
        EXPECT_EQ(ray.life, 20);
        EXPECT_EQ(ray.light, probe.light);
        EXPECT_EQ(ray.transform.scale, probe.scale);
        EXPECT_EQ(ray.velocity,
                  Client::Render::Models::OriginalVectorRotate(
                      {0.0f, probe.rush, 0.0f}, Client::Render::Models::OriginalAngleMatrix(arguments.angles)));
        EXPECT_EQ(tested.Draws(), 0);
        ASSERT_TRUE(StepHighModelEffect(ray, nullptr, tested.StepContext()).HasValue());
        EXPECT_EQ(ray.transform.position, arguments.position);
        EXPECT_EQ(ray.life, 19);
        EXPECT_EQ(tested.Draws(), 1);
    }
    HighWorld full;
    full.FillTheEffects();
    const auto none =
        CreateHighModelEffect({.type = 0x5e8, .owner = &full.world->Actors()[HighWorld::HeroSlot]}, full.Context());
    ASSERT_TRUE(none.HasValue()) << none.Error();
    EXPECT_FALSE(none.Value().has_value());
    EXPECT_EQ(full.Draws(), 0);
}

// 0x51e6bf..0x51e76a: con el héroe de dueño, cuatro 0x517a20 a 150, 300, 450 y 600 delante del rayo; cada uno abre y
// cierra el registro propio (sus rand()) y, sin blancos, no manda nada.
TEST(HighModelEffectsTest, TheHerosMovingLightLooksForTargetsFourTimes)
{
    HighWorld tested;
    int sent = 0;
    HighEffectContext context = tested.Context();
    context.send = [&sent](Client::Protocol::Outgoing) { ++sent; };
    ASSERT_TRUE(
        CreateHighModelEffect(
            {.type = 0x5e8, .position = {300, 400, 50}, .owner = &tested.world->Actors()[HighWorld::HeroSlot]}, context)
            .HasValue());
    EXPECT_EQ(tested.Draws(), 4 * Client::Render::Models::OwnRecordCloseRandoms);
    EXPECT_EQ(sent, 0);
}
// 0x51f048..0x51f153: el 0x625 con su vida, +0xe4, +0xd8, la luz en 0, la escala y el reparto del subtipo, la z 500
// más, la x 100 más, la partida en la posición y el sonido 0x42; con la tabla llena, nada y sin rand().
TEST(HighModelEffectsTest, TheFallingStarSpreadsAboveItsTarget)
{
    for (const std::int32_t subtype : {0, 1})
    {
        SCOPED_TRACE(subtype);
        HighWorld tested;
        const ModelEffectArguments arguments{.type = 0x625,
                                             .position = {1000, 2000, 30},
                                             .angles = {0, 0, 45},
                                             .light = glm::vec3(1.0f),
                                             .subtype = subtype};
        const auto made = CreateHighModelEffect(arguments, tested.Context());
        ASSERT_TRUE(made.HasValue()) << made.Error();
        ASSERT_EQ(made.Value(), std::optional<std::size_t>{0});
        CrtRandom expected(Seed);
        const int life = expected.Next() % 15 + 15;
        const auto speed = static_cast<float>(expected.Next() % 360);
        float rise = -20.0f;
        const int spread = subtype == 1 ? 300 : 200;
        const double half = subtype == 1 ? 150.0 : 100.0;
        if (subtype == 1)
            rise = static_cast<float>(-20.0 - static_cast<double>(expected.Next() % 20 + 10));
        const auto x = static_cast<float>(static_cast<double>(expected.Next() % spread) + 1000.0 - half);
        const auto y = static_cast<float>(static_cast<double>(expected.Next() % spread) + 2000.0 - half);
        const glm::vec3 position{static_cast<float>(static_cast<double>(x) + 100.0), y, 530.0f};
        const ModelEffect& star = tested.Effect(0);
        EXPECT_EQ(star.type, 0x625);
        EXPECT_EQ(star.life, life);
        EXPECT_EQ(star.speed, speed);
        EXPECT_EQ(star.turnDirection, rise);
        EXPECT_EQ(star.light, glm::vec3(0.0f));
        EXPECT_EQ(star.transform.scale, subtype == 1 ? 0.5f : 0.0f);
        EXPECT_EQ(star.transform.position, position);
        EXPECT_EQ(star.start, position);
        EXPECT_EQ(tested.Draws(), subtype == 1 ? 5 : 4);
        EXPECT_EQ(tested.world->Runtime().lastSoundId, 0x42);
    }
    HighWorld full;
    full.FillTheEffects();
    const auto none = CreateHighModelEffect({.type = 0x625}, full.Context());
    ASSERT_TRUE(none.HasValue()) << none.Error();
    EXPECT_FALSE(none.Value().has_value());
    EXPECT_EQ(full.Draws(), 0);
    EXPECT_NE(full.world->Runtime().lastSoundId, 0x42);
}

// Un 0x625 vivo en el lugar 0, con todo lo demás como recién construido.
ModelEffect& FallingStarAt(HighWorld& tested, std::int32_t subtype, std::int32_t life)
{
    ModelEffect& star = tested.Effect(0);
    star = ModelEffect{};
    star.alive = true;
    star.type = 0x625;
    star.subtype = subtype;
    star.life = life;
    return star;
}

// 0x52be78: con más de 15 de vida, el 0x625 sólo saca la luz local y pierde una vida.
TEST(HighModelEffectsTest, TheFallingStarWaitsUntilLifeFifteen)
{
    HighWorld tested;
    ModelEffect& star = FallingStarAt(tested, 0, 16);
    star.transform.position = {10, 20, 30};
    star.turnDirection = -20.0f;
    ASSERT_TRUE(StepHighModelEffect(star, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(star.life, 15);
    EXPECT_EQ(star.transform.position, glm::vec3(10, 20, 30));
    EXPECT_EQ(star.turnDirection, -20.0f);
    EXPECT_EQ(tested.Draws(), 1);
}

// 0x52be8d..0x52bfce: el /0 tiembla alrededor de la partida, baja +0xe4, corre la partida, suelta el fuego 0x5db/7 (con
// la tabla de partículas llena, nada y sin rand()), sube la luz 0,1f y deja los sprites 0x5fd y 0x5ac; con lugar para
// el fuego, corta en 0x52bf29 después de los dos rand() del temblor.
TEST(HighModelEffectsTest, TheWobblyFallingStarLeavesItsFireAndTwoSprites)
{
    HighWorld tested;
    auto sprites = std::make_unique<Client::World::FrameSprites>();
    tested.services.sprites = sprites.get();
    tested.FillTheParticles();
    ModelEffect& star = FallingStarAt(tested, 0, 15);
    star.transform.position = {10, 20, 30};
    star.transform.scale = 0.0f;
    star.start = {500, 600, 700};
    star.turnDirection = -20.0f;
    star.light = glm::vec3(0.0f);
    ASSERT_TRUE(StepHighModelEffect(star, nullptr, tested.StepContext()).HasValue());
    CrtRandom expected(Seed);
    static_cast<void>(expected.Next());
    const double rate = static_cast<double>(std::bit_cast<float>(0x3c23d70au));
    glm::vec3 position{0.0f, 0.0f, 10.0f};
    for (int axis = 0; axis < 2; ++axis)
    {
        const auto wobble =
            Client::Render::Models::Detail::SineProductPC53(static_cast<double>(expected.Next() % 1000) * rate, 10.0f);
        ASSERT_TRUE(wobble.has_value());
        position[axis] = static_cast<float>(*wobble + (axis == 0 ? 500.0 : 600.0));
    }
    const auto shineTurn = static_cast<float>(expected.Next() % 360);
    const auto shineScale = static_cast<float>(static_cast<double>((expected.Next() & 3) + 4) *
                                               static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)));
    const auto flareTurn = static_cast<float>(expected.Next() % 360);
    EXPECT_EQ(star.transform.position, position);
    EXPECT_EQ(star.turnDirection, -22.0f);
    EXPECT_EQ(star.start, glm::vec3(490, 600, 700));
    const float glow = std::bit_cast<float>(0x3dcccccdu);
    EXPECT_EQ(star.light, glm::vec3(glow));
    EXPECT_EQ(star.life, 14);
    EXPECT_EQ(tested.Draws(), 6);
    const auto made = sprites->Sprites();
    ASSERT_TRUE(made[0].alive);
    EXPECT_EQ(made[0].texture, 0x5fd);
    EXPECT_EQ(made[0].position, position);
    EXPECT_EQ(made[0].scale, shineScale);
    EXPECT_EQ(made[0].rotation, shineTurn);
    EXPECT_EQ(made[0].light, glm::vec3(glow));
    ASSERT_TRUE(made[1].alive);
    EXPECT_EQ(made[1].texture, 0x5ac);
    EXPECT_EQ(made[1].scale, 1.0f);
    EXPECT_EQ(made[1].rotation, flareTurn);
    EXPECT_FALSE(made[2].alive);

    // Con lugar, el fuego 0x5db/7 (0x52bf29) saca sus rand() entre el temblor y los sprites.
    HighWorld room;
    room.services.sprites = sprites.get();
    ModelEffect& next = FallingStarAt(room, 0, 3);
    const auto lit = StepHighModelEffect(next, nullptr, room.StepContext());
    ASSERT_TRUE(lit.HasValue()) << lit.Error();
    CrtRandom replay(Seed);
    Skip(replay, 3);
    ReplayParticle(replay, Client::World::DeathSpiritFireParticle, 7);
    Skip(replay, 3);
    EXPECT_EQ(room.random.State(), replay.State());
    EXPECT_EQ(AliveParticles(*room.world, Client::World::DeathSpiritFireParticle, 7), 1);
}

// 0x52bfdc..0x52c195: el /1 se queda en la partida y, con el terreno por encima, saca la escala del humo 0x5f2/0xb y
// el rand() % 5 que, en 0, suelta el pedazo de hielo 0xca 50 más arriba; con el terreno por debajo, nada de eso. Con
// lugar, el fuego 0x5db/0xb (0x52c036) saca sus rand() antes de los sprites.
TEST(HighModelEffectsTest, TheStraightFallingStarSmokesAndDropsIceBelowTheTerrain)
{
    for (const double ground : {-1000.0, 1000.0})
    {
        SCOPED_TRACE(ground);
        HighWorld tested;
        auto sprites = std::make_unique<Client::World::FrameSprites>();
        tested.services.sprites = sprites.get();
        tested.services.terrainHeight = [ground](float, float) { return ground; };
        tested.FillTheParticles();
        ModelEffect& star = FallingStarAt(tested, 1, 10);
        star.transform.scale = 0.5f;
        star.start = {500, 600, 700};
        star.transform.position = {1, 2, 100};
        star.turnDirection = -30.0f;
        ASSERT_TRUE(StepHighModelEffect(star, nullptr, tested.StepContext()).HasValue());
        EXPECT_EQ(star.transform.position, glm::vec3(500, 600, 70));
        EXPECT_EQ(star.turnDirection, -32.0f);
        EXPECT_EQ(star.start.x, 490.0f);
        CrtRandom expected(Seed);
        for (int draw = 0; draw < 4; ++draw)
            static_cast<void>(expected.Next());
        if (ground < 0)
        {
            EXPECT_EQ(tested.Draws(), 4);
            EXPECT_FALSE(tested.Effect(1).alive);
            continue;
        }
        static_cast<void>(expected.Next());
        const bool shard = expected.Next() % 5 == 0;
        const ModelEffect& ice = tested.Effect(1);
        EXPECT_EQ(ice.alive, shard);
        if (shard)
        {
            EXPECT_EQ(ice.type, 0xca);
            EXPECT_EQ(ice.transform.position.x, 500.0f);
            EXPECT_EQ(ice.transform.position.y, 600.0f);
        }
        EXPECT_EQ(tested.Draws(), DrawsSince(Seed, expected) + (shard ? 5 : 0));
    }
    HighWorld room;
    ModelEffect& star = FallingStarAt(room, 1, 10);
    const auto cut = StepHighModelEffect(star, nullptr, room.StepContext());
    EXPECT_TRUE(HasError(cut, "0x52c0a5")) << (cut.HasValue() ? std::string("sin corte") : cut.Error());
    CrtRandom replay(Seed);
    Skip(replay, 1);
    ReplayParticle(replay, Client::World::DeathSpiritFireParticle, 0xb);
    Skip(replay, 2);
    EXPECT_EQ(room.random.State(), replay.State());
    EXPECT_EQ(AliveParticles(*room.world, Client::World::DeathSpiritFireParticle, 0xb), 1);
}
// 0x51ca31: el 0x116/0 guarda la luz que recibe como la partida y pone la suya en 1; el /2 baja 20; el /1 toma la vida
// de su dueño (otro efecto) y suelta la llama 0x5da/0 (0x51cb38) con sus rand(); con la tabla de partículas llena,
// sigue sin rand().
TEST(HighModelEffectsTest, ThePierPartsTakeTheirAimLifeAndFlame)
{
    HighWorld tested;
    const ModelEffectArguments homing{.type = 0x116,
                                      .position = {10, 20, 30},
                                      .angles = {5, 6, 70},
                                      .light = {0, 0, 150},
                                      .owner = &tested.world->Actors()[3]};
    ASSERT_TRUE(CreateHighModelEffect(homing, tested.Context()).HasValue());
    const ModelEffect& part = tested.Effect(0);
    EXPECT_EQ(part.life, 20);
    EXPECT_EQ(part.turnDirection, 2.0f);
    EXPECT_EQ(part.speed, 10.0f);
    EXPECT_EQ(part.objectState.hiddenMesh, 1);
    EXPECT_EQ(part.transform.scale, std::bit_cast<float>(0x3f99999au));
    EXPECT_EQ(part.light, glm::vec3(1.0f));
    EXPECT_EQ(part.velocity, (glm::vec3{0, -26, 0}));
    EXPECT_EQ(part.start, (glm::vec3{0, 0, 150}));
    EXPECT_EQ(part.copiedAngles, (glm::vec3{5, 6, 70}));
    EXPECT_EQ(part.transform.angles, (glm::vec3{0, 0, 70}));

    ModelEffectArguments diving = homing;
    diving.subtype = 2;
    ASSERT_TRUE(CreateHighModelEffect(diving, tested.Context()).HasValue());
    const ModelEffect& dive = tested.Effect(1);
    EXPECT_EQ(dive.life, 20);
    EXPECT_EQ(dive.speed, 50.0f);
    EXPECT_EQ(dive.objectState.hiddenMesh, -2);
    EXPECT_EQ(dive.transform.position, (glm::vec3{10, 20, 10}));
    EXPECT_EQ(dive.velocity, (glm::vec3{0, -40, 0}));
    EXPECT_EQ(dive.transform.angles, (glm::vec3{0, 0, 70}));

    tested.Effect(0).life = 12;
    const ModelEffectArguments trail{.type = 0x116, .position = {1, 2, 3}, .subtype = 1, .owner = &tested.Effect(0)};
    ASSERT_TRUE(CreateHighModelEffect(trail, tested.Context()).HasValue());
    EXPECT_EQ(AliveParticles(*tested.world, Client::World::MissileFireParticle, 0), 1);
    CrtRandom replay(Seed);
    ReplayParticle(replay, Client::World::MissileFireParticle, 0);
    EXPECT_EQ(tested.random.State(), replay.State());
    tested.random = CrtRandom(Seed);
    tested.FillTheParticles();
    tested.Effect(2).alive = false;
    ASSERT_TRUE(CreateHighModelEffect(trail, tested.Context()).HasValue());
    const ModelEffect& flame = tested.Effect(2);
    EXPECT_EQ(flame.life, 12);
    EXPECT_EQ(flame.objectState.hiddenMesh, 0);
    EXPECT_EQ(flame.transform.scale, 0.5f);
    EXPECT_EQ(flame.alpha, static_cast<float>(8.0 * static_cast<double>(std::bit_cast<float>(0x3e4ccccdu))));
    EXPECT_EQ(flame.velocity, glm::vec3(0.0f));
    EXPECT_EQ(tested.Draws(), 0);
}

// 0x52727b..0x527447: el 0x116/0 con +0xe4 en 2 da una vuelta (un rand(), el giro hacia su dueño más la partida, +0xd8
// 0,4f más, el avance y el 0x116/1, que con lugar suelta su llama 0x5da/0), y después +0xe4 0,1f más y el sonido 0x1ca;
// sin dueño, sólo la luz local.
TEST(HighModelEffectsTest, TheHomingPartTurnsTowardItsOwnerAndLeavesItsTrail)
{
    HighWorld tested;
    ActorState& owner = tested.world->Actors()[3];
    owner.position = {1000, 0, 0};
    const ModelEffectArguments homing{.type = 0x116, .position = {0, 0, 0}, .light = {0, 0, 100}, .owner = &owner};
    ASSERT_TRUE(CreateHighModelEffect(homing, tested.Context()).HasValue());
    ModelEffect& part = tested.Effect(0);
    tested.FillTheParticles();
    ASSERT_TRUE(StepHighModelEffect(part, nullptr, tested.StepContext()).HasValue());
    CrtRandom expected(Seed);
    static_cast<void>(expected.Next());
    glm::vec3 angles{0, 0, 0};
    if ((expected.Next() & 1) == 0)
        angles.x = -20.0f;
    static_cast<void>(Client::Render::Models::MoveToward({0, 0, 0}, angles, {1000, 0, 100}, 10.0f));
    const glm::vec3 moved =
        Client::Render::Models::OriginalVectorRotate({0, -26, 0}, Client::Render::Models::OriginalAngleMatrix(angles));
    EXPECT_EQ(part.transform.angles, angles);
    EXPECT_EQ(part.transform.position, moved);
    EXPECT_EQ(part.speed, static_cast<float>(10.0 + static_cast<double>(std::bit_cast<float>(0x3ecccccdu))));
    EXPECT_EQ(part.turnDirection, static_cast<float>(2.0 + static_cast<double>(std::bit_cast<float>(0x3dcccccdu))));
    EXPECT_EQ(part.life, 19);
    EXPECT_EQ(tested.world->Runtime().lastSoundId, 0x1ca);
    const ModelEffect& trail = tested.Effect(1);
    ASSERT_TRUE(trail.alive);
    EXPECT_EQ(trail.subtype, 1);
    EXPECT_EQ(trail.ownerContext, &part);
    EXPECT_EQ(trail.transform.position, moved);
    EXPECT_EQ(trail.life, 20);
    EXPECT_EQ(tested.Draws(), 2);

    HighWorld room;
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x116, .owner = &room.world->Actors()[3]}, room.Context()).HasValue());
    ASSERT_TRUE(StepHighModelEffect(room.Effect(0), nullptr, room.StepContext()).HasValue());
    CrtRandom replay(Seed);
    Skip(replay, 2);
    ReplayParticle(replay, Client::World::MissileFireParticle, 0);
    EXPECT_EQ(room.random.State(), replay.State());
    EXPECT_EQ(AliveParticles(*room.world, Client::World::MissileFireParticle, 0), 1);

    HighWorld alone;
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x116}, alone.Context()).HasValue());
    ASSERT_TRUE(StepHighModelEffect(alone.Effect(0), nullptr, alone.StepContext()).HasValue());
    EXPECT_EQ(alone.Effect(0).turnDirection, 2.0f);
    EXPECT_EQ(alone.Draws(), 1);
}

// 0x527455..0x527534 y 0x527252: el 0x116/2 gira hacia su dueño, +0xd8 2,4f más y avanza; con la vida múltiplo de 3,
// el joint 0x61c/2 (del lote de los joints: con lugar corta en 0x52752c; lleno, sigue). El /1, con menos de 5 de vida,
// el alpha por 0,7692308f.
TEST(HighModelEffectsTest, TheDivingPartDropsAJointEveryThreeSteps)
{
    HighWorld tested;
    ActorState& owner = tested.world->Actors()[3];
    owner.position = {0, 1000, 0};
    ASSERT_TRUE(
        CreateHighModelEffect({.type = 0x116, .angles = {0, 0, 0}, .subtype = 2, .owner = &owner}, tested.Context())
            .HasValue());
    ModelEffect& dive = tested.Effect(0);
    dive.life = 10;
    ASSERT_TRUE(StepHighModelEffect(dive, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(dive.speed, static_cast<float>(50.0 + static_cast<double>(std::bit_cast<float>(0x4019999au))));
    EXPECT_EQ(dive.life, 9);
    EXPECT_TRUE(HasError(StepHighModelEffect(dive, nullptr, tested.StepContext()), "0x52752c"));
    tested.FillTheJoints();
    dive.life = 9;
    ASSERT_TRUE(StepHighModelEffect(dive, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(tested.Draws(), 3);

    ModelEffect& trail = tested.Effect(1);
    trail = ModelEffect{};
    trail.alive = true;
    trail.type = 0x116;
    trail.subtype = 1;
    trail.life = 5;
    trail.alpha = 0.5f;
    ASSERT_TRUE(StepHighModelEffect(trail, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(trail.alpha, 0.5f);
    ASSERT_TRUE(StepHighModelEffect(trail, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(trail.alpha, static_cast<float>(0.5 * static_cast<double>(std::bit_cast<float>(0x3f44ec4fu))));
    EXPECT_EQ(trail.life, 3);
}

// 0x51cbb2 y 0x527539: el 0x117 nace con vida 10, escala 0,2f y +0xd8 0,1f; con el subtipo 1 o menos, un rand() % 2
// elige entre la inclinación subtipo · −90 + 45 y la fija; el /2, vida 12 y +0xd8 0,4f. Su paso crece y apaga +0x68.
TEST(HighModelEffectsTest, TheDarkLordSkillTiltsByItsSubtypeAndGrows)
{
    for (const std::int32_t subtype : {-1, 0, 1, 2, 3})
    {
        SCOPED_TRACE(subtype);
        HighWorld tested;
        ASSERT_TRUE(CreateHighModelEffect({.type = 0x117, .angles = {1, 2, 3}, .subtype = subtype}, tested.Context())
                        .HasValue());
        ModelEffect& hand = tested.Effect(0);
        CrtRandom expected(Seed);
        if (subtype <= 1)
        {
            const float yaw =
                expected.Next() % 2 != 0 ? static_cast<float>(subtype * -90.0 + 45.0) : (subtype == 0 ? 45.0f : -45.0f);
            EXPECT_EQ(hand.transform.angles, (glm::vec3{45, yaw, 0}));
            EXPECT_EQ(tested.Draws(), 1);
        }
        else
        {
            EXPECT_EQ(hand.transform.angles, (glm::vec3{1, 2, 3}));
            EXPECT_EQ(tested.Draws(), 0);
        }
        EXPECT_EQ(hand.life, subtype == 2 ? 12 : 10);
        EXPECT_EQ(hand.transform.scale, std::bit_cast<float>(0x3e4ccccdu));
        const float growth = std::bit_cast<float>(subtype == 2 ? 0x3ecccccdu : 0x3dcccccdu);
        EXPECT_EQ(hand.speed, growth);
        hand.life = 7;
        hand.objectState.light = 0.9f;
        ASSERT_TRUE(StepHighModelEffect(hand, nullptr, tested.StepContext()).HasValue());
        EXPECT_EQ(hand.transform.scale,
                  static_cast<float>(static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)) + growth));
        const std::uint32_t change = subtype <= 1 ? 0x3ca3d70au : subtype == 2 ? 0x3d75c28fu : 0x3d23d70au;
        const double sign = subtype == 2 ? -1.0 : 1.0;
        EXPECT_EQ(hand.speed, static_cast<float>(static_cast<double>(growth) +
                                                 sign * static_cast<double>(std::bit_cast<float>(change))));
        const std::uint32_t fade = subtype == 2 ? 0x3f2aaaabu : 0x3f0e38e4u;
        EXPECT_EQ(hand.objectState.light,
                  subtype <= 1 ? 0.9f : static_cast<float>(0.9 * static_cast<double>(std::bit_cast<float>(fade))));
        EXPECT_EQ(hand.life, 6);
    }
}

// 0x51da46 y 0x529a4f: el 0x138/0 nace 300 más arriba, oculto hasta la vida 40, crece desde la 40 y, al pasar de 1,
// suelta el 0x138/1 con su luz; por debajo de 15 se achica, se apaga con la vida y baja. El /1 se achica hasta 1.
TEST(HighModelEffectsTest, TheManaRuneGrowsFlashesAndSinks)
{
    HighWorld tested;
    ASSERT_TRUE(
        CreateHighModelEffect({.type = 0x138, .position = {10, 20, 30}, .light = glm::vec3(0.5f)}, tested.Context())
            .HasValue());
    ModelEffect& rune = tested.Effect(0);
    EXPECT_EQ(rune.life, 50);
    EXPECT_EQ(rune.transform.scale, 0.0f);
    EXPECT_EQ(rune.alpha, std::bit_cast<float>(0x3e99999au));
    EXPECT_EQ(rune.transform.position, (glm::vec3{10, 20, 330}));
    EXPECT_EQ(rune.transform.angles, (glm::vec3{0, 0, 45}));
    ASSERT_TRUE(StepHighModelEffect(rune, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(rune.objectState.hiddenMesh, -2);
    rune.life = 41;
    ASSERT_TRUE(StepHighModelEffect(rune, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(rune.objectState.hiddenMesh, 0);
    int steps = 0;
    while (!tested.Effect(1).alive && steps < 10)
    {
        ASSERT_TRUE(StepHighModelEffect(rune, nullptr, tested.StepContext()).HasValue());
        ++steps;
    }
    ASSERT_TRUE(tested.Effect(1).alive);
    EXPECT_EQ(rune.transform.scale, 1.0f);
    EXPECT_EQ(rune.turnDirection, std::bit_cast<float>(0x3c23d70au));
    const ModelEffect& flash = tested.Effect(1);
    EXPECT_EQ(flash.subtype, 1);
    EXPECT_EQ(flash.life, 10);
    EXPECT_EQ(flash.transform.scale, std::bit_cast<float>(0x3f8ccccdu));
    EXPECT_EQ(flash.objectState.blendMesh, -2);
    EXPECT_EQ(flash.objectState.light, std::bit_cast<float>(0x3ecccccdu));
    EXPECT_EQ(flash.light, glm::vec3(0.5f));
    rune.life = 14;
    ASSERT_TRUE(StepHighModelEffect(rune, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(rune.alpha, static_cast<float>(14.0 * static_cast<double>(std::bit_cast<float>(0x3d4ccccdu))));
    EXPECT_EQ(rune.transform.position.z, 320.0f);
    EXPECT_EQ(tested.Draws(), steps + 3);

    ModelEffect& shrinking = tested.Effect(1);
    shrinking.transform.scale = 1.01f;
    ASSERT_TRUE(StepHighModelEffect(shrinking, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(shrinking.transform.scale, 1.0f);
}
// 0x52c981 y 0x52caa5: el 0x812 suelta en cada paso el joint 0x60f/0xc (el /1) o 0x60f/4 (el /0), y el 0x813 tres
// 0x619/6 con dos rand() cada uno; los dos joints son del lote de los joints: con lugar cortan (después de los rand()
// del primero) y, con la tabla llena, siguen. Los dos pierden una vida y no se mueven.
TEST(HighModelEffectsTest, TheEmittersLeaveJointsEveryStep)
{
    for (const std::int32_t subtype : {0, 1})
    {
        SCOPED_TRACE(subtype);
        HighWorld tested;
        ASSERT_TRUE(CreateHighModelEffect({.type = 0x812, .position = {1, 2, 3}, .subtype = subtype}, tested.Context())
                        .HasValue());
        ModelEffect& spark = tested.Effect(0);
        EXPECT_EQ(spark.life, 10);
        EXPECT_TRUE(HasError(StepHighModelEffect(spark, nullptr, tested.StepContext()),
                             subtype == 1 ? "0x52c9a7" : "0x52c9da"));
        tested.FillTheJoints();
        ASSERT_TRUE(StepHighModelEffect(spark, nullptr, tested.StepContext()).HasValue());
        EXPECT_EQ(spark.life, 9);
        EXPECT_EQ(spark.transform.position, (glm::vec3{1, 2, 3}));
        EXPECT_EQ(tested.Draws(), 2);
    }
    HighWorld tested;
    ASSERT_TRUE(
        CreateHighModelEffect({.type = 0x813, .angles = {4, 5, 6}, .subtype = 0x812}, tested.Context()).HasValue());
    ModelEffect& bolt = tested.Effect(0);
    EXPECT_EQ(bolt.life, 5);
    EXPECT_EQ(bolt.transform.angles, glm::vec3(0.0f));
    EXPECT_TRUE(HasError(StepHighModelEffect(bolt, nullptr, tested.StepContext()), "0x52cb85"));
    EXPECT_EQ(tested.Draws(), 3);
    tested.FillTheJoints();
    ASSERT_TRUE(StepHighModelEffect(bolt, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(bolt.life, 4);
    EXPECT_EQ(tested.Draws(), 3 + 7);
}
// El fld/fadd/fstp de cada eje, como Sum de HighModelEffects.
glm::vec3 SumOf(const glm::vec3& a, const glm::vec3& b)
{
    return {static_cast<float>(static_cast<double>(a.x) + static_cast<double>(b.x)),
            static_cast<float>(static_cast<double>(a.y) + static_cast<double>(b.y)),
            static_cast<float>(static_cast<double>(a.z) + static_cast<double>(b.z))};
}

glm::vec3 TurnedBy(const glm::vec3& vector, const glm::vec3& angles)
{
    return Client::Render::Models::OriginalVectorRotate(vector, Client::Render::Models::OriginalAngleMatrix(angles));
}

// 0x51f5fe..0x51f7f8: el 0x6ac vive 10, con +0xd8 en 1 y la velocidad (0, −30, 0), corrido (0, −20, 50) girado. Sus dos
// bocas, (−20, −20, 60) y (30, −20, 60) giradas desde la posición que recibe (no la corrida), sueltan veinte 0x617/1
// cada una: rand() % 20 + x + 5 en la x de los ángulos y la y que suma las vueltas de 18; dos rand() más por joint
// (0x5320a4). Con los joints llenos, solo los cuarenta rand() del alta; con la tabla de efectos llena, nada.
TEST(HighModelEffectsTest, TheFireBreathRaisesFortyJointsFromTwoMouths)
{
    const glm::vec3 origin{1000, 2000, 300};
    const glm::vec3 angles{10, 20, 30};
    HighWorld tested;
    const auto made = CreateHighModelEffect({.type = 0x6ac, .position = origin, .angles = angles}, tested.Context());
    ASSERT_TRUE(made.HasValue()) << made.Error();
    ASSERT_EQ(made.Value(), std::optional<std::size_t>{0});
    const ModelEffect& breath = tested.Effect(0);
    EXPECT_EQ(breath.life, 10);
    EXPECT_EQ(breath.speed, 1.0f);
    EXPECT_EQ(breath.velocity, (glm::vec3{0, -30, 0}));
    EXPECT_EQ(breath.transform.position, SumOf(TurnedBy({0, -20, 50}, angles), origin));
    CrtRandom expected{Seed};
    const std::array<glm::vec3, 2> mouths{SumOf(TurnedBy({-20, -20, 60}, angles), origin),
                                          SumOf(TurnedBy({30, -20, 60}, angles), origin)};
    std::size_t index = 0;
    for (const glm::vec3& mouth : mouths)
    {
        float yaw = angles.y;
        for (int turn = 0; turn < 360; turn += 18, ++index)
        {
            SCOPED_TRACE(index);
            const int tilt = expected.Next() % 20;
            static_cast<void>(expected.Next());
            static_cast<void>(expected.Next());
            yaw = static_cast<float>(static_cast<double>(turn) + static_cast<double>(yaw));
            const auto& joint = tested.world->DrawTail().joints[index];
            ASSERT_TRUE(joint.alive);
            EXPECT_EQ(joint.type, 0x617);
            EXPECT_EQ(joint.subtype, 1);
            EXPECT_EQ(joint.position, mouth);
            EXPECT_EQ(joint.angles,
                      (glm::vec3{static_cast<float>(static_cast<double>(tilt) + static_cast<double>(angles.x) + 5.0),
                                 yaw, angles.z}));
            EXPECT_EQ(joint.width, 2.0f);
        }
    }
    EXPECT_FALSE(tested.world->DrawTail().joints[40].alive);
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(tested.Draws(), 40 * 3);

    HighWorld crowded;
    crowded.FillTheJoints();
    ASSERT_TRUE(
        CreateHighModelEffect({.type = 0x6ac, .position = origin, .angles = angles}, crowded.Context()).HasValue());
    EXPECT_EQ(crowded.Effect(0).transform.position, breath.transform.position);
    EXPECT_EQ(crowded.Draws(), 40);

    HighWorld full;
    full.FillTheEffects();
    const auto none = CreateHighModelEffect({.type = 0x6ac}, full.Context());
    ASSERT_TRUE(none.HasValue());
    EXPECT_FALSE(none.Value().has_value());
    EXPECT_EQ(full.Draws(), 0);
}

// 0x52c7f0..0x52c97c: el paso del 0x6ac prende la luz dinámica de radio 2, saca rand() % 3 + 2 para la escala
// (· (15 − vida) · 0,05f) y pone la partida a 20 de cada lado (los ángulos con la z 90 más) con el fuego 0x5db/0xa
// (0x52c8f7, 0x52c95d; lote de partículas), que con lugar saca sus rand(); con las partículas llenas, sigue sin ellos,
// se mueve (0, −30, 0) girado y, con la vida en 1, 0x51fef0 saca los cuarenta rand() de sus veinte chispas 0x5c5/2 (sin
// lugar, ni ellas ni la 0x5ee/0) y se apaga.
TEST(HighModelEffectsTest, TheFireBreathBurnsAtItsSidesAndSparksAtTheEnd)
{
    const glm::vec3 angles{10, 20, 30};
    HighWorld tested;
    tested.FillTheJoints();
    ASSERT_TRUE(
        CreateHighModelEffect({.type = 0x6ac, .position = {1000, 2000, 300}, .angles = angles}, tested.Context())
            .HasValue());
    ModelEffect& breath = tested.Effect(0);
    ASSERT_EQ(tested.Draws(), 40);
    const auto stepped = StepHighModelEffect(breath, nullptr, tested.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    CrtRandom replay{Seed};
    Skip(replay, 40 + 2);
    ReplayParticle(replay, Client::World::DeathSpiritFireParticle, 0xa, breath.transform.scale);
    ReplayParticle(replay, Client::World::DeathSpiritFireParticle, 0xa, breath.transform.scale);
    EXPECT_EQ(tested.random.State(), replay.State());
    EXPECT_EQ(AliveParticles(*tested.world, Client::World::DeathSpiritFireParticle, 0xa), 2);
    EXPECT_TRUE(std::ranges::any_of(tested.lighting->cells, [](const auto& cell) { return cell[0] > 0; }));

    HighWorld full;
    full.FillTheJoints();
    full.FillTheParticles();
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x6ac, .position = {1000, 2000, 300}, .angles = angles}, full.Context())
                    .HasValue());
    ModelEffect& burning = full.Effect(0);
    CrtRandom expected{Seed};
    for (int draw = 0; draw < 40; ++draw)
        static_cast<void>(expected.Next());
    const glm::vec3 turned{angles.x, angles.y, 120.0f};
    for (std::int32_t life = 10; life >= 1; --life)
    {
        SCOPED_TRACE(life);
        const glm::vec3 before = burning.transform.position;
        ASSERT_TRUE(StepHighModelEffect(burning, nullptr, full.StepContext()).HasValue());
        static_cast<void>(expected.Next());
        const int size = expected.Next() % 3 + 2;
        EXPECT_EQ(burning.transform.scale,
                  static_cast<float>(static_cast<double>(size) *
                                     (static_cast<double>(15 - life) * static_cast<double>(0.05f))));
        EXPECT_EQ(burning.start, SumOf(TurnedBy({0, -20, 0}, turned), before));
        EXPECT_EQ(burning.transform.position, SumOf(TurnedBy({0, -30, 0}, angles), before));
        if (life == 1)
        {
            for (int draw = 0; draw < 40; ++draw)
                static_cast<void>(expected.Next());
        }
        EXPECT_EQ(full.random.State(), expected.State());
    }
    EXPECT_FALSE(burning.alive);
    EXPECT_EQ(full.Draws(), 40 + 10 * 2 + 40);
}
// 0x51dacb..0x51db6e: el 0x139 vive 35, con +0xe4 en 2, +0xd8 en 10, escala 1,2f y la velocidad (0, −5, 0); copia sus
// ángulos en +0x28, toma la partida de la posición de su dueño y sube 150. Un rand() % 80 + 10 (r) mueve la z de
// +0x28 subtipo · r − r. Sin dueño corta (el original lee la dirección 0x10); con la tabla llena, nada.
TEST(HighModelEffectsTest, TheJavelinStartsOverItsThrowerAimingAtItsOwner)
{
    for (const std::int32_t subtype : {0, 1, 2})
    {
        SCOPED_TRACE(subtype);
        HighWorld tested;
        ActorState& owner = tested.world->Actors()[3];
        owner.position = {500, 600, 700};
        const auto made = CreateHighModelEffect({.type = 0x139,
                                                 .position = {100, 200, 300},
                                                 .angles = {1, 2, 3},
                                                 .light = {0.5f, 0.6f, 0.7f},
                                                 .subtype = subtype,
                                                 .owner = &owner},
                                                tested.Context());
        ASSERT_TRUE(made.HasValue()) << made.Error();
        const ModelEffect& javelin = tested.Effect(0);
        CrtRandom expected{Seed};
        const double spread = static_cast<double>(expected.Next() % 80 + 10);
        EXPECT_EQ(javelin.life, 35);
        EXPECT_EQ(javelin.turnDirection, 2.0f);
        EXPECT_EQ(javelin.speed, 10.0f);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(javelin.transform.scale), 0x3f99999au);
        EXPECT_EQ(javelin.velocity, (glm::vec3{0, -5, 0}));
        EXPECT_EQ(javelin.start, owner.position);
        EXPECT_EQ(javelin.transform.position, (glm::vec3{100, 200, 450}));
        EXPECT_EQ(javelin.copiedAngles,
                  (glm::vec3{1, 2, static_cast<float>(static_cast<double>(subtype) * spread - spread + 3.0)}));
        EXPECT_EQ(tested.Draws(), 1);
    }
    HighWorld lonely;
    EXPECT_TRUE(HasError(CreateHighModelEffect({.type = 0x139}, lonely.Context()), "0x51db1b"));
    HighWorld full;
    full.FillTheEffects();
    const auto none = CreateHighModelEffect({.type = 0x139}, full.Context());
    ASSERT_TRUE(none.HasValue());
    EXPECT_FALSE(none.Value().has_value());
    EXPECT_EQ(full.Draws(), 0);
}

// 0x529b86..0x529de9 y la cola (0x52ccb0 → 0x52cd94, sin moverse): con 25 o más de vida el 0x139 sube +0xe4 de a 10 y
// avanza con +0x28; de 24 a 20, +0xd8 1,5 más, avanza, +0xe4 en rand() % 30 + 30 y la velocidad y 8 menos. Siempre
// la partida sobre su dueño, la escala 0,015f más, la z de los ángulos más +0xe4, +0x68 y el alpha en vida · 0,1f, la
// z del /1 en la partida más FSIN(vida · 0,1f) · 30 y la luz dinámica.
TEST(HighModelEffectsTest, TheJavelinClimbsSpinsAndBrakes)
{
    HighWorld tested;
    ActorState& owner = tested.world->Actors()[3];
    owner.position = {500, 600, 700};
    ASSERT_TRUE(CreateHighModelEffect(
                    {.type = 0x139, .position = {100, 200, 300}, .angles = {1, 2, 3}, .subtype = 1, .owner = &owner},
                    tested.Context())
                    .HasValue());
    ModelEffect& javelin = tested.Effect(0);
    const glm::vec3 aim = javelin.copiedAngles;
    ASSERT_TRUE(StepHighModelEffect(javelin, nullptr, tested.StepContext()).HasValue());
    const glm::vec3 start{500, 600, 850};
    EXPECT_EQ(javelin.start, start);
    EXPECT_EQ(javelin.transform.scale,
              static_cast<float>(static_cast<double>(std::bit_cast<float>(0x3f99999au)) + static_cast<double>(0.015f)));
    EXPECT_EQ(javelin.turnDirection, 12.0f);
    const glm::vec3 advanced = SumOf(TurnedBy({0, -5, 0}, aim), {100, 200, 450});
    EXPECT_EQ(javelin.transform.position.x, advanced.x);
    EXPECT_EQ(javelin.transform.position.y, advanced.y);
    const auto wave = Client::Render::Models::Detail::SineProductPC53(35.0 * static_cast<double>(0.1f), 30.0f);
    ASSERT_TRUE(wave.has_value());
    EXPECT_EQ(javelin.transform.position.z, static_cast<float>(*wave + 850.0));
    EXPECT_EQ(javelin.transform.angles.z, 15.0f);
    EXPECT_EQ(javelin.objectState.light, static_cast<float>(35.0 * static_cast<double>(0.1f)));
    EXPECT_EQ(javelin.alpha, javelin.objectState.light);
    EXPECT_EQ(javelin.life, 34);
    EXPECT_TRUE(std::ranges::any_of(tested.lighting->cells, [](const auto& cell) { return cell[0] > 0; }));
    EXPECT_EQ(tested.Draws(), 1 + 1);

    javelin.life = 22;
    ASSERT_TRUE(StepHighModelEffect(javelin, nullptr, tested.StepContext()).HasValue());
    CrtRandom expected{Seed};
    for (int draw = 0; draw < 3; ++draw)
        static_cast<void>(expected.Next());
    EXPECT_EQ(javelin.speed, 11.5f);
    EXPECT_EQ(javelin.turnDirection, static_cast<float>(expected.Next() % 30 + 30));
    EXPECT_EQ(javelin.velocity, (glm::vec3{0, -13, 0}));
    EXPECT_EQ(javelin.life, 21);
    EXPECT_EQ(tested.random.State(), expected.State());
}

// 0x529bde..0x529c69: por debajo de 20, 0x4e9810 gira +0x28 hacia la partida. Lejos (100 o más), la partícula 0x6ad/1,
// que MuRet no tiene: con lugar corta. Con las partículas llenas sigue; cerca, va a la partida y crece 0,04f más.
TEST(HighModelEffectsTest, TheJavelinHuntsItsOwnerAndTrailsFire)
{
    HighWorld tested;
    ActorState& owner = tested.world->Actors()[3];
    owner.position = {500, 600, 700};
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x139, .position = {100, 200, 300}, .owner = &owner}, tested.Context())
                    .HasValue());
    ModelEffect& javelin = tested.Effect(0);
    javelin.life = 19;
    EXPECT_TRUE(HasError(StepHighModelEffect(javelin, nullptr, tested.StepContext()), "0x529c69"));
    EXPECT_EQ(tested.Draws(), 1 + 1);

    tested.FillTheParticles();
    javelin.life = 18;
    javelin.transform.position = {500, 600, 860};
    const float scale = javelin.transform.scale;
    ASSERT_TRUE(StepHighModelEffect(javelin, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(javelin.transform.scale,
              static_cast<float>(
                  static_cast<double>(static_cast<float>(static_cast<double>(scale) + static_cast<double>(0.015f))) +
                  static_cast<double>(0.04f)));
    EXPECT_EQ(javelin.life, 17);
    EXPECT_EQ(tested.Draws(), 1 + 1 + 2);
}

// 0x51ed79 y 0x52b4a7: el 0x615 vive 10 y, con dueño y el subtipo 0, se corre (−25, −80, 0) girado por los ángulos del
// dueño. Su paso no hace nada hasta que le quedan menos de 5 pasos; con la vida par, tres rand() (x e y de rand() % 64
// − 32 y la escala rand() % 10 + 50) y el joint 0x615/5, que MuRet no tiene: con lugar corta (0x52b560); con los joints
// llenos sigue. La cola no lo mueve (sin velocidad) ni lo dibuja.
TEST(HighModelEffectsTest, TheThunderShiftsOverItsOwnerAndDropsBoltsAtTheEnd)
{
    HighWorld tested;
    ActorState& owner = tested.world->Actors()[3];
    owner.angles = {0, 0, 90};
    const glm::vec3 origin{100, 200, 300};
    ASSERT_TRUE(
        CreateHighModelEffect({.type = 0x615, .position = origin, .owner = &owner}, tested.Context()).HasValue());
    ModelEffect& thunder = tested.Effect(0);
    const glm::vec3 shifted = SumOf(TurnedBy({-25, -80, 0}, owner.angles), origin);
    EXPECT_EQ(thunder.life, 10);
    EXPECT_EQ(thunder.transform.position, shifted);
    EXPECT_EQ(tested.Draws(), 0);
    for (const std::int32_t subtype : {1, 2})
    {
        HighWorld other;
        ASSERT_TRUE(CreateHighModelEffect({.type = 0x615, .position = origin, .subtype = subtype, .owner = &owner},
                                          other.Context())
                        .HasValue());
        EXPECT_EQ(other.Effect(0).transform.position, origin);
    }
    HighWorld lonely;
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x615, .position = origin}, lonely.Context()).HasValue());
    EXPECT_EQ(lonely.Effect(0).transform.position, origin);
    EXPECT_EQ(lonely.Effect(0).life, 10);

    for (int step = 0; step < 6; ++step)
        ASSERT_TRUE(StepHighModelEffect(thunder, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(thunder.life, 4);
    EXPECT_EQ(thunder.transform.position, shifted);
    EXPECT_EQ(tested.Draws(), 6);
    EXPECT_TRUE(HasError(StepHighModelEffect(thunder, nullptr, tested.StepContext()), "0x52b560"));
    EXPECT_EQ(tested.Draws(), 6 + 1 + 3);
    tested.FillTheJoints();
    ASSERT_TRUE(StepHighModelEffect(thunder, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(tested.Draws(), 6 + 4 + 4);
    ASSERT_TRUE(StepHighModelEffect(thunder, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(tested.Draws(), 6 + 4 + 4 + 1);
    EXPECT_EQ(thunder.life, 2);
}

// 0x51efa5 y 0x52bb79: el 0x621/1 guarda la posición como partida y vive 20; su paso pone el origen del modelo de su
// dueño en la partida, va al punto (0, 0, 10) del hueso 33 del dueño y hace las tres emisiones del /0 (cinco rand()
// cada una). El /0 nace en World (vida 10) y su paso es de World.
TEST(HighModelEffectsTest, TheHeldPiercingGlowFollowsTheOwnersBone)
{
    HighWorld tested;
    auto sprites = std::make_unique<Client::World::FrameSprites>();
    tested.services.sprites = sprites.get();
    tested.services.effects = &tested.world->Map().effects;
    ActorState& owner = tested.world->Actors()[3];
    owner.type = 0x4a2;
    owner.bodyPose.assign(34, Client::Render::Models::BoneTransform(1.0f));
    owner.bodyPose[33][3] = {1.0f, 2.0f, 3.0f};
    tested.world->PlaceModel(0x4a2, {.scale = 1.0f, .origin = {7, 8, 9}});
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x621, .position = {100, 200, 300}, .subtype = 1, .owner = &owner},
                                      tested.Context())
                    .HasValue());
    ModelEffect& glow = tested.Effect(0);
    EXPECT_EQ(glow.life, 20);
    EXPECT_EQ(glow.start, (glm::vec3{100, 200, 300}));
    EXPECT_TRUE(Client::Scenes::StepsAsHighModelEffect(glow));
    ASSERT_TRUE(StepHighModelEffect(glow, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(tested.world->ModelPlacement(0x4a2).origin, (glm::vec3{100, 200, 300}));
    EXPECT_EQ(glow.transform.position, Client::Scenes::ActorBonePoint(owner, 33, {0.0f, 0.0f, 10.0f}, *tested.world));
    EXPECT_EQ(glow.life, 19);
    EXPECT_EQ(tested.Draws(), 1 + 3 * 5);
    EXPECT_TRUE(sprites->Sprites()[2].alive);

    ASSERT_TRUE(CreateHighModelEffect({.type = 0x621}, tested.Context()).HasValue());
    EXPECT_EQ(tested.Effect(1).life, 10);
    EXPECT_FALSE(Client::Scenes::StepsAsHighModelEffect(tested.Effect(1)));
}
} // namespace
