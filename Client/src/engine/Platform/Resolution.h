#pragma once

#include "EngineApi.h"

#include <array>
// The defaulted operator<=> below needs it, though it names nothing from it.
#include <compare> // IWYU pragma: keep
#include <span>
#include <string>

// The original's five or a real monitor mode. The original picks one of five with the registry Resolution value
// (0x4b90a0, 02 § La resolución) and its window does not resize; MuMain offers the monitor's modes
// (SDL_GetFullscreenDisplayModes). An arbitrary size like 7 × 3 is neither, and the UI is built on 640 × 480 (D-C11).
namespace Client::Platform
{
struct Resolution
{
    int width = 0;
    int height = 0;

    // Width first, then height: the order of DescribeValidResolutions' list.
    friend constexpr auto operator<=>(const Resolution&, const Resolution&) = default;
};

// The registry's, 0 to 4. 640 × 480 is the UI's reference.
inline constexpr std::array<Resolution, 5> OriginalResolutions{
    {{640, 480}, {800, 600}, {1024, 768}, {1280, 1024}, {1600, 1200}}};

// One of the original's five or a monitor mode, with the same width and height.
[[nodiscard]] ENGINE_API bool IsValidResolution(Resolution resolution, std::span<const Resolution> monitorModes);

// For the error message: smallest to largest, without repeats, like "640 × 480, 800 × 600".
[[nodiscard]] ENGINE_API std::string DescribeValidResolutions(std::span<const Resolution> monitorModes);
} // namespace Client::Platform
