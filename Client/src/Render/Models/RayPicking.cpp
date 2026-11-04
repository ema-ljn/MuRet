#include "Render/Models/RayPicking.h"

#include <glm/common.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>

namespace Client::Render::Models
{
Box PlayerBoxAt(const glm::vec3& position)
{
    return {position + PlayerBoxMin, position + PlayerBoxMax};
}

// Con el centro de la caja en el origen: el punto medio del segmento m, su mitad d y la media caja e.
// Un eje separa si la proyección de la caja y la del segmento sobre él no se tocan.
bool SegmentHitsBox(const glm::vec3& origin, const glm::vec3& end, const Box& box)
{
    const glm::vec3 halfBox = (box.max - box.min) * 0.5f;
    const glm::vec3 centre = (box.min + box.max) * 0.5f;
    const glm::vec3 half = (end - origin) * 0.5f;
    const glm::vec3 middle = origin + half - centre;
    const glm::vec3 absHalf = glm::abs(half);
    // Los 3 ejes de la caja.
    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::fabs(middle[axis]) > halfBox[axis] + absHalf[axis])
        {
            return false;
        }
    }
    // Los 3 productos de los ejes de la caja con el segmento.
    if (std::fabs(middle.y * half.z - middle.z * half.y) > halfBox.y * absHalf.z + halfBox.z * absHalf.y)
    {
        return false;
    }
    if (std::fabs(middle.z * half.x - middle.x * half.z) > halfBox.x * absHalf.z + halfBox.z * absHalf.x)
    {
        return false;
    }
    if (std::fabs(middle.x * half.y - middle.y * half.x) > halfBox.x * absHalf.y + halfBox.y * absHalf.x)
    {
        return false;
    }
    return true;
}

std::optional<std::size_t> NearestHit(const glm::vec3& origin, const glm::vec3& end,
                                      std::span<const glm::vec3> positions)
{
    std::optional<std::size_t> nearest;
    float best = std::numeric_limits<float>::max();
    for (std::size_t k = 0; k < positions.size(); ++k)
    {
        if (!SegmentHitsBox(origin, end, PlayerBoxAt(positions[k])))
        {
            continue;
        }
        const glm::vec3 offset = positions[k] - origin;
        const float distance = glm::dot(offset, offset);
        if (distance < best)
        {
            best = distance;
            nearest = k;
        }
    }
    return nearest;
}
} // namespace Client::Render::Models
