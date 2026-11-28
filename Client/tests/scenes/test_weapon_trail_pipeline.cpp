#include "Models/IModelLibrary.h"
#include "Actors/CharacterStep.h"
#include "Session/CharacterStepHooks.h"

#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Assets/FaunaModels.h"
#include "Scenes/GameDrawTail.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Particles/Particles.h"
#include "Terrain/WorldTerrain.h"

#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace
{
const Client::Session::CharacterStepHooks StepHooks{};
class RibbonModels final : public Client::Models::IModelLibrary
{
public:
    RibbonModels()
    {
        skeleton.actions.resize(8, {.repeats = true, .speed = 0.0f, .keyCount = 10});
        Client::Models::SkeletonBone bone;
        bone.actions.assign(8, std::vector<Client::Models::BoneKey>(10));
        skeleton.bones.assign(90, bone);
    }
    const Client::Data::Models::Model* Find(Client::Models::ModelId) const override
    {
        return nullptr;
    }
    const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId) const override
    {
        return &skeleton;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return {};
    }
    Client::Models::Skeleton skeleton;
};

// 4fd5c1..4fd5df: 1b1/1b3 pasan por dos productores distintos que apendean las mismas keys.
TEST(WeaponTrailPipelineTest, TheWholeBodyRunsEveryProducerBeforeAndAfterTheUnconditionalRandom)
{
    struct Case
    {
        std::uint16_t type;
        std::uint8_t action;
        std::int32_t count;
        std::int32_t firstKey;
        std::int32_t secondKey;
    };
    constexpr std::array cases{Case{0x1a7, 3, 10, 23, 34}, Case{0x1b3, 3, 20, 25, 0}, Case{0x1b1, 4, 20, 80, 84},
                               Case{0x1b8, 3, 10, 23, 24}};
    for (const auto& probe : cases)
    {
        SCOPED_TRACE(probe.type);
        auto world = std::make_unique<Client::Kernel::WorldState>();
        RibbonModels models;
        Client::Assets::FaunaModels faunaModels;
        faunaModels.Bind(&models);
        world->Runtime().map = 0;
        Client::Terrain::WorldTerrain terrain;
        terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        Client::Kernel::ActorState actor;
        actor.alive = true;
        actor.type = probe.type;
        actor.byte88 = 2;
        actor.scale = 1.0f;
        actor.position = {10000, 20000, 0}; // Fuera del trapecio: el sufijo no pide efectos del dibujo.
        actor.animation.action = probe.action;
        actor.animation.previousAction = probe.action;
        actor.animation.frame = 3.0f;
        actor.hands[0].model = actor.hands[1].model = 0xffff;
        Client::Crt::CrtRandom random{123};

        const auto stepped = Client::Actors::StepCharacter(
            actor,
            {.world = *world, .random = random, .terrain = &terrain, .faunaModels = &faunaModels, .hooks = &StepHooks});

        // 1b1 alcanza después el sprite de un helper CL11; la estela ya tiene ambas tandas.
        if (probe.type == 0x1b1)
        {
            ASSERT_FALSE(stepped.HasValue());
            EXPECT_NE(stepped.Error().find("0x431d5c"), std::string::npos) << stepped.Error();
        }
        else
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        const auto& first = world->DrawTail().ribbons[0];
        EXPECT_TRUE(first.alive);
        EXPECT_EQ(first.key, probe.firstKey);
        EXPECT_EQ(first.count, probe.count);
        EXPECT_EQ(first.ownerContext, &actor);
        const auto& second = world->DrawTail().ribbons[1];
        EXPECT_EQ(second.alive, probe.secondKey != 0);
        if (probe.secondKey != 0)
        {
            EXPECT_EQ(second.key, probe.secondKey);
            EXPECT_EQ(second.count, probe.count);
        }
    }
}
// 0x4fd580 antes de 0x4fd5a0: una partícula cada dos muestras y después el rand() común de 0x432bd0.
TEST(WeaponTrailPipelineTest, TheKalimaWeaponTrailReturnsThroughTheCharacterStep)
{
    std::uint32_t withoutBirths = 0;
    for (const bool full : {true, false})
    {
        SCOPED_TRACE(full);
        auto world = std::make_unique<Client::Kernel::WorldState>();
        RibbonModels models;
        Client::Assets::FaunaModels faunaModels;
        faunaModels.Bind(&models);
        world->Runtime().map = 0;
        Client::Terrain::WorldTerrain terrain;
        terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        Client::Kernel::ActorState actor;
        actor.alive = true;
        actor.type = 0x194;
        actor.byte88 = 2;
        actor.scale = 1.0f;
        actor.position = {10000, 20000, 0};
        actor.animation.action = actor.animation.previousAction = 3;
        actor.animation.frame = 3.0f;
        actor.hands[0].model = actor.hands[1].model = 0xffff;
        Client::Crt::CrtRandom random{0x12345678u};

        if (full)
            for (auto& particle : world->Particles().All())
                particle.alive = true;

        const auto stepped = Client::Actors::StepCharacter(actor, {.world = *world,
                                                                   .random = random,
                                                                   .terrain = &terrain,
                                                                   .particles = &world->Particles(),
                                                                   .faunaModels = &faunaModels,
                                                                   .hooks = &StepHooks});

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        const auto& ribbon = world->DrawTail().ribbons[0];
        ASSERT_TRUE(ribbon.alive);
        EXPECT_EQ(ribbon.count, 10);
        EXPECT_EQ(ribbon.type, 2);
        EXPECT_EQ(ribbon.ownerContext, &actor);
        if (full)
        {
            withoutBirths = random.State();
            continue;
        }
        for (std::size_t slot = 0; slot < 5; ++slot)
        {
            const auto& particle = world->Particles().All()[slot];
            EXPECT_TRUE(particle.alive);
            EXPECT_EQ(particle.type, 0x5dc);
            EXPECT_EQ(particle.subtype, 12);
            EXPECT_EQ(particle.position, ribbon.second[8 - 2 * slot]);
        }
        EXPECT_FALSE(world->Particles().All()[5].alive);
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(withoutBirths, 5 * 4).State());
    }
}

// 4fcbe3 vuelve a 4f9580 sin volver a publicar+a0 después de cambiar la acción del actor.
TEST(WeaponTrailPipelineTest, AdvancingAgainUsesThePublishedActionForItsSpeed)
{
    auto world = std::make_unique<Client::Kernel::WorldState>();
    RibbonModels models;
    models.skeleton.actions[0].speed = 0.25f;
    models.skeleton.actions[3].speed = 2.0f;
    Client::Assets::FaunaModels faunaModels;
    faunaModels.Bind(&models);
    world->Runtime().map = 30;
    Client::Terrain::WorldTerrain terrain;
    terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    Client::Kernel::ActorState actor;
    actor.alive = true;
    actor.type = 0x153;
    actor.byte88 = 2;
    actor.word376 = 221;
    actor.scale = 1.0f;
    actor.position = {10000, 20000, 0};
    actor.animation.action = actor.animation.previousAction = 3;
    actor.animation.frame = 9.0f;
    actor.hands[0].model = actor.hands[1].model = 0xffff;
    Client::Crt::CrtRandom random{123};

    const auto stepped = Client::Actors::StepCharacter(
        actor,
        {.world = *world, .random = random, .terrain = &terrain, .faunaModels = &faunaModels, .hooks = &StepHooks});

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(actor.animation.action, 0);
    EXPECT_EQ(actor.animation.frame, 2.0f);
    EXPECT_EQ(faunaModels.PoseFrame(actor.type), 2.0f);
}

} // namespace
