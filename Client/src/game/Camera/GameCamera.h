#pragma once

#include "Render/Pass3D.h"
#include "Terrain/TerrainCulling.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <optional>

// 06 § La cámara del juego: 0x635ae0 per drawn frame, with the hero of the last step. While the CCameraMove tour of
// 0x6e57a8 runs (BD 00, Events::CameraTour), the camera looks at its position (0x635ead) and takes its +0x150 as level.
namespace Client::Camera
{
// 0x635ec0: while the tour runs, the hero's z ([0x743a170] + 0x18) at 300 before the camera height. The scene, which
// owns the hero, writes it.
inline constexpr float CameraTourHeroHeight = 300.0f;

// 0x4034b0: what the camera tour (Events::CameraTour) gives the game camera while it runs (0x4033d0).
struct GameCameraTour
{
    glm::vec3 position{};   // +4..+0xc: overrides 0x44dc60's point.
    std::int32_t level = 0; // +0x150: returned in EAX; the camera stores AX in word 0x7d3a0ec.
};

struct GameCamera
{
    float yaw = -45.0f;             // 0x7d36870, written by scene 5's first step.
    float distance = 1000.0f;       // 0x7d36ba4, D before the next frame's smoothing.
    float zoom = 0.0f;              // 0x7d3689c.
    int level = 0;                  // 0x7d3a0ec, read for the far plane before going back to 0 or to the tour's +0x150.
    float farPlane = 2000.0f;       // 0x6d5d44, without the 3D pass's 1.4 factor.
    float fixedHeight = 1200.0f;    // 0x6c9d5c, stays at 1201 from the first 0x40 attribute.
    float targetDistance = 1000.0f; // 0x6d5ee0, the smoothing's target: 1000 + 100 · level (0x6361d0).
    // D is smoothed after the position is computed: the draws in between use the previous D.
    std::optional<float> lastFrameDistance;
};

struct GameCameraInput
{
    glm::vec3 hero{};
    // Low byte of the attribute of the hero's server cell (+0x41c/+0x420), Q-C173.
    std::uint8_t cellAttribute = 0;
    std::uint8_t map = 0;
    Terrain::CullingScene scene = Terrain::CullingScene::Game;
    float shake = 0.0f;     // 0x7d325c8, already reduced by the logic steps.
    float worldTime = 0.0f; // 0x5767b88, in milliseconds, for map 5's sway.
    bool webzen = false;    // The name contains "webzen" (0x576960).
    // 0x724588/0x44dc60: picks the offset if a component is ordered and nonzero. With invalid masked, signed zeros
    // and NaN alone keep the hero.
    glm::vec3 effectTarget{};
    // 0x635ead and 0x637324 (0x4033d0): the running camera tour, which the game's draw fills. Scenes 2 and 4 do not:
    // the tour does not run there (0x4c26f2 resets it with 0x403300 on returning to the selection).
    std::optional<GameCameraTour> tour{};
};

// 0x44dc60: 0x724588's offset if a component is ordered and nonzero; otherwise the hero.
[[nodiscard]] glm::vec3 CameraTargetOf(const GameCameraInput& input);

// The point the camera follows (0x635e9f..0x635ed8), which the game's draw passes to 0x5fbb00 as the trapezoid's center
// (0x637316..0x637337): CameraTargetOf or, while the tour runs, the tour's position (0x4034b0).
[[nodiscard]] glm::vec3 FollowedPointOf(const GameCameraInput& input);

// 0x635ae0: one drawn original frame, with the hero of the last logic step. With the tour (outside map 6 with "webzen",
// which leaves earlier), the position comes from FollowedPointOf, the base height is CameraTourHeroHeight (except map
// 30, which uses its own) and the level is the +0x150 word (0x635ed2); without it, 0 (0x635eda). That level picks D's
// target (outside maps 5, 6 with "webzen" and 30) and the next frame's far plane.
[[nodiscard]] Render::Camera3D StepGameCamera(GameCamera& state, const GameCameraInput& input);

// The hero of the last logic step with the D, zoom and far plane of the last original frame, without advancing the
// state. Before the first frame, the initial D and far plane.
[[nodiscard]] Render::Camera3D GameCameraOf(const GameCamera& state, const GameCameraInput& input);
} // namespace Client::Camera
