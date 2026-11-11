#pragma once

#include <string_view>

// El GLSL 330 core del 2D (D-C3; la CL5.0 lo armó en el lab, labs/cl5.0-pipeline/gl/). Hace lo que
// en el original hacían la función fija y la pila de matrices (06 § Primitivas 2D): el vertex
// shader lleva cada vértice, en píxeles de la pantalla, al clip con la matriz de gluOrtho2D, y el
// fragment shader multiplica la textura por el color del vértice, el GL_MODULATE de glTexEnv. Con
// la textura apagada (GL_TEXTURE_2D en el original, la bandera `textured` acá) el fragmento es el
// color solo, y con el alpha test prendido descarta lo que tiene un alfa de 0,25 o menos, como
// glAlphaFunc(GL_GREATER, 0,25) (06 § Cámara). La mezcla no es del shader: es glBlendFunc, de
// GlSpriteRenderer.
namespace Client::Render::Gl
{
// Las location de los atributos, las de SpriteVertex en el VAO de GlRenderer.
inline constexpr unsigned SpritePositionLocation = 0;
inline constexpr unsigned SpriteUvLocation = 1;
inline constexpr unsigned SpriteColorLocation = 2;

// El uniform de la matriz, en column-major como la da glm::value_ptr.
inline constexpr const char* SpriteProjectionUniform = "projection";
// Las dos banderas del fragment shader, que GL guarda como int (glUniform1i, 0 o 1): si se
// muestrea la textura y si va el alpha test.
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

// El par del 3D (CL6.3): lo mismo para un ModelVertex, con la posición en el mundo llevada al clip
// por la perspectiva y la vista del pase (projection · view), y el color recortado a [0, 1] antes de
// la textura, como la función fija recorta el glColor del vértice (la luz del original pasa de 1:
// 1,34 en la selección, hasta 3,45 con la luz por vértice). Con `textureAdd` la textura se combina
// como con GL_ADD (la 6ac/2, 0x5489eb): el color es téxel + color recortado a [0, 1] y el alfa,
// téxel · color. Después va la niebla de GL_LINEAR (Render/ModelFog.h, la de 0x42b0a0): la distancia
// es |z| del ojo, que con la perspectiva de gluPerspective es la w del clip, interpolada en el
// triángulo; el factor (fin − d) / (fin − inicio), recortado a [0, 1], mezcla el color con el de la
// niebla y deja el alfa. El alpha test no depende de la niebla: la niebla no toca el alfa.
inline constexpr unsigned ModelPositionLocation = 0;
inline constexpr unsigned ModelUvLocation = 1;
inline constexpr unsigned ModelColorLocation = 2;

inline constexpr const char* ModelViewProjectionUniform = "viewProjection";
inline constexpr const char* ModelTexturedUniform = "textured";
inline constexpr const char* ModelAlphaTestUniform = "alphaTest";
// Las del modo de la textura y la niebla: la bandera de GL_ADD y la de GL_FOG como int (glUniform1i, 0 o 1), el
// color (glUniform3f) y el inicio y el fin (glUniform1f).
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
out vec2 fragmentUv;
out vec4 fragmentColor;
out float fragmentDistance;
void main()
{
    fragmentUv = uv;
    fragmentColor = clamp(color, 0.0, 1.0);
    gl_Position = viewProjection * vec4(position, 1.0);
    fragmentDistance = abs(gl_Position.w);
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
