#include "Models/IModelLibrary.h"
#include "Actors/Drawing/VertexEffects.h"

#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Kernel/JointRequest.h"
#include "Models/Skeleton.h"
#include "Models/Skinning.h"
#include "Models/ModelId.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/DrawTailState.h"
#include "Effects/ActorEffectHooks.h"
#include "Session/CharacterDrawHooks.h"
#include "Effects/BlastModelEffects.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Effects/SkinShellEffects.h"
#include "Particles/Sprites.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
const Client::Effects::ActorEffectHooks EffectHooks;

class VertexModels final : public Client::Models::IModelLibrary
{
public:
    VertexModels()
    {
        Client::Data::Models::Mesh mesh;
        mesh.triangles = {{3, {0, 0, 0, 0}}, {3, {0, 0, 0, 0}}};
        model.meshes.push_back(mesh);
    }
    const Client::Data::Models::Model* Find(Client::Models::ModelId) const override
    {
        return &model;
    }
    const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId) const override
    {
        return nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return {};
    }
    Client::Data::Models::Model model;
};

TEST(VertexEffectsTest, RepeatedCornersConsumeThreeDrawsEachEvenWithAFullEffectPool)
{
    VertexModels models;
    const Client::Actors::Drawing::DrawnObject object;
    const std::array meshes{Client::Models::SkinnedMesh{{glm::vec3{11, 22, 33}}, {}}};
    auto effects = std::make_unique<Client::Effects::ModelEffects>();
    for (auto& effect : effects->Effects())
        effect.alive = true;
    Client::Crt::CrtRandom random(1), expected(1);
    expected.Discard(18);
    const auto result =
        Client::Actors::Drawing::EmitVertexEffects(models, object, meshes, 0, 0x131, 0, *effects, &EffectHooks, random);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(random.State(), expected.State());
}

TEST(VertexEffectsTest, GateChildrenUseThePublishedScratchAndRunTheirConstructorBeforeTheNextCorner)
{
    VertexModels models;
    models.model.meshes[0].triangles.resize(1);
    models.model.meshes[0].triangles[0].polygon = 1;
    const Client::Actors::Drawing::DrawnObject object;
    const std::array meshes{Client::Models::SkinnedMesh{{glm::vec3{11, 22, 33}}, {}}};
    auto effects = std::make_unique<Client::Effects::ModelEffects>();
    Client::Crt::CrtRandom random(2), expected(2);
    // CRT seed 2: 45 % 3 = 0; the second draw chooses 0x12f/0x130; eight from the constructor.
    expected.Discard(10);
    const auto result =
        Client::Actors::Drawing::EmitVertexEffects(models, object, meshes, 0, 0x12f, 0, *effects, &EffectHooks, random);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(effects->Effects()[0].alive);
    EXPECT_EQ(effects->Effects()[0].transform.position, (glm::vec3{11, 22, 83}));
    EXPECT_EQ(effects->Effects()[0].light, glm::vec3(1));
    EXPECT_FALSE(effects->Effects()[1].alive);
    EXPECT_EQ(random.State(), expected.State());
}

TEST(VertexEffectsTest, AnInvalidMeshAndSignedEmptyPolygonsDoNotConsumeRandom)
{
    VertexModels models;
    for (auto& triangle : models.model.meshes[0].triangles)
        triangle.polygon = 0xff;
    const std::array meshes{Client::Models::SkinnedMesh{}};
    auto effects = std::make_unique<Client::Effects::ModelEffects>();
    Client::Crt::CrtRandom random(1);
    const Client::Actors::Drawing::DrawnObject object;
    ASSERT_TRUE(
        Client::Actors::Drawing::EmitVertexEffects(models, object, meshes, -1, 0x131, 0, *effects, &EffectHooks, random)
            .HasValue());
    ASSERT_TRUE(
        Client::Actors::Drawing::EmitVertexEffects(models, object, meshes, 0, 0x131, 0, *effects, &EffectHooks, random)
            .HasValue());
    EXPECT_EQ(random.State(), 1u);
}

