#include "Scenes/GameProjection.h"

#include "Core/Expected.h"
#include "Render/Pass3D.h"

#include "Render/Billboard.h"
#include "Render/Models/OriginalRotation.h"
#include "World/OriginalFtol.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_int2.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <string>

namespace Client::Scenes
{
namespace
{
std::int32_t WrappedAdd(std::int32_t a, std::int32_t b)
{
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a) + std::bit_cast<std::uint32_t>(b));
}
std::int32_t WrappedSubtract(std::int32_t a, std::int32_t b)
{
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a) - std::bit_cast<std::uint32_t>(b));
}
std::int32_t WrappedProduct(std::int32_t a, std::uint32_t b)
{
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a) * b);
}
} // namespace

Core::Expected<GameProjection, std::string> MakeGameProjection(const Render::Pass3D& pass, float fieldOfView)
{
    const auto width = pass.viewport.z;
    const auto height = pass.viewport.w;
    if (width < 2 || height < 2 || pass.screen.width <= 0 || pass.screen.height <= 0)
        return Core::Unexpected(std::string("0x6203c0: viewport o pantalla sin semilados positivos"));
    constexpr float HalfDegree = std::bit_cast<float>(0x3c0efa33u); // 6ba70c; conserva sus bits, no π/360 recalculado.
    const double tangent = std::tan(static_cast<double>(fieldOfView) * HalfDegree);
    const float aspect = static_cast<float>(static_cast<double>(width) / height);
    GameProjection projection;
    projection.view = Render::ViewRows(pass.view);
    projection.horizontalScale = static_cast<float>((tangent / (width / 2)) * aspect);
    projection.verticalScale =
        static_cast<float>((tangent / (height / 2)) * (static_cast<double>(pass.screen.height) / height));
    projection.centerX = WrappedAdd(pass.viewport.x, width / 2);
    const auto top = WrappedSubtract(WrappedSubtract(pass.screen.height, pass.viewport.y), height);
    projection.centerY = WrappedAdd(top, height / 2);
    projection.screenWidth = pass.screen.width;
    projection.screenHeight = pass.screen.height;
    return projection;
}

Core::Expected<glm::ivec2, std::string> ProjectGamePoint(const GameProjection& projection, const glm::vec3& point)
{
    if (projection.screenWidth <= 0 || projection.screenHeight <= 0)
        return Core::Unexpected(std::string("0x620610: pantalla sin divisores positivos"));
    const auto eye = Render::Models::OriginalVectorTransform(point, projection.view);
    const auto x = World::OriginalFtolLow((static_cast<double>(eye.x) / projection.horizontalScale) / eye.z);
    if (!x)
        return Core::Unexpected(std::string("0x6205d8: proyección x no representable"));
    const auto y = World::OriginalFtolLow((static_cast<double>(eye.y) / projection.verticalScale) / eye.z);
    if (!y)
        return Core::Unexpected(std::string("0x6205f6: proyección y no representable"));
    const auto screenX = WrappedSubtract(projection.centerX, *x);
    const auto screenY = WrappedAdd(projection.centerY, *y);
    constexpr std::uint32_t VirtualWidth = 640;
    constexpr std::uint32_t VirtualHeight = 480;
    return glm::ivec2(WrappedProduct(screenX, VirtualWidth) / projection.screenWidth,
                      WrappedProduct(screenY, VirtualHeight) / projection.screenHeight);
}
} // namespace Client::Scenes
