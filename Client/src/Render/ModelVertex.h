#pragma once

#include "Render/SpriteVertex.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

// Un vértice del 3D: lo que el ModelBatch junta por cada esquina de un triángulo de un modelo, y lo
// que Gl::GlModelRenderer sube al VBO tal cual, en este orden (su VAO lo describe así). El original
// manda glTexCoord2f, glColor y glVertex3f por vértice, ya en el mundo (06 § El personaje, vértice a
// vértice): no hay una matriz por modelo.
namespace Client::Render
{
struct ModelVertex
{
    glm::vec3 position{}; // en el mundo
    glm::vec2 uv{};
    // Sin recortar: la luz del original pasa de 1 (1,34 en la selección, hasta 3,45 con la luz por
    // vértice del caballo), y la función fija la recorta a [0, 1] antes de rasterizar; acá la recorta
    // el shader.
    Color color{};
};
} // namespace Client::Render
