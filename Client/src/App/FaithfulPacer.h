#pragma once

#include "App/LoopTiming.h"
#include "Core/IClock.h"

#include <cstdint>

// El ritmo del original (CL3.2, modo Faithful): el bucle de 0x637700 con GetTickCount sobre la
// grilla de FaithfulTick. Solo el dibujo, el swap y la espera van al acumulador; los pasos y lo
// que corre antes de medir se pierden de a ticks enteros. Sin las reglas de FixedStep.
namespace Client::App
{
// Los ms que leería GetTickCount en `now`, de a FaithfulTick: DWORD, como el original.
[[nodiscard]] std::uint32_t TickCountMs(Core::Duration now);

// El ritmo del original, sin las reglas de FixedStep: sin tope y en ms enteros. El acumulador
// arranca en FaithfulInitialAccumulatorMs, como el original.
class FaithfulPacer
{
public:
    // Los pasos que tocan (mientras el acumulador valga 40 o más), como 0x637791.
    int BeginFrame();
    // Toma el inicio (0x63852b). Se llama después de los pasos y antes de dibujar.
    void StartMeasure(Core::IClock& clock);
    // Duerme de tick en tick mientras static_cast<std::int32_t>(TickCountMs(now) - inicio) sea
    // menor que 40, y suma esa diferencia al acumulador (0x638632). La comparación es con signo,
    // como el JGE de 0x638616: un dibujo de más de 24,8 días da una resta negativa y la espera
    // sigue hasta que la resta da la vuelta, unos 19,7 días más con uno de 30. Así cuelga el
    // original, y así MuRet.
    void EndFrame(Core::IClock& clock);
    [[nodiscard]] std::int32_t AccumulatedMs() const;
    [[nodiscard]] std::int32_t LastMeasuredMs() const;

private:
    // DAT_006d5ee4: un int con signo, sin tope.
    std::int32_t m_accumulatedMs = FaithfulInitialAccumulatorMs;
    // Lo que leyó GetTickCount en 0x63852b.
    std::uint32_t m_startMs = 0;
    std::int32_t m_lastMeasuredMs = 0;
};
} // namespace Client::App
