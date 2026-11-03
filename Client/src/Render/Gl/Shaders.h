#pragma once

#include <string_view>

// El GLSL 330 core del 2D (D-C3; la CL5.0 lo armó en el lab, labs/cl5.0-pipeline/gl/). Hace lo que
// en el original hacían la función fija y la pila de matrices (06 § Primitivas 2D): el vertex
// shader lleva cada vértice, en píxeles de la pantalla, al clip con la matriz de gluOrtho2D, y el
// fragment shader multiplica la textura por el color del vértice, el GL_MODULATE de glTexEnv. El
// alpha test (el discard en 0,25) y la mezcla llegan en la CL5.2.
namespace Client::Render::Gl
{
// Las location de los atributos, las de SpriteVertex en el VAO de GlRenderer.
inline constexpr unsigned SpritePositionLocation = 0;
inline constexpr unsigned SpriteUvLocation = 1;
inline constexpr unsigned SpriteColorLocation = 2;

// El uniform de la matriz, en column-major como la da glm::value_ptr.
inline constexpr const char* SpriteProjectionUniform = "projection";

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
out vec4 outputColor;
void main()
{
    outputColor = texture(image, fragmentUv) * fragmentColor;
}
)";
} // namespace Client::Render::Gl
