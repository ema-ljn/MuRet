#include "Session/ItemCreators.h"

#include "Actors/Drawing/CharacterDrawing.h"
#include "Actors/Drawing/GameItemRenderState.h"
#include "Actors/Drawing/ItemDrawResources.h"
#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/JointRequest.h"
#include "Kernel/PoseBuffer.h"
#include "Kernel/DrawTailState.h"
#include "Effects/ModelEffects.h"
#include "WorldObjects/ObjectModelStepState.h"
#include "Particles/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace
{
using Client::Kernel::JointRequest;

class ItemCreatorsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Client::Session::FillItemCreators(m_item, m_resources);
    }

    [[nodiscard]] Client::Core::Expected<void, std::string> Joint(std::int32_t type, std::int32_t subtype)
    {
        int owner = 0;
        const JointRequest request{.type = type,
                                   .firstPosition = m_position,
                                   .secondPosition = m_position,
                                   .angles = m_angles,
                                   .subtype = subtype,
                                   .ownerContext = &owner,
                                   .scale = 10.0f,
                                   .key = -1,
                                   .skill = 0,
                                   .skillSerial = 0,
                                   .target = -1};
        return m_item.createJoint(request);
    }

    [[nodiscard]] std::size_t LiveJoints() const
    {
        return static_cast<std::size_t>(
            std::ranges::count_if(m_joints, [](const Client::Kernel::JointState& joint) { return joint.alive; }));
    }

    [[nodiscard]] std::vector<std::int16_t> LiveEffects() const
    {
        std::vector<std::int16_t> live;
        for (const auto& effect : m_effects->Effects())
        {
            if (effect.alive)
                live.push_back(effect.type);
        }
        return live;
    }

    glm::vec3 m_position{1000.0f, 1000.0f, 300.0f};
    glm::vec3 m_angles{};
    Client::Crt::CrtRandom m_random{1};
    Client::Actors::Drawing::GameItemRenderState m_render;
    Client::Particles::FrameSprites m_sprites;
    Client::Kernel::PoseBuffer m_carry;
    Client::WorldObjects::ObjectModelStepState m_modelState;
    std::vector<Client::Kernel::JointState> m_joints = std::vector<Client::Kernel::JointState>(4);
    std::unique_ptr<Client::Effects::ModelEffects> m_effects = std::make_unique<Client::Effects::ModelEffects>();
    Client::Actors::Drawing::ItemDrawResources m_resources{.random = m_random,
                                                           .itemRender = m_render,
                                                           .sprites = m_sprites,
                                                           .poseCarry = m_carry,
                                                           .modelState = m_modelState,
                                                           .joints = m_joints,
                                                           .effects = *m_effects};
    Client::Actors::Drawing::ItemDrawContext m_item;
};

// 0x530860: the items create only Helper07's 0x613/4 and 0x614/14, whose owner is opaque; any other joint stops
// before it is created.
TEST_F(ItemCreatorsTest, OnlyHelper07sJointsAreCreated)
{
    ASSERT_TRUE(Joint(0x613, 4).HasValue());
    ASSERT_TRUE(Joint(0x614, 14).HasValue());
    EXPECT_EQ(LiveJoints(), 2U);

    const auto refused = Joint(0x613, 5);
    ASSERT_FALSE(refused.HasValue());
    EXPECT_NE(refused.Error().find("0x530860"), std::string::npos) << refused.Error();
    EXPECT_EQ(LiveJoints(), 2U);
}

// 0x518cf0: the items create only the lightning 0xc2/3 and the dragon, the arrival and the teleport with subtype 0;
// any other effect stops before it is created.
TEST_F(ItemCreatorsTest, OnlyTheLightningAndThePortableEffectsAreCreated)
{
    Client::Actors::Drawing::DrawnObject object;
    object.transform.position = m_position;
    ASSERT_TRUE(m_item.createEffect(0xc2, 3, object).HasValue());
    ASSERT_TRUE(m_item.createEffect(Client::Effects::DragonEffect, 0, object).HasValue());
    ASSERT_TRUE(m_item.createEffect(Client::Effects::ArrivalEffect, 0, object).HasValue());
    ASSERT_TRUE(m_item.createEffect(Client::Effects::TeleportEffect, 0, object).HasValue());
    EXPECT_EQ(LiveEffects(),
              (std::vector<std::int16_t>{0xc2, Client::Effects::DragonEffect, Client::Effects::ArrivalEffect,
                                         Client::Effects::TeleportEffect}));

    const auto subtyped = m_item.createEffect(Client::Effects::DragonEffect, 1, object);
    ASSERT_FALSE(subtyped.HasValue());
    EXPECT_NE(subtyped.Error().find("0x518cf0"), std::string::npos) << subtyped.Error();
    EXPECT_FALSE(m_item.createEffect(0xc2, 0, object).HasValue());
    EXPECT_EQ(LiveEffects().size(), 4U);
}
} // namespace
