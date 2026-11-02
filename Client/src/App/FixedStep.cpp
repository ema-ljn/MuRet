#include "App/FixedStep.h"

#include "App/LoopTiming.h"

#include <algorithm>
#include <cmath>

namespace Client::App
{
int FixedStep::Advance(Core::Duration frameTime)
{
    // Un reloj que vuelve para atrás no puede dejar el acumulador negativo: el frame cuenta 0.
    frameTime = std::max(frameTime, Core::Duration::zero());
    m_lastFrameWasClamped = frameTime > MaxFrameTime;
    frameTime = std::min(frameTime, MaxFrameTime);

    m_accumulated += frameTime;
    // Con el tope, el acumulador queda debajo de MaxFrameTime + LogicStep: 25 pasos como mucho.
    const auto steps = m_accumulated / LogicStep;
    m_accumulated -= steps * LogicStep;
    return static_cast<int>(steps);
}

float FixedStep::Alpha() const
{
    // Para los nanosegundos de un paso, en double alcanza la precisión; el float que sale puede
    // redondear a 1, y el recorte lo deja debajo.
    const double fraction = static_cast<double>(m_accumulated.count()) / static_cast<double>(LogicStep.count());
    return std::min(static_cast<float>(fraction), std::nextafter(1.0f, 0.0f));
}

Core::Duration FixedStep::Accumulated() const
{
    return m_accumulated;
}

bool FixedStep::LastFrameWasClamped() const
{
    return m_lastFrameWasClamped;
}
} // namespace Client::App
