#pragma once

#include "Data/Terrain/TerrainObjects.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "World/WorldObjectRegistrations.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Los objetos de un mundo (CL7.2): los del .obj como los arma 0x606660, su paso de 0x604ab0 y lo que
// 0x6002e0 hace con cada uno. Contrato: 06 § El mundo del login; vectores del código real en
// Client/tests/vectors/world55/objects.txt (scripts/login/make_world55_vectors.py).
namespace Client::World
{
// La escena en que se crea: los casos del farol y del dueño del dragón de 0x606660 solo corren en el
// login (2) y la selección (4). Los casos por mapa no dependen de la escena.
enum class ObjectScene : std::uint8_t
{
    Login,
    CharacterSelect,
    Other,
};

// Proyección tipada de los campos usados de 0x1d4 bytes; no reproduce el layout i386.
struct WorldObject
{
    WorldObjectIdentity identity = 0;              // ID del caller; 0 en la proyección sin registro
    std::int16_t type = 0;                         // +0x2, el modelo
    Render::Models::ObjectTransform transform;     // +0xc la escala, +0x10 la posición, +0x1c los ángulos
    Render::Models::ObjectState state;             // +0x64, +0x68, +0x6c, +0x70 y +0x58
    glm::vec3 light{};                             // +0xf4..+0xfc, la que se suma a la del terreno
    float speed = 0.0f;                            // +0xd8, keys por paso
    float cullingMargin = -30.0f;                  // +0xdc, margen inicial de 0x6067ae
    float fieldE0 = 50.0f;                         // +0xe0, significado/consumidor sin modelar
    glm::vec3 basePosition{};                      // +0x17c, segunda copia inicial de la posición
    glm::vec3 boundsMinimum{-40.0f, -40.0f, 0.0f}; // +0x124..+0x12c
    glm::vec3 boundsMaximum{40.0f, 40.0f, 80.0f};  // +0x130..+0x138
    bool alive = true;                             // +0x0
    float alpha = 1.0f;                            // +0x174
    float targetAlpha = 1.0f;                      // +0x170
    bool vertexLight = true;                       // +0xe8
    Render::Models::AnimationState animation;      // +0x111 la acción, +0x114 el frame
    // +0x16c: se vio en el último dibujo; 0x606560 solo avanza los que tienen esto.
    bool seen = false;
    // +0x1c8: el sector de 16 × 16 celdas donde lo guardó 0x606660, de la posición del archivo.
    int sectorX = 0;
    int sectorY = 0;
};

// +0x58 = −2: 0x6002e0 no lo dibuja (DAT_077f3b18, el modo editor, nunca está prendido).
inline constexpr std::int16_t HiddenObject = -2;

// El farol del castillo (Object61.bmd, de un hueso) y lo que pide 0x602ed0 en la escena 2 (0x602f21): dos
// sprites de flare01 (0x5ac), de escala 1, aditivos y sin giro, en las traslaciones de los huesos 8 y 9
// del buffer global de la pose (0x68b199c y 0x68b19cc), las del último modelo posado que los tiene
// (Q-C140).
inline constexpr std::int16_t StreetLight = 0x3c;
inline constexpr std::uint16_t StreetLightGlowTexture = 0x5ac;
inline constexpr std::array<std::size_t, 2> StreetLightGlowBones{8, 9};

// La luz de los dos sprites del farol con `random`, el rand() que 0x602ef4 saca por cada objeto: L =
// (random % 30 + 70) · 0,01f (0x6b88d4) guardado en float, y (L; L · 0,5; L · 0,2f), cada producto
// guardado en float (0x6b882c y 0x6b8854).
[[nodiscard]] glm::vec3 StreetLightGlowLight(int random);

// Proyección de creación sin registro auxiliar: conserva el vector usado por escenas existentes,
// casos directos del mapa 0/55 y de las escenas 2/4. No equivale al handler completo: omite
// 0x606630 y no crea el efecto transitivo de 180 (integrado por su dueño del login). Cada registro
// válido crea otro objeto, sin pool ni reutilización. Fuera de la grilla no se crea.
[[nodiscard]] std::vector<WorldObject> CreateWorldObjects(std::span<const Data::Terrain::TerrainObject> records,
                                                          std::uint8_t map, ObjectScene scene);

// Ruta con el estado auxiliar PREEXISTENTE del caller: cada entidad recibe un ID nuevo de
// registrations.nextIdentity y los tipos 6/133/145/146 del mapa 0 lo registran. Los records deben ser
// estables y disjuntos del estado/resultados durante la creación. El dominio de tipo es el int16 del
// Data reader, extendido con signo al despacho (el callee nativo admite int32). El caller conserva
// el mismo registro/contador durante la vida del mundo y aporta nextIdentity libre al restaurarlo.
// Precondición del contador portable: 1 <= nextIdentity y records.size() <= UINT64_MAX-nextIdentity;
// no reproduce un contador nativo. La tabla no mantiene con vida ni resuelve por sí misma los IDs.
[[nodiscard]] std::vector<WorldObject> CreateWorldObjects(std::span<const Data::Terrain::TerrainObject> records,
                                                          std::uint8_t map, ObjectScene scene,
                                                          WorldObjectRegistrations& registrations);

// 0x604ab0 sobre un objeto que se vio: el alpha hacia su meta (0x4e9980) y, si no quedó por debajo de
// 0,01, la animación a su velocidad
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
    // 0x5ffdf0 lo posó en el buffer global de la pose (0x5fff7a): todos menos los de alpha por debajo de
    // 0,01, que sale antes (0x5ffe3d); también los de +0x58 en −2, que 0x6002e0 no dibuja.
    bool posed = false;
    float alpha = 1.0f;
    Render::Models::ObjectState state;
    // +0x48..+0x50 del modelo, el que le pone 0x5ffce0.
    glm::vec3 color{1.0f};
    bool vertexLight = false;
};

// El dibujo de `object` con `terrainLight`, la del terreno en su lugar (0x5f99e0).
[[nodiscard]] ObjectDraw DrawOf(const WorldObject& object, const glm::vec3& terrainLight, std::uint8_t map);

// El sector de 16 × 16 celdas de un objeto, de 0 a 15: el que le dio 0x606660 con la posición del
// archivo por 0,000625f, el producto sin redondear (la x87) y ftol. Fuera de la grilla, o con un NaN,
// 0x606660 no lo crea.
[[nodiscard]] int SectorX(const WorldObject& object);
[[nodiscard]] int SectorY(const WorldObject& object);
// El sector de una coordenada, como lo saca 0x606660 (de 0 a 15, o −1): para un objeto que no viene de
// un .obj, como el logo.
[[nodiscard]] int SectorOf(float coordinate);

// El orden del dibujo de 0x6046a0: los sectores por x y después por y (0x7d325d0 + (x · 16 + y) ·
// 0x10) y, en cada uno, el orden en que se crearon. Devuelve los índices de `objects`.
[[nodiscard]] std::vector<std::size_t> DrawOrder(std::span<const WorldObject> objects);
} // namespace Client::World
