#include "World/GameCamera.h"

#include "Render/Models/OriginalRotation.h"
#include "Render/Pass3D.h"
#include "World/TerrainCulling.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Client::World
{
namespace
{
// 06 § La cámara del juego: las constantes de 0x635ae0, con los bits de su .data.
constexpr std::uint8_t FixedHeightAttribute = 0x40;
constexpr std::uint8_t ZoomAttribute = 0x80;
constexpr std::uint8_t RockingMap = 5;
constexpr std::uint8_t WebzenMap = 6;
constexpr std::uint8_t CastleMap = 0x1e;
constexpr float HeightOffset = 150.0f;
constexpr float CastleHeight = 255.0f;
constexpr float AttributeHeight = 1201.0f;
constexpr float ZoomStep = 10.0f;
constexpr float ZoomLimit = 200.0f;
constexpr float ZoomYaw = -45.0f;
constexpr double Third = std::bit_cast<float>(std::uint32_t{0x3eaaaaab});
constexpr double RockingPitchRate = std::bit_cast<float>(std::uint32_t{0x3a03126f});
constexpr double RockingYawRate = std::bit_cast<float>(std::uint32_t{0x3a51b717});
constexpr std::array<float, 5> LevelFar{2000.0f, 2500.0f, 2600.0f, 2950.0f, 3200.0f};
constexpr float LoginFar = 20400.0f;
constexpr float SelectionFar = std::bit_cast<float>(std::uint32_t{0x4633ff9a});
constexpr float WebzenFar = 5000.0f;
constexpr float NormalDistance = 1000.0f;
constexpr float CastleDistance = 1100.0f;
constexpr float WebzenDistance = 3200.0f;

float F32(double value)
{
    return static_cast<float>(value);
}

glm::vec3 Offset(float distance, float yaw)
{
    return Render::Models::OriginalVectorIRotate({0.0f, distance, 0.0f},
                                                 Render::Models::OriginalAngleMatrix({0.0f, 0.0f, yaw}));
}

glm::vec3 Add(const glm::vec3& left, const glm::vec3& right)
{
    return {F32(static_cast<double>(left.x) + static_cast<double>(right.x)),
            F32(static_cast<double>(left.y) + static_cast<double>(right.y)),
            F32(static_cast<double>(left.z) + static_cast<double>(right.z))};
}

float FarPlaneOf(const GameCamera& state, const GameCameraInput& input)
{
    constexpr float RectangleMinX = 16100.0f, RectangleMaxX = 19000.0f;
    constexpr float RectangleMinY = 18900.0f, RectangleMaxY = 21700.0f;
    constexpr float RectangleFar = 3000.0f;
    if (input.hero.x >= RectangleMinX && input.hero.x <= RectangleMaxX && input.hero.y >= RectangleMinY &&
        input.hero.y <= RectangleMaxY)
        return RectangleFar;
    if (input.map == CastleMap && input.scene == CullingScene::Game)
        return LevelFar[1];
    if (state.level < 0 || state.level >= static_cast<int>(LevelFar.size()))
        return state.farPlane;
    if (state.level != 0)
        return LevelFar[static_cast<std::size_t>(state.level)];
    if (input.scene == CullingScene::Login)
        return LoginFar;
    if (input.scene == CullingScene::CharacterSelect)
        return SelectionFar;
    return LevelFar[0];
}

void SetFixedSceneCamera(Render::Camera3D& camera, const GameCameraInput& input)
{
    float pitch = -48.5f;
    if (input.scene == CullingScene::Login)
    {
        camera.position = {12778.0f, 12678.0f, 579.0f};
        camera.angles.z = -36.0f;
        pitch = -96.5f;
    }
    else if (input.scene == CullingScene::CharacterSelect)
    {
        camera.position = {13909.0f, 14482.0f, 543.0f};
        camera.angles.z = -45.0f;
        pitch = -84.5f;
    }
    camera.angles.x = F32(static_cast<double>(input.shake) + static_cast<double>(pitch));
}

void AdvanceZoom(GameCamera& state, std::uint8_t attribute)
{
    if ((attribute & ZoomAttribute) != 0)
    {
        if (state.zoom <= ZoomLimit)
            state.zoom = F32(static_cast<double>(state.zoom) + ZoomStep);
    }
    else if (state.zoom > 0.0f)
        state.zoom = F32(static_cast<double>(state.zoom) - ZoomStep);
}

Render::Camera3D NormalCamera(float distance, float yaw, float farPlane, float zoom, const GameCameraInput& input)
{
    Render::Camera3D camera;
    camera.angles.z = yaw;
    camera.farPlane = farPlane;
    const glm::vec3 offset = Offset(-distance, yaw);
    const bool hasEffectTarget =
        input.effectTarget.x != 0.0f || input.effectTarget.y != 0.0f || input.effectTarget.z != 0.0f;
    camera.position = Add(offset, hasEffectTarget ? input.effectTarget : input.hero);
    float baseHeight = input.map == CastleMap ? CastleHeight : input.hero.z;
    if ((input.cellAttribute & FixedHeightAttribute) != 0)
    {
        baseHeight = AttributeHeight;
    }
    camera.position.z = F32((static_cast<double>(distance) - HeightOffset) + static_cast<double>(baseHeight));
    SetFixedSceneCamera(camera, input);
    if (zoom != 0.0f)
        camera.position = Add(camera.position, Offset(zoom, ZoomYaw));
    return camera;
}

Render::Camera3D WebzenCamera(float distance)
{
    Render::Camera3D camera;
    constexpr float WebzenYaw = -40.0f;
    const glm::vec3 offset = Offset(-distance, WebzenYaw);
    camera.position = Add(offset, {5400.0f, 16000.0f, 0.0f});
    camera.position.z = 1000.0f;
    camera.angles = {-70.0f, 0.0f, WebzenYaw};
    camera.farPlane = WebzenFar;
    return camera;
}

void ApplyRocking(Render::Camera3D& camera, const GameCameraInput& input)
{
    if (input.map == RockingMap)
    {
        const double pitchWave = std::sin(static_cast<double>(input.worldTime) * RockingPitchRate);
        camera.angles.x = F32((pitchWave + pitchWave) + static_cast<double>(camera.angles.x));
        camera.angles.y = F32((std::sin(static_cast<double>(input.worldTime) * RockingYawRate) * 2.5) +
                              static_cast<double>(camera.angles.y));
    }
}

void FinishFrame(GameCamera& state, const GameCameraInput& input)
{
    if (input.map == RockingMap)
        return;
    if (input.map == WebzenMap && input.webzen)
    {
        state.targetDistance = state.distance = WebzenDistance;
        return;
    }
    if (input.map == CastleMap)
    {
        state.targetDistance = state.distance = CastleDistance;
        return;
    }
    state.targetDistance = NormalDistance;
    state.distance = F32(((static_cast<double>(state.targetDistance) - static_cast<double>(state.distance)) * Third) +
                         static_cast<double>(state.distance));
}
} // namespace

Render::Camera3D StepGameCamera(GameCamera& state, const GameCameraInput& input)
{
    state.lastFrameDistance = state.distance;
    const bool webzen = input.map == WebzenMap && input.webzen;
    if (!webzen)
    {
        state.farPlane = FarPlaneOf(state, input);
        state.level = 0;
        if ((input.cellAttribute & FixedHeightAttribute) != 0)
            state.fixedHeight = AttributeHeight;
        AdvanceZoom(state, input.cellAttribute);
    }
    Render::Camera3D camera = webzen ? WebzenCamera(state.distance)
                                     : NormalCamera(state.distance, state.yaw, state.farPlane, state.zoom, input);
    ApplyRocking(camera, input);
    FinishFrame(state, input);
    state.yaw = camera.angles.z;
    state.farPlane = camera.farPlane;
    return camera;
}

Render::Camera3D GameCameraOf(const GameCamera& state, const GameCameraInput& input)
{
    const float distance = state.lastFrameDistance.value_or(state.distance);
    Render::Camera3D camera = input.map == WebzenMap && input.webzen
                                  ? WebzenCamera(distance)
                                  : NormalCamera(distance, state.yaw, state.farPlane, state.zoom, input);
    camera.farPlane = state.farPlane;
    ApplyRocking(camera, input);
    return camera;
}
} // namespace Client::World
