#pragma once

#include <array>

// Misión de la CL5.0 (docs/learning/ola-cl5.0-el-pipeline-grafico.md, actividad 6): la matriz de
// gluOrtho2D(left, right, bottom, top) escrita a mano, la que pone 0x6219b0 al entrar al 2D
// (06, "Primitivas 2D"), y lo que hace con un punto.
//
// Es la de la página de manual de glOrtho con near en −1 y far en 1: 2/(r − l) y 2/(t − b) en la
// diagonal, −1 en z, y la traslación −(r + l)/(r − l) y −(t + b)/(t − b). Los tests la comparan con
// glm::ortho.
namespace Labs
{
// Una matriz de 4 × 4 en column-major, el orden que espera glUniformMatrix4fv con transpose en
// GL_FALSE: el elemento de la columna c y la fila r está en [c * 4 + r], así que la traslación (la
// cuarta columna) queda en 12, 13 y 14.
using Matrix4 = std::array<float, 16>;

// Un punto en coordenadas homogéneas. Uno de la ventana va con z = 0 y w = 1.
struct Vec4
{
    float x = 0;
    float y = 0;
    float z = 0;
    float w = 1;
};

[[nodiscard]] Matrix4 Ortho2D(float left, float right, float bottom, float top);

// matrix · point, lo que hace el vertex shader con gl_Position: da el punto en el clip.
[[nodiscard]] Vec4 Multiply(const Matrix4& matrix, Vec4 point);
} // namespace Labs
