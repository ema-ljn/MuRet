#pragma once

#include "Core/IClock.h"

// El paso fijo del modo normal (CL3.2, D-C4): un acumulador de tiempo que se gasta de a LogicStep,
// con MaxFrameTime de tope, y el alfa para interpolar el dibujo entre el último paso y el próximo.
// Arranca en 0: sus tests cuentan los pasos exactos por segundo simulado.
namespace Client::App
{
class FixedStep
{
public:
    // Suma el frame (negativo cuenta 0; más que MaxFrameTime cuenta MaxFrameTime; MaxFrameTime
    // justo no se recorta) y devuelve cuántos pasos corren.
    int Advance(Core::Duration frameTime);
    // acumulado / paso, recortado a std::nextafter(1.0f, 0.0f): con 39.999.999 ns el float
    // redondearía a 1.
    [[nodiscard]] float Alpha() const;
    [[nodiscard]] Core::Duration Accumulated() const;
    [[nodiscard]] bool LastFrameWasClamped() const;

private:
    Core::Duration m_accumulated{};
    bool m_lastFrameWasClamped = false;
};
} // namespace Client::App
