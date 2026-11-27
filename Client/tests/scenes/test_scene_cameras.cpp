#include "Camera/SceneCameras.h"

#include "Core/PixelSize.h"
#include "Render/Pass3D.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_int4.hpp>

// Camera/SceneCameras.h: 0x620db0's world pass and the fixed cameras 0x635ae0 sets in scene 4 and in scene 2 (06 §
// Cámara). The numbers are 06's.
namespace
{
using Client::Camera::LoginCamera;
using Client::Camera::SelectionCamera;
using Client::Camera::WorldViewport;
using Client::Core::PixelSize;
using Client::Render::Camera3D;
using Client::Render::MakePass3D;

TEST(SceneCamerasTest, TheWorldViewportIsThatOf0x620db0)
{
    EXPECT_EQ(WorldViewport.x, 0);
    EXPECT_EQ(WorldViewport.y, 25);
    EXPECT_EQ(WorldViewport.width, 640);
    EXPECT_EQ(WorldViewport.height, 430);
}

// Scene 4: fov 35, near 20, 0x6d5d44's far 11519.9 with DAT_07d3a0ec at 0, x −84.5° and z −45°, and the position
// (13909, 14482, 543). At 1024 × 768, the trace's viewport.
TEST(SceneCamerasTest, TheSelectionCameraIsThatOfSceneFour)
{
    const Camera3D camera = SelectionCamera();

    EXPECT_EQ(camera.position, glm::vec3(13909.0f, 14482.0f, 543.0f));
    EXPECT_EQ(camera.angles, glm::vec3(-84.5f, 0.0f, -45.0f));
    EXPECT_FALSE(camera.skipXRotation);
    EXPECT_FLOAT_EQ(camera.fieldOfView, 35.0f);
    EXPECT_FLOAT_EQ(camera.nearPlane, 20.0f);
    EXPECT_NEAR(camera.farPlane, 11519.9f, 0.01f);
    EXPECT_EQ(MakePass3D(PixelSize{1024, 768}, WorldViewport, camera).viewport, glm::ivec4(0, 40, 1024, 688));
}

// Scene 2: far 20400, x −96.5° and z −36°, and the position (12778, 12678, 579).
TEST(SceneCamerasTest, TheLoginCameraIsThatOfSceneTwo)
{
    const Camera3D camera = LoginCamera();

    EXPECT_EQ(camera.position, glm::vec3(12778.0f, 12678.0f, 579.0f));
    EXPECT_EQ(camera.angles, glm::vec3(-96.5f, 0.0f, -36.0f));
    EXPECT_FALSE(camera.skipXRotation);
    EXPECT_FLOAT_EQ(camera.fieldOfView, 35.0f);
    EXPECT_FLOAT_EQ(camera.nearPlane, 20.0f);
    EXPECT_FLOAT_EQ(camera.farPlane, 20400.0f);
}
} // namespace
