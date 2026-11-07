#include "Render/Models/OriginalAngles.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <cmath>

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
    for (const auto& sample : Cases)
    {
        SCOPED_TRACE(&sample - Cases.data());
        const auto& a = sample.arguments;
        double result = 0.0;
        if (sample.function == 0x4e93f0u)
            result = CreateAngle(std::bit_cast<float>(a[0]), std::bit_cast<float>(a[1]), std::bit_cast<float>(a[2]),
                                 std::bit_cast<float>(a[3]));
        else if (sample.function == 0x4e9560u)
            result = TurnAngle(std::bit_cast<float>(a[0]), std::bit_cast<float>(a[1]), std::bit_cast<float>(a[2]));
        else
            result =
                AngleDistance(std::bit_cast<float>(a[0]), std::bit_cast<float>(a[1]), static_cast<std::uint8_t>(a[2]));
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
} // namespace
