#include "Camera/SceneCameras.h"

#include "Core/IClock.h"
#include "Models/Animation.h"
#include "Render/Pass3D.h"
#include "Camera/GameCamera.h"

#include <bit>
#include <cstdint>

namespace Client::Camera
{
namespace
{
// 0x635ae0's floats: 35 (0x6d5d48) and 20 (0x6d5d40) in both scenes.
constexpr float FieldOfView = 35.0f;
constexpr float NearPlane = 20.0f;
// 0x6d5d44's far plane in scene 4, the 0x4633ff9a that 0x635ae0 leaves: 11519.9.
constexpr float SelectionFar = std::bit_cast<float>(std::uint32_t{0x4633ff9a});
// Scene 2's: 20400.
constexpr float LoginFar = 20400.0f;
} // namespace

Render::Camera3D SelectionCamera()
{
    Render::Camera3D camera;
    camera.position = {13909.0f, 14482.0f, 543.0f};
    camera.angles = {-84.5f, 0.0f, -45.0f};
    camera.fieldOfView = FieldOfView;
    camera.nearPlane = NearPlane;
    camera.farPlane = SelectionFar;
    return camera;
}

Render::Camera3D LoginCamera()
{
    Render::Camera3D camera;
    camera.position = {12778.0f, 12678.0f, 579.0f};
    camera.angles = {-96.5f, 0.0f, -36.0f};
    camera.fieldOfView = FieldOfView;
    camera.nearPlane = NearPlane;
    camera.farPlane = LoginFar;
    return camera;
}
Render::Camera3D CameraForFrame(Camera::GameCamera& state, const Camera::GameCameraInput& input,
                                Models::OriginalFrameCarry& frames, Core::Duration clock,
                                Models::OriginalFramePace pace)
{
    const int count = frames.FramesAt(clock, pace);
    if (count == 0)
        return Camera::GameCameraOf(state, input);
    Render::Camera3D camera;
    for (int frame = 0; frame < count; ++frame)
        camera = Camera::StepGameCamera(state, input);
    return camera;
}
} // namespace Client::Camera
