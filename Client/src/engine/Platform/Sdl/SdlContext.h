#pragma once

#include "Core/Expected.h"
#include "PlatformApi.h"

#include <memory>
#include <string>

// RAII: SDL_Quit runs even when main exits on an error. A Window needs a live SdlContext.
namespace Client::Platform::Sdl
{
class PLATFORM_API SdlContext
{
public:
    // SDL_Init(SDL_INIT_VIDEO). The error carries SDL_GetError().
    [[nodiscard]] static Core::Expected<std::unique_ptr<SdlContext>, std::string> Create();

    ~SdlContext();
    SdlContext(const SdlContext&) = delete;
    SdlContext& operator=(const SdlContext&) = delete;
    SdlContext(SdlContext&&) = delete;
    SdlContext& operator=(SdlContext&&) = delete;

private:
    SdlContext() = default;
};
} // namespace Client::Platform::Sdl
