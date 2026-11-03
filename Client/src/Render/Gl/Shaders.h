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
} // namespace Client::Render::Gl
