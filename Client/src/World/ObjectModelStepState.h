#pragma once

#include <cstdint>
#include <map>

namespace Client::World
{
// model+88: selección compartida por tipo, no un campo por instancia. Ausente equivale al -1
// inicial del modelo cargado. El owner conserva o limpia la tabla con el ciclo de sus modelos.
struct ObjectModelStepState
{
    std::map<std::int16_t, std::int8_t> unlitMeshes;
};
} // namespace Client::World
