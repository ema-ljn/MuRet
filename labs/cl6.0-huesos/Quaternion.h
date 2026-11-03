#pragma once

#include <array>

// Misión de la CL6.0 (docs/learning/ola-cl6.0-huesos-y-cuaterniones.md, actividad 4): los
// cuaterniones del original escritos a mano. Son los de Half-Life (mathlib de su studiomdl):
// AngleQuaternion es 0x5fcd20 y QuaternionMatrix, 0x5fcdb0. Los tests los comparan con glm.
//
// La x87 del original trabaja con 53 bits de mantisa (la precisión que pone el CRT de MSVC,
// *inferido* como en la CL5.0): las cuentas van en double, y cada resultado se redondea a float
// donde el binario lo guarda en memoria (FST o FSTP a un DWORD).
namespace Labs
{
// Un punto o un vector en 3D, o tres ángulos (x, y, z).
using Vec3 = std::array<float, 3>;

// Un cuaternión como lo guarda el original: x, y, z y w, en ese orden. Para un giro de ángulo θ
// alrededor del eje unitario e, (x, y, z) = e · sin(θ/2) y w = cos(θ/2).
struct Quat
{
    float x = 0;
    float y = 0;
    float z = 0;
    float w = 1;

    friend bool operator==(const Quat&, const Quat&) = default;
};

// Una matriz de 3 × 4 por filas, la de Half-Life: m[fila][columna], el giro en las tres primeras
// columnas y la traslación en la cuarta. La fila (0, 0, 0, 1) de abajo no se guarda.
using Matrix34 = std::array<std::array<float, 4>, 3>;

// 0x5fcd20: de tres ángulos de Euler en radianes al cuaternión del giro Rz · Ry · Rx (primero
// alrededor de x, después de y, después de z, en los ejes fijos).
[[nodiscard]] Quat AngleQuaternion(Vec3 radians);

// 0x5fcdb0: el giro de un cuaternión unitario como matriz. El original no escribe la columna 3;
// acá queda en 0.
[[nodiscard]] Matrix34 QuaternionMatrix(Quat q);
} // namespace Labs
