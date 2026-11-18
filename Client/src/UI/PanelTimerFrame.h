#pragma once

#include <cstdint>

// 0x660ad6..0x660b98: los datos de la barra del diálogo calculados durante su dibujo, con los FSTP de la x87.
namespace Client::UI
{
struct PanelTimerFrame
{
    std::uint32_t elapsedSeconds = 0;
    float width = 0;
    float du = 0;
};

[[nodiscard]] PanelTimerFrame PanelTimerFrameAt(std::uint32_t now,std::uint32_t started,std::uint32_t ends);
} // namespace Client::UI
