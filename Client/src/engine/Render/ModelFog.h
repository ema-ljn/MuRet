#pragma once

#include <glm/ext/vector_float3.hpp>

// Fixed-function fog (glFog with GL_LINEAR). The original's only live fog is Castle Siege's, set by 0x42b0a0 (06 § Los
// objetos de Castle Siege). GL applies it after the texture: f = (end − d) / (end − start) clamped to [0, 1], color
// f · C + (1 − f) · fog, alpha unchanged. With GL_FOG_COORDINATE_SOURCE at GL_FRAGMENT_DEPTH, d is the eye distance,
// which drivers approximate with eye |z|: MuRet uses that (Q-C391).
namespace Client::Render
{
struct ModelFog
{
    // GL_FOG_COLOR. Of the four floats glFogfv reads, fog uses the first three.
    glm::vec3 color{};
    // GL_FOG_START and GL_FOG_END, in world units.
    float start = 0.0f;
    float end = 1.0f;

    friend bool operator==(const ModelFog&, const ModelFog&) = default;
};
} // namespace Client::Render
