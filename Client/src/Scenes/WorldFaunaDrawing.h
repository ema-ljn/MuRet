#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <system_error>

namespace Client::Render
{
class ModelBatch;
}
namespace Client::World
{
class Leaves;
}
namespace Client::Scenes
{
class ILeafDrawingServices;

// Los diez argumentos de621400, sin estrechar selector32 ni copiar posición/luz del slot.
struct LeafBillboardRequest
{
    std::int32_t selector;
    glm::vec3& position;
    float width;
    float height;
    glm::vec3& light;
    float rotation = 0;
    glm::vec2 uv{0, 0};
    glm::vec2 uvSize{1, 1};
};

// Cuerpo propio5306a0: mezcla/color una vez y siempre200slots; binder32 y decisionesM0/M1/M2.
// En map2, Billboard representa621400 completo (incluido su segundo binder interno, metadata tardía
// y estado GL persistente), no sólo el cálculo MakeBillboard. Reusa geometría/matrices existentes,
// sin renderer/cache/pool/RNG alternativos. El binding debe
// conectar el contexto gráfico real; un error conserva el prefijo ejecutado, sin finally ficticio.
[[nodiscard]] Core::Expected<void, std::error_code> DrawWorldLeaves(Render::ModelBatch& batch, World::Leaves& leaves,
                                                                    const std::int32_t& map,
                                                                    const std::int32_t& selector,
                                                                    ILeafDrawingServices* services);
} // namespace Client::Scenes
