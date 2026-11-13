#include "Scenes/CameraTour.h"

#include "Core/Expected.h"
#include "Scenes/EventGlobals.h"
#include "Scenes/WorldRuntime.h"

#include <cstdint>
#include <format>
#include <string>

namespace Client::Scenes
{
namespace
{
// 0x4033d0 y 0x4033f0: la etapa del recorrido y la que lo deshace.
constexpr std::int32_t TourStage = 2;
constexpr std::int32_t UndoStage = 3;
// 0x403400: 0x404c90(5000).
constexpr std::int32_t TourDelay = 5000;
// 0x403d60: desde 10000 (el valor de +0x14c en 0x403300).
constexpr std::int32_t FirstTourActor = 10000;

// 0x404c90(período): con +0x37, +0x148 en ahora; vence con +0x148 + período <= ahora (con signo), y entonces +0x37 en
// 1 y +0x140 uno más.
bool TourTimerFires(CameraTour& tour, std::uint32_t now, std::int32_t period)
{
    if (tour.timerRestart)
    {
        tour.timerStart = now;
        tour.timerRestart = false;
    }
    const auto due = static_cast<std::int32_t>(tour.timerStart + static_cast<std::uint32_t>(period));
    if (due > static_cast<std::int32_t>(now))
        return false;
    tour.timerRestart = true;
    ++tour.timerCount;
    return true;
}
} // namespace

bool CameraTourRunning(const CameraTour& tour)
{
    return !tour.skipped && !tour.waiting && tour.stage == TourStage;
}

void ResetTheCameraTour(CameraTour& tour)
{
    tour = CameraTour{};
}

Core::Expected<void, std::string> RemoveTheCameraTourActors(const CameraTour& tour)
{
    if (tour.nextActorKey > FirstTourActor)
        return Core::Unexpected(std::format("0x403d60: borrar los personajes {}..{} del guion del recorrido "
                                            "(0x510ff0)",
                                            FirstTourActor, tour.nextActorKey - 1));
    return {};
}

Core::Expected<void, std::string> StepTheCameraTour(WorldRuntime& runtime, std::uint32_t now)
{
    CameraTour& tour = runtime.events.cameraTour;
    // 0x4033f0.
    if (tour.stage == TourStage && tour.waiting && TourTimerFires(tour, now, TourDelay))
        tour.waiting = false;
    if (tour.stage == UndoStage)
    {
        if (auto removed = RemoveTheCameraTourActors(tour); !removed.HasValue())
            return removed;
        ResetTheCameraTour(tour);
    }
    if (!CameraTourRunning(tour))
        return {};
    // 0x4035a3..0x4035da: la etapa es 2 (0x4033d0 lo pide), así que la rama de la 5 (0x403660) y la de otra
    // (0x403d60 y 0x403300) no se alcanzan.
    return Core::Unexpected(std::string("0x4035da: el recorrido de la cámara de Crywolf empieza (0x403390: 0x5be150 y "
                                        "0x657880(0x1b)) y su guion 0x4035f0 (0x403e00..0x404bb0: el camino de la "
                                        "cámara, los personajes 10000 en adelante de 0x4039f0, sus acciones y el "
                                        "efecto 0xed) no está portado"));
}
} // namespace Client::Scenes
