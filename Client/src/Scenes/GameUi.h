#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "UI/OldWindows.h"

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

namespace Client::Render::Text
{
class ITextMeasure;
}

// La UI del juego en el paso y en el frame, lo del hito 1 de la CL8 (06 § HUD y ventanas, "El orden" y "El clic
// es primero de la UI"): los diálogos antes del paso (0x623e70), el bloque de la UI del paso (0x636ded..0x63705b)
// con 0x5ee140 y 0x59ff10, y el picking del frame (0x59ec80). Lo que es de otro hito o de otra ola corta con su
// dirección; lo que el original hace sin efecto en MuRet (listas vacías, objetos que nadie arma) va como comentario
// en su lugar.
namespace Client::Scenes
{
class WorldState;
struct GameChatContext;
struct GameMovementState;
struct WorldRuntime;

struct GameUiContext
{
    WorldState& world;
    Render::Models::CrtRandom& random;
    const Input::InputState& input;
    // 0x7d3682c y 0x7d36828: el mouse en 640 × 480.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    // GetTextExtentPointA sobre el DC de los textos (0x57634f4): lo mide el mouse sobre los nombres del registro del
    // chat (0x45dec0) y los espacios y el pedazo del cartel (0x46a9c0). nullptr sin texto (los tests que no lo miran):
    // no elige ninguno, y el cartel que tiene que agregar un texto corta.
    const Render::Text::ITextMeasure* measure = nullptr;
    // La pantalla en píxeles (0x6d5d60 y 0x6d5d64): el clic derecho del área vieja pasa a 640 los anchos que dejó su
    // dibujo, y el cartel la usa en su rx (0x46a9c0 y 0x46abc0).
    App::PixelSize screen{640, 480};
    // Lo que las macros (0x5a116f) mandan y avisan. nullptr (los tests que no las miran): una macro es el error.
    const GameChatContext* chat = nullptr;
    // timeGetTime() de los segundos del cartel (0x46ada0).
    std::uint32_t systemTime = 0;
};

// El WndProc: 0x7d366d4 (el flanco del clic, que apagan quien lo usa y el botón suelto) y 0x7d36874 (el botón
// abajo), antes de cualquier lógica del paso. GameScene lo llama al empezar cada paso; MoveHero también, para sus
// pruebas.
void LatchTheLeftButton(GameMovementState& state, bool pressed, bool down);

// El WndProc y el soltado del izquierdo: WM_LBUTTONUP prende 0x7d366ec con el mouse de ese momento (0x4b3cea, en
// 0x576358c y 0x5763590), WM_LBUTTONDOWN lo apaga (0x4b3947) y cualquier mensaje con el mouse en otro lugar también
// (0x4b344d). MuRet ve los mensajes del paso juntos, con el mouse del paso (*inferido*, P-C96). Va antes de
// LatchTheLeftButton, con el mouse en 640 × 480.
void LatchTheLeftRelease(GameMovementState& state, bool pressed, bool released, bool down, std::int32_t mouseX,
                         std::int32_t mouseY);

// Los globales de las ventanas viejas sobre los del juego: los del mouse, los de la UI, el foco del teclado (el de la
// entrada del chat) y el último sonido.
[[nodiscard]] UI::OldWindowGlobals OldWindowGlobalsOf(WorldRuntime& runtime);

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

// 0x63717f → 0x5ac8d0: las teclas de función, al final de la lógica del paso con el mundo cargado. La barra (levantar
// el ítem del piso) y F1 (la ayuda) son del hito 6 y cortan; F2 (el chat normal en el registro), F3 (el modo del
// susurro) y F4 (el tamaño y el lugar del registro) cambian lo suyo y suenan.
[[nodiscard]] Core::Expected<void, std::string> StepTheFunctionKeys(const GameUiContext& context);

// 0x637641 → 0x59ec80: el picking del frame, primero del 2D. Las búsquedas del ítem del piso, del NPC, del
// personaje y del objeto son de sus olas (CL9.1, CL9.3) y dejan −1, como en la traza.
[[nodiscard]] Core::Expected<void, std::string> PickInTheFrame(const FramePickingContext& context);
} // namespace Client::Scenes
