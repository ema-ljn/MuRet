#include "UI/PanelTimerFrame.h"

#include <bit>
#include <cstdint>

namespace Client::UI
{
PanelTimerFrame PanelTimerFrameAt(std::uint32_t now, std::uint32_t started, std::uint32_t ends)
{
    const std::uint32_t elapsed = now - started;
    const auto duration = std::bit_cast<std::int32_t>(ends - started);
    // FILD del transcurrido sin signo, FIDIV del plazo con signo y los tres FSTP nativos.
    const float ratio = static_cast<float>(static_cast<double>(elapsed) / static_cast<double>(duration));
    const float width = static_cast<float>(static_cast<double>(ratio) * 150.0);
    return {.elapsedSeconds = elapsed / 1000u, .width = width,
            .du = static_cast<float>(static_cast<double>(width) * 0.00390625)};
}
} // namespace Client::UI
