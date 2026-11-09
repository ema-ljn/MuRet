#include "Render/Models/OriginalAngles.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <cmath>
#include <cfenv>

namespace
{
struct AngularCase
{
    std::uint32_t function;
    std::array<std::uint32_t, 4> arguments;
    double expected;
};
// Valores numéricos PC53/RN; no contiene registros ni bytes del ejecutable.
// Primeros 133: run3, SHA256 salida: 5b4d0c36cc15bd2496d3f5382f5614c35a50c1ab7c3f8125e82910d8bfba5e0e.
// Tres qNaN adicionales: run4, SHA256 e6481dc2b83c8dae0b579dbb21cffcec14c53b340371667625fc8e5e46775a7b.
constexpr std::array Cases{
    AngularCase{0x4e93f0u, {0x00000000u, 0x00000000u, 0xbf800000u, 0xbf800000u}, 0x1.3affffd68c14ap+8},
    AngularCase{0x4e93f0u, {0x00000000u, 0x00000000u, 0xbf800000u, 0x3f800000u}, 0x1.c2000052e7d6cp+7},
    AngularCase{0x4e93f0u, {0x00000000u, 0x00000000u, 0x3f800000u, 0xbf800000u}, 0x1.6800014b9f5aep+5},
    AngularCase{0x4e93f0u, {0x00000000u, 0x00000000u, 0x3f800000u, 0x3f800000u}, 0x1.0dffffad18294p+7},
    AngularCase{0x4e96f0u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x0.0p+0},
    AngularCase{0x4e96f0u, {0x00000000u, 0x00000000u, 0x00000001u, 0x00000000u}, 0x0.0p+0},
    AngularCase{0x4e96f0u, {0x00000000u, 0x42340000u, 0x00000000u, 0x00000000u}, 0x1.6800000000000p+5},
    AngularCase{0x4e96f0u, {0x00000000u, 0x42340000u, 0x00000001u, 0x00000000u}, 0x1.6800000000000p+5},
    AngularCase{0x4e96f0u, {0x00000000u, 0x43340000u, 0x00000000u, 0x00000000u}, 0x1.6800000000000p+7},
    AngularCase{0x4e96f0u, {0x00000000u, 0x43340000u, 0x00000001u, 0x00000000u}, 0x1.6800000000000p+7},
    AngularCase{0x4e96f0u, {0x00000000u, 0x43870000u, 0x00000000u, 0x00000000u}, 0x1.6800000000000p+6},
    AngularCase{0x4e96f0u, {0x00000000u, 0x43870000u, 0x00000001u, 0x00000000u}, 0x1.6800000000000p+6},
    AngularCase{0x4e96f0u, {0x3f800000u, 0x00000000u, 0x00000000u, 0x00000000u}, -0x1.0000000000000p+0},
    AngularCase{0x4e96f0u, {0x3f800000u, 0x00000000u, 0x00000001u, 0x00000000u}, 0x1.0000000000000p+0},
    AngularCase{0x4e96f0u, {0x3f800000u, 0x42340000u, 0x00000000u, 0x00000000u}, 0x1.6000000000000p+5},
    AngularCase{0x4e96f0u, {0x3f800000u, 0x42340000u, 0x00000001u, 0x00000000u}, 0x1.6000000000000p+5},
    AngularCase{0x4e96f0u, {0x3f800000u, 0x43340000u, 0x00000000u, 0x00000000u}, 0x1.6600000000000p+7},
    AngularCase{0x4e96f0u, {0x3f800000u, 0x43340000u, 0x00000001u, 0x00000000u}, 0x1.6600000000000p+7},
    AngularCase{0x4e96f0u, {0x3f800000u, 0x43870000u, 0x00000000u, 0x00000000u}, 0x1.6c00000000000p+6},
    AngularCase{0x4e96f0u, {0x3f800000u, 0x43870000u, 0x00000001u, 0x00000000u}, 0x1.6c00000000000p+6},
    AngularCase{0x4e96f0u, {0x43330000u, 0x00000000u, 0x00000000u, 0x00000000u}, -0x1.6600000000000p+7},
    AngularCase{0x4e96f0u, {0x43330000u, 0x00000000u, 0x00000001u, 0x00000000u}, 0x1.6600000000000p+7},
    AngularCase{0x4e96f0u, {0x43330000u, 0x42340000u, 0x00000000u, 0x00000000u}, -0x1.0c00000000000p+7},
    AngularCase{0x4e96f0u, {0x43330000u, 0x42340000u, 0x00000001u, 0x00000000u}, 0x1.0c00000000000p+7},
    AngularCase{0x4e96f0u, {0x43330000u, 0x43340000u, 0x00000000u, 0x00000000u}, 0x1.0000000000000p+0},
    AngularCase{0x4e96f0u, {0x43330000u, 0x43340000u, 0x00000001u, 0x00000000u}, 0x1.0000000000000p+0},
    AngularCase{0x4e96f0u, {0x43330000u, 0x43870000u, 0x00000000u, 0x00000000u}, 0x1.6c00000000000p+6},
    AngularCase{0x4e96f0u, {0x43330000u, 0x43870000u, 0x00000001u, 0x00000000u}, 0x1.6c00000000000p+6},
    AngularCase{0x4e96f0u, {0x43340000u, 0x00000000u, 0x00000000u, 0x00000000u}, -0x1.6800000000000p+7},
    AngularCase{0x4e96f0u, {0x43340000u, 0x00000000u, 0x00000001u, 0x00000000u}, 0x1.6800000000000p+7},
    AngularCase{0x4e96f0u, {0x43340000u, 0x42340000u, 0x00000000u, 0x00000000u}, -0x1.0e00000000000p+7},
    AngularCase{0x4e96f0u, {0x43340000u, 0x42340000u, 0x00000001u, 0x00000000u}, 0x1.0e00000000000p+7},
    AngularCase{0x4e96f0u, {0x43340000u, 0x43340000u, 0x00000000u, 0x00000000u}, 0x0.0p+0},
    AngularCase{0x4e96f0u, {0x43340000u, 0x43340000u, 0x00000001u, 0x00000000u}, 0x0.0p+0},
    AngularCase{0x4e96f0u, {0x43340000u, 0x43870000u, 0x00000000u, 0x00000000u}, 0x1.6800000000000p+6},
    AngularCase{0x4e96f0u, {0x43340000u, 0x43870000u, 0x00000001u, 0x00000000u}, 0x1.6800000000000p+6},
    AngularCase{0x4e96f0u, {0x43350000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x1.6600000000000p+7},
    AngularCase{0x4e96f0u, {0x43350000u, 0x00000000u, 0x00000001u, 0x00000000u}, 0x1.6600000000000p+7},
    AngularCase{0x4e96f0u, {0x43350000u, 0x42340000u, 0x00000000u, 0x00000000u}, -0x1.1000000000000p+7},
    AngularCase{0x4e96f0u, {0x43350000u, 0x42340000u, 0x00000001u, 0x00000000u}, 0x1.1000000000000p+7},
    AngularCase{0x4e96f0u, {0x43350000u, 0x43340000u, 0x00000000u, 0x00000000u}, -0x1.0000000000000p+0},
    AngularCase{0x4e96f0u, {0x43350000u, 0x43340000u, 0x00000001u, 0x00000000u}, 0x1.0000000000000p+0},
    AngularCase{0x4e96f0u, {0x43350000u, 0x43870000u, 0x00000000u, 0x00000000u}, 0x1.6400000000000p+6},
    AngularCase{0x4e96f0u, {0x43350000u, 0x43870000u, 0x00000001u, 0x00000000u}, 0x1.6400000000000p+6},
    AngularCase{0x4e96f0u, {0x43b38000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x1.0000000000000p+0},
    AngularCase{0x4e96f0u, {0x43b38000u, 0x00000000u, 0x00000001u, 0x00000000u}, 0x1.0000000000000p+0},
    AngularCase{0x4e96f0u, {0x43b38000u, 0x42340000u, 0x00000000u, 0x00000000u}, 0x1.7000000000000p+5},
    AngularCase{0x4e96f0u, {0x43b38000u, 0x42340000u, 0x00000001u, 0x00000000u}, 0x1.7000000000000p+5},
    AngularCase{0x4e96f0u, {0x43b38000u, 0x43340000u, 0x00000000u, 0x00000000u}, -0x1.6600000000000p+7},
    AngularCase{0x4e96f0u, {0x43b38000u, 0x43340000u, 0x00000001u, 0x00000000u}, 0x1.6600000000000p+7},
    AngularCase{0x4e96f0u, {0x43b38000u, 0x43870000u, 0x00000000u, 0x00000000u}, -0x1.6400000000000p+6},
    AngularCase{0x4e96f0u, {0x43b38000u, 0x43870000u, 0x00000001u, 0x00000000u}, 0x1.6400000000000p+6},
    AngularCase{0x4e96f0u, {0x35800000u, 0x42340000u, 0x00000001u, 0x00000000u}, 0x1.67ffff8000000p+5},
    AngularCase{0x4e9560u, {0x00000000u, 0x00000000u, 0x3f800000u, 0x00000000u}, 0x0.0p+0},
    AngularCase{0x4e9560u, {0x00000000u, 0x00000000u, 0x43480000u, 0x00000000u}, 0x0.0p+0},
    AngularCase{0x4e9560u, {0x00000000u, 0x41a00000u, 0x3f800000u, 0x00000000u}, 0x1.0000000000000p+0},
    AngularCase{0x4e9560u, {0x00000000u, 0x41a00000u, 0x43480000u, 0x00000000u}, 0x1.4000000000000p+4},
    AngularCase{0x4e9560u, {0x00000000u, 0x43340000u, 0x3f800000u, 0x00000000u}, 0x1.6700000000000p+8},
    AngularCase{0x4e9560u, {0x00000000u, 0x43340000u, 0x43480000u, 0x00000000u}, 0x1.6800000000000p+7},
    AngularCase{0x4e9560u, {0x00000000u, 0x43480000u, 0x3f800000u, 0x00000000u}, 0x1.6700000000000p+8},
    AngularCase{0x4e9560u, {0x00000000u, 0x43480000u, 0x43480000u, 0x00000000u}, 0x1.9000000000000p+7},
    AngularCase{0x4e9560u, {0x00000000u, 0x43b38000u, 0x3f800000u, 0x00000000u}, 0x1.6700000000000p+8},
    AngularCase{0x4e9560u, {0x00000000u, 0x43b38000u, 0x43480000u, 0x00000000u}, 0x1.6700000000000p+8},
    AngularCase{0x4e9560u, {0x3f800000u, 0x00000000u, 0x3f800000u, 0x00000000u}, 0x0.0p+0},
    AngularCase{0x4e9560u, {0x3f800000u, 0x00000000u, 0x43480000u, 0x00000000u}, 0x0.0p+0},
    AngularCase{0x4e9560u, {0x3f800000u, 0x41a00000u, 0x3f800000u, 0x00000000u}, 0x1.0000000000000p+1},
    AngularCase{0x4e9560u, {0x3f800000u, 0x41a00000u, 0x43480000u, 0x00000000u}, 0x1.4000000000000p+4},
    AngularCase{0x4e9560u, {0x3f800000u, 0x43340000u, 0x3f800000u, 0x00000000u}, 0x1.0000000000000p+1},
    AngularCase{0x4e9560u, {0x3f800000u, 0x43340000u, 0x43480000u, 0x00000000u}, 0x1.6800000000000p+7},
    AngularCase{0x4e9560u, {0x3f800000u, 0x43480000u, 0x3f800000u, 0x00000000u}, 0x0.0p+0},
    AngularCase{0x4e9560u, {0x3f800000u, 0x43480000u, 0x43480000u, 0x00000000u}, 0x1.9000000000000p+7},
    AngularCase{0x4e9560u, {0x3f800000u, 0x43b38000u, 0x3f800000u, 0x00000000u}, 0x0.0p+0},
    AngularCase{0x4e9560u, {0x3f800000u, 0x43b38000u, 0x43480000u, 0x00000000u}, 0x1.6700000000000p+8},
    AngularCase{0x4e9560u, {0x41200000u, 0x00000000u, 0x3f800000u, 0x00000000u}, 0x1.2000000000000p+3},
    AngularCase{0x4e9560u, {0x41200000u, 0x00000000u, 0x43480000u, 0x00000000u}, 0x0.0p+0},
    AngularCase{0x4e9560u, {0x41200000u, 0x41a00000u, 0x3f800000u, 0x00000000u}, 0x1.6000000000000p+3},
    AngularCase{0x4e9560u, {0x41200000u, 0x41a00000u, 0x43480000u, 0x00000000u}, 0x1.4000000000000p+4},
    AngularCase{0x4e9560u, {0x41200000u, 0x43340000u, 0x3f800000u, 0x00000000u}, 0x1.6000000000000p+3},
    AngularCase{0x4e9560u, {0x41200000u, 0x43340000u, 0x43480000u, 0x00000000u}, 0x1.6800000000000p+7},
    AngularCase{0x4e9560u, {0x41200000u, 0x43480000u, 0x3f800000u, 0x00000000u}, 0x1.2000000000000p+3},
    AngularCase{0x4e9560u, {0x41200000u, 0x43480000u, 0x43480000u, 0x00000000u}, 0x1.9000000000000p+7},
    AngularCase{0x4e9560u, {0x41200000u, 0x43b38000u, 0x3f800000u, 0x00000000u}, 0x1.2000000000000p+3},
    AngularCase{0x4e9560u, {0x41200000u, 0x43b38000u, 0x43480000u, 0x00000000u}, 0x1.6700000000000p+8},
    AngularCase{0x4e9560u, {0x43330000u, 0x00000000u, 0x3f800000u, 0x00000000u}, 0x1.6400000000000p+7},
    AngularCase{0x4e9560u, {0x43330000u, 0x00000000u, 0x43480000u, 0x00000000u}, 0x0.0p+0},
    AngularCase{0x4e9560u, {0x43330000u, 0x41a00000u, 0x3f800000u, 0x00000000u}, 0x1.6400000000000p+7},
    AngularCase{0x4e9560u, {0x43330000u, 0x41a00000u, 0x43480000u, 0x00000000u}, 0x1.4000000000000p+4},
    AngularCase{0x4e9560u, {0x43330000u, 0x43340000u, 0x3f800000u, 0x00000000u}, 0x1.6800000000000p+7},
    AngularCase{0x4e9560u, {0x43330000u, 0x43340000u, 0x43480000u, 0x00000000u}, 0x1.6800000000000p+7},
    AngularCase{0x4e9560u, {0x43330000u, 0x43480000u, 0x3f800000u, 0x00000000u}, 0x1.6800000000000p+7},
    AngularCase{0x4e9560u, {0x43330000u, 0x43480000u, 0x43480000u, 0x00000000u}, 0x1.9000000000000p+7},
    AngularCase{0x4e9560u, {0x43330000u, 0x43b38000u, 0x3f800000u, 0x00000000u}, 0x1.6400000000000p+7},
    AngularCase{0x4e9560u, {0x43330000u, 0x43b38000u, 0x43480000u, 0x00000000u}, 0x1.6700000000000p+8},
    AngularCase{0x4e9560u, {0x43340000u, 0x00000000u, 0x3f800000u, 0x00000000u}, 0x1.6600000000000p+7},
    AngularCase{0x4e9560u, {0x43340000u, 0x00000000u, 0x43480000u, 0x00000000u}, 0x0.0p+0},
    AngularCase{0x4e9560u, {0x43340000u, 0x41a00000u, 0x3f800000u, 0x00000000u}, 0x1.6600000000000p+7},
    AngularCase{0x4e9560u, {0x43340000u, 0x41a00000u, 0x43480000u, 0x00000000u}, 0x1.4000000000000p+4},
    AngularCase{0x4e9560u, {0x43340000u, 0x43340000u, 0x3f800000u, 0x00000000u}, 0x1.6800000000000p+7},
    AngularCase{0x4e9560u, {0x43340000u, 0x43340000u, 0x43480000u, 0x00000000u}, 0x1.6800000000000p+7},
    AngularCase{0x4e9560u, {0x43340000u, 0x43480000u, 0x3f800000u, 0x00000000u}, 0x1.6a00000000000p+7},
    AngularCase{0x4e9560u, {0x43340000u, 0x43480000u, 0x43480000u, 0x00000000u}, 0x1.9000000000000p+7},
    AngularCase{0x4e9560u, {0x43340000u, 0x43b38000u, 0x3f800000u, 0x00000000u}, 0x1.6a00000000000p+7},
    AngularCase{0x4e9560u, {0x43340000u, 0x43b38000u, 0x43480000u, 0x00000000u}, 0x1.6700000000000p+8},
    AngularCase{0x4e9560u, {0x43350000u, 0x00000000u, 0x3f800000u, 0x00000000u}, 0x1.6c00000000000p+7},
    AngularCase{0x4e9560u, {0x43350000u, 0x00000000u, 0x43480000u, 0x00000000u}, 0x0.0p+0},
    AngularCase{0x4e9560u, {0x43350000u, 0x41a00000u, 0x3f800000u, 0x00000000u}, 0x1.6800000000000p+7},
    AngularCase{0x4e9560u, {0x43350000u, 0x41a00000u, 0x43480000u, 0x00000000u}, 0x1.4000000000000p+4},
    AngularCase{0x4e9560u, {0x43350000u, 0x43340000u, 0x3f800000u, 0x00000000u}, 0x1.6800000000000p+7},
    AngularCase{0x4e9560u, {0x43350000u, 0x43340000u, 0x43480000u, 0x00000000u}, 0x1.6800000000000p+7},
    AngularCase{0x4e9560u, {0x43350000u, 0x43480000u, 0x3f800000u, 0x00000000u}, 0x1.6c00000000000p+7},
    AngularCase{0x4e9560u, {0x43350000u, 0x43480000u, 0x43480000u, 0x00000000u}, 0x1.9000000000000p+7},
    AngularCase{0x4e9560u, {0x43350000u, 0x43b38000u, 0x3f800000u, 0x00000000u}, 0x1.6c00000000000p+7},
    AngularCase{0x4e9560u, {0x43350000u, 0x43b38000u, 0x43480000u, 0x00000000u}, 0x1.6700000000000p+8},
    AngularCase{0x4e9560u, {0x43af0000u, 0x00000000u, 0x3f800000u, 0x00000000u}, 0x1.5f00000000000p+8},
    AngularCase{0x4e9560u, {0x43af0000u, 0x00000000u, 0x43480000u, 0x00000000u}, 0x0.0p+0},
    AngularCase{0x4e9560u, {0x43af0000u, 0x41a00000u, 0x3f800000u, 0x00000000u}, 0x1.5f00000000000p+8},
    AngularCase{0x4e9560u, {0x43af0000u, 0x41a00000u, 0x43480000u, 0x00000000u}, 0x1.4000000000000p+4},
    AngularCase{0x4e9560u, {0x43af0000u, 0x43340000u, 0x3f800000u, 0x00000000u}, 0x1.5d00000000000p+8},
    AngularCase{0x4e9560u, {0x43af0000u, 0x43340000u, 0x43480000u, 0x00000000u}, 0x1.6800000000000p+7},
    AngularCase{0x4e9560u, {0x43af0000u, 0x43480000u, 0x3f800000u, 0x00000000u}, 0x1.5d00000000000p+8},
    AngularCase{0x4e9560u, {0x43af0000u, 0x43480000u, 0x43480000u, 0x00000000u}, 0x1.9000000000000p+7},
    AngularCase{0x4e9560u, {0x43af0000u, 0x43b38000u, 0x3f800000u, 0x00000000u}, 0x1.5f00000000000p+8},
    AngularCase{0x4e9560u, {0x43af0000u, 0x43b38000u, 0x43480000u, 0x00000000u}, 0x1.6700000000000p+8},
    AngularCase{0x4e9560u, {0x43b38000u, 0x00000000u, 0x3f800000u, 0x00000000u}, 0x0.0p+0},
    AngularCase{0x4e9560u, {0x43b38000u, 0x00000000u, 0x43480000u, 0x00000000u}, 0x0.0p+0},
    AngularCase{0x4e9560u, {0x43b38000u, 0x41a00000u, 0x3f800000u, 0x00000000u}, 0x0.0p+0},
    AngularCase{0x4e9560u, {0x43b38000u, 0x41a00000u, 0x43480000u, 0x00000000u}, 0x1.4000000000000p+4},
    AngularCase{0x4e9560u, {0x43b38000u, 0x43340000u, 0x3f800000u, 0x00000000u}, 0x1.6600000000000p+8},
    AngularCase{0x4e9560u, {0x43b38000u, 0x43340000u, 0x43480000u, 0x00000000u}, 0x1.6800000000000p+7},
    AngularCase{0x4e9560u, {0x43b38000u, 0x43480000u, 0x3f800000u, 0x00000000u}, 0x1.6600000000000p+8},
    AngularCase{0x4e9560u, {0x43b38000u, 0x43480000u, 0x43480000u, 0x00000000u}, 0x1.9000000000000p+7},
    AngularCase{0x4e9560u, {0x43b38000u, 0x43b38000u, 0x3f800000u, 0x00000000u}, 0x1.6700000000000p+8},
    AngularCase{0x4e9560u, {0x43b38000u, 0x43b38000u, 0x43480000u, 0x00000000u}, 0x1.6700000000000p+8},
    AngularCase{0x4e9560u, {0x7fc00001u, 0x41a00000u, 0x41200000u, 0x00000000u}, 0x1.4000000000000p+4},
    AngularCase{0x4e9560u, {0x41200000u, 0x7fc00001u, 0x41200000u, 0x00000000u}, 0x0.0p+0},
    AngularCase{0x4e9560u, {0x43340000u, 0x7fc00001u, 0x41200000u, 0x00000000u}, 0x1.5400000000000p+7},
};
TEST(OriginalAnglesTest, NativePc53NearestCorpus)
{
    using namespace Client::Render::Models;
    ASSERT_EQ(std::fegetround(), FE_TONEAREST);
    for (const auto& sample : Cases)
    {
        const auto& a = sample.arguments;
        SCOPED_TRACE(::testing::Message() << "case " << (&sample - Cases.data()) << " function " << sample.function
                                          << " args " << a[0] << ',' << a[1] << ',' << a[2] << ',' << a[3]);
        double result = 0.0;
        if (sample.function == 0x4e93f0u)
            result = CreateAngle(std::bit_cast<float>(a[0]), std::bit_cast<float>(a[1]), std::bit_cast<float>(a[2]),
                                 std::bit_cast<float>(a[3]));
        else if (sample.function == 0x4e9560u)
            result = TurnAngle(std::bit_cast<float>(a[0]), std::bit_cast<float>(a[1]), std::bit_cast<float>(a[2]));
        else if (sample.function == 0x4e96f0u)
            result =
                AngleDistance(std::bit_cast<float>(a[0]), std::bit_cast<float>(a[1]), static_cast<std::uint8_t>(a[2]));
        else
            FAIL() << "Id de función angular no reconocido";
        EXPECT_EQ(std::bit_cast<std::uint64_t>(result), std::bit_cast<std::uint64_t>(sample.expected));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(static_cast<float>(result)),
                  std::bit_cast<std::uint32_t>(static_cast<float>(sample.expected)));
    }
}
TEST(OriginalAnglesTest, OnlyModeOneChangesTheSign)
{
    using Client::Render::Models::AngleDistance;
    EXPECT_EQ(AngleDistance(270.0f, 90.0f, static_cast<std::uint8_t>(2)), -180.0);
    EXPECT_EQ(AngleDistance(270.0f, 90.0f, static_cast<std::uint8_t>(255)), -180.0);
    EXPECT_EQ(AngleDistance(270.0f, 90.0f, static_cast<std::uint8_t>(1)), 180.0);
}
// Cambiar Q a float, o eliminar el spill de P, rompe respectivamente estas dos expectativas.
TEST(OriginalAnglesTest, TurnKeepsQButSpillsP)
{
    using Client::Render::Models::TurnAngle;
    EXPECT_EQ(TurnAngle(10.0f, 20.0f, 0x1p-25f), 0x1.4000001p+3);
    EXPECT_EQ(TurnAngle(10.0f, 5.0f, 0x1p-25f), 10.0);
    EXPECT_EQ(TurnAngle(180.0f, 170.0f, 0x1p-20f), 180.0 - 0x1p-20);
    EXPECT_EQ(TurnAngle(180.0f, 190.0f, 0x1p-20f), 180.0);
}
TEST(OriginalAnglesTest, DistanceRemainsBelowFortyFiveBeforeTheCallerSpill)
{
    const double result = Client::Render::Models::AngleDistance(0x1p-20f, 45.0f, 1);
    EXPECT_EQ(result, 45.0 - 0x1p-20);
    EXPECT_LT(result, 45.0);
    EXPECT_EQ(static_cast<float>(result), 45.0f);
}
TEST(OriginalAnglesTest, AxialEpsilonIsStrictAndCoincidentPointsFaceHalfTurn)
{
    using Client::Render::Models::CreateAngle;
    constexpr float Epsilon = std::bit_cast<float>(0x38d1b717u);
    constexpr float BelowEpsilon = std::bit_cast<float>(0x38d1b716u);
    EXPECT_EQ(CreateAngle(0, 0, 0, 0), 180.0);
    EXPECT_EQ(CreateAngle(0, 0, 0, -1), 0.0);
    EXPECT_EQ(CreateAngle(0, 0, -1, 0), 270.0);
    EXPECT_EQ(CreateAngle(0, 0, Epsilon, 0), 90.0);
    EXPECT_EQ(CreateAngle(0, 0, BelowEpsilon, 0), 180.0);
    EXPECT_EQ(CreateAngle(0, 0, -Epsilon, 0), 270.0);
    EXPECT_EQ(CreateAngle(0, 0, -BelowEpsilon, 0), 180.0);
}
TEST(OriginalAnglesTest, OnlyOneNegativeAdjustmentAndAsymmetricAntipodes)
{
    using Client::Render::Models::AngleDistance;
    using Client::Render::Models::TurnAngle;
    EXPECT_EQ(AngleDistance(0, 720, 1), 360.0);
    EXPECT_EQ(AngleDistance(90, 270, 0), 180.0);
    EXPECT_EQ(AngleDistance(270, 90, 0), -180.0);
    EXPECT_EQ(TurnAngle(-720, 0, 0), 0.0);
    EXPECT_EQ(TurnAngle(360, 0, 0), 0.0);
    EXPECT_EQ(TurnAngle(10, 20, -1), 9.0);
    EXPECT_EQ(TurnAngle(10, 5, -1), 11.0);
    EXPECT_EQ(TurnAngle(10, 20, -1000), -990.0);
}
// TEST AH,1 incluye unordered. Usar < como sustituto cambia estos tres resultados de gameplay.
TEST(OriginalAnglesTest, NaNCurrentReturnsTheTarget)
{
    const float nan = std::bit_cast<float>(0x7fc00001u);
    EXPECT_EQ(Client::Render::Models::TurnAngle(nan, 20.0f, 10.0f), 20.0);
}
TEST(OriginalAnglesTest, NaNTargetInTheLowArmReturnsSpilledP)
{
    const float nan = std::bit_cast<float>(0x7fc00001u);
    EXPECT_EQ(Client::Render::Models::TurnAngle(10.0f, nan, 10.0f), 0.0);
}
TEST(OriginalAnglesTest, NaNTargetInTheHighArmReturnsRetainedQ)
{
    const float nan = std::bit_cast<float>(0x7fc00001u);
    EXPECT_EQ(Client::Render::Models::TurnAngle(180.0f, nan, 10.0f), 170.0);
}
TEST(OriginalAnglesTest, CreateAngleTreatsUnorderedAsC0InBothAxialGuards)
{
    using Client::Render::Models::CreateAngle;
    const float nan = std::bit_cast<float>(0x7fc00001u);
    EXPECT_EQ(CreateAngle(0, 0, nan, 1), 180.0);
    EXPECT_EQ(CreateAngle(0, 0, nan, -1), 0.0);
    EXPECT_EQ(CreateAngle(0, 0, nan, nan), 0.0);
    EXPECT_EQ(CreateAngle(0, 0, 1, nan), 90.0);
    EXPECT_EQ(CreateAngle(0, 0, -1, nan), 270.0);
}
TEST(OriginalAnglesTest, NaNStepFollowsDifferentBranchesOnEachSideOfHalfTurn)
{
    using Client::Render::Models::TurnAngle;
    const float nan = std::bit_cast<float>(0x7fc00001u);
    EXPECT_EQ(TurnAngle(10, 20, nan), 20.0);
    EXPECT_TRUE(std::isnan(TurnAngle(180, 20, nan)));
    EXPECT_TRUE(std::isnan(TurnAngle(0, nan, 1)));
}
TEST(OriginalAnglesTest, DistanceModeOneFlipsTheSignOfUnordered)
{
    using Client::Render::Models::AngleDistance;
    const float nan = std::bit_cast<float>(0x7fc00001u);
    const double signedResult = AngleDistance(10, nan, 0);
    const double modeOneResult = AngleDistance(10, nan, 1);
    EXPECT_TRUE(std::isnan(signedResult));
    EXPECT_TRUE(std::isnan(modeOneResult));
    EXPECT_NE(std::signbit(signedResult), std::signbit(modeOneResult));
}
TEST(OriginalAnglesTest, NegativeInputsReceiveOnlyOneAdjustment)
{
    using Client::Render::Models::AngleDistance;
    using Client::Render::Models::TurnAngle;
    EXPECT_EQ(TurnAngle(-720.0f, 20.0f, 1.0f), 20.0);
    EXPECT_EQ(AngleDistance(-720.0f, 20.0f, 0), -20.0);
}
TEST(OriginalAnglesTest, NegativeAdjustmentSpillsToExactlyOneTurn)
{
    const double result = Client::Render::Models::AngleDistance(-0x1p-30f, 0.0f, 0);
    EXPECT_EQ(std::bit_cast<std::uint64_t>(result), 0u);
}
TEST(OriginalAnglesTest, HostRoundingMatchesTheDeclaredNearestProfile)
{
    EXPECT_EQ(std::fegetround(), FE_TONEAREST);
}
} // namespace

// R5: sólo valores numéricos archivados, sin registros ni volcados (ADR0014).
TEST(OriginalAnglesTest, RetainsPhiThroughNativeNonunitProducts)
{
    struct Case
    {
        std::array<std::uint32_t, 4> arguments;
        std::uint64_t expectedDouble;
        std::uint32_t expectedFloat;
    };
    constexpr std::array cases{
        Case{{0x00000000U, 0x00000000U, 0x3f800000U, 0x40000000U}, 0x40632deb127d540fULL, 0x43196f59U},
        Case{{0x00000000U, 0x00000000U, 0xbf800000U, 0x40000000U}, 0x4069d214ed82abf1ULL, 0x434e90a7U},
        Case{{0x00000000U, 0x00000000U, 0x3f800000U, 0xc0000000U}, 0x403a90a76c155f8aULL, 0x41d4853bU},
        Case{{0x00000000U, 0x00000000U, 0xbf800000U, 0xc0000000U}, 0x4074d6f5893eaa08ULL, 0x43a6b7acU},
        Case{{0x00000000U, 0x00000000U, 0x40000000U, 0x3f800000U}, 0x405d2429c64b6235ULL, 0x42e9214eU},
        Case{{0x00000000U, 0x00000000U, 0xc0000000U, 0x3f800000U}, 0x406e6deb1cda4ee6ULL, 0x43736f59U},
        Case{{0x00000000U, 0x00000000U, 0x40000000U, 0xbf800000U}, 0x404fb7ac73693b96ULL, 0x427dbd64U},
        Case{{0x00000000U, 0x00000000U, 0xc0000000U, 0xbf800000U}, 0x4072890a7192d88dULL, 0x43944854U},
        Case{{0x00000000U, 0x00000000U, 0x3f800000U, 0x40400000U}, 0x40643214ddf733afULL, 0x432190a7U},
        Case{{0x00000000U, 0x00000000U, 0xbf800000U, 0x40400000U}, 0x4068cdeb2208cc51ULL, 0x43466f59U},
        Case{{0x00000000U, 0x00000000U, 0x3f800000U, 0xc0400000U}, 0x40326f5910466288ULL, 0x41937ac9U},
        Case{{0x00000000U, 0x00000000U, 0xbf800000U, 0xc0400000U}, 0x4075590a6efb99d8ULL, 0x43aac853U},
        Case{{0x00000000U, 0x00000000U, 0x40400000U, 0x3f800000U}, 0x405b1bd62f57a2f4ULL, 0x42d8deb1U},
        Case{{0x00000000U, 0x00000000U, 0xc0400000U, 0x3f800000U}, 0x406f7214e8542e86ULL, 0x437b90a7U},
        Case{{0x00000000U, 0x00000000U, 0x40400000U, 0xbf800000U}, 0x4051e429d0a85d0cULL, 0x428f214fU},
        Case{{0x00000000U, 0x00000000U, 0xc0400000U, 0xbf800000U}, 0x407206f58bd5e8bdULL, 0x439037acU},
        Case{{0x00000000U, 0x00000000U, 0x40000000U, 0x40400000U}, 0x406249eaf1061b49ULL, 0x43124f58U},
        Case{{0x00000000U, 0x00000000U, 0xc0000000U, 0x40400000U}, 0x406ab6150ef9e4b7ULL, 0x4355b0a8U},
        Case{{0x00000000U, 0x00000000U, 0x40000000U, 0xc0400000U}, 0x4040d8543be792dbULL, 0x4206c2a2U},
        Case{{0x00000000U, 0x00000000U, 0xc0000000U, 0xc0400000U}, 0x407464f578830da4ULL, 0x43a327acU},
        Case{{0x00000000U, 0x00000000U, 0x40e00000U, 0x41300000U}, 0x406270ebf78546baULL, 0x43138760U},
        Case{{0x00000000U, 0x00000000U, 0xc0e00000U, 0x41300000U}, 0x406a8f14087ab946ULL, 0x435478a0U},
        Case{{0x00000000U, 0x00000000U, 0x40e00000U, 0xc1300000U}, 0x40403c5021eae517ULL, 0x4201e281U},
        Case{{0x00000000U, 0x00000000U, 0xc0e00000U, 0xc1300000U}, 0x40747875fbc2a35dULL, 0x43a3c3b0U},
        Case{{0x41200000U, 0xc1a00000U, 0x41300000U, 0xc1900000U}, 0x40632deb127d540fULL, 0x43196f59U},
        Case{{0xc1000000U, 0x41800000U, 0xc0c00000U, 0x41880000U}, 0x405d2429c64b6235ULL, 0x42e9214eU},
        Case{{0x4b800000U, 0x4b000000U, 0x4b800001U, 0x4b000001U}, 0x405d2429c64b6235ULL, 0x42e9214eU},
        Case{{0x3e800000U, 0xbf000000U, 0x40e80000U, 0x41280000U}, 0x406270ebf78546baULL, 0x43138760U},
        Case{{0x00000000U, 0x00000000U, 0x38d1b716U, 0x3f800000U}, 0x4066800000000000ULL, 0x43340000U},
        Case{{0x00000000U, 0x00000000U, 0x38d1b717U, 0x3f800000U}, 0x40667fd105d78974ULL, 0x4333fe88U},
        Case{{0x00000000U, 0x00000000U, 0x38d1b718U, 0x3f800000U}, 0x40667fd105d75028ULL, 0x4333fe88U},
        Case{{0x00000000U, 0x00000000U, 0x3f800000U, 0x38d1b716U}, 0x4056800000000000ULL, 0x42b40000U},
        Case{{0x00000000U, 0x00000000U, 0x3f800000U, 0x38d1b717U}, 0x4056805ddf96f76bULL, 0x42b402efU},
        Case{{0x00000000U, 0x00000000U, 0x3f800000U, 0x38d1b718U}, 0x4056805ddf976a03ULL, 0x42b402efU},
        Case{{0x00000000U, 0x00000000U, 0xb8d1b716U, 0x3f800000U}, 0x4066800000000000ULL, 0x43340000U},
        Case{{0x00000000U, 0x00000000U, 0xb8d1b717U, 0x3f800000U}, 0x4066802efa28768cULL, 0x43340178U},
        Case{{0x00000000U, 0x00000000U, 0x3f800000U, 0x3f800000U}, 0x4060dffffad18294ULL, 0x43070000U},
        Case{{0x00000000U, 0x00000000U, 0xbf800000U, 0x3f800000U}, 0x406c2000052e7d6cULL, 0x43610000U},
    };
    static_assert(cases.size() == 38);
    EXPECT_EQ(std::fegetround(), FE_TONEAREST);
    for (const auto& row : cases)
    {
        const auto& p = row.arguments;
        const double actual =
            Client::Render::Models::CreateAngle(std::bit_cast<float>(p[0]), std::bit_cast<float>(p[1]),
                                                std::bit_cast<float>(p[2]), std::bit_cast<float>(p[3]));
        EXPECT_EQ(std::bit_cast<std::uint64_t>(actual), row.expectedDouble);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(static_cast<float>(actual)), row.expectedFloat);
    }
}

