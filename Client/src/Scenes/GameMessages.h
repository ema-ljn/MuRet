#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "UI/MessageLog.h"

#include <cstdint>
#include <string>
#include <string_view>

// Los mensajes del juego sobre el chat de WorldRuntime (CL8 hito 3; 06 § El chat): 0x555190 para quien manda un
// mensaje (la red, los comandos) y los dos relojes que 0x637700 corre detrás del paso de cualquier escena.
namespace Client::Scenes
{
struct WorldRuntime;

// La entrada de 0x555190 sobre el chat y los modos de la UI de `runtime`, midiendo con `measure` en una pantalla de
// `screen` y con la fuente del DC que dejó el último dibujo.
[[nodiscard]] UI::MessageSink MessageSinkOf(WorldRuntime& runtime, const Render::Text::ITextMeasure& measure,
                                            App::PixelSize screen);

// 0x555190(nombre, texto, tipo). El sonido que pide (el 0x26 del filtro) queda en WorldRuntime::lastSoundId.
[[nodiscard]] Core::Expected<void, std::string> AddGameMessage(WorldRuntime& runtime,
                                                               const Render::Text::ITextMeasure& measure,
                                                               App::PixelSize screen, std::string_view name,
                                                               std::string_view text, std::int32_t type);

// 0x637cf3 y 0x637cf8: el reloj de los avisos (0x5547e0) y el del área vieja (0x5554b0), uno por paso.
[[nodiscard]] Core::Expected<void, std::string> StepTheMessageClocks(WorldRuntime& runtime,
                                                                     const Render::Text::ITextMeasure& measure,
                                                                     App::PixelSize screen);
} // namespace Client::Scenes
