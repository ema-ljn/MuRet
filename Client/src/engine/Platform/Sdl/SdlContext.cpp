#include "Platform/Sdl/SdlContext.h"

#include "Core/Expected.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>

#include <memory>
#include <string>
#include <utility>

namespace Client::Platform::Sdl
{
namespace
{
// SDL_Quit shuts SDL down for everyone (SDL_init.h): with two, the first to die would leave the other without SDL.
bool s_alive = false;
} // namespace

Core::Expected<std::unique_ptr<SdlContext>, std::string> SdlContext::Create()
{
    if (s_alive)
    {
        return Core::Unexpected(std::string("ya hay un SdlContext vivo: el SDL_Quit de uno cerraría el otro"));
    }
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::string error = std::string("SDL_Init falló: ") + SDL_GetError();
        // Even after a failed SDL_Init: frees whatever was left half done.
        SDL_Quit();
        return Core::Unexpected(std::move(error));
    }
    s_alive = true;
    // The constructor is private: make_unique cannot see it.
    return std::unique_ptr<SdlContext>(new SdlContext());
}

SdlContext::~SdlContext()
{
    SDL_Quit();
    s_alive = false;
}
} // namespace Client::Platform::Sdl
