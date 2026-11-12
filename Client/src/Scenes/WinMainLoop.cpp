#include "Scenes/WinMainLoop.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"

#include <cstdint>

namespace Client::Scenes
{
namespace
{
// 0x412890: el cierre de una variable protegida saca un rand() por byte; el contador tiene 4.
constexpr std::uint16_t CounterCloseRandoms = Render::Models::SmallLogicalCloseRandoms;
// 0x4c0ec0: el cierre del byte del foco.
constexpr std::uint16_t FocusCloseRandoms = 1;
// 0x4bbd44 y 0x4bbed5: los dos contadores comparan con 30.
constexpr std::int32_t LastQuietLoop = 0x1e;

void Draw(Render::Models::CrtRandom& random, std::uint16_t count)
{
    for (std::uint16_t i = 0; i < count; ++i)
        static_cast<void>(random.Next());
}
} // namespace

void StepTheWinMainLoop(WinMainCounters& counters, Render::Models::CrtRandom& random)
{
    // 0x4bbc57: abre el contador; 0x4bbd35: lo sube.
    ++counters.loops;
    if (counters.loops > LastQuietLoop)
    {
        // 0x4bbd51..0x4bbde5: cierra el acceso de la vuelta.
        Draw(random, CounterCloseRandoms);
        // 0x4bbdea..0x4bbecf: abre el segundo contador y lo sube; 0x4bda4d..0x4bdae3: menor que 31, lo cierra. Con
        // 31 o más (0x4bbedc), el chequeo de 0x4bc27d, que solo alcanza la ventana minimizada: el reinicio de abajo
        // lo deja en 0 en cada ronda.
        ++counters.rounds;
        Draw(random, CounterCloseRandoms);
        // 0x4bdaef: con IsIconic(hWnd) en 0, los dos en 0, cada uno con su apertura y su cierre (0x4bdbd9,
        // 0x4bdd58). 0x4bddfd: vuelve a abrir el primero, que cierra la cola común.
        counters.rounds = 0;
        Draw(random, CounterCloseRandoms);
        counters.loops = 0;
        Draw(random, CounterCloseRandoms);
    }
    // 0x4bded9: la cola común cierra el contador.
    Draw(random, CounterCloseRandoms);
    // 0x4bdf76..0x4bdfdd: abre el foco, lo lee y lo cierra (0x4c0ec0). Con foco, el frame.
    Draw(random, FocusCloseRandoms);
}

void CloseTheFrameCounter(Render::Models::CrtRandom& random)
{
    Draw(random, CounterCloseRandoms);
}
} // namespace Client::Scenes
