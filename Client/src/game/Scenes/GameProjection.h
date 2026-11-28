#pragma once

#include "Core/Expected.h"
#include "Math/OriginalRotation.h"
#include "Render/Pass3D.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_int2.hpp>

#include <cstdint>
#include <string>

// The 0x6203c0 cache and the 0x6205b0 consumer. The projection uses the stored scalars; it does not multiply a GLM
// matrix or round after adding the center. Static contract P-C55(f), local report.
namespace Client::Kernel
{
struct GameProjection
{
    Math::OriginalMatrix view{};
    float horizontalScale = 0;
    float verticalScale = 0;
    std::int32_t centerX = 0;
    std::int32_t centerY = 0;
    std::int32_t screenWidth = 0;
    std::int32_t screenHeight = 0;
};

// The math setup of 0x6203c0's two binary32 stores. The library's tan does not vouch for FPTAN equivalence for every
// argument; the scalars can be lent explicitly.
[[nodiscard]] Core::Expected<GameProjection, std::string> MakeGameProjection(const Render::Pass3D& pass,
                                                                             float fieldOfView);
[[nodiscard]] Core::Expected<glm::ivec2, std::string> ProjectGamePoint(const GameProjection& projection,
                                                                       const glm::vec3& point);

// 0x620480(x, y, output, 0): what the back-projection of a 640 × 480 point leaves.
struct UnprojectedPoint
{
    // 0x7d36834: the camera, 0x7d366f0's translation negated, times the transpose of its rotation (0x5fcc30).
    glm::vec3 camera{};
    // The output: the camera plus the point at 2000 from the camera, also times the transpose of the rotation.
    glm::vec3 point{};
};

// 0x620480 with the last argument at 0: (x · W) / 640 and (y · H) / 480 unsigned (W and H, the screen), minus the
// cache's center; each times its scale and ±2000 (0x6ba148 and 0x6ba768) on the x87, with the fstp to float, and
// −2000 in z. The cache's far (0x6d5d44) belongs to the last argument at 1, which MuRet does not use.
[[nodiscard]] UnprojectedPoint UnprojectGamePoint(const GameProjection& projection, std::int32_t x, std::int32_t y);
} // namespace Client::Kernel
