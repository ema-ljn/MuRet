#pragma once

#include <glm/ext/vector_float3.hpp>

// La niebla de la función fija (glFog con GL_LINEAR) como dato de un comando del ModelBatch. La única niebla viva
// del original es la de Castle Siege, que arma 0x42b0a0 (06 § Los objetos de Castle Siege). GL la aplica después
// de la textura: con d la distancia del fragmento al ojo, el factor f = (fin − d) / (fin − inicio), recortado a
// [0, 1], y el color f · C + (1 − f) · niebla; el alfa no cambia. GL_FOG_COORDINATE_SOURCE queda en
// GL_FRAGMENT_DEPTH, y con él la distancia es la del ojo, que los drivers aproximan con |z| del ojo: es la que
// usa MuRet (Q-C391).
namespace Client::Render
{
struct ModelFog
{
    // GL_FOG_COLOR. De los cuatro floats que lee glFogfv, la niebla usa los tres primeros.
    glm::vec3 color{};
    // GL_FOG_START y GL_FOG_END, en unidades del mundo.
    float start = 0.0f;
    float end = 1.0f;

    friend bool operator==(const ModelFog&, const ModelFog&) = default;
};
} // namespace Client::Render
