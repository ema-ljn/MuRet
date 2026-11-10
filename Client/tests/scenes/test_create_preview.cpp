#include "Scenes/CreatePreview.h"

#include "App/WindowState.h"
#include "Data/Models/Model.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/SceneCameras.h"
#include "UI/Widget.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <vector>

// Client::Scenes::CreatePreview: la vista previa de la ventana de crear (0x63e460 y 0x63e3e0, con
// 0x63e430 en el paso), con un NewFace sintético por clase.
namespace
{
using Client::App::PixelSize;
using Client::Render::Models::ActionInfo;
using Client::Render::Models::BoneKey;
using Client::Render::Models::Skeleton;
using Client::Render::Models::SkeletonBone;
using Client::Scenes::ChoosePreviewClass;
using Client::Scenes::CreatePreview;
using Client::Scenes::DrawnPreview;
using Client::Scenes::FirstNewFaceModel;
using Client::Scenes::PreviewCamera;
using Client::Scenes::PreviewViewport;
using Client::Scenes::StepPreview;
using Client::UI::Rect;

// Dos acciones de 4 keys, la 1 sin repetir, en un hueso.
class FaceModels final : public Client::Scenes::IModelLibrary
{
public:
    FaceModels()
    {
        SkeletonBone bone;
        bone.parent = -1;
        BoneKey key;
        key.rotation = Client::Render::Models::AngleQuaternion({0.0f, 0.0f, 0.0f});
        bone.actions.assign(2, std::vector<BoneKey>(4, key));
        m_skeleton.bones = {bone};
        m_skeleton.actions = {ActionInfo{.repeats = true, .speed = 0.3f, .keyCount = 4, .lockPositions = false},
                              ActionInfo{.repeats = true, .speed = 0.3f, .keyCount = 4, .lockPositions = false}};
    }
    [[nodiscard]] const Client::Data::Models::Model* Find(std::uint16_t /*model*/) const override
    {
        return nullptr;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return model >= FirstNewFaceModel && model < FirstNewFaceModel + 5 ? &m_skeleton : nullptr;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t /*model*/) const override
    {
        return {};
    }

private:
    Skeleton m_skeleton;
};

// 0x6338a0 con la posición del objeto y (10, −500, 48): la cámara en el objeto más eso (girado por
// el z de los ángulos, que es 0), mirando con (−90, 0, 0), con el fov de 10 de 0x63e460 y el near y
// el far de la escena 4.
TEST(CreatePreviewTest, TheCameraIsRelativeToTheModel)
{
    const Client::Render::Camera3D camera = PreviewCamera();
    EXPECT_EQ(camera.position, (glm::vec3{50.0f + 10.0f, 50.0f - 500.0f, 0.0f + 48.0f}));
    EXPECT_EQ(camera.angles, (glm::vec3{-90.0f, 0.0f, 0.0f}));
    EXPECT_FLOAT_EQ(camera.fieldOfView, 10.0f);
    EXPECT_FLOAT_EQ(camera.nearPlane, 20.0f);
    EXPECT_FLOAT_EQ(camera.farPlane, Client::Scenes::SelectionCamera().farPlane);
}

// 0x620db0(ftol(cx / rx), ftol(cy / ry), ftol(410 / rx), ftol(335 / ry)) con la esquina de la caja de
// la ventana: (93, 37, 410, 335) en 640 × 480 y (178, 113, 256, 209) en 1024 × 768.
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

// 0x63e460 por clase: los ángulos y la escala, y la clase 2 corrida 4,8 en x y la 3 1,8 en z (de la
// cámara, que se arma antes). El modelo es el NewFace de la clase, con la luz por vértice que prende
// 0x511290 (+0xe8 en 1, 0x511be6) y el color en 4,8 que le pone 0x6002e0 a los 0xa8 a 0xac.
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
        const Client::Scenes::DrawnObject drawn = DrawnPreview(preview, models, 0.0f);
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

// 0x63e3e0 arma el objeto de nuevo con la acción 1; 0x63e430 lo avanza 0,6 keys (la velocidad 0,3
// por el 2 de 0x4f9580 con los NewFace) y, cuando la acción terminó, pasa a la 0. Las de los NewFace
// repiten, así que termina por la regla de la escena 4 (0x4ec200 mira el frame + 2): con 4 keys, en el
// cuarto paso (2,4 + 2 ≥ 4), y no en el séptimo.
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

    // Otra clase: otra vez la 1 desde el principio.
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
