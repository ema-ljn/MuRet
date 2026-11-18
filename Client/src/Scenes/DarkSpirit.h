#pragma once

#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Pose.h"
#include "Scenes/CharacterDrawing.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
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
// y 1, los del vuelo libre: los únicos del login. El 3 (posado en el hombro) y el 4 (bajando al hombro) son
// los de la zona segura, portados desde el ASM; el 2, el 5 y el 6 son los del ataque que pone el A8 (Scenes/PetOrders),
// portados desde el ASM en el CL9 hito 4 (06 § El ataque del cuervo).
namespace Client::Scenes
{
inline constexpr std::uint16_t DarkSpiritModel = 0xc0;

struct DarkSpirit
{
    float scale = 0.7f;     // +0xc (0x4167b0)
    glm::vec3 position{};   // +0x10
    glm::vec3 angles{};     // +0x1c, en grados: x el cabeceo, z el rumbo
    std::int32_t far = 0;   // +0x60, los pasos lejos del dueño
    glm::vec3 direction{};  // +0xcc, la velocidad en sus ejes
    float speed = 1.0f;     // +0xd8
    glm::vec3 light{0.5f};  // +0xf4
    std::uint8_t state = 0; // +0x110: 0 rápido (dirección y < −12), 1 lento, 2 volviendo, 3 posado, 4 bajando, 5 el
                            // ataque, 6 el grito
    Render::Models::AnimationState animation; // +0x111 la acción, +0x114 el frame
    float alpha = 1.0f;                       // +0x174: aparece de golpe
    // Lo que escriben las órdenes que llegan (Scenes/PetOrders, CL9 hito 4), con el offset del objeto y, entre
    // paréntesis, el del CHARACTER de +0xc. Qué deja en cada uno 0x4167b0, sin leer: quedan en 0 hasta la primera.
    // +0x45c: la orden del A7 (0x416510). La lee el estado 5 del paso (0x417a9d): con la 3, +0x1ad queda en 1.
    std::uint8_t order = 0;
    // +0x1ad (+0x1a1): en 1 con la orden 3 del A7 y con el estado 5 del A8. En 1, el vuelo va hacia el blanco de +8 y
    // no hacia el dueño (0x4177b1); los estados 0 y 1 lo ponen en 0 (0x417b33).
    std::uint8_t byte1ad = 0;
    // +8: el personaje del blanco (0x416510 con la orden 3 y 0x416570), el lugar del pool; el 400 si la clave no está.
    std::size_t target = 0;
    // +0x3b0 (+0x3a4): el lugar del blanco del A8, que lee el paso (0x416a73, con signo); 0xffff después del aviso.
    std::uint16_t target3b0 = 0;
    // +0x38d (+0x381): el A8 lo pone en 0; el paso lo sube hasta 15 en los estados 5 y 6 (0x417e15) y 0x418920 lo
    // lee.
    std::uint8_t byte38d = 0;
    // +0xf0 (+0xe4): el A8 con el estado 5 lo pone en 0,5; es la aceleración del ataque, que sube 0,2 por paso.
    float floatF0 = 0.0f;
    // Lo que escribe 0x417f30 (la vtable + 8) con el A9 del cuervo (0x421db0, Scenes/PetInfo.h): el nivel en +0x1e2
    // (+0x1d6) y +0x450, la experiencia, la que sigue, la durabilidad del lugar 1 del registro propio (+0xbce) y lo que
    // sale del nivel y del liderazgo, como las del cuervo de 0x4221d0.
    std::uint16_t word1e2 = 0;
    std::uint32_t experience = 0;     // +0x448
    std::uint32_t nextExperience = 0; // +0x44c
    std::uint16_t level = 0;          // +0x450
    std::uint16_t durability = 0;     // +0x452
    std::uint16_t word454 = 0;
    std::uint16_t word456 = 0;
    std::uint16_t word458 = 0;
    std::uint16_t word45a = 0;
    // La posición antes del último paso, para dibujarlo entre dos pasos (D-C4); el salto junto al dueño
    // la pisa con la nueva.
    glm::vec3 previousPosition{};
    // +0x120: sus huesos como los dejó el último dibujo, en el mundo (0x602e90 con el segundo argumento
    // en 0). Antes del primero, vacío (Q-C133).
    std::vector<Render::Models::OriginalMatrix> pose;
};

// Lo que el paso lee del dueño: su posición (+0x10) y sus banderas (+0x78). Con 0x1000 el paso no hace
// nada; con 0x4000 no hay chispa. En la zona segura (+0x3e2 en 1), además, sus ángulos (+0x1c), sus huesos
// propios (+0x120, los del último dibujo; vacíos antes del primero, Q-C318) y la escala y la posición que
// tiene el modelo compartido de su tipo (+0x68 y +0x6c de 0x5769ad8 + tipo · 0xe0): las del último 0x20a que
// las copió (WorldState::PlayerModel, Q-C412). Con la mascota (+0x318) en 0x1a04, vuela a 350 (Q-C413).
struct DarkSpiritOwner
{
    glm::vec3 position{};
    std::uint32_t flags = 0;
    glm::vec3 angles{};
    std::uint16_t pet = 0;
    std::uint8_t safeZone = 0;
    std::span<const Render::Models::BoneTransform> pose{};
    float modelScale = 0.0f;
    glm::vec3 modelOrigin{};
};

// Lo que leen el aviso y los estados del ataque de los otros personajes del pool (CL9 hito 4): el de +8 (`target`),
// que con +0x1ad en 1 es el destino del vuelo en vez del dueño (0x4177b1..0x417811), y el de +0x3b0: si está entre
// los 400 (0x416a88..0x416a9b, con signo), sus banderas (+0x78, el 0x4000 de 0x416ab6), y su posición, que el
// estado 6 mira con +0x3b0 distinto de 0xffff (0x417847).
struct DarkSpiritTargets
{
    glm::vec3 target{};
    bool attackInPool = false;
    std::uint32_t attackFlags = 0;
    glm::vec3 attackPosition{};
};

// Lo que el paso pide afuera: el grito (0x41ed10(0x1d1)) y el A7 que corta el ataque (C1 07 A7 00 78 FF FF,
// Protocol::Pet::WritePetCommand).
struct DarkSpiritStepped
{
    bool shouts = false;
    bool stopsTheAttack = false;
};

struct DarkSpiritBirthAccesses
{
    std::uint8_t cellX = 0;
    std::uint8_t cellY = 0;
    std::int32_t counter = 0;
};

// 0x4167b0 con 0x415be0: los 12 rand() con las cuentas cerradas, de los cierres de las celdas del dueño y del contador
// (Q-C125), y el cuervo en el centro de la celda del dueño (con `and 0xff`), a la altura del terreno ahí
// (`groundHeight`, la de 0x5f93c0 en (celda · 100 + 50), que 0x415ea7 guarda en float) más 300. En el
// login, la celda (0, 0) (Q-C130).
[[nodiscard]] DarkSpirit CreateDarkSpirit(int cellX, int cellY, double groundHeight, Render::Models::CrtRandom& random,
                                          DarkSpiritBirthAccesses accesses = {});

// 0x416a20, con el esqueleto del cuervo, en el mapa `map`, con el rand() de la CRT y el valor que el grito lee de
// 0x7d3a0f0 (`counter`). Con 0x1000 en el dueño, nada. Primero el aviso (0x416a88..0x4176e6): con el blanco de
// +0x3b0 entre los 400 y con 0x4000, o con el dueño con 0x4000, en los estados 2, 5 y 6, +0x3b0 en 0xffff, el A7
// que corta el ataque (`stopsTheAttack`) y el estado 4 con la velocidad en 3: pasa por el 3 y la comparación con 50
// de la distancia al dueño lee el estado ya en 3 y no decide nada (Q-C1206). Después la zona segura (0x4176f0):
// dentro, si no está posado ni bajando, pasa a bajar (4) con la velocidad en 3 (Q-C414); fuera, posado o bajando
// vuelve al 0. Después la animación de a 0,4 y la acción por el estado al dar la vuelta (0x417f0c). El destino es el
// dueño o, con +0x1ad en 1, el blanco de +8 (`targets.target`). En los estados 0 y 1: +0x1ad en 0; la chispa desde
// un hueso al azar de `pose` (0x5c6, subtipo 5, en `particles`; sin pose, desde su posición); el giro hacia el
// destino a más de 150; el movimiento; el cabeceo hacia 350 sobre el destino en el mapa 0x37 o con la mascota en
// 0x1a04 y 250 en los demás (Q-C131, Q-C413); el sorteo de la dirección y el estado. En el 5 y el 2 (0x4179ef),
// avanza su velocidad; en el 5 gira hacia el destino 50 más arriba con 0x4e9810 y, a menos de 20 o con la lejanía
// en más de 20, pasa al 2 con el cabeceo en −45 (y +0x1ad en 0 salvo con la orden 3); sube la lejanía, la
// velocidad suma +0xf0 y +0xf0 suma 0,2; en el 2, a 250 o más del destino vuelve al 0 y frena 1; en los dos, la
// acción 3. En el 6 (0x417847), con +0x3b0 distinto de 0xffff, gira 40 hacia ese personaje. En el 3 (0x4178a2), en
// el hueso 37 del dueño con (−10, 0, 10) y sus ángulos, el rumbo menos 120. En el 4 (0x4178f4), avanza su
// velocidad, gira hacia el hueso 42 del dueño con 0x4e9810, acelera 1 y se posa a menos de 50. En el 5 y el 6,
// +0x38d sube hasta 15. En todos, el grito y el salto junto al dueño cuando el contador de lejanía pasa de 90.
[[nodiscard]] DarkSpiritStepped StepDarkSpirit(DarkSpirit& spirit, const Render::Models::Skeleton& skeleton,
                                               const DarkSpiritOwner& owner, std::uint8_t map,
                                               World::Particles& particles, float worldClock,
                                               Render::Models::CrtRandom& random, std::int32_t counter,
                                               const DarkSpiritTargets& targets = {});

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
