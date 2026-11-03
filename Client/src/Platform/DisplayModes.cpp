#include "Platform/DisplayModes.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

#include <cstddef>
#include <memory>

// DisplayModes: lo que dice SDL del monitor principal.
namespace Client::Platform
{
Core::Expected<std::vector<App::Resolution>, std::string> MonitorResolutions(const SdlContext& /*sdl*/)
{
    const SDL_DisplayID display = SDL_GetPrimaryDisplay();
    if (display == 0)
    {
        return Core::Unexpected(std::string("no hay monitor principal: ") + SDL_GetError());
    }
    std::vector<App::Resolution> resolutions;
    if (const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display); desktop != nullptr)
    {
        resolutions.push_back({desktop->w, desktop->h});
    }
    int count = 0;
    // Una sola reserva que se libera con SDL_free.
    const std::unique_ptr<SDL_DisplayMode*, decltype(&SDL_free)> modes(SDL_GetFullscreenDisplayModes(display, &count),
                                                                       &SDL_free);
    if (modes)
    {
        resolutions.reserve(resolutions.size() + static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            const SDL_DisplayMode* mode = modes.get()[i];
            if (mode != nullptr)
            {
                resolutions.push_back({mode->w, mode->h});
            }
        }
    }
    if (resolutions.empty())
    {
        return Core::Unexpected(std::string("no se leyeron los modos del monitor: ") + SDL_GetError());
    }
    return resolutions;
}
} // namespace Client::Platform
