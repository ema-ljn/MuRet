#pragma once

#include "Core/IClock.h"
#include "Models/Animation.h"
#include "Render/Pass3D.h"
#include "Camera/GameCamera.h"

#include <glm/ext/vector_float3.hpp>

// The world pass of the scenes without terrain yet and their fixed cameras (06 § Cámara, CL6.R). 0x633920's paths run
// in the logic and write another position, other angles and a fov of 45, but 0x635ae0 overwrites them before they
// reach 0x620db0: this is what draws.
namespace Client::Camera
{
// 0x620db0(0, 25, 640, 430), in 0x6350b0 (the login) and 0x634680 (the selection).
inline constexpr Render::ViewportRect WorldViewport{0, 25, 640, 430};

// 0x635ae0 in scene 4: fov 35 (0x6d5d48), near 20 (0x6d5d40), 0x6d5d44's far plane with DAT_07d3a0ec at 0 (11519.9),
// x −84.5° (0x6ba7bc plus DAT_07d325c8, at 0), z −45° and position (13909, 14482, 543). Checked against the trace (06 §
// Cámara).
[[nodiscard]] Render::Camera3D SelectionCamera();

// 0x635ae0 in scene 2: the same with far 20400, x −96.5° (0x6ba7c0 plus DAT_07d325c8), z −36° and position (12778,
// 12678, 579). *Static*: there is no trace of the login (P-C13).
[[nodiscard]] Render::Camera3D LoginCamera();

// 0x635ae0 once per original frame; the draws in between of the same input only read. ADR0005: each Enter starts a
// local carry; its first draw counts a frame even at the same clock.
[[nodiscard]] Render::Camera3D CameraForFrame(Camera::GameCamera& state, const Camera::GameCameraInput& input,
                                              Models::OriginalFrameCarry& frames, Core::Duration clock,
                                              Models::OriginalFramePace pace);

// The login's hero (DAT_0743a170, character 0): 0x6350b0 writes it this position every frame (0x6350d3..0x6350e1). It
// is the center of the terrain's trapezoid (0x5fb570) and of the embers (CL7.6 B).
inline constexpr glm::vec3 LoginHeroPosition{11679.0f, 14191.0f, 333.0f};

// The point 0x634680 passes to 0x5fbb00 (0x63475c), a fixed local (0x6346b5..0x6346c3): the selection's trapezoid
// center, which is no character. The game's first step reads it (Q-C216).
inline constexpr glm::vec3 SelectionCullingPoint{12975.0f, 15423.0f, 333.0f};
} // namespace Client::Camera
