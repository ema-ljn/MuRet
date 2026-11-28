#include "Hud/HealthBars.h"

#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Kernel/ActorState.h"
#include "Scenes/GameProjection.h"
#include "Kernel/WorldRuntime.h"
#include "Ui/HudPrimitives.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_int2.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace Client::Hud
{
namespace
{
// 0x42a2b9: type 278 of 0x42d4a1.
constexpr std::uint16_t HealthBarType = 0x1a8;
// 0x42a2cb: +0x93 below 5, unsigned (JNC).
constexpr std::uint8_t HealthBarLimit = 5;
// 0x42a2f1: 100 (0x6b86b4) above the box's top.
constexpr double AboveTheTop = 100.0;
// 0x42a534 and 0x42a561: the bar starts 25 to the left and 12 below the point.
constexpr std::uint32_t BarLeft = 0x19;
constexpr std::uint32_t BarDown = 0xc;
// 0x42a572 and 0x42a56a: 50 (0x42480000) by 2 (0x40000000).
constexpr float BarWidth = 50.0f;
constexpr float BarHeight = 2.0f;
// 0x42a54c: times 10 (0x6b87a8), with _ftol.
constexpr std::int32_t FillStep = 10;

// The 32-bit sum and difference of ADD and SUB, without C++'s signed overflow.
std::int32_t Wrapped(std::int32_t value, std::uint32_t added)
{
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(value) + added);
}
} // namespace

void QueueTheHealthBar(const Kernel::ActorState& actor, std::deque<Kernel::QueuedHealthBar>& queue)
{
    if (actor.type != HealthBarType || actor.byte93 >= HealthBarLimit)
        return;
    // 0x42a2d4..0x42a2f9: FLD, FADD and FADD on the x87, and FSTP to float.
    const float top =
        static_cast<float>(static_cast<double>(actor.position.z) + static_cast<double>(actor.boundsTop) + AboveTheTop);
    queue.push_back(
        Kernel::QueuedHealthBar{.position = glm::vec3(actor.position.x, actor.position.y, top), .fill = actor.byte93});
}

Core::Expected<void, std::string> DrawTheHealthBars(std::deque<Kernel::QueuedHealthBar>& queue,
                                                    const Kernel::GameProjection* projection,
                                                    Render::SpriteBatch& sprites)
{
    // 0x42a4d6..0x42a4e0 and 0x42a645..0x42a64d: the count is read again after each pop_front.
    for (std::size_t i = 0; i < queue.size(); ++i)
    {
        const Kernel::QueuedHealthBar bar = queue.front();
        if (projection == nullptr)
            return Core::Unexpected(
                std::string("0x42a529: la barra de vida no tiene la proyección del pase del mundo"));
        const auto projected = Kernel::ProjectGamePoint(*projection, bar.position);
        if (!projected.HasValue())
            return Core::Unexpected(projected.Error());
        const glm::ivec2 point = projected.Value();
        // 0x42a53a..0x42a582: the low byte of the fourth DWORD, times 10 and truncated, and the three as float from the
        // integer.
        const std::int32_t fill = static_cast<std::int32_t>(bar.fill) * FillStep;
        Ui::DrawGauge(sprites, static_cast<float>(Wrapped(point.x, 0u - BarLeft)),
                      static_cast<float>(Wrapped(point.y, BarDown)), BarWidth, BarHeight, static_cast<float>(fill),
                      false, true);
        queue.pop_front();
    }
    return {};
}
} // namespace Client::Hud