TEST(VertexEffectsTest, TheTwoProjectileFamiliesPreserveTheirDifferentChildSubtypes)
{
    VertexModels models;
    models.model.meshes[0].triangles.resize(1);
    models.model.meshes[0].triangles[0].polygon = 1;
    const Client::Actors::Drawing::DrawnObject object;
    const std::array meshes{Client::Models::SkinnedMesh{{glm::vec3{11, 22, 33}}, {}}};
    for (const std::int32_t family : {0x12d, 0x12e})
    {
        auto effects = std::make_unique<Client::Effects::ModelEffects>();
        Client::Crt::CrtRandom random(2), expected(2);
        const auto result = Client::Actors::Drawing::EmitVertexEffects(models, object, meshes, 0, family, 0, *effects,
                                                                       &EffectHooks, random);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(effects->Effects()[0].alive);
        EXPECT_EQ(effects->Effects()[0].subtype, family == 0x12d ? 1 : 0);
        EXPECT_EQ(effects->Effects()[0].transform.position, (glm::vec3{11, 22, 83}));
        expected.Discard(10);
        EXPECT_EQ(random.State(), expected.State());
    }
}
// 0x4eed94..0x4eee66: 0x13c draws one rand() % 45 per corner; with 0 and fewer than 20 counted, /0 creates the 0x13c/0
// (eight rand() calls of its addition) and /1 the 0x12d/2 and the 0x12e/2 (eight each), with the angles at 0 and the
// light at 1; another subtype only counts. From 20 on, only each corner's rand().
TEST(VertexEffectsTest, TheGolemStonesComeOneInFortyFiveUpToTwenty)
{
    VertexModels models;
    models.model.meshes[0].triangles.assign(1500, {1, {0, 0, 0, 0}});
    const Client::Actors::Drawing::DrawnObject object;
    const std::array meshes{Client::Models::SkinnedMesh{{glm::vec3{11, 22, 33}}, {}}};
    for (const std::int32_t subtype : {0, 1, 2})
    {
        SCOPED_TRACE(subtype);
        auto effects = std::make_unique<Client::Effects::ModelEffects>();
        Client::Crt::CrtRandom random(3), expected(3);
        int counted = 0;
        std::size_t made = 0;
        for (std::size_t corner = 0; corner < models.model.meshes[0].triangles.size(); ++corner)
        {
            if (expected.Next() % 45 != 0 || counted >= 20)
                continue;
            const int children = subtype == 0 ? 1 : (subtype == 1 ? 2 : 0);
            expected.Discard(static_cast<std::uint32_t>(8 * children));
            made += static_cast<std::size_t>(children);
            ++counted;
        }
        ASSERT_EQ(counted, 20);
        const auto result = Client::Actors::Drawing::EmitVertexEffects(models, object, meshes, 0, 0x13c, subtype,
                                                                       *effects, &EffectHooks, random);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(random.State(), expected.State());
        const auto live = static_cast<std::size_t>(
            std::ranges::count_if(effects->Effects(), [](const auto& effect) { return effect.alive; }));
        EXPECT_EQ(live, made);
        if (subtype == 0)
        {
            EXPECT_EQ(effects->Effects()[0].type, 0x13c);
            EXPECT_EQ(effects->Effects()[0].subtype, 0);
        }
        if (subtype == 1)
        {
            EXPECT_EQ(effects->Effects()[0].type, 0x12d);
            EXPECT_EQ(effects->Effects()[1].type, 0x12e);
            EXPECT_EQ(effects->Effects()[1].subtype, 2);
        }
        if (made > 0)
        {
            EXPECT_EQ(effects->Effects()[0].transform.position, (glm::vec3{11, 22, 83}));
            EXPECT_EQ(effects->Effects()[0].light, glm::vec3(1));
        }
    }
}

using Client::Actors::Drawing::EmitVertexEffectsOf;
using Client::Crt::CrtRandom;
using Client::Models::SkinnedMesh;

const Client::Session::CharacterDrawHooks JointHooks;

