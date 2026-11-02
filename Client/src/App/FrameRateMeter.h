#pragma once

#include "App/GameLoop.h"
#include "Core/IClock.h"

#include <chrono>
#include <cstdint>
#include <optional>

// Lo que imprime --stats (CL3.2, paso 3 del plan): los FPS y los pasos por segundo, promediados de
// a StatsInterval. Mide y no imprime: main lo escribe, y los tests lo miran sin salida.
namespace Client::App
{
// Cada cuánto da un promedio: 5 s, como lo pide el paso 3 de la CL3.2.
inline constexpr Core::Duration StatsInterval = std::chrono::seconds{5};

struct FrameRates
{
    double framesPerSecond = 0.0;
    double stepsPerSecond = 0.0;
};

class FrameRateMeter
{
public:
    // `start`: el Now() desde el que cuenta el primer promedio, con FrameStats en 0.
    explicit FrameRateMeter(Core::Duration start);
    // Si desde el último promedio pasó StatsInterval o más, los frames y los pasos de ese tramo por
    // segundo, y el próximo cuenta desde `now`. Si no, vacío (también si el reloj volvió).
    [[nodiscard]] std::optional<FrameRates> Update(Core::Duration now, const FrameStats& stats);

private:
    Core::Duration m_last;
    std::uint64_t m_frames = 0;
    std::uint64_t m_steps = 0;
};
} // namespace Client::App
