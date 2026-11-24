#pragma once

#include "Core/IClock.h"
#include "Render/Models/Animation.h"

#include <cstdint>

namespace Client::Crt
{
class CrtRandom;
}

// Los rand() de la vuelta de WinMain (P-C61, Q-C457, Q-C461), que no son de ninguna escena: cada vuelta sin
// mensajes pendientes abre y cierra el contador protegido de 4 bytes 0x5763578 y el byte del foco 0x5763580 antes
// del frame (0x63945d), y el frame, después de la escena (0x637700), vuelve a abrir y cerrar el contador. Con
// la ventana minimizada el foco está en 0 y la vuelta va por otra rama (SetForegroundWindow, 0x576357c y
// ShowWindow), que MuRet no modela; tampoco el rand() del foco que el WndProc saca por cada mensaje.
namespace Client::Scenes
{
struct ActorState;
struct WorldRuntime;
class WorldState;
// 0x5763578 y 0x5763574, en la .bss.
struct WinMainCounters
{
    // Las vueltas: el contador que sube en cada una (0x4bbd35).
    std::int32_t loops = 0;
    // Las rondas de 31 vueltas (0x4bbec6): con la ventana sin minimizar vuelve a 0 en la misma vuelta.
    std::int32_t rounds = 0;
};

// What lasts from WinMain's start to its end.
struct WinMainLoopState
{
    WinMainCounters counters;
    Render::Models::OriginalFrameCarry frames;
};

// 0x4bbc57..0x4bdf6d y 0x4bdf76..0x4be02f: el contador y el foco, antes del frame. 5 rand(); en la vuelta 31,
// con la ventana sin minimizar, 21, y los dos contadores en 0.
void StepTheWinMainLoop(WinMainCounters& counters, Crt::CrtRandom& random);

// 0x63950d..0x63b066: 0x63945d abre y cierra el contador después de 0x637700 (4 rand()). Su rama de más de 31
// (0x6395e7) no corre: WinMain lo vuelve a 0 en la vuelta 31.
void CloseTheFrameCounter(Crt::CrtRandom& random);

// 0x638ae4..0x6391e4: en Devias, cada comparación del rectángulo abre y cierra la celda por su cuenta.
[[nodiscard]] bool InTheDeviasChurch(ActorState& hero, Crt::CrtRandom& random);

// 0x63869c..0x6388a8: el ambiente y las paradas de la escena del mundo preceden a la selección de música.
void StepWorldAmbience(WorldRuntime& runtime, Crt::CrtRandom& random);

// The end of a loop turn. 0x63b0b7: the tail of 0x63945d switches off the dialogs' Enter (0x5763588). Then, once per
// frame of the original in this turn: in the game scene the ambience, the map's music and Crywolf's (0x430250); the
// frame counter of 0x63945d; the Slide banner's WM_TIMER 0x3eb if it is due (P-C80; once, with the loop's time in ms
// as timeGetTime); and the counters of the next turn (P-C61).
void FinishTheWinMainLoop(WinMainLoopState& loop, WorldState& world, Crt::CrtRandom& random, Core::Duration now,
                          Render::Models::OriginalFramePace pace, bool gameScene);
} // namespace Client::Scenes
