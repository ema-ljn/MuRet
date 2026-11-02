#pragma once

#include "Core/Expected.h"

#include <memory>
#include <string>

// SDL_Init y SDL_Quit con RAII (CL3.1, lo que en la CL0.3 era SdlVideo en main.cpp): SDL_Quit
// corre aunque main salga por un error. Una Window necesita un SdlContext vivo.
namespace Client::Platform
{
class SdlContext
{
public:
    // SDL_Init(SDL_INIT_VIDEO). El error trae el SDL_GetError().
    [[nodiscard]] static Core::Expected<std::unique_ptr<SdlContext>, std::string> Create();

    ~SdlContext();
    SdlContext(const SdlContext&) = delete;
    SdlContext& operator=(const SdlContext&) = delete;
    SdlContext(SdlContext&&) = delete;
    SdlContext& operator=(SdlContext&&) = delete;

private:
    SdlContext() = default;
};
} // namespace Client::Platform
