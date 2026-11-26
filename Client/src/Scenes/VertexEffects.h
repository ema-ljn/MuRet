#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <span>
#include <string>

namespace Client::Data::Models
{
struct Model;
}
namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
class IModelLibrary;
struct SkinnedMesh;
} // namespace Client::Models
namespace Client::World
{
class FrameSprites;
class ModelEffects;
class Particles;
} // namespace Client::World
namespace Client::Scenes
{
struct ActorState;
struct DrawnObject;
struct JointState;

// Lo que 0x4ee770 recibe además del modelo (ECX): la malla, el tipo, el subtipo, los ángulos del cuarto argumento (sólo
// el 0x5ac/1 lee su z) y el personaje del quinto (el 0x5ac/1: el destino y el dueño de sus joints).
struct VertexEffectCall
{
    std::int32_t mesh = 0;
    std::int32_t type = 0;
    std::int32_t subtype = 0;
    glm::vec3 angles{};
    ActorState* target = nullptr;
};

// Las tablas a las que suelta: 0x518cf0 (los efectos con modelo), 0x53fe70 (las partículas, con su reloj), 0x5491e0
// (los sprites) y 0x530860 (los joints). Sólo hace falta la que pide el tipo; sin ella, corta en su dirección.
struct VertexEffectTables
{
    World::ModelEffects* effects = nullptr;
    World::Particles* particles = nullptr;
    float worldClock = 0.0f;
    World::FrameSprites* sprites = nullptr;
    std::span<JointState> joints{};
};

// 0x4ee770: con la malla entre 0 y las del modelo (+0x24) y con triángulos (+0xa), cada esquina de cada polígono (el
// byte de la cuenta con signo; sin deduplicar) sobre el scratch de 0x4ebc80 (0x5786f9c + (malla · 15.000 + vértice) ·
// 12, `meshes`), con los ángulos en 0 y la luz en 1 al empezar:
// - 0x10a: el /0, rand() & 1 con signo y con 0 el 0x10b/0, y rand() % 10 y con 0 el 0x10a/0; el /1 el 0x10b/2 y el /2
//   el 0x10b/3, sin rand(); el /3, el 0x10a + (rand() & 1) con el subtipo 4.
// - 0x10c, con la luz en 0,2f: el /1, rand() % 5 (0x10d/2) y rand() % 10 (0x10c/2); el /0, rand() % 12 (0x10d/0) y
//   rand() % 50 (0x10c/0).
// - 0x12d, 0x12e y 0x12f: rand() % 3 y con 0 el 0x12d (el 0x12f el 0x12f) + (rand() & 1), con el subtipo 1 el 0x12d y
//   0 los otros dos.
// - 0x131, con la luz en 0,2f: rand() % 12 (0x132), rand() % 40 (0x131) y rand() % 40 (0x133).
// - 0x137: rand() & 7 con signo y con 0 la cáscara 0x137 del subtipo (World/SkinShellEffects.h; la piden el 0x632/0 y
//   /1 con dueño al llegar al suelo, Scenes/SkinShells.h).
// - 0x13c: rand() % 45; con 0 y menos de 20 contados, el /0 el 0x13c/0 y el /1 el 0x12d/2 y el 0x12e/2; cualquier
//   subtipo cuenta uno.
// - 0x5ac: el /0, el sprite 0x5ac con la escala del modelo (+0x68) y la luz en 0,08f; el /1, en los triángulos de
//   índice múltiplo de 22, rand() % 90 (la x de los ángulos en −ese, la y en 0), rand() % 120 − 60 más la z del cuarto
//   argumento (la z) y el joint 0x613/0xd del vértice hacia el personaje (+0x10), con él de dueño y ancho 20.
// - 0x607, con la luz en 1: rand() % 30 y con 0 la partícula 0x607/2 de escala 1.
// Otro tipo o subtipo no hace nada. Todas las altas sin dueño y con la clave en −1 (los efectos); con la tabla llena,
// ninguna y sin sus rand(). Los rand() de 0x4ee770 salen igual.
[[nodiscard]] Core::Expected<void, std::string>
EmitVertexEffectsOf(const Data::Models::Model& model, float modelScale, std::span<const Models::SkinnedMesh> meshes,
                    const VertexEffectCall& call, const VertexEffectTables& tables, Crt::CrtRandom& random);

// EmitVertexEffectsOf con el modelo de `object` (sin él, nada), su escala y sólo la tabla de los efectos con modelo.
[[nodiscard]] Core::Expected<void, std::string>
EmitVertexEffects(const Models::IModelLibrary& models, const DrawnObject& object,
                  std::span<const Models::SkinnedMesh> meshes, std::int32_t mesh, std::int32_t type,
                  std::int32_t subtype, World::ModelEffects& effects, Crt::CrtRandom& random);
} // namespace Client::Scenes
