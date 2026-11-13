#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <functional>
#include <string>

// El recorrido de la cámara de Crywolf (CCameraMove, 0x403300..0x404c90; CL9 hito 11, fases 3 y 5; 06 § El recorrido
// de la cámara). Lo empieza solo el BD 00 con la etapa 2 (0x4e597f → 0x4033b0), 5.000 ms después; ninguna tecla,
// comando ni entrada a un mapa lo pone. Después, el guion (0x4035f0): la cámara va de celda en celda (0x403d90,
// 0x4034e0) y, en cada una, la etapa crea personajes con las claves 10000 en adelante (0x4039f0, el alta de los
// monstruos del hito 2), los hace caminar (0x403760) y les cambia la acción (0x403900). *Estático*.
namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::Scenes
{
struct CameraTour;
struct WorldEntryResources;
class WorldState;

// Lo que pide el paso del recorrido (0x403590) afuera del objeto.
struct CameraTourContext
{
    WorldState& world;
    Render::Models::CrtRandom& random;
    // timeGetTime, de 0x4033f0 y 0x404c90.
    std::uint32_t now = 0;
    // 0x5141e0 y 0x4ea030: el alta de los personajes del guion y sus acciones. nullptr corta en el primer alta.
    const WorldEntryResources* resources = nullptr;
    // 0x508a80 desde 0x403760: la altura del terreno (0x5f93c0). Vacía, el primer personaje que camina corta.
    std::function<double(float, float)> height{};
    // 0x403390, en cada paso con el recorrido andando: 0x5be150 (el ítem del cursor vuelve a su lugar) y
    // 0x657880(0x1b) (los paneles, como con Esc). Vacío corta.
    std::function<Core::Expected<void, std::string>()> releaseTheUi{};
};

// 0x4033d0: +0x158 y +0x6a en 0 y la etapa en 2.
[[nodiscard]] bool CameraTourRunning(const CameraTour& tour);

// 0x403300: los valores del constructor.
void ResetTheCameraTour(CameraTour& tour);

// 0x403d60: borra los personajes del guion, de 10000 a +0x14c − 1, con 0x510ff0.
void RemoveTheCameraTourActors(WorldState& world, const CameraTour& tour);

// 0x403590, desde el paso del juego (0x637138):
//   - 0x4033f0: con la etapa 2 y la espera, 0x404c90(5000) la apaga; con la etapa 3, 0x403d60 y 0x403300;
//   - con 0x4033d0 verdadero, 0x403390 y el guion de la etapa 2 (0x4035f0).
// 0x4033d0 lo leen también el clic (0x59ae24), la UI (0x59ff3f), Esc (0x623ec8), la cámara (0x635ead) y el centro del
// mundo (0x637324); los dos últimos no están portados (P-C299).
[[nodiscard]] Core::Expected<void, std::string> StepTheCameraTour(const CameraTourContext& context);
} // namespace Client::Scenes
