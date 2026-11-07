#pragma once

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>

namespace Client::GameLogic
{
struct Path;
struct MovePathState
{
    Path& path;
    glm::vec3& position;
    float& yaw;
    std::uint32_t& logicalX;
    std::uint32_t& logicalY;
};
enum class LogicalAxis
{
    X,
    Y
};
enum class LogicalStoreStage
{
    BeginningBefore,
    BeginningAfter,
    ArrivalBefore,
    ArrivalAfter
};
struct LogicalCellEffects
{
    // La frontera preserva pre/store/post de cada eje, incluso si el valor no cambia. El servicio
    // dueño de P conserva sus contadores/aliases; BeginningAfter de Y difiere de ArrivalAfter.
    std::function<void(LogicalAxis, LogicalStoreStage, std::uint32_t&)> onStore;
};
// 0x4ea270. Dominio: buffers estables, ruta válida con count<=15 y progress<=3; los callbacks pueden alterar
// valores vivos manteniendo ese dominio. Retorna llegada; no desplaza XY salvo al llegar.
[[nodiscard]] bool MovePath(MovePathState state, std::uint8_t rotate, const LogicalCellEffects& effects = {});
} // namespace Client::GameLogic
