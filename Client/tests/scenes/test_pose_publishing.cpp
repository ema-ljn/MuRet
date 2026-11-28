#include "Actors/Drawing/PosePublishing.h"

#include "Actors/Drawing/CharacterDrawing.h"
#include "Data/Models/Model.h"
#include "Kernel/PoseBuffer.h"
#include "Math/HalfLifeMath.h"
#include "Models/IModelLibrary.h"
#include "Models/ModelId.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Render/TextureSlot.h"

#include "support/SyntheticModels.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <vector>

namespace
{
using Client::Kernel::PoseBuffer;
using Client::Math::BoneTransform;

constexpr std::uint16_t SampleModel = 0x24;

// The sample model (a root, a child and a dummy bone) under every id.
class SampleModels : public Client::Models::IModelLibrary
{
public:
    SampleModels()
    {
        auto loaded = Client::Models::MakeSkeleton(model);
        if (loaded.HasValue())
            skeleton = loaded.Value();
    }
    const Client::Data::Models::Model* Find(Client::Models::ModelId) const override
    {
        return &model;
    }
    const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId) const override
    {
        return &skeleton;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return {};
    }
    Client::Data::Models::Model model = Client::Tests::SampleModel();
    Client::Models::Skeleton skeleton;
};

class PosePublishingTest : public ::testing::Test
{
protected:
    Client::Actors::Drawing::DrawnObject Object(float alpha) const
    {
        Client::Actors::Drawing::DrawnObject object;
        object.model = SampleModel;
        object.alpha = alpha;
        object.transform.position = {1000, 1000, 300};
        return object;
    }

    SampleModels m_models;
    PoseBuffer m_buffer;
};

// Q-C347: a mesh with a vertex on a bone that does not exist (mesh 1 of Crywolf's Object36.bmd names −8888) is not
// posed and its scratch is emptied; the others are posed and the publication goes on.
TEST_F(PosePublishingTest, AMeshOnAMissingBoneIsLeftUnposedAndTheOthersPose)
{
    auto broken = Client::Tests::SampleMesh();
    ASSERT_FALSE(broken.vertices.empty());
    broken.vertices[0].bone = -8888;
    m_models.model.meshes.push_back(broken);
    m_buffer.meshes.resize(2);
    m_buffer.meshes[1].positions = {glm::vec3(1.0f)};
    m_buffer.meshes[1].normals = {glm::vec3(0.0f, 0.0f, 1.0f)};
    const std::vector<BoneTransform> bones(3, BoneTransform(1.0f));
    const Client::Models::ObjectTransform transform{.position = {1000, 1000, 300}};
    ASSERT_TRUE(
        Client::Actors::Drawing::PublishPoseRaw(m_buffer, m_models, SampleModel, bones, transform, false).HasValue());
    EXPECT_EQ(m_buffer.meshes[0].positions.size(), m_models.model.meshes[0].vertices.size());
    EXPECT_TRUE(m_buffer.meshes[1].positions.empty());
    EXPECT_TRUE(m_buffer.meshes[1].normals.empty());
}

// 0x4eb973: the pose skips the dummy bones, which keep what the buffer had; the others take the lent matrices, and
// the model's transform stays under its id.
TEST_F(PosePublishingTest, TheDummyBonesKeepWhatTheBufferHad)
{
    m_buffer.bones.resize(3, BoneTransform(0));
    m_buffer.bones[2][3] = {7, 8, 9};
    const std::vector<BoneTransform> bones(3, BoneTransform(1.0f));
    const Client::Models::ObjectTransform transform{.position = {1, 2, 3}};
    ASSERT_TRUE(
        Client::Actors::Drawing::PublishPoseRaw(m_buffer, m_models, SampleModel, bones, transform, false).HasValue());
    EXPECT_EQ(m_buffer.bones[0], BoneTransform(1.0f));
    EXPECT_EQ(m_buffer.bones[1], BoneTransform(1.0f));
    EXPECT_EQ(m_buffer.bones[2][3], glm::vec3(7, 8, 9));
    ASSERT_TRUE(m_buffer.models.contains(SampleModel));
    EXPECT_EQ(m_buffer.models.at(SampleModel).position, glm::vec3(1, 2, 3));
}

// 0x5ffdf0 does not pose an object below an alpha of 0.01: the buffer stays as it was.
TEST_F(PosePublishingTest, AnObjectBelowTheVisibleAlphaLeavesTheBufferAlone)
{
    ASSERT_TRUE(Client::Actors::Drawing::PublishPose(m_buffer, m_models, Object(0.005f), true).HasValue());
    EXPECT_TRUE(m_buffer.bones.empty());
    EXPECT_TRUE(m_buffer.models.empty());
    EXPECT_TRUE(m_buffer.meshes.empty());

    ASSERT_TRUE(Client::Actors::Drawing::PublishPose(m_buffer, m_models, Object(1.0f), true).HasValue());
    EXPECT_EQ(m_buffer.bones.size(), 3U);
    EXPECT_TRUE(m_buffer.models.contains(SampleModel));
    EXPECT_FALSE(m_buffer.meshes.empty());
}

// 0x608b0a poses an item on the floor at any alpha, but skins its meshes only above 0.01.
TEST_F(PosePublishingTest, AFloorItemIsSkinnedOnlyAboveTheVisibleAlpha)
{
    ASSERT_TRUE(Client::Actors::Drawing::PublishFloorPose(m_buffer, m_models, Object(0.01f)).HasValue());
    EXPECT_EQ(m_buffer.bones.size(), 3U);
    EXPECT_TRUE(m_buffer.models.contains(SampleModel));
    EXPECT_TRUE(m_buffer.meshes.empty());

    ASSERT_TRUE(Client::Actors::Drawing::PublishFloorPose(m_buffer, m_models, Object(0.5f)).HasValue());
    ASSERT_FALSE(m_buffer.meshes.empty());
    EXPECT_EQ(m_buffer.meshes[0].positions.size(), m_models.model.meshes[0].vertices.size());
}
} // namespace
