#include "Scenes/SceneCameras.h"

#include "Render/Pass3D.h"

#include <bit>
#include <cstdint>

namespace Client::Scenes
{
namespace
{
// Los floats de 0x635ae0: 35 (0x6d5d48) y 20 (0x6d5d40) en las dos escenas.
constexpr float FieldOfView = 35.0f;
constexpr float NearPlane = 20.0f;
// El far de 0x6d5d44 en la escena 4, el 0x4633ff9a que deja 0x635ae0: 11.519,9.
constexpr float SelectionFar = std::bit_cast<float>(std::uint32_t{0x4633ff9a});
// El de la escena 2: 20.400.
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
} // namespace Client::Scenes
