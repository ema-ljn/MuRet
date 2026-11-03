#pragma once

#include "Quaternion.h"

#include <optional>
#include <span>
#include <vector>

// Misión de la CL6.0 (docs/learning/ola-cl6.0-huesos-y-cuaterniones.md, actividad 9): una cadena de
// huesos y el skinning rígido, con la matemática de Half-Life que usa 0x4eb860: R_ConcatTransforms
// (0x5fca90), AngleMatrix (0x5fc8f0), VectorTransform (0x5fccc0) y VectorRotate (0x5fcbd0).
namespace Labs
{
// Un hueso en una pose: su padre (−1 en la raíz) y su posición y su giro, los dos relativos al
// padre. En el .bmd cada key trae la posición y tres ángulos en radianes; el original los pasa a
// cuaternión al leer el archivo (0x4f13d0 llama a 0x5fcd20 por key).
struct Bone
{
    int parent = -1;
    Vec3 position{};
    Quat rotation{};
};

// 0x5fca90(a, b, out): out = a · b, las dos como 4 × 4 con la fila (0, 0, 0, 1) abajo. Con a la del
// padre en el mundo y b la del hueso relativa al padre, da la del hueso en el mundo.
[[nodiscard]] Matrix34 Concat(const Matrix34& a, const Matrix34& b);

// 0x5fc8f0: el giro Rz · Ry · Rx de tres ángulos en grados, como matriz, con el π/180 en float de
// 0x6ba360. Es el giro de AngleQuaternion con los mismos ángulos en radianes.
[[nodiscard]] Matrix34 AngleMatrix(Vec3 degrees);

// Lo que 0x4eb860 hace con el objeto antes del hueso raíz: AngleMatrix de sus ángulos (+0x78), la
// escala (+0x68) en las tres columnas del giro y la posición (+0x6c) en la columna 3.
[[nodiscard]] Matrix34 ObjectMatrix(Vec3 degrees, float scale, Vec3 position);

// La matriz de un hueso relativa a su padre: QuaternionMatrix de su giro y su posición en la
// columna 3.
[[nodiscard]] Matrix34 LocalMatrix(const Bone& bone);

// La matriz de cada hueso en el mundo: la raíz cuelga del objeto y cada hueso de su padre. El padre
// tiene que estar antes que el hijo, porque su matriz se usa ya calculada; si no (o si el índice no
// existe), nullopt.
[[nodiscard]] std::optional<std::vector<Matrix34>> PoseChain(std::span<const Bone> bones, const Matrix34& object);

// 0x5fccc0: un punto llevado por la matriz entera, giro y traslación. Es el skinning rígido de un
// vértice, guardado en el espacio de su hueso.
[[nodiscard]] Vec3 Transform(const Matrix34& matrix, Vec3 point);

// 0x5fcbd0: un vector llevado solo por el giro, sin la traslación. Es lo que se hace con una
// normal.
[[nodiscard]] Vec3 Rotate(const Matrix34& matrix, Vec3 vector);
} // namespace Labs
