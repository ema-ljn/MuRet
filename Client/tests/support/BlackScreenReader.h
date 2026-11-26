#pragma once

#include "Core/PixelSize.h"
#include "Render/Frame/IScreenReader.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Client::Tests
{
// A screen of black pixels that counts how many times it was read.
class BlackScreenReader final : public Render::Frame::IScreenReader
{
public:
    [[nodiscard]] std::vector<std::uint8_t> ReadTheScreen(Core::PixelSize screen) override
    {
        ++reads;
        return std::vector<std::uint8_t>(
            static_cast<std::size_t>(screen.width) * static_cast<std::size_t>(screen.height) * 3, 0);
    }

    int reads = 0;
};
} // namespace Client::Tests
