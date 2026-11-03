#pragma once

#include "Core/IClock.h"

#include <optional>

// Algo que pasa cada `period` de reloj, no de pasos: el keepalive de la ServerSession (el plan de
// la CL4, CL4.5). Lo consulta quien lo tiene, una vez por frame, con el Now() del reloj del bucle.
//
// Sin deriva: el siguiente vence a un periodo del anterior, no del instante en que se miró, así que
// lo que tarda en mirarse no se va sumando. Si se miró un periodo o más tarde (un frame largo, un
// breakpoint), vence una sola vez y el siguiente queda a un periodo de ese instante: no se pone al
// día con una ráfaga. El original tiene un WM_TIMER, que tampoco se junta (02, "Qué avanza por
// paso, por frame y por reloj").
namespace Client::Core
{
class IntervalTimer
{
public:
    // `period` es mayor que 0.
    explicit IntervalTimer(Duration period);

    // Empieza, o vuelve a empezar: el primero vence en now + period.
    void Start(Duration now);
    void Stop();

    // true si venció, una vez por vencimiento, y deja puesto el siguiente como dice arriba.
    // Detenido, false. Un reloj que va para atrás no vence, y el vencimiento no se mueve.
    [[nodiscard]] bool Due(Duration now);

    [[nodiscard]] bool Running() const;

private:
    Duration m_period;
    // El próximo vencimiento; vacío si está detenido.
    std::optional<Duration> m_deadline;
};
} // namespace Client::Core