// A model with `count` triangles of one corner (vertex 0) in mesh 0.
Client::Data::Models::Model OneCornerModel(std::size_t count)
{
    Client::Data::Models::Model model;
    Client::Data::Models::Mesh mesh;
    mesh.triangles.assign(count, {1, {0, 0, 0, 0}});
    model.meshes.push_back(mesh);
    return model;
}

std::size_t Alive(const Client::Effects::ModelEffects& effects)
{
    return static_cast<std::size_t>(
        std::ranges::count_if(effects.Effects(), [](const auto& effect) { return effect.alive; }));
}

// 0x4ee8e4..0x4ee9d3: 0x10a/1 and /2 release the 0x10b/2 and /3 without rand() calls of their own (the eight of their
// addition); /3, the 0x10a + (rand() & 1) with subtype 4. Another subtype, nothing.
TEST(VertexEffectsTest, TheStonesOfTheStruckGlowTakeTheirSubtypes)
{
    const auto model = OneCornerModel(1);
    const std::array meshes{SkinnedMesh{{glm::vec3{11, 22, 33}}, {}}};
    struct Case
    {
        std::int32_t subtype;
        std::int32_t childSubtype;
    };
    for (const Case probe : {Case{1, 2}, Case{2, 3}, Case{3, 4}, Case{4, -1}})
    {
        SCOPED_TRACE(probe.subtype);
        auto effects = std::make_unique<Client::Effects::ModelEffects>();
        CrtRandom random(5), expected(5);
        std::int16_t type = 0x10b;
        if (probe.subtype == 3)
            type = static_cast<std::int16_t>(0x10a + (expected.Next() & 1));
        if (probe.childSubtype >= 0)
            expected.Discard(8);
        ASSERT_TRUE(EmitVertexEffectsOf(model, 1.0f, meshes, {.type = 0x10a, .subtype = probe.subtype},
                                        {.effects = effects.get(), .effectHooks = &EffectHooks}, random)
                        .HasValue());
        EXPECT_EQ(random.State(), expected.State());
        ASSERT_EQ(Alive(*effects), probe.childSubtype >= 0 ? 1u : 0u);
        if (probe.childSubtype < 0)
            continue;
        const auto& made = effects->Effects()[0];
        EXPECT_EQ(made.type, type);
        EXPECT_EQ(made.subtype, probe.childSubtype);
        EXPECT_EQ(made.transform.position, (glm::vec3{11, 22, 83}));
        EXPECT_EQ(made.light, glm::vec3(1));
    }
}

// 0x4ee9d8..0x4eeb3b: 0x10c with the light at 0.2f: /0, rand() % 12 (0x10d/0) and rand() % 50 (0x10c/0); /1, rand() % 5
// (0x10d/2) and rand() % 10 (0x10c/2); each addition before the next rand(). Another subtype, without rand().
TEST(VertexEffectsTest, TheGateStonesComeWithADimLight)
{
    const auto model = OneCornerModel(300);
    const std::array meshes{SkinnedMesh{{glm::vec3{11, 22, 33}}, {}}};
    const float dim = std::bit_cast<float>(0x3e4ccccdu);
    for (const std::int32_t subtype : {0, 1, 2})
    {
        SCOPED_TRACE(subtype);
        auto effects = std::make_unique<Client::Effects::ModelEffects>();
        auto twin = std::make_unique<Client::Effects::ModelEffects>();
        CrtRandom random(6), expected(6);
        const int shardChance = subtype == 0 ? 12 : 5, stoneChance = subtype == 0 ? 50 : 10;
        const std::int32_t childSubtype = subtype == 0 ? 0 : 2;
        for (std::size_t corner = 0; subtype != 2 && corner < 300; ++corner)
        {
            for (const auto& [chance, type] :
                 {std::pair{shardChance, std::int16_t{0x10d}}, std::pair{stoneChance, std::int16_t{0x10c}}})
            {
                if (expected.Next() % chance != 0)
                    continue;
                ASSERT_TRUE(
                    Client::Effects::CreateBlastModelEffect(
                        *twin,
                        {.type = type, .position = {11, 22, 33}, .light = glm::vec3(dim), .subtype = childSubtype}, {},
                        expected, {})
                        .HasValue());
            }
        }
        ASSERT_TRUE(EmitVertexEffectsOf(model, 1.0f, meshes, {.type = 0x10c, .subtype = subtype},
                                        {.effects = effects.get(), .effectHooks = &EffectHooks}, random)
                        .HasValue());
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(Alive(*effects), Alive(*twin));
        EXPECT_EQ(Alive(*effects) > 0, subtype != 2);
        for (std::size_t slot = 0; slot < Alive(*twin); ++slot)
        {
            EXPECT_EQ(effects->Effects()[slot].type, twin->Effects()[slot].type);
            EXPECT_EQ(effects->Effects()[slot].subtype, twin->Effects()[slot].subtype);
            EXPECT_EQ(effects->Effects()[slot].light, glm::vec3(dim));
        }
    }
}

