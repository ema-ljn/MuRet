#include "Models/IModelLibrary.h"
#include "FrontEnd/CreatePreview.h"

#include "Core/PixelSize.h"
#include "Data/Models/Model.h"
#include "Math/HalfLifeMath.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Camera/SceneCameras.h"
#include "Ui/Widget.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <vector>

// Client::FrontEnd::CreatePreview: the create window's preview (0x63e460 and 0x63e3e0, with 0x63e430 in the step),
// with a synthetic NewFace per class.
namespace
{
using Client::Core::PixelSize;
using Client::FrontEnd::ChoosePreviewClass;
using Client::FrontEnd::CreatePreview;
using Client::FrontEnd::DrawnPreview;
using Client::FrontEnd::FirstNewFaceModel;
using Client::FrontEnd::PreviewCamera;
using Client::FrontEnd::PreviewViewport;
using Client::FrontEnd::StepPreview;
using Client::Models::ActionInfo;
using Client::Models::BoneKey;
using Client::Models::Skeleton;
using Client::Models::SkeletonBone;
using Client::Ui::Rect;

// Two actions of 4 keys, action 1 not repeating, on one bone.
class FaceModels final : public Client::Models::IModelLibrary
{
public:
    FaceModels()
    {
        SkeletonBone bone;
        bone.parent = -1;
        BoneKey key;
        key.rotation = Client::Math::AngleQuaternion({0.0f, 0.0f, 0.0f});
        bone.actions.assign(2, std::vector<BoneKey>(4, key));
        m_skeleton.bones = {bone};
        m_skeleton.actions = {ActionInfo{.repeats = true, .speed = 0.3f, .keyCount = 4, .lockPositions = false},
                              ActionInfo{.repeats = true, .speed = 0.3f, .keyCount = 4, .lockPositions = false}};
    }
    [[nodiscard]] const Client::Data::Models::Model* Find(Client::Models::ModelId /*model*/) const override
    {
        return nullptr;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        return model.Value() >= FirstNewFaceModel && model.Value() < FirstNewFaceModel + 5 ? &m_skeleton : nullptr;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot>
    MeshTextures(Client::Models::ModelId /*model*/) const override
    {
        return {};
    }

private:
    Skeleton m_skeleton;
};

// 0x6338a0 with the object's position and (10, −500, 48): the camera at the object plus that (rotated by the angles'
// z, which is 0), looking with (−90, 0, 0), with 0x63e460's fov of 10 and scene 4's near and far.
TEST(CreatePreviewTest, TheCameraIsRelativeToTheModel)
{
    const Client::Render::Camera3D camera = PreviewCamera();
    EXPECT_EQ(camera.position, (glm::vec3{50.0f + 10.0f, 50.0f - 500.0f, 0.0f + 48.0f}));
    EXPECT_EQ(camera.angles, (glm::vec3{-90.0f, 0.0f, 0.0f}));
    EXPECT_FLOAT_EQ(camera.fieldOfView, 10.0f);
    EXPECT_FLOAT_EQ(camera.nearPlane, 20.0f);
    EXPECT_FLOAT_EQ(camera.farPlane, Client::Camera::SelectionCamera().farPlane);
}

// 0x620db0(ftol(cx / rx), ftol(cy / ry), ftol(410 / rx), ftol(335 / ry)) with the corner of the window's box:
// (93, 37, 410, 335) at 640 × 480 and (178, 113, 256, 209) at 1024 × 768.
TEST(CreatePreviewTest, TheViewportIs410By335FromTheBox)
{
    const Client::Render::ViewportRect at640 =
        PreviewViewport(Rect{.x = 93, .y = 37, .width = 454, .height = 406}, PixelSize{640, 480});
    EXPECT_EQ(at640.x, 93);
    EXPECT_EQ(at640.y, 37);
    EXPECT_EQ(at640.width, 410);
    EXPECT_EQ(at640.height, 335);
    const Client::Render::ViewportRect at1024 =
        PreviewViewport(Rect{.x = 285, .y = 181, .width = 454, .height = 406}, PixelSize{1024, 768});
    EXPECT_EQ(at1024.x, 178);
    EXPECT_EQ(at1024.y, 113);
    EXPECT_EQ(at1024.width, 256);
    EXPECT_EQ(at1024.height, 209);
}

// 0x63e460 per class: the angles and the scale, with class 2 shifted 4.8 in x and class 3 1.8 in z (of the camera,
// which is built before). The model is the class's NewFace, with the per-vertex light 0x511290 turns on (+0xe8 at 1,
// 0x511be6) and the color at 4.8 that 0x6002e0 gives 0xa8 to 0xac.
TEST(CreatePreviewTest, EachClassHasItsAngleAndScale)
{
    const FaceModels models;
    struct Expected
    {
        glm::vec3 position;
        glm::vec3 angles;
        float scale;
    };
    const std::vector<Expected> table{{{50.0f, 50.0f, 0.0f}, {0.0f, 0.0f, -40.0f}, 5.9f},
                                      {{50.0f, 50.0f, 0.0f}, {0.0f, 0.0f, -12.0f}, 6.05f},
                                      {{54.8f, 50.0f, 0.0f}, {8.0f, 0.0f, 5.0f}, 9.1f},
                                      {{50.0f, 50.0f, 1.8f}, {8.0f, 0.0f, -13.0f}, 6.0f},
                                      {{50.0f, 50.0f, 0.0f}, {8.0f, 0.0f, -18.0f}, 6.0f}};
    for (std::uint8_t characterClass = 0; characterClass < 5; ++characterClass)
    {
        CreatePreview preview;
        ChoosePreviewClass(preview, characterClass, models);
        const Client::Actors::Drawing::DrawnObject drawn = DrawnPreview(preview, models, 0.0f);
        const Expected& expected = table[characterClass];
        EXPECT_EQ(drawn.model, FirstNewFaceModel + characterClass);
        EXPECT_FLOAT_EQ(drawn.transform.position.x, expected.position.x) << static_cast<int>(characterClass);
        EXPECT_FLOAT_EQ(drawn.transform.position.z, expected.position.z) << static_cast<int>(characterClass);
        EXPECT_EQ(drawn.transform.angles, expected.angles) << static_cast<int>(characterClass);
        EXPECT_FLOAT_EQ(drawn.transform.scale, expected.scale) << static_cast<int>(characterClass);
        EXPECT_EQ(drawn.color, (glm::vec3{4.8f, 4.8f, 4.8f}));
        EXPECT_TRUE(drawn.vertexLight);
    }
}

// 0x63e3e0 builds the object again with action 1; 0x63e430 advances it 0.6 keys (speed 0.3 times the 2 of 0x4f9580
// with the NewFace) and, when the action has ended, moves to 0. The NewFace ones repeat, so it ends by scene 4's rule
// (0x4ec200 looks at frame + 2): with 4 keys, on the fourth step (2.4 + 2 ≥ 4), and not on the seventh.
TEST(CreatePreviewTest, ActionOneThenZero)
{
    const FaceModels models;
    CreatePreview preview;
    ChoosePreviewClass(preview, 2, models);
    EXPECT_EQ(preview.animation.action, 1u);
    EXPECT_FLOAT_EQ(preview.animation.frame, 0.0f);

    StepPreview(preview, models);
    EXPECT_FLOAT_EQ(preview.animation.frame, 0.6f);
    StepPreview(preview, models);
    StepPreview(preview, models);
    EXPECT_EQ(preview.animation.action, 1u);
    StepPreview(preview, models);
    EXPECT_EQ(preview.animation.action, 0u);

    ChoosePreviewClass(preview, 3, models);
    EXPECT_EQ(preview.characterClass, 3u);
    EXPECT_EQ(preview.animation.action, 1u);
    EXPECT_FLOAT_EQ(preview.animation.frame, 0.0f);
}

TEST(CreatePreviewTest, DrawingKeepsTheClassOffsetForTheFollowingCommonStep)
{
    const FaceModels models;
    CreatePreview preview;
    ChoosePreviewClass(preview, 2, models);

    const auto first = DrawnPreview(preview, models, 0.0f);
    EXPECT_FLOAT_EQ(preview.position.x, 54.8f);
    EXPECT_FLOAT_EQ(first.transform.position.x, 54.8f);
    const auto second = DrawnPreview(preview, models, 0.0f);

    EXPECT_FLOAT_EQ(preview.position.x, 59.6f);
    EXPECT_FLOAT_EQ(second.transform.position.x, 59.6f);
}
} // namespace
