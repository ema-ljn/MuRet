#include "Effects/ActorEffectHooks.h"

#include "Actors/EffectHooks.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainLight.h"
#include "Kernel/ActorState.h"
#include "Kernel/ModelEffectArguments.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldState.h"
#include "Effects/GuildMarkEffects.h"
#include "Effects/HighModelEffects.h"
#include "Effects/SkullEffects.h"
#include "Effects/WizardEffects.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/WorldTerrain.h"
#include "Effects/BlastModelEffects.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Effects/SkinShellEffects.h"
#include "Effects/WizardModelEffects.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>

// Effects::ActorEffectHooks: each slot does what the function it names does, in the same table and with the same rand()
// calls.
namespace
{
using Client::Crt::CrtRandom;
using Client::Effects::ModelEffects;
using Client::Kernel::ActorState;
using Client::Kernel::ModelEffectArguments;
using Client::Kernel::WorldState;

const Client::Effects::ActorEffectHooks Hooks;
constexpr std::uint32_t Seed = 0x2468aceu;
constexpr glm::vec3 Position{1200.0f, 3400.0f, 150.0f};
constexpr glm::vec3 Angles{0.0f, 0.0f, 45.0f};
constexpr glm::vec3 Light{0.5f, 0.25f, 1.0f};

// What a creation writes, slot by slot. An effect owned by `madeOwner` must be owned by `wantedOwner` in the twin.
void ExpectTheSameEffects(const ModelEffects& made, const ModelEffects& wanted, const void* madeOwner = nullptr,
                          const void* wantedOwner = nullptr)
{
    for (std::size_t slot = 0; slot < made.Effects().size(); ++slot)
    {
        SCOPED_TRACE(slot);
        const auto& effect = made.Effects()[slot];
        const auto& twin = wanted.Effects()[slot];
        ASSERT_EQ(effect.alive, twin.alive);
        if (!effect.alive)
            continue;
        EXPECT_EQ(effect.type, twin.type);
        EXPECT_EQ(effect.subtype, twin.subtype);
        EXPECT_EQ(effect.transform.position, twin.transform.position);
        EXPECT_EQ(effect.transform.angles, twin.transform.angles);
        EXPECT_EQ(effect.transform.scale, twin.transform.scale);
        EXPECT_EQ(effect.light, twin.light);
        EXPECT_EQ(effect.life, twin.life);
        if (madeOwner != nullptr && effect.ownerContext == madeOwner)
            EXPECT_EQ(twin.ownerContext, wantedOwner);
        else
            EXPECT_EQ(effect.ownerContext, twin.ownerContext);
    }
}

std::size_t LiveParticles(const Client::Particles::Particles& particles)
{
    std::size_t live = 0;
    for (const auto& particle : particles.All())
        live += particle.alive ? 1u : 0u;
    return live;
}

// 0x518ac0: only the live effects of that type, owner and subtype; −1 is any subtype.
TEST(ActorEffectHooksTest, TheKillTurnsOffOnlyTheOwnersEffectsOfThatTypeAndSubtype)
{
    auto effects = std::make_unique<ModelEffects>();
    ActorState owner;
    ActorState other;
    const auto swell = effects->CreateSkillFlare(Position, Angles, Light, 1, &owner);
    const auto guard = effects->CreateSkillFlare(Position, Angles, Light, 2, &owner);
    const auto others = effects->CreateSkillFlare(Position, Angles, Light, 1, &other);
    const auto fullSet = effects->CreateFullSetFlare(Position, Angles, Light, &owner);
    ASSERT_TRUE(swell && guard && others && fullSet);

    EXPECT_TRUE(Hooks.KillTheEffectsOf(*effects, 0x5ac, &owner, 1));
    EXPECT_FALSE(effects->Effects()[*swell].alive);
    EXPECT_TRUE(effects->Effects()[*guard].alive);
    EXPECT_TRUE(effects->Effects()[*others].alive);
    EXPECT_TRUE(effects->Effects()[*fullSet].alive);
    EXPECT_FALSE(Hooks.KillTheEffectsOf(*effects, 0x5ac, &owner, 1));
    EXPECT_FALSE(Hooks.KillTheEffectsOf(*effects, 0xc1, &owner, -1));

    EXPECT_TRUE(Hooks.KillTheEffectsOf(*effects, 0x5ac, &owner, -1));
    EXPECT_FALSE(effects->Effects()[*guard].alive);
    EXPECT_TRUE(effects->Effects()[*others].alive);
    EXPECT_FALSE(effects->Effects()[*fullSet].alive);
}

TEST(ActorEffectHooksTest, TheFlaresTheShardsAndTheDebrisAreTheTablesOwn)
{
    auto made = std::make_unique<ModelEffects>();
    auto wanted = std::make_unique<ModelEffects>();
    ActorState owner;
    CrtRandom random{Seed};
    CrtRandom twin{Seed};
    EXPECT_EQ(Hooks.CreateTheSkillFlare(*made, Position, Angles, Light, 2, &owner),
              wanted->CreateSkillFlare(Position, Angles, Light, 2, &owner));
    EXPECT_EQ(Hooks.CreateTheFullSetFlare(*made, Position, Angles, Light, &owner),
              wanted->CreateFullSetFlare(Position, Angles, Light, &owner));
    EXPECT_EQ(Hooks.CreateTheIceShard(*made, Position, Angles, Light, random),
              wanted->CreateIceShard(Position, Angles, Light, twin));
    EXPECT_EQ(Hooks.CreateTheDebris(*made, 0x10b, Position, Angles, Light, random, 2),
              wanted->CreateDebris(0x10b, Position, Angles, Light, twin, 2));
    EXPECT_NE(random.State(), Seed);
    EXPECT_EQ(random.State(), twin.State());
    ExpectTheSameEffects(*made, *wanted);
}

TEST(ActorEffectHooksTest, TheShellAndTheBlastAreTheirModulesOwn)
{
    auto made = std::make_unique<ModelEffects>();
    auto wanted = std::make_unique<ModelEffects>();
    CrtRandom random{Seed};
    CrtRandom twin{Seed};
    const ModelEffectArguments shell{
        .type = Client::Effects::SkinShellEffect, .position = Position, .angles = Angles, .light = Light, .subtype = 1};
    EXPECT_EQ(Hooks.CreateTheSkinShell(*made, shell, random), Client::Effects::CreateSkinShell(shell, *wanted, twin));
    const ModelEffectArguments gateStone{
        .type = 0x10c, .position = Position, .angles = Angles, .light = Light, .subtype = 2};
    const auto blasted = Hooks.CreateTheBlastEffect(*made, gateStone, random);
    const auto expected = Client::Effects::CreateBlastModelEffect(
        *wanted, gateStone, Client::Effects::ModelEffectOwner{}, twin, Client::Effects::BlastEffectServices{});
    ASSERT_TRUE(blasted.HasValue()) << blasted.Error();
    ASSERT_TRUE(expected.HasValue()) << expected.Error();
    EXPECT_EQ(blasted.Value(), expected.Value());
    EXPECT_NE(random.State(), Seed);
    EXPECT_EQ(random.State(), twin.State());
    ExpectTheSameEffects(*made, *wanted);
}

// The Dark Lord's 0x117/2 and the wizard's 0xc1/1 go to the world's table, with the world's particles, the scene's
// dynamic light and the light the creation overwrites.
TEST(ActorEffectHooksTest, TheHighAndTheWizardEffectsGoToTheWorldsTable)
{
    auto world = std::make_unique<WorldState>();
    auto twinWorld = std::make_unique<WorldState>();
    CrtRandom random{Seed};
    CrtRandom twin{Seed};
    const ModelEffectArguments skill{
        .type = 0x117, .position = Position, .angles = Angles, .light = Light, .subtype = 2};
    const auto high = Hooks.CreateTheHighEffect(skill, *world, random, 1000.0f);
    const auto wantedHigh = Client::Effects::CreateHighModelEffect(
        skill, Client::Effects::HighEffectContext{.world = *twinWorld, .random = twin, .worldClock = 1000.0f});
    ASSERT_TRUE(high.HasValue()) << high.Error();
    ASSERT_TRUE(wantedHigh.HasValue()) << wantedHigh.Error();
    EXPECT_EQ(high.Value(), wantedHigh.Value());
    EXPECT_TRUE(high.Value().has_value());

    ActorState owner;
    glm::vec3 light = Light;
    glm::vec3 twinLight = Light;
    const ModelEffectArguments ice{.type = Client::Effects::IceEffect,
                                   .position = Position,
                                   .angles = Angles,
                                   .light = Light,
                                   .subtype = 1,
                                   .owner = &owner};
    const auto lighting = std::make_unique<Client::Terrain::TerrainLighting>();
    const auto twinLighting = std::make_unique<Client::Terrain::TerrainLighting>();
    const auto iced =
        Hooks.CreateTheWizardEffect(*world, random,
                                    Client::Actors::WizardEffectScene{.terrain = &Client::Terrain::EmptyTerrain(),
                                                                      .dynamicLighting = lighting.get()},
                                    ice, &light);
    const auto services = Client::Effects::WizardEffectServicesOf(
        *twinWorld, twin,
        Client::Effects::WizardSceneParts{.particles = &twinWorld->Particles(),
                                          .terrain = &Client::Terrain::EmptyTerrain(),
                                          .dynamicLighting = twinLighting.get()});
    const auto wantedIce =
        Client::Effects::CreateWizardModelEffect(twinWorld->Map().effects, ice, twin, services, &twinLight);
    ASSERT_TRUE(iced.HasValue()) << iced.Error();
    ASSERT_TRUE(wantedIce.HasValue()) << wantedIce.Error();
    EXPECT_EQ(iced.Value(), wantedIce.Value());
    EXPECT_TRUE(iced.Value().has_value());
    EXPECT_EQ(light, twinLight);
    EXPECT_TRUE(std::ranges::any_of(lighting->cells, [](const Client::Data::Terrain::TerrainLightColor& cell)
                                    { return cell[0] != 0.0f; }));
    EXPECT_TRUE(lighting->cells == twinLighting->cells);
    EXPECT_EQ(random.State(), twin.State());
    EXPECT_EQ(LiveParticles(world->Particles()), LiveParticles(twinWorld->Particles()));
    ExpectTheSameEffects(world->Map().effects, twinWorld->Map().effects);
}

// The skull 0x628/0 and the guild mark 0x140, with the character as owner.
TEST(ActorEffectHooksTest, TheSkullAndTheGuildMarkAreTheirModulesOwn)
{
    auto world = std::make_unique<WorldState>();
    auto twinWorld = std::make_unique<WorldState>();
    ActorState& actor = world->Actors()[2];
    ActorState& twinActor = twinWorld->Actors()[2];
    for (ActorState* each : {&actor, &twinActor})
    {
        each->type = 0x20a;
        each->position = Position;
        each->angles = Angles;
        each->light = Light;
        each->byte369 = 1;
    }
    CrtRandom random{Seed};
    CrtRandom twin{Seed};
    ASSERT_TRUE(Hooks.PutTheSkullOn(actor, *world, random, 0x4e688c).HasValue());
    ASSERT_TRUE(Client::Effects::PutTheSkullOn(twinActor, *twinWorld, twin, 0x4e688c).HasValue());

    constexpr float Clock = 6000.0f;
    EXPECT_TRUE(Hooks.IsTheGuildMarkDue(actor, Clock));
    EXPECT_EQ(Hooks.IsTheGuildMarkDue(actor, Clock), Client::Effects::GuildMarkIsDue(twinActor, Clock));
    const glm::vec3 bone{1210.0f, 3390.0f, 260.0f};
    ASSERT_TRUE(
        Hooks.RaiseTheGuildMark(actor, bone, world->Map().effects, world->Particles(), random, Clock).HasValue());
    ASSERT_TRUE(Client::Effects::RaiseTheGuildMark(twinActor, bone, twinWorld->Map().effects, twinWorld->Particles(),
                                                   twin, Clock)
                    .HasValue());
    EXPECT_FALSE(Hooks.IsTheGuildMarkDue(actor, Clock));
    EXPECT_EQ(actor.dword36c, twinActor.dword36c);
    EXPECT_NE(random.State(), Seed);
    EXPECT_EQ(random.State(), twin.State());
    EXPECT_EQ(LiveParticles(world->Particles()), LiveParticles(twinWorld->Particles()));
    ExpectTheSameEffects(world->Map().effects, twinWorld->Map().effects, &actor, &twinActor);
}
} // namespace
