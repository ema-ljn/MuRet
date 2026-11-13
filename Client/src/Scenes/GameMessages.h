#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/ITextDrawer.h"
#include "UI/MessageLog.h"
#include "UI/Widget.h"

#include <cstdint>
#include <string>
#include <string_view>

// Los mensajes del juego sobre el chat de WorldRuntime (CL8 hito 3; 06 § El chat): 0x555190 para quien manda un
// mensaje (la red, los comandos) y los dos relojes que 0x637700 corre detrás del paso de cualquier escena.
namespace Client::Scenes
{
class WorldState;
struct ActorState;
struct HudState;
struct WorldRuntime;

// El nombre del héroe (+0x1d9) como lo leen strcmp y sprintf: hasta su '\0'. Con los diez bytes llenos sigue +0x1e3,
// que la respuesta del F3 03 pone en 0.
[[nodiscard]] std::string_view HeroNameOf(const ActorState& hero);

// La entrada de 0x555190 sobre el chat y los modos de la UI de `runtime`, midiendo con `measure` en una pantalla de
// `screen` y con la fuente del DC que dejó el último dibujo.
[[nodiscard]] UI::MessageSink MessageSinkOf(WorldRuntime& runtime, const Render::Text::ITextMeasure& measure,
                                            App::PixelSize screen);

// 0x555190(nombre, texto, tipo). El sonido que pide (el 0x26 del filtro) queda en WorldRuntime::lastSoundId.
[[nodiscard]] Core::Expected<void, std::string> AddGameMessage(WorldRuntime& runtime,
                                                               const Render::Text::ITextMeasure& measure,
                                                               App::PixelSize screen, std::string_view name,
                                                               std::string_view text, std::int32_t type);

// Las 120 llamadas a 0x555190 con dos textos vacíos de la .bss con que entran el login (0x634b4a), la selección
// (0x6341d3) y el primer paso del juego (0x636881): envejecen el registro y vacían el área vieja. Sin medidor (los
// tests que no lo miran), nada.
[[nodiscard]] Core::Expected<void, std::string> AddTheEmptyMessages(WorldRuntime& runtime,
                                                                    const Render::Text::ITextMeasure* measure);

// 0x637cf3 y 0x637cf8: el reloj de los avisos (0x5547e0) y el del área vieja (0x5554b0), uno por paso; después,
// 0x637d2c y 0x637d44 bajan el freno del chat y el de las macros.
[[nodiscard]] Core::Expected<void, std::string>
StepTheMessageClocks(WorldRuntime& runtime, const Render::Text::ITextMeasure& measure, App::PixelSize screen);
// Lo común del dibujo de las tres escenas antes de su UI (0x6376a0..0x6376b6 en el juego, 0x63552a..0x63553f en el
// login y 0x634949..0x63495f en la selección): los avisos (0x554810) y, con el registro del chat en su ventana o
// fuera del juego, el área vieja (0x5554e0). Escribe en `hud` lo que el dibujo deja: un dibujo intermedio de MuRet
// pasa una copia.
void DrawTheMessages(const WorldState& world, HudState& hud, Render::SpriteBatch& sprites, ITextDrawer& text,
                     UI::Point mouse, bool inTheGame);
} // namespace Client::Scenes
