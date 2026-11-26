#pragma once

#include "Platform/IClipboard.h"
#include "PlatformApi.h"

#include <optional>
#include <string>
#include <string_view>

namespace Client::Platform::Sdl
{
// Adaptador SDL3; sus operaciones se llaman desde el hilo principal.
class PLATFORM_API Clipboard final : public IClipboard
{
public:
    [[nodiscard]] std::optional<std::string> ReadText() override;
    [[nodiscard]] bool WriteText(std::string_view text) override;
};
} // namespace Client::Platform::Sdl
