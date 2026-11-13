#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// El arranque del recorrido de la cámara de Crywolf (CCameraMove, 0x403300..0x403d60; CL9 hito 11, fase 3; 06 § El
// recorrido de la cámara). Lo empieza solo el BD 00 con la etapa 2 (0x4e597f → 0x4033b0), 5.000 ms después; ninguna
// tecla, comando ni entrada a un mapa lo pone. El guion (0x4035f0 y 0x403660) no está portado: corta donde empieza.
namespace Client::Scenes
{
struct CameraTour;
struct WorldRuntime;

// 0x4033d0: +0x158 y +0x6a en 0 y la etapa en 2.
[[nodiscard]] bool CameraTourRunning(const CameraTour& tour);

// 0x403300: los valores del constructor.
void ResetTheCameraTour(CameraTour& tour);

// 0x403d60: borra los personajes del guion, de 10000 a +0x14c − 1, con 0x510ff0. Sin guion no hay ninguno; con +0x14c
// en más de 10000, corta.
[[nodiscard]] Core::Expected<void, std::string> RemoveTheCameraTourActors(const CameraTour& tour);

// 0x403590, desde el paso del juego (0x637138), con timeGetTime en `now`:
//   - 0x4033f0: con la etapa 2 y la espera, 0x404c90(5000) la apaga; con la etapa 3, 0x403d60 y 0x403300;
//   - con 0x4033d0 verdadero, 0x403390 (0x5be150 y 0x657880(0x1b)) y el guion de la etapa 2 (0x4035f0), que corta.
// 0x4033d0 lo leen también el clic (0x59ae24), la UI (0x59ff3f), Esc (0x623ec8), la cámara (0x635ead) y el centro del
// mundo (0x637324): como el guion corta en el paso en que empieza, en MuRet ninguno lo ve verdadero.
[[nodiscard]] Core::Expected<void, std::string> StepTheCameraTour(WorldRuntime& runtime, std::uint32_t now);
} // namespace Client::Scenes
