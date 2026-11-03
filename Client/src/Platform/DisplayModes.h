#pragma once

#include "App/Resolution.h"
#include "Core/Expected.h"
#include "Platform/SdlContext.h"

#include <string>
#include <vector>

// Los modos del monitor, para App::IsValidResolution: los que lista MuMain en sus opciones
// (MuGetSupportedDisplayResolutions, con SDL_GetFullscreenDisplayModes) y el del escritorio.
namespace Client::Platform
{
// El ancho y el alto de los modos del monitor principal: primero el del escritorio y después los de
// pantalla completa, en el orden de SDL (del más grande al más chico) y con repetidos, porque SDL
// da un modo por frecuencia y por densidad. El del escritorio va siempre: hay monitores que no
// listan ninguno de pantalla completa (Xvfb da 0). Necesita el SdlContext vivo. El error, sin
// monitor principal o sin ningún modo, trae el SDL_GetError().
[[nodiscard]] Core::Expected<std::vector<App::Resolution>, std::string> MonitorResolutions(const SdlContext& sdl);
} // namespace Client::Platform
