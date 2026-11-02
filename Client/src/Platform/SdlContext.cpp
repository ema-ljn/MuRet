#include "Platform/SdlContext.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>

#include <string>
#include <utility>

// SdlContext: el SDL_Init y el SDL_Quit del cliente.
namespace Client::Platform
{
namespace
{
// Si hay un SdlContext vivo. SDL_Quit cierra SDL para todos (SDL_init.h): con dos, el primero que
// muere dejaría al otro sin SDL.
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
        // Como el SdlVideo de la CL0.3, que llamaba a SDL_Quit aunque SDL_Init fallara: libera lo
        // que haya quedado a medias.
        SDL_Quit();
        return Core::Unexpected(std::move(error));
    }
    s_alive = true;
    // El constructor es privado: make_unique no lo ve.
    return std::unique_ptr<SdlContext>(new SdlContext());
}

SdlContext::~SdlContext()
{
    SDL_Quit();
    s_alive = false;
}
} // namespace Client::Platform
