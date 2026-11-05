#pragma once

#include "Data/Terrain/TerrainObjects.h"
#include "Render/Models/Animation.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <span>
#include <vector>

// Los objetos de un mundo (CL7.2): los del .obj como los arma 0x606660, su paso de 0x604ab0 y lo que
// 0x6002e0 hace con cada uno. Contrato: 06 § El mundo del login; vectores del código real en
// Client/tests/vectors/world55/objects.txt (scripts/login/make_world55_vectors.py).
namespace Client::World
{
// La escena en que se crea: los casos por tipo de 0x606660 solo corren en el login (2) y la selección
// (4).
enum class ObjectScene : std::uint8_t
{
    Login,
    CharacterSelect,
    Other,
};

// Lo que el objeto de 0x1d4 bytes guarda y usa el login.
struct WorldObject
{
    std::int16_t type = 0;                     // +0x2, el modelo
    Render::Models::ObjectTransform transform; // +0xc la escala, +0x10 la posición, +0x1c los ángulos
    Render::Models::ObjectState state;         // +0x64, +0x68, +0x6c, +0x70 y +0x58
    glm::vec3 light{};                         // +0xf4..+0xfc, la que se suma a la del terreno
    float speed = 0.0f;                        // +0xd8, keys por paso
    float alpha = 1.0f;                        // +0x174
    float targetAlpha = 1.0f;                  // +0x170
    bool vertexLight = true;                   // +0xe8
    Render::Models::AnimationState animation;  // +0x111 la acción, +0x114 el frame
    // +0x16c: se vio en el último dibujo; 0x606560 solo avanza los que tienen esto.
    bool seen = false;
};

// +0x58 = −2: 0x6002e0 no lo dibuja (DAT_077f3b18, el modo editor, nunca está prendido).
inline constexpr std::int16_t HiddenObject = -2;

// 0x606660 con cada registro de `records`, en su orden: vivo, alpha y meta en 1, luz por vértice, luz
// en 0, velocidad 0,16, sin malla oculta ni blendMesh, luz del blendMesh en 1 y U y V en 0; en el
// login y la selección, el farol (0x3c) a escala 0,8 y luz 0,2, y el dueño del dragón (0xb4) oculto y
// con luz blanca; en el mapa 0x37, los remolinos 0x56 y 0x5a a (8.600; 25.000; 5.000) con ángulos 0 y
// escalas 60 y 110. Uno fuera de la grilla de sectores no se crea.
[[nodiscard]] std::vector<WorldObject> CreateWorldObjects(std::span<const Data::Terrain::TerrainObject> records,
                                                          std::uint8_t map, ObjectScene scene);

// 0x604ab0 sobre un objeto que se vio: el alpha hacia su meta (0x4e9980), la animación a su velocidad
// (0x4ec200, con la acción de `skeleton`, si tiene) y los casos del mapa: en el 0x37, los marcadores
// 0x54, 0x55, 0x57 y 0x59 se ocultan, el remolino 0x56 gira −0,6° (alpha 0,2, luz (1, 0, 0) y todas
// las mallas por el blendMesh) y el 0x5a −0,23° (alpha 1 y luz blanca, también por el blendMesh).
void StepWorldObject(WorldObject& object, std::uint8_t map, const Render::Models::Skeleton* skeleton);

// Lo que 0x5ffdf0 y 0x6002e0 hacen con un objeto en el dibujo.
enum class ObjectDrawKind : std::uint8_t
{
    // Nada: el alpha por debajo de 0,01 o +0x58 en −2.
    Hidden,
    // Un RenderBody(2, alpha, blendMesh, luz del blendMesh, U, V, malla oculta, −1) con el color del
    // modelo.
    Body,
    // El remolino 0x5a del mapa 0x37: 0x4ecc30(0, 0x80, 1, blendMesh, 1, U, V, −1), la malla 0 sola,
    // con GL_ZERO, GL_ONE_MINUS_SRC_COLOR, en blanco y sin luz.
    SubtractiveMesh,
};

struct ObjectDraw
{
    ObjectDrawKind kind = ObjectDrawKind::Hidden;
    float alpha = 1.0f;
    Render::Models::ObjectState state;
    // +0x48..+0x50 del modelo, el que le pone 0x5ffce0.
    glm::vec3 color{1.0f};
    bool vertexLight = false;
};

// El dibujo de `object` con `terrainLight`, la del terreno en su lugar (0x5f99e0).
[[nodiscard]] ObjectDraw DrawOf(const WorldObject& object, const glm::vec3& terrainLight, std::uint8_t map);

// El sector de 16 × 16 celdas de un objeto (0x606660: la posición por 0,000625), de 0 a 15, o −1 fuera
// de la grilla, donde 0x606660 no lo crea.
[[nodiscard]] int SectorX(const WorldObject& object);
[[nodiscard]] int SectorY(const WorldObject& object);

// El orden del dibujo de 0x6046a0: los sectores por x y después por y (0x7d325d0 + (x · 16 + y) ·
// 0x10) y, en cada uno, el orden en que se crearon. Devuelve los índices de `objects`.
[[nodiscard]] std::vector<std::size_t> DrawOrder(std::span<const WorldObject> objects);
} // namespace Client::World
