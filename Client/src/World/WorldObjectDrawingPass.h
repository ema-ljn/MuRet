#pragma once
#include <cstdint>
namespace Client::World
{
// Slots completos4ef0c0/4ecc30, preparados desde O de nuevo por llamada.
struct WorldObjectDrawingPass
{
    std::uint32_t mask;
    float alpha;
    std::int32_t blendMesh;
    float blendLight;
    float u;
    float v;
    std::int32_t hiddenMesh;
    std::int32_t texture = -1;
};
} // namespace Client::World
