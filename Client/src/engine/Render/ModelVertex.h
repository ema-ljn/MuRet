#pragma once

#include "Render/SpriteVertex.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

// Gl::GlModelRenderer uploads it to the VBO as is, in this order (its VAO describes it so). The original sends
// glTexCoord2f, glColor and glVertex3f per vertex, already in the world (06 § El personaje, vértice a vértice): there
// is no per-model matrix.
namespace Client::Render
{
struct ModelVertex
{
    glm::vec3 position{}; // world space
    glm::vec2 uv{};
    // Unclamped: the original's light exceeds 1 (1.34 in the selection, up to 3.45 with the horse's per-vertex light),
    // and the fixed function clamps it to [0, 1] before rasterizing; here the shader clamps it.
    Color color{};
};
} // namespace Client::Render
