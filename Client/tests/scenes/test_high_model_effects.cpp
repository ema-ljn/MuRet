#include "Scenes/HighModelEffects.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Render/Models/Cloth.h"
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
#include "World/TerrainLighting.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
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

    HighEffectStepContext StepContext()
    {
        return {.world = *world, .random = random, .services = services, .worldClock = clock};
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