// ADR0014: phi64 es el atan64 bien redondeado, no el FPATAN de la x87. Desde (0,0) hasta estos destinos
// enteros, la x87 medida (un Xeon) deja 1 ULP de double de diferencia; el store f32 es el mismo. Los
// valores del modelo se verificaron con Boost 1.83 cpp_bin_float<113> y con mpmath a 400 bits.
TEST(OriginalAnglesTest, KeepsTheRoundedAtanModelWhereFpatanDiffers)
{
    struct Case
    {
        float x2;
        float y2;
        std::uint64_t expectedDouble;
        std::uint32_t expectedFloat;
    };
    constexpr std::array cases{
        Case{7703.0f, -8981.0f, 0x40444f51b2dfecbdULL, 0x42227a8eU},  // x87: 0x40444f51b2dfecbc
        Case{8793.0f, 6798.0f, 0x405fed523db8e943ULL, 0x42ff6a92U},   // x87: 0x405fed523db8e944
        Case{-643.0f, -24380.0f, 0x407667d3da839f3eULL, 0x43b33e9fU}, // x87: 0x407667d3da839f3d
    };
    for (const auto& row : cases)
    {
        const double actual = Client::Render::Models::CreateAngle(0.0f, 0.0f, row.x2, row.y2);
        EXPECT_EQ(std::bit_cast<std::uint64_t>(actual), row.expectedDouble);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(static_cast<float>(actual)), row.expectedFloat);
    }
}
