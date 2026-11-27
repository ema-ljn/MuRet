#pragma once

#include <cstdint>

namespace Client::World
{
// Stores DWORD TargetX/Y y salida del cursor de la rama 0x59ddf3..0x59deb3 de MoveHero. DontMove
// no impide calcular el camino. Un retry fallido conserva los tres campos anteriores.
struct TerrainPickingTarget
{
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    bool dontMove = false;
};
} // namespace Client::World