// 0x4eee6b..0x4eeec1: 0x137 draws one rand() % 8 per corner; with 0, the 0x137 shell of the requested subtype (that of
// the 0x632 joint, also 1) at the corner, with the angles at 0 and the light at 1, and the nine rand() calls of its
// addition. With the table full, only each corner's.
TEST(VertexEffectsTest, TheSkinShellsComeOneInEightWithTheJointSubtype)
{
    VertexModels models;
    models.model.meshes[0].triangles.assign(40, {3, {0, 1, 2, 0}});
    const Client::Actors::Drawing::DrawnObject object;
    const std::array meshes{
        Client::Models::SkinnedMesh{{glm::vec3{11, 22, 33}, glm::vec3{44, 55, 66}, glm::vec3{77, 88, 99}}, {}}};
    for (const std::int32_t subtype : {0, 1})
    {
        SCOPED_TRACE(subtype);
        auto effects = std::make_unique<Client::Effects::ModelEffects>();
        auto twin = std::make_unique<Client::Effects::ModelEffects>();
        Client::Crt::CrtRandom random(4), expected(4);
        for (std::size_t corner = 0; corner < 3 * models.model.meshes[0].triangles.size(); ++corner)
        {
            if (expected.Next() % 8 != 0)
                continue;
            ASSERT_TRUE(Client::Effects::CreateSkinShell({.type = Client::Effects::SkinShellEffect,
                                                          .position = meshes[0].positions[corner % 3],
                                                          .angles = glm::vec3(0.0f),
                                                          .light = glm::vec3(1.0f),
                                                          .subtype = subtype},
                                                         *twin, expected)
                            .has_value());
        }
        const auto result = Client::Actors::Drawing::EmitVertexEffects(
            models, object, meshes, 0, Client::Effects::SkinShellEffect, subtype, *effects, &EffectHooks, random);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(random.State(), expected.State());
        std::size_t made = 0;
        for (std::size_t slot = 0; slot < effects->Effects().size(); ++slot)
        {
            const auto& effect = effects->Effects()[slot];
            const auto& replica = twin->Effects()[slot];
            ASSERT_EQ(effect.alive, replica.alive);
            if (!effect.alive)
                continue;
            ++made;
            EXPECT_EQ(effect.type, Client::Effects::SkinShellEffect);
            EXPECT_EQ(effect.subtype, subtype);
            EXPECT_EQ(effect.transform.position, replica.transform.position);
            EXPECT_EQ(effect.transform.angles, replica.transform.angles);
            EXPECT_EQ(effect.velocity, replica.velocity);
            EXPECT_EQ(effect.light, replica.light);
        }
        EXPECT_GT(made, 0U);

        for (auto& effect : effects->Effects())
            effect.alive = true;
        Client::Crt::CrtRandom full(4), corners(4);
        corners.Discard(static_cast<std::uint32_t>(3 * models.model.meshes[0].triangles.size()));
        ASSERT_TRUE(Client::Actors::Drawing::EmitVertexEffects(models, object, meshes, 0,
                                                               Client::Effects::SkinShellEffect, subtype, *effects,
                                                               &EffectHooks, full)
                        .HasValue());
        EXPECT_EQ(full.State(), corners.State());
    }
}

