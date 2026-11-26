#pragma once
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <cstdint>
#include <functional>
namespace Client::Render
{
// Scalars captured by value on entering the service; P/C stay live borrows. Do not keep the request, P/C or scratch
// after returning.
struct ParticleBillboardRequest
{
    std::int32_t selector;
    const std::reference_wrapper<const glm::vec3> position;
    float width;
    float height;
    const std::reference_wrapper<const glm::vec3> light;
    float rotation;
    glm::vec2 uv;
    glm::vec2 uvSize;
};
} // namespace Client::Render
