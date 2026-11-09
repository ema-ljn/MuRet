#pragma once

#include <cstdint>

namespace Client::World
{
struct WorldObjectStepModel;

// Metadata canónica por tipo válido; el proveedor y sus entradas viven durante todo el paso.
class IWorldObjectStepModels
{
public:
    virtual ~IWorldObjectStepModels() = default;
    [[nodiscard]] virtual WorldObjectStepModel& Step(std::int16_t type) = 0;
};
} // namespace Client::World