// 0x4eeec6..0x4eef14: 0x5ac/0, one 0x5ac sprite per corner with the model's scale (+0x68) and the light at 0.08f,
// without rand(). 0x4eef19..0x4eefca: /1, on triangles 0 and 22, two rand() calls and the 0x613/0xd joint; without the
// fifth argument's character, it stops at the call to 0x530860.
TEST(VertexEffectsTest, TheRavenShoutLeavesSpritesOrJointsEveryTwentyTwoTriangles)
{
    const auto model = OneCornerModel(23);
    const std::array meshes{SkinnedMesh{{glm::vec3{11, 22, 33}}, {}}};
    auto sprites = std::make_unique<Client::Particles::FrameSprites>();
    CrtRandom random(8);
    ASSERT_TRUE(
        EmitVertexEffectsOf(model, 0.7f, meshes, {.type = 0x5ac}, {.sprites = sprites.get()}, random).HasValue());
    EXPECT_EQ(random.State(), 8u);
    const auto created = static_cast<std::size_t>(
        std::ranges::count_if(sprites->Sprites(), [](const auto& sprite) { return sprite.alive; }));
    EXPECT_EQ(created, 23u);
    EXPECT_EQ(sprites->Sprites()[0].texture, 0x5ac);
    EXPECT_EQ(sprites->Sprites()[0].scale, 0.7f);
    EXPECT_EQ(sprites->Sprites()[0].light, glm::vec3(std::bit_cast<float>(0x3da3d70au)));
    EXPECT_EQ(sprites->Sprites()[0].position, (glm::vec3{11, 22, 33}));

    Client::Kernel::ActorState target;
    target.position = {100.0f, 200.0f, 300.0f};
    std::vector<Client::Kernel::JointState> joints(4);
    CrtRandom shout(9), expected(9);
    for (int joint = 0; joint < 2; ++joint)
    {
        const auto pitch = static_cast<double>(expected.Next() % 90);
        const auto turn = static_cast<double>((expected.Next() % 120) - 60);
        glm::vec3 first{11, 22, 33};
        glm::vec3 second = target.position;
        glm::vec3 angles{static_cast<float>(-pitch), 0.0f, static_cast<float>(turn + 45.0)};
        std::vector<Client::Kernel::JointState> twin(1);
        ASSERT_TRUE(Client::Joints::CreateFaunaJoint(twin,
                                                     {.type = 0x613,
                                                      .firstPosition = first,
                                                      .secondPosition = second,
                                                      .angles = angles,
                                                      .subtype = 0xd,
                                                      .ownerContext = &target,
                                                      .scale = 20.0f,
                                                      .key = 0,
                                                      .skill = 0,
                                                      .skillSerial = 0,
                                                      .target = -1},
                                                     expected, Client::Kernel::JointOwnerKind::Actor)
                        .HasValue());
    }
    ASSERT_TRUE(EmitVertexEffectsOf(model, 1.0f, meshes,
                                    {.type = 0x5ac, .subtype = 1, .angles = {0.0f, 0.0f, 45.0f}, .target = &target},
                                    {.joints = joints, .jointHooks = &JointHooks}, shout)
                    .HasValue());
    EXPECT_EQ(shout.State(), expected.State());
    // By hand, with seed 9 (68, 6151, 18175, 22398) and the turn at 45: (−68, 0, 31 − 60 + 45) and (−85, 0, 78 − 60 +
    // 45). The addition of the 0x613/0xd (0x53312c) draws no rand(): four in total.
    CrtRandom four(9);
    for (int draw = 0; draw < 4; ++draw)
        static_cast<void>(four.Next());
    EXPECT_EQ(shout.State(), four.State());
    EXPECT_TRUE(joints[0].alive);
    EXPECT_TRUE(joints[1].alive);
    EXPECT_FALSE(joints[2].alive);
    EXPECT_EQ(joints[0].angles, glm::vec3(-68.0f, 0.0f, 16.0f));
    EXPECT_EQ(joints[1].angles, glm::vec3(-85.0f, 0.0f, 63.0f));
    EXPECT_EQ(joints[1].type, 0x613);
    EXPECT_EQ(joints[1].ownerContext, &target);

    CrtRandom orphan(9);
    const auto cut = EmitVertexEffectsOf(model, 1.0f, meshes, {.type = 0x5ac, .subtype = 1},
                                         {.joints = joints, .jointHooks = &JointHooks}, orphan);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x4eefc5"), std::string::npos) << cut.Error();
}

