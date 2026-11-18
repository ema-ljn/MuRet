#include "Scenes/CharacterStep.h"

#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace
{
class RibbonModels final : public Client::Scenes::IModelLibrary
{
public:
    RibbonModels()
    {
        skeleton.actions.resize(8, {.repeats = true, .speed = 0.0f, .keyCount = 10});
        Client::Render::Models::SkeletonBone bone;
        bone.actions.assign(8, std::vector<Client::Render::Models::BoneKey>(10));
        skeleton.bones.assign(90, bone);
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
        auto world = std::make_unique<Client::Scenes::WorldState>();
        RibbonModels models;
        world->FaunaResources().Bind(&models);
        world->Runtime().map = 0;
        world->Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        Client::Scenes::ActorState actor;
        actor.alive = true;
        actor.type = probe.type;
        actor.byte88 = 2;
        actor.scale = 1.0f;
        actor.position = {10000, 20000, 0}; // Fuera del trapecio: el sufijo no pide efectos del dibujo.
        actor.animation.action = probe.action;
        actor.animation.previousAction = probe.action;
        actor.animation.frame = 3.0f;
        actor.hands[0].model = actor.hands[1].model = 0xffff;
        Client::Render::Models::CrtRandom random{123};

        const auto stepped = Client::Scenes::StepCharacter(actor, {.world = *world, .random = random});

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
// 4fcbe3 vuelve a 4f9580 sin volver a publicar+a0 después de cambiar la acción del actor.
TEST(WeaponTrailPipelineTest, AdvancingAgainUsesThePublishedActionForItsSpeed)
{
    auto world = std::make_unique<Client::Scenes::WorldState>();
    RibbonModels models;
    models.skeleton.actions[0].speed = 0.25f;
    models.skeleton.actions[3].speed = 2.0f;
    world->FaunaResources().Bind(&models);
    world->Runtime().map = 30;
    world->Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    Client::Scenes::ActorState actor;
    actor.alive = true;
    actor.type = 0x153;
    actor.byte88 = 2;
    actor.word376 = 221;
    actor.scale = 1.0f;
    actor.position = {10000, 20000, 0};
    actor.animation.action = actor.animation.previousAction = 3;
    actor.animation.frame = 9.0f;
    actor.hands[0].model = actor.hands[1].model = 0xffff;
    Client::Render::Models::CrtRandom random{123};

    const auto stepped = Client::Scenes::StepCharacter(actor, {.world = *world, .random = random});

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(actor.animation.action, 0);
    EXPECT_EQ(actor.animation.frame, 2.0f);
    EXPECT_EQ(world->FaunaResources().PoseFrame(actor.type), 2.0f);
}

} // namespace
