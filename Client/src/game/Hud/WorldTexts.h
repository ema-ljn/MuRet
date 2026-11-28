#pragma once

#include "Core/Expected.h"
#include "Kernel/WorldTextQueue.h"

#include <string>

namespace Client::Render
{
class SpriteBatch;
}

// Los textos 3D del juego (CL8 hito 6; 06 § HUD y ventanas, "La pasada 2D del juego"): la cola de la .bss en 0x723c18,
// un std::deque de entradas de 0x4c bytes (el texto en 64 y la posición en tres float), que llena 0x4381f0 desde el
// cuerpo de los personajes (0x4fd603, el tipo 0xc5 con su nombre y +0x1d6, MonsterStepCases.h) y que vacía el dibujo,
// 0x4383f0, lo primero de la pasada 2D (0x63764b). Nada de esto saca rand().
namespace Client::Render::Frame
{
class ITextDrawer;
} // namespace Client::Render::Frame
namespace Client::Kernel
{
struct HudState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct GameProjection;
} // namespace Client::Scenes

namespace Client::Hud
{

struct WorldTextDrawContext
{
    // 0x723c18. En un dibujo intermedio de MuRet la cola se lee sin vaciarla: la vacía el frame del original.
    Kernel::WorldTextQueue& queue;
    // 0x6203c0 del pase del mundo de este frame, el que lee 0x6205b0. Sin ella, una entrada en la cola corta.
    const Scenes::GameProjection* projection = nullptr;
    Render::SpriteBatch& sprites;
    Render::Frame::ITextDrawer& text;
    // La fuente del DC, 0x6c8e5c y 0x6c8e64 (quedan los del último texto) y 0x77f31b8 y 0x77f31bc (0x554180).
    Kernel::HudState& hud;
    bool projecting = false;
};

// 0x4383f0: glColor3f(1, 1, 1) y, mientras la cola tenga entradas, la de adelante: su posición con la z 370 más arriba
// (0x6b8b88) proyectada con 0x6205b0; con las dos coordenadas en 0 o más, la negrita en el DC, el fondo 0xff000064, el
// texto 0xffc8e6ff y 0x623690, el texto centrado en la x. Después la saca de la cola, se haya dibujado o no.
[[nodiscard]] Core::Expected<void, std::string> DrawTheWorldTexts(WorldTextDrawContext& context);
} // namespace Client::Hud
