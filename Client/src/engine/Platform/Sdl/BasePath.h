#pragma once

#include "Core/Expected.h"
#include "PlatformApi.h"

#include <filesystem>
#include <string>

namespace Client::Platform::Sdl
{
// SDL gives it in UTF-8, absolute and ending in a separator, and keeps it (in SDL3 it is not freed). Works before
// SDL_Init. If SDL does not know it, returns SDL's error.
PLATFORM_API Core::Expected<std::filesystem::path, std::string> ExecutableFolder();
} // namespace Client::Platform::Sdl
