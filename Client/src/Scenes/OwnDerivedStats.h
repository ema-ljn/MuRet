#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
}
namespace Client::Scenes
{
// Campos del payload R empleados por la cola propia550d10, tras sus ocho helpers.
// Se conservan entre aplicaciones; sus nombres sólo acreditan offsets, no significados de stats.
struct OwnDerivedStats
{
    std::uint16_t word4e = 0;
    std::uint16_t word60 = 0;
    std::uint16_t word62 = 0;
    std::uint16_t wordee2 = 0;
    std::uint16_t wordee4 = 0;
    std::uint16_t wordee6 = 0;
    std::uint16_t wordeea = 0;
    std::uint16_t wordeec = 0;
    std::uint16_t wordef0 = 0;
    std::uint16_t wordef2 = 0;
    std::uint16_t wordef8 = 0;
    std::uint16_t wordefa = 0;
    std::uint16_t wordefc = 0;
    std::uint16_t wordefe = 0;
    std::uint16_t wordf00 = 0;
    std::uint8_t bytef02 = 0;
    std::uint8_t bytef03 = 0;
};
// 5512bf..5513b1: conserva wrap16, clamps unsigned y los tres sorteos en orden.
// El error representa el IDIV por cero nativo; no elige divisor ni resultado sustituto.
[[nodiscard]] Core::Expected<void, std::string> UpdateOwnDerivedStatsTail(OwnDerivedStats& state,
                                                                          Render::Models::CrtRandom& random);
} // namespace Client::Scenes
