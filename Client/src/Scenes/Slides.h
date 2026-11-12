#pragma once

#include <cstdint>

namespace Client::Render::Models
{
class CrtRandom;
}

// El cartel de Slide.bmd del lado del juego (CL8 hito 3; 06 § El cartel de Slide.bmd): lo que lee el registro propio
// (0x46b0e0) y el timer 0x3eb de la ventana, que arma 0x46b010 en el primer paso del juego y atiende el WndProc. El
// objeto y sus colas están en UI/SlideBanner, en WorldRuntime::hud.slides.
namespace Client::Scenes
{
class WorldState;

// 0x46b0e0: con la escena 5, los carteles prendidos (0x6c7a8c) y la cola normal apagada (+0x8c0 ≤ 0), el nivel propio
// (+0xe, con un acceso al registro propio: 3.848 rand() si lo cierra en 0), un texto de su tramo (0x46b680, un rand()
// si el tramo tiene cuenta) y, si hay, a la cola normal con 0x46b720(1, 0, texto, 1, [+0x1218], 0xffc8dce6).
void QueueATimedSlide(WorldState& world, Render::Models::CrtRandom& random, bool gameScene);

// 0x636920 → 0x46b010, en el primer paso del juego: las dos colas sin texto y en (0, 0x1ad) (0x46a460 y 0x45a9d0),
// el timer 0x3eb con el período de Slide.bmd desde `now` (SetTimer, que pisa el de antes) y 0x46b0e0.
void StartTheSlides(WorldState& world, Render::Models::CrtRandom& random, std::uint32_t now);

// El WM_TIMER 0x3eb, entre dos vueltas de WinMain: con el timer vencido, el WndProc lee el foco (0x4b1a6b..0x4b1b24,
// un rand() al cerrarlo), con foco corre 0x46b0e0 y la cola común vuelve a leer el foco (0x4b335a, otro rand()). El
// timer vuelve a contar el período desde `now`. MuRet tiene siempre el foco, como en StepTheWinMainLoop. *Inferido*
// (P-C80): Windows deja el WM_TIMER para cuando la cola está vacía y no lo acumula, así que corre una vez en la pausa
// después del frame en que vence, detrás de los mensajes de la red de esa pausa; MuRet lo corre al cerrar la vuelta,
// antes del Poll de la siguiente.
void RunTheSlideTimer(WorldState& world, Render::Models::CrtRandom& random, std::uint32_t now, bool gameScene);
} // namespace Client::Scenes
