#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace Client::Platform
{
// Ctrl+C and Ctrl+X hand WriteText the selection the EDIT decides; Ctrl+V asks ReadText. The EDIT keeps the selection
// and replacement decisions. The original's ANSI conversion is still open in Client/docs/research/11-edit-win32.md,
// «Dibujo: correcciones y dudas abiertas».
class IClipboard
{
public:
    virtual ~IClipboard() = default;

    // nullopt means an error or no text; an empty string means available text of length zero.
    [[nodiscard]] virtual std::optional<std::string> ReadText() = 0;
    [[nodiscard]] virtual bool WriteText(std::string_view text) = 0;
};
} // namespace Client::Platform