// 0x4eefd6..0x4ef02e: 0x607, rand() % 30 per corner and with 0 the 0x607/2 particle of scale 1 with the light at 1.
// Another type (0x10b, 0x5ad) draws no rand().
TEST(VertexEffectsTest, TheFadingParticlesComeOneInThirty)
{
    const auto model = OneCornerModel(200);
    const std::array meshes{SkinnedMesh{{glm::vec3{11, 22, 33}}, {}}};
    auto particles = std::make_unique<Client::Particles::Particles>();
    auto twin = std::make_unique<Client::Particles::Particles>();
    CrtRandom random(10), expected(10);
    constexpr float Clock = 1500.0f;
    for (std::size_t corner = 0; corner < 200; ++corner)
    {
        if (expected.Next() % 30 != 0)
            continue;
        ASSERT_TRUE(twin->CreateChecked(0x607, {11, 22, 33}, glm::vec3(0.0f), glm::vec3(1.0f), 2, 1.0f, Clock, expected)
                        .HasValue());
    }
    ASSERT_TRUE(EmitVertexEffectsOf(model, 1.0f, meshes, {.type = 0x607},
                                    {.particles = particles.get(), .worldClock = Clock}, random)
                    .HasValue());
    EXPECT_EQ(random.State(), expected.State());
    std::size_t made = 0;
    for (std::size_t slot = 0; slot < particles->All().size(); ++slot)
    {
        const auto& real = particles->All()[slot];
        EXPECT_EQ(real.alive, twin->All()[slot].alive);
        if (!real.alive)
            continue;
        ++made;
        EXPECT_EQ(real.type, 0x607);
        EXPECT_EQ(real.subtype, 2);
        EXPECT_EQ(real.position, twin->All()[slot].position);
        EXPECT_EQ(real.light, twin->All()[slot].light);
    }
    EXPECT_GT(made, 0u);
    for (const std::int32_t other : {0x10b, 0x5ad, 0x138})
    {
        CrtRandom quiet(11);
        ASSERT_TRUE(EmitVertexEffectsOf(model, 1.0f, meshes, {.type = other}, {}, quiet).HasValue());
        EXPECT_EQ(quiet.State(), 11u);
    }
}

// 0x4ee770 creates the stones, the gates' stones and the shells through the hooks: without them those stop before
// their rand() calls; the siege's fragments do not need them.
TEST(VertexEffectsTest, TheStonesAndTheShellsStopWithoutTheEffectHooks)
{
    const auto model = OneCornerModel(1);
    const std::array meshes{SkinnedMesh{{glm::vec3{11, 22, 33}}, {}}};
    for (const std::int32_t type : {0x10a, 0x10c, 0x137})
    {
        SCOPED_TRACE(type);
        auto effects = std::make_unique<Client::Effects::ModelEffects>();
        CrtRandom random(5);
        const auto stopped =
            EmitVertexEffectsOf(model, 1.0f, meshes, {.type = type, .subtype = 1}, {.effects = effects.get()}, random);
        ASSERT_FALSE(stopped.HasValue());
        EXPECT_NE(stopped.Error().find("0x518cf0"), std::string::npos) << stopped.Error();
        EXPECT_EQ(random.State(), 5u);
        EXPECT_EQ(Alive(*effects), 0u);
    }
    auto effects = std::make_unique<Client::Effects::ModelEffects>();
    CrtRandom random(5);
    ASSERT_TRUE(
        EmitVertexEffectsOf(model, 1.0f, meshes, {.type = 0x131}, {.effects = effects.get()}, random).HasValue());
}
} // namespace
