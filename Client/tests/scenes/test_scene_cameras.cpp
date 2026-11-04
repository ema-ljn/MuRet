#include "Scenes/SceneCameras.h"

#include "App/WindowState.h"
#include "Render/Pass3D.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_int4.hpp>

// Client::Scenes::SceneCameras: el pase del mundo de 0x620db0 y las cámaras fijas que 0x635ae0 pone
// en la escena 4 y en la 2 (06 § Cámara). Los números son los del 06.
namespace
{
using Client::App::PixelSize;
using Client::Render::Camera3D;
using Client::Render::MakePass3D;
using Client::Scenes::LoginCamera;
using Client::Scenes::SelectionCamera;
using Client::Scenes::WorldViewport;

TEST(SceneCamerasTest, TheWorldViewportIsThatOf0x620db0)
{
    EXPECT_EQ(WorldViewport.x, 0);
    EXPECT_EQ(WorldViewport.y, 25);
    EXPECT_EQ(WorldViewport.width, 640);
    EXPECT_EQ(WorldViewport.height, 430);
}

// La escena 4: fov 35, near 20, el far 11.519,9 de 0x6d5d44 con DAT_07d3a0ec en 0, x −84,5° y
// z −45°, y la posición (13.909; 14.482; 543). En 1024 × 768, el viewport de la traza.
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

// La escena 2: el far 20.400, x −96,5° y z −36°, y la posición (12.778; 12.678; 579).
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
