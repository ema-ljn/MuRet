#pragma once

#include "Core/Expected.h"
#include "Core/LocalTime.h"

#include <string>

// La hora local del sistema (CL8 hito 6), la del GetLocalTime del original (0x6381c7, el nombre de la captura de
// pantalla), con el reloj de SDL.
namespace Client::Platform
{
// SDL_GetCurrentTime y SDL_TimeToDateTime con la hora local. En Windows SDL la saca de la hora del sistema con la
// zona de ahora (SystemTimeToTzSpecificLocalTime), lo que GetLocalTime da (*inferido* de las fuentes de SDL 3.2). Si
// SDL no la sabe, devuelve su error.
[[nodiscard]] Core::Expected<Core::LocalTime, std::string> ReadLocalTime();
} // namespace Client::Platform
