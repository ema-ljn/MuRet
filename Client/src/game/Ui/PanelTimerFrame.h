#pragma once

#include <cstdint>

// 0x660ad6..0x660b98: the dialog bar's data computed during its drawing, with the x87 FSTPs.
namespace Client::Ui
{
struct PanelTimerFrame
{
    std::uint32_t elapsedSeconds = 0;
    float width = 0;
    float du = 0;
};

[[nodiscard]] PanelTimerFrame PanelTimerFrameAt(std::uint32_t now, std::uint32_t started, std::uint32_t ends);
} // namespace Client::Ui
