#include "Fan.h"

namespace Labs
{
std::vector<Triangle> FanToTriangles(std::uint32_t vertexCount)
{
    std::vector<Triangle> triangles;
    if (vertexCount < 3)
    {
        return triangles;
    }
    triangles.reserve(vertexCount - 2);
    for (std::uint32_t second = 1; second + 1 < vertexCount; ++second)
    {
        triangles.push_back(Triangle{0, second, second + 1});
    }
    return triangles;
}
} // namespace Labs
