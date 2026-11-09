#pragma once

#include <cstdint>

namespace Client::Render::Models
{
struct Skeleton;
}

namespace Client::World
{
// Proyección del byte model+a0 compartido entre objetos del mismo tipo; el esqueleto es prestado.
struct WorldObjectStepModel
{
    const Render::Models::Skeleton* skeleton = nullptr;
    std::uint8_t action = 0;
    std::uint8_t flag88 = 0; // Metadata compartida: mapa1 escribe1; mapa3/tipos42/43 escribe0.
};
} // namespace Client::World
