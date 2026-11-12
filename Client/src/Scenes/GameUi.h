#pragma once

#include "Core/Expected.h"

#include <array>
#include <cstdint>
#include <string>

namespace Client::Input
{
class InputState;
}

namespace Client::Render::Models
{
class CrtRandom;
}

// La UI del juego en el paso y en el frame, lo del hito 1 de la CL8 (06 § HUD y ventanas, "El orden" y "El clic
// es primero de la UI"): los diálogos antes del paso (0x623e70), el bloque de la UI del paso (0x636ded..0x63705b)
// con 0x5ee140 y 0x59ff10, y el picking del frame (0x59ec80). Lo que es de otro hito o de otra ola corta con su
// dirección; lo que el original hace sin efecto en MuRet (listas vacías, objetos que nadie arma) va como comentario
// en su lugar.
namespace Client::Scenes
{
class WorldState;
struct GameMovementState;

struct GameUiContext
{
    WorldState& world;
    Render::Models::CrtRandom& random;
    const Input::InputState& input;
    // 0x7d3682c y 0x7d36828: el mouse en 640 × 480.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
};

// El WndProc: 0x7d366d4 (el flanco del clic, que apagan quien lo usa y el botón suelto) y 0x7d36874 (el botón
// abajo), antes de cualquier lógica del paso. GameScene lo llama al empezar cada paso; MoveHero también, para sus pruebas.
void LatchTheLeftButton(GameMovementState& state, bool pressed, bool down);

// 0x637c4a..0x637c5c → 0x623e70: Esc y el diálogo abierto, en cada paso de la escena 5 antes de su lógica.
[[nodiscard]] Core::Expected<void, std::string> StepTheGameDialogs(const GameUiContext& context);

// 0x636ded..0x63705b: el bloque de la UI del paso, después de las olas del agua (0x636de8) y antes de los objetos
// (0x63706a).
[[nodiscard]] Core::Expected<void, std::string> StepTheGameUi(const GameUiContext& context);

struct FramePickingContext
{
    WorldState& world;
    // El rand() del frame: el real en un frame del original, la copia en uno proyectado.
    Render::Models::CrtRandom& random;
    std::uint8_t& ownRecordDepth;
    // 0x6c8e28, 0x6c8e2c, 0x6c8e30, 0x6c8e34 y 0x6c8e38.
    std::array<std::int32_t, 5>& targets;
    // GetAsyncKeyState(0x12) en 0x59ef37.
    bool altDown = false;
};

// 0x637641 → 0x59ec80: el picking del frame, primero del 2D. Las búsquedas del ítem del piso, del NPC, del
// personaje y del objeto son de sus olas (CL9.1, CL9.3) y dejan −1, como en la traza.
[[nodiscard]] Core::Expected<void, std::string> PickInTheFrame(const FramePickingContext& context);
} // namespace Client::Scenes
