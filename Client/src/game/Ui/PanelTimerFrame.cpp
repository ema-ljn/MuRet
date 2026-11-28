#include "Ui/PanelTimerFrame.h"

#include <bit>
#include <cstdint>

namespace Client::Ui
{
PanelTimerFrame PanelTimerFrameAt(std::uint32_t now, std::uint32_t started, std::uint32_t ends)
{
    const std::uint32_t elapsed = now - started;
    const auto duration = std::bit_cast<std::int32_t>(ends - started);
    // FILD of the unsigned elapsed time, FIDIV of the signed duration and the three native FSTPs.
    const float ratio = static_cast<float>(static_cast<double>(elapsed) / static_cast<double>(duration));
    const float width = static_cast<float>(static_cast<double>(ratio) * 150.0);
    return {.elapsedSeconds = elapsed / 1000u,
            .width = width,
            .du = static_cast<float>(static_cast<double>(width) * 0.00390625)};
}
} // namespace Client::Ui
