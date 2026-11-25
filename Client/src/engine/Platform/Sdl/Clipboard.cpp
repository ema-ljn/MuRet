#include "Platform/Sdl/Clipboard.h"

#include <optional>
#include <string_view>

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

#include <memory>
#include <string>

namespace Client::Platform
{
namespace
{
struct SdlTextDeleter
{
    void operator()(char* text) const
    {
        SDL_free(text);
    }
};

bool HasSdlError()
{
    const char* error = SDL_GetError();
    return error != nullptr && error[0] != '\0';
}
} // namespace

std::optional<std::string> Clipboard::ReadText()
{
    SDL_ClearError();
    if (!SDL_HasClipboardText())
        return std::nullopt;

    const std::unique_ptr<char, SdlTextDeleter> text{SDL_GetClipboardText()};
    if (!text)
    {
        static_cast<void>(SDL_GetError());
        return std::nullopt;
    }
    if (text.get()[0] == '\0')
    {
        if (HasSdlError())
        {
            return std::nullopt;
        }
        return std::string{};
    }
    return std::string{text.get()};
}

bool Clipboard::WriteText(std::string_view text)
{
    const std::string nullTerminated{text};
    return SDL_SetClipboardText(nullTerminated.c_str());
}
} // namespace Client::Platform
