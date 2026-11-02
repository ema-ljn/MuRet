#include "Platform/BasePath.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>

#include <algorithm>
#include <cstring>

namespace Client::Platform
{
Core::Expected<std::filesystem::path, std::string> ExecutableFolder()
{
    const char* const basePath = SDL_GetBasePath();
    if (basePath == nullptr)
    {
        return Core::Unexpected(std::string("no se sabe la carpeta del ejecutable: ") + SDL_GetError());
    }
    // Como char8_t, path la lee como UTF-8; con char, en Windows usaría la página ANSI.
    const std::size_t length = std::strlen(basePath);
    std::u8string utf8(length, u8'\0');
    std::transform(basePath, basePath + length, utf8.begin(), [](char c) { return static_cast<char8_t>(c); });
    return std::filesystem::path(utf8);
}
} // namespace Client::Platform
