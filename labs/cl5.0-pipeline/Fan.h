#pragma once

#include <array>
#include <cstdint>
#include <vector>

// Misión de la CL5.0 (docs/learning/ola-cl5.0-el-pipeline-grafico.md, actividad 8): de un
// GL_TRIANGLE_FAN a GL_TRIANGLES. El original manda cada quad como un fan de 4 vértices
// (glBegin(6) en 0x621b60); un lote de muchos quads en un solo glDrawElements necesita los
// triángulos sueltos.
namespace Labs
{
// Los índices de un triángulo, en el orden en que los dibuja GL.
using Triangle = std::array<std::uint32_t, 3>;

// Un fan de `vertexCount` vértices son los triángulos (0, i, i + 1) para i de 1 a n − 2: todos
// comparten el vértice 0 y conservan el orden, así que el sentido de giro no cambia. Con menos de 3
// vértices no hay ninguno.
[[nodiscard]] std::vector<Triangle> FanToTriangles(std::uint32_t vertexCount);
} // namespace Labs
