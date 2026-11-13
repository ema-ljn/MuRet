#include "Scenes/HealthBars.h"

#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameProjection.h"
#include "Scenes/WorldRuntime.h"
#include "UI/HudPrimitives.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_int2.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace Client::Scenes
{
namespace
{
// 0x42a2b9: el tipo 278 de 0x42d4a1.
constexpr std::uint16_t HealthBarType = 0x1a8;
// 0x42a2cb: +0x93 debajo de 5, sin signo (JNC).
constexpr std::uint8_t HealthBarLimit = 5;
// 0x42a2f1: 100 (0x6b86b4) sobre el techo de la caja.
constexpr double AboveTheTop = 100.0;
// 0x42a534 y 0x42a561: la barra empieza 25 a la izquierda y 12 abajo del punto.
constexpr std::uint32_t BarLeft = 0x19;
constexpr std::uint32_t BarDown = 0xc;
// 0x42a572 y 0x42a56a: 50 (0x42480000) por 2 (0x40000000).
constexpr float BarWidth = 50.0f;
constexpr float BarHeight = 2.0f;
// 0x42a54c: por 10 (0x6b87a8), con _ftol.
constexpr std::int32_t FillStep = 10;

// La suma y la resta de 32 bits del ADD y del SUB, sin el desborde con signo de C++.
std::int32_t Wrapped(std::int32_t value, std::uint32_t added)
{
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(value) + added);
}
} // namespace

void QueueTheHealthBar(const ActorState& actor, std::deque<QueuedHealthBar>& queue)
{
    if (actor.type != HealthBarType || actor.byte93 >= HealthBarLimit)
        return;
    // 0x42a2d4..0x42a2f9: FLD, FADD y FADD en la x87, y FSTP en float.
    const float top =
        static_cast<float>(static_cast<double>(actor.position.z) + static_cast<double>(actor.boundsTop) + AboveTheTop);
    queue.push_back(
        QueuedHealthBar{.position = glm::vec3(actor.position.x, actor.position.y, top), .fill = actor.byte93});
}

Core::Expected<void, std::string> DrawTheHealthBars(std::deque<QueuedHealthBar>& queue,
                                                    const GameProjection* projection, Render::SpriteBatch& sprites)
{
    // 0x42a4d6..0x42a4e0 y 0x42a645..0x42a64d: la cuenta se lee otra vez después de cada pop_front.
    for (std::size_t i = 0; i < queue.size(); ++i)
    {
        const QueuedHealthBar bar = queue.front();
        if (projection == nullptr)
            return Core::Unexpected(
                std::string("0x42a529: la barra de vida no tiene la proyección del pase del mundo"));
        const auto projected = ProjectGamePoint(*projection, bar.position);
        if (!projected.HasValue())
            return Core::Unexpected(projected.Error());
        const glm::ivec2 point = projected.Value();
        // 0x42a53a..0x42a582: el byte bajo del cuarto DWORD, por 10 y truncado, y las tres en float desde el entero.
        const std::int32_t fill = static_cast<std::int32_t>(bar.fill) * FillStep;
        UI::DrawGauge(sprites, static_cast<float>(Wrapped(point.x, 0u - BarLeft)),
                      static_cast<float>(Wrapped(point.y, BarDown)), BarWidth, BarHeight, static_cast<float>(fill),
                      false, true);
        queue.pop_front();
    }
    return {};
}
} // namespace Client::Scenes
