#pragma once

#include "Core/Expected.h"

#include <filesystem>
#include <string>

// La carpeta del ejecutable, donde Main busca muret-client.ini (CL3.4), con SDL_GetBasePath.
namespace Client::Platform
{
// SDL la da en UTF-8, absoluta y terminada en separador, y la guarda (en SDL3 no se libera). Anda
// antes de SDL_Init. Si SDL no la sabe, devuelve su error.
Core::Expected<std::filesystem::path, std::string> ExecutableFolder();
} // namespace Client::Platform
