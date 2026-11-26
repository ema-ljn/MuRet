#pragma once

#include "Platform/IClipboard.h"
#include "PlatformApi.h"

#include <optional>
#include <string>
#include <string_view>

namespace Client::Platform::Sdl
{
// Its operations are called from the main thread.
class PLATFORM_API Clipboard final : public IClipboard
{
public:
    [[nodiscard]] std::optional<std::string> ReadText() override;
    [[nodiscard]] bool WriteText(std::string_view text) override;
};
} // namespace Client::Platform::Sdl
