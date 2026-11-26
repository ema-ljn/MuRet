#pragma once

#include "Platform/Resolution.h"
#include "Core/Expected.h"
#include "Platform/Sdl/SdlContext.h"
#include "PlatformApi.h"

#include <string>
#include <vector>

// For Platform::IsValidResolution: the ones MuMain lists in its options (MuGetSupportedDisplayResolutions, with
// SDL_GetFullscreenDisplayModes) and the desktop's.
namespace Client::Platform::Sdl
{
// The desktop's first, then the fullscreen ones in SDL's order (largest to smallest) and with repeats, because SDL
// gives a mode per refresh rate and density. The desktop's always goes: some monitors list no fullscreen mode (Xvfb
// gives 0). Needs the SdlContext alive. The error, with no primary monitor or no mode, carries SDL_GetError().
[[nodiscard]] PLATFORM_API Core::Expected<std::vector<Resolution>, std::string>
MonitorResolutions(const SdlContext& sdl);
} // namespace Client::Platform::Sdl
