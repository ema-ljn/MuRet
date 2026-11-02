#pragma once

#include "Core/IClock.h"

#include <chrono>
#include <cstdint>

// Las constantes del bucle (CL3.2): el paso de la lógica, el tope del frame y el ritmo del modo
// fiel. Vienen de 02 § El ritmo y de la tabla "Lo que dice el binario del bucle" del plan de la
// CL3.
namespace Client::App
{
// 02 § El ritmo: CMP [0x6d5ee4],0x28 en 0x637791, SUB EAX,0x28 en 0x638167.
inline constexpr Core::Duration LogicStep = std::chrono::milliseconds{40};
// Pregunta 2: un frame más largo cuenta como este y el resto se pierde. El aviso llega con el Log
// de la CL3.4; hasta entonces cuenta en FrameStats::clampedFrames. No es como MuMain (ADR 0001):
// ahí, debajo de 25 FPS, el factor del frame se topea en 1 paso y el juego va en cámara lenta
// (ZzzAI.cpp:678); MuRet se pone al día con hasta 25 pasos por frame.
inline constexpr Core::Duration MaxFrameTime = std::chrono::seconds{1};
// El tick de GetTickCount en la VM XP: 15,625 ms, 64 por segundo (02 § El ritmo).
inline constexpr Core::Duration FaithfulTick = std::chrono::microseconds{15'625};
// La espera de 0x6385fd..0x638624 (CMP ...,0x28 en 0x63860f + JGE en 0x638616), en ms de
// GetTickCount.
inline constexpr std::int32_t FaithfulWaitMs = 40;
// El valor inicial del acumulador del original: la .data de 0x6d5ee4 vale 0x28 (CL3.R).
inline constexpr std::int32_t FaithfulInitialAccumulatorMs = 40;
// El tope de FPS más alto que aceptan [Video] MaxFps y --max-fps (0 es sin tope). No sale del
// original, que no tiene tope porque espera siempre 46,875 ms: con más de 1000, un frame duraría
// menos de 1 ms y el tope ya no frenaría nada que se note.
inline constexpr int MaxFpsLimit = 1000;
} // namespace Client::App
