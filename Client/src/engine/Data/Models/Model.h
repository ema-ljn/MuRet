#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Un modelo .bmd leído, como lo deja el lector del original, 0x4f13d0 (Client/docs/research/
// 05-formatos-de-datos.md, "Modelos .bmd"): las mallas con sus vértices, normales, UV y triángulos,
// las acciones y los huesos con las keys de cada acción. Sin matemática: el cuaternión de cada key y
// la pose son de la CL6.2.
//
// Cada float queda con los bits del archivo, aunque sea un NaN, como en el .obj (CL1.3). Los índices
// van como los trae el archivo, sin mirar que entren: los golden miran que en el laboratorio entran.
namespace Client::Data::Models
{
struct Vertex
{
    // El hueso del que cuelga, rígido. En las partes del personaje es un índice en los huesos de
    // Player.bmd, no en los de la parte (05).
    std::int16_t bone = 0;
    // En el espacio de ese hueso.
    std::array<float, 3> position{};
};

struct Normal
{
    std::int16_t bone = 0;
    std::array<float, 3> normal{};
    // El int16 de +16: un índice en los vértices de la malla, el del vértice que la usa (*inferido*:
    // en el laboratorio, los 301.367 entran en su malla).
    std::int16_t vertex = 0;
};

struct TexCoord
{
    float u = 0.0f;
    float v = 0.0f;
};

// Lo que se lee de los 36 bytes que copia 0x4f13d0 de cada triángulo de 64. Los 6 de +0x1a, los pisa
// la adyacencia que calculan 0x4f0870 y 0x4f0910, y los 4 de +0x20 no los usa nada de la CL6.
struct Triangle
{
    // 3 en todas las mallas del laboratorio. El cuarto índice de cada juego va solo con 4.
    std::uint8_t polygon = 3;
    std::array<std::int16_t, 4> vertices{};
    std::array<std::int16_t, 4> normals{};
    std::array<std::int16_t, 4> texCoords{};
};

struct Mesh
{
    // El int16 de la cabecera de la malla, como viene. El slot de la textura lo da el asignador del
    // original (0x60d5f0, CL6.1 PR B), por el nombre.
    std::int16_t texture = 0;
    std::vector<Vertex> vertices;
    std::vector<Normal> normals;
    std::vector<TexCoord> texCoords;
    std::vector<Triangle> triangles;
    // Los 32 bytes del final de la malla, hasta el primer 0.
    std::string textureName;
};

struct Action
{
    std::int16_t keyCount = 0;
    bool lockPositions = false;
    // Con lockPositions, una posición por key. El original las guarda y nada las lee (05).
    std::vector<std::array<float, 3>> lockedPositions;
};

// Las keys de un hueso en una acción: keyCount posiciones y después keyCount rotaciones (ángulos de
// Euler en radianes), las dos relativas al padre.
struct BoneKeys
{
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 3>> rotations;
};

struct Bone
{
    // Un hueso dummy no trae nombre, padre ni keys, y la pose no lo toca (05).
    bool dummy = false;
    std::string name;
    std::int16_t parent = -1;
    // Una por acción del modelo, en el orden de Model::actions. Vacío en un dummy.
    std::vector<BoneKeys> actions;
};

struct Model
{
    // El byte 3 del archivo: 0x0C va cifrado con K16, cualquier otro en claro.
    std::uint8_t version = 0;
    // Los 32 bytes del nombre, hasta el primer 0.
    std::string name;
    std::vector<Mesh> meshes;
    std::vector<Action> actions;
    std::vector<Bone> bones;
    // Lo que sobra después del último hueso, en lo que lee el original: el bloque cifrado en la
    // versión 0x0C y el resto del archivo en las otras. No lo mira; en el laboratorio es 0.
    std::size_t trailingBytes = 0;
};
} // namespace Client::Data::Models
