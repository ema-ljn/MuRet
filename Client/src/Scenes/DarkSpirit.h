#pragma once

#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Pose.h"
#include "Scenes/CharacterDrawing.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <vector>

namespace Client::Render::Models
{
class CrtRandom;
struct Skeleton;
} // namespace Client::Render::Models

namespace Client::World
{
class Particles;
struct TerrainPolygon;
} // namespace Client::World

// El cuervo (CL7.6 C): el pájaro del ítem 0x1c17 en la mano del Dark Lord, el modelo 0xc0
// (Skill\DarkSpirit.bmd). Lo crea 0x420d60 con 0x4167b0 (un objeto de 0x460 bytes con un CHARACTER en
// +0xc), lo mueve 0x416a20 en cada paso de su dueño y lo dibuja 0x4182f0. Contrato: 06 § El cuervo.
// Verificado bit a bit contra el código real emulado (tests/vectors/world55/raven.txt), en los estados 0
// y 1: los de fuera de la zona segura, los únicos del login.
namespace Client::Scenes
{
inline constexpr std::uint16_t DarkSpiritModel = 0xc0;

struct DarkSpirit
{
    float scale = 0.7f;                       // +0xc (0x4167b0)
    glm::vec3 position{};                     // +0x10
    glm::vec3 angles{};                       // +0x1c, en grados: x el cabeceo, z el rumbo
    std::int32_t far = 0;                     // +0x60, los pasos lejos del dueño
    glm::vec3 direction{};                    // +0xcc, la velocidad en sus ejes
    float speed = 1.0f;                       // +0xd8
    glm::vec3 light{0.5f};                    // +0xf4
    std::uint8_t state = 0;                   // +0x110: 0 rápido (dirección y < −12), 1 lento
    Render::Models::AnimationState animation; // +0x111 la acción, +0x114 el frame
    float alpha = 1.0f;                       // +0x174: aparece de golpe
    // La posición antes del último paso, para dibujarlo entre dos pasos (D-C4); el salto junto al dueño
    // la pisa con la nueva.
    glm::vec3 previousPosition{};
    // +0x120: sus huesos como los dejó el último dibujo, en el mundo (0x602e90 con el segundo argumento
    // en 0). Antes del primero, vacío (Q-C133).
    std::vector<Render::Models::OriginalMatrix> pose;
};

// Lo que el paso lee del dueño: su posición (+0x10) y sus banderas (+0x78). Con 0x1000 el paso no hace
// nada; con 0x4000 no hay chispa.
struct DarkSpiritOwner
{
    glm::vec3 position{};
    std::uint32_t flags = 0;
};

// 0x4167b0 con 0x415be0: los 12 rand() de los cierres de las celdas protegidas del dueño y del contador
// (Q-C125), y el cuervo en el centro de la celda del dueño, a la altura del terreno ahí (`groundHeight`,
// la de 0x5f93c0 en (celda · 100 + 50), como la deja la x87) más 300. En el login, la celda (0, 0)
// (Q-C130).
[[nodiscard]] DarkSpirit CreateDarkSpirit(int cellX, int cellY, double groundHeight, Render::Models::CrtRandom& random);

// 0x416a20 en los estados 0 y 1, con el esqueleto del cuervo, en el mapa `map`, con el rand() de la CRT
// y el valor que el grito lee de 0x7d3a0f0 (`counter`): la animación de a 0,4 y la acción por el estado
// al dar la vuelta (0x417f0c); la chispa desde un hueso al azar de `pose` (0x5c6, subtipo 5, en
// `particles`; sin pose, desde su posición); el giro hacia el dueño a más de 150; el movimiento; el
// cabeceo hacia 350 sobre el dueño en el mapa 0x37 y 250 en los demás (Q-C131); el sorteo de la
// dirección; el estado; el grito; y el salto junto al dueño cuando el contador de lejanía pasa de 90.
// Devuelve si grita (0x41ed10(0x1d1)).
bool StepDarkSpirit(DarkSpirit& spirit, const Render::Models::Skeleton& skeleton, const DarkSpiritOwner& owner,
                    std::uint8_t map, World::Particles& particles, float worldClock, Render::Models::CrtRandom& random,
                    std::int32_t counter);

// 0x4182f0: si el cuervo pasa el trapecio de 0x5fba90 en su (x, y) por 0,01f, con −20. Si no, el dibujo
// le pone la velocidad en 3 y, salvo en el estado 4 (0x418375), el estado en 0 (MissTheView).
[[nodiscard]] bool DarkSpiritSeen(const DarkSpirit& spirit, const World::TerrainPolygon& polygon);
void MissTheView(DarkSpirit& spirit);

// Lo que se dibuja del cuervo (DrawDarkSpirit) entre dos pasos (`stepAlpha` de un paso más, D-C4): el
// 0xc0 con su escala, sus ángulos y la posición más lo que se movió en el último paso por `stepAlpha`,
// la animación interpolada, la luz del terreno en su lugar (`terrainLight`) más la suya, la luz por
// vértice y su alpha. Con `stepAlpha` en 0, el estado del paso: el que posa el original.
[[nodiscard]] DrawnObject DrawnDarkSpirit(const DarkSpirit& spirit, const Render::Models::Skeleton* skeleton,
                                          const glm::vec3& terrainLight, float stepAlpha);
} // namespace Client::Scenes
