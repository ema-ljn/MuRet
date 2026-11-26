#pragma once

#include "Core/IClock.h"
#include "EngineApi.h"
#include "Input/InputState.h"

#include <chrono>
#include <optional>

// The selection's double click, like 0x640940: its own clock, not Windows' WM_LBUTTONDBLCLK nor SDL's `clicks`, which
// tolerates a small mouse move (Q-C99). A second left press within GetDoubleClickTime, with the first pending, is the
// double click; any mouse move between them clears the pending one (0x6409c3), one pixel is enough. Contract:
// Client/docs/research/06-render-escenas-y-ui.md, "La lógica" of the selection.
namespace Client::Input
{
// GetDoubleClickTime with Windows' default settings.
inline constexpr Core::Duration DoubleClickTime = std::chrono::milliseconds{500};

class ENGINE_API DoubleClick
{
public:
    // true if the second press arrived in this step.
    bool Step(const InputState& input, Core::Duration now);

private:
    std::optional<Core::Duration> m_firstPress;
    float m_x = 0.0f;
    float m_y = 0.0f;
};
} // namespace Client::Input
