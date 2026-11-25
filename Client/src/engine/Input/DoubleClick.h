#pragma once

#include "Core/IClock.h"
#include "Input/InputState.h"

#include <chrono>
#include <optional>

// El doble clic de la selección, como 0x640940: un reloj propio y no el WM_LBUTTONDBLCLK de Windows
// ni el `clicks` de SDL, que tolera que el mouse se mueva un poco (Q-C99). El segundo apretón del
// botón izquierdo dentro de GetDoubleClickTime, con el primero pendiente, es el doble clic; cualquier
// movimiento del mouse entre los dos borra el pendiente (0x6409c3), un píxel basta.
// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "La lógica" de la selección.
namespace Client::Input
{
// GetDoubleClickTime con la configuración de Windows por defecto.
inline constexpr Core::Duration DoubleClickTime = std::chrono::milliseconds{500};

class DoubleClick
{
public:
    // Un paso de la lógica en `now`: true si en este paso llegó el segundo apretón.
    bool Step(const InputState& input, Core::Duration now);

private:
    std::optional<Core::Duration> m_firstPress;
    float m_x = 0.0f;
    float m_y = 0.0f;
};
} // namespace Client::Input
