#pragma once

#include <string_view>

// The 2D GLSL 330 core (D-C3), replacing the original's fixed function and matrix stack (06 § Primitivas 2D): the
// gluOrtho2D matrix, texture × vertex color as glTexEnv's GL_MODULATE, and an alpha test that discards alpha ≤ 0.25
// like glAlphaFunc(GL_GREATER, 0.25) (06 § Cámara). Blending is glBlendFunc's, in GlSpriteRenderer.
namespace Client::Render::Gl
{
// SpriteVertex's attribute locations in GlRenderer's VAO.
inline constexpr unsigned SpritePositionLocation = 0;
inline constexpr unsigned SpriteUvLocation = 1;
inline constexpr unsigned SpriteColorLocation = 2;

// Column-major, as glm::value_ptr gives it.
inline constexpr const char* SpriteProjectionUniform = "projection";
// Fragment shader flags, which GL stores as int (glUniform1i, 0 or 1).
inline constexpr const char* SpriteTexturedUniform = "textured";
inline constexpr const char* SpriteAlphaTestUniform = "alphaTest";

inline constexpr std::string_view SpriteVertexShader = R"(#version 330 core
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 color;
uniform mat4 projection;
out vec2 fragmentUv;
out vec4 fragmentColor;
void main()
{
    fragmentUv = uv;
    fragmentColor = color;
    gl_Position = projection * vec4(position, 0.0, 1.0);
}
)";

inline constexpr std::string_view SpriteFragmentShader = R"(#version 330 core
in vec2 fragmentUv;
in vec4 fragmentColor;
uniform sampler2D image;
uniform bool textured;
uniform bool alphaTest;
out vec4 outputColor;
// El valor de glAlphaFunc(GL_GREATER, 0.25): pasa lo que tiene más.
const float alphaReference = 0.25;
void main()
{
    vec4 color = textured ? texture(image, fragmentUv) * fragmentColor : fragmentColor;
    if (alphaTest && color.a <= alphaReference)
    {
        discard;
    }
    outputColor = color;
}
)";

// The 3D counterpart for a ModelVertex. The color is clamped to [0, 1] before the texture, as the fixed function clamps
// the vertex glColor (the original's light exceeds 1: 1.34 in the selection, up to 3.45 with per-vertex light). With
// `textureAdd`, GL_ADD (6ac/2, 0x5489eb): texel + color clamped to [0, 1], alpha texel · color. Then GL_LINEAR fog
// (Render/ModelFog.h, 0x42b0a0) on eye |z|; the alpha test does not depend on fog, which leaves alpha alone.
inline constexpr unsigned ModelPositionLocation = 0;
inline constexpr unsigned ModelUvLocation = 1;
inline constexpr unsigned ModelColorLocation = 2;

inline constexpr const char* ModelViewProjectionUniform = "viewProjection";
inline constexpr const char* ModelEyeDepthAxisUniform = "eyeDepthAxis";
inline constexpr const char* ModelEyeDepthOffsetUniform = "eyeDepthOffset";
inline constexpr const char* ModelTexturedUniform = "textured";
inline constexpr const char* ModelAlphaTestUniform = "alphaTest";
// Texture env and fog: the GL_ADD and GL_FOG flags as int (glUniform1i, 0 or 1), the color (glUniform3f), and start
// and end (glUniform1f).
inline constexpr const char* ModelTextureAddUniform = "textureAdd";
inline constexpr const char* ModelFogUniform = "fog";
inline constexpr const char* ModelFogColorUniform = "fogColor";
inline constexpr const char* ModelFogStartUniform = "fogStart";
inline constexpr const char* ModelFogEndUniform = "fogEnd";

inline constexpr std::string_view ModelVertexShader = R"(#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 color;
uniform mat4 viewProjection;
uniform vec3 eyeDepthAxis;
uniform float eyeDepthOffset;
out vec2 fragmentUv;
out vec4 fragmentColor;
out float fragmentDistance;
void main()
{
    fragmentUv = uv;
    fragmentColor = clamp(color, 0.0, 1.0);
    gl_Position = viewProjection * vec4(position, 1.0);
    fragmentDistance = abs(dot(eyeDepthAxis, position) + eyeDepthOffset);
}
)";

inline constexpr std::string_view ModelFragmentShader = R"(#version 330 core
in vec2 fragmentUv;
in vec4 fragmentColor;
in float fragmentDistance;
uniform sampler2D image;
uniform bool textured;
uniform bool alphaTest;
uniform bool textureAdd;
uniform bool fog;
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;
out vec4 outputColor;
// El valor de glAlphaFunc(GL_GREATER, 0.25): pasa lo que tiene más.
const float alphaReference = 0.25;
void main()
{
    vec4 color = fragmentColor;
    if (textured)
    {
        vec4 texel = texture(image, fragmentUv);
        color = textureAdd ? vec4(min(texel.rgb + fragmentColor.rgb, 1.0), texel.a * fragmentColor.a)
                           : texel * fragmentColor;
    }
    if (alphaTest && color.a <= alphaReference)
    {
        discard;
    }
    if (fog)
    {
        float factor = clamp((fogEnd - fragmentDistance) / (fogEnd - fogStart), 0.0, 1.0);
        color.rgb = mix(fogColor, color.rgb, factor);
    }
    outputColor = color;
}
)";
} // namespace Client::Render::Gl
