#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
}
namespace Client::World
{
class FrameSprites;
class ModelEffects;
struct FishEffectRequest;
struct TerrainLighting;
struct WorldObject;
} // namespace Client::World

// Lo que 0x606560 hace en Icarus (el mapa 10) fuera de 0x604ab0: el preludio 0x6061c0 (con 0x605d70) antes
// de los objetos y 0x6063d0 con cada objeto visto. Contrato: 06 § Los objetos de Icarus.
namespace Client::Scenes
{
struct IcarusObjectServices
{
    Render::Models::CrtRandom& random;
    // +0x10 del héroe (0x743a170).
    const glm::vec3& heroPosition;
    // La luz dinámica durable (0x7b4ccc4).
    World::TerrainLighting* lighting = nullptr;
    // La tabla de 0x518cf0.
    World::ModelEffects* effects = nullptr;
    // 0x5491e0.
    World::FrameSprites* sprites = nullptr;
    // 0x530860.
    std::function<Core::Expected<void, std::string>(const World::FishEffectRequest&)> createJoint;
};

// 0x6061c0 en el mapa 10, con `count` el 0x7d365a0 del pase anterior (todavía sin poner en cero):
// 0x605d70 da el selector y, con rand() % 10 == 0, el efecto 0x5ac a ±2.500 del héroe en x y en y
// (rand() % 5.000 − 2.500 en entero, cada uno sumado en la x87) y 1.000 más abajo, sin giro, luz 1.
// 0x605d70, con rand() % 50 == 0: una luz dinámica de radio 2 en (x del héroe + rand() % 300 − 150; y del
// héroe), con L = ((rand() & 3) + 4) · 0,05f y el color (L · 0,3f; L · 0,3f; L · 0,081f) (un rand() se
// descarta en el medio), el efecto 0xbb en el héroe con ese color, el selector rand() % count si count
// no es cero, y con rand() % 5 == 0 dos uniones 615/0 a uno de cuatro lados (rand() & 3), de ancho
// rand() % 10 + 40. Devuelve el selector (0 sin la primera puerta).
[[nodiscard]] Core::Expected<std::int32_t, std::string> StepIcarusObjectPrelude(std::int32_t count,
                                                                                const IcarusObjectServices& services);

// 0x6063d0 en el mapa 10: `count` (0x7d365a0) sube uno; con el selector en cero, nada más. Si no, con
// rand() % 10 == 0 y el tipo del 0 al 5, un sprite 0x623 de escala 0,5 en el objeto con la luz
// ((rand() % 10) · 0,02f) por canal y dos uniones 614/6 del objeto, de ancho rand() % 20 + 10; si no, count
// vuelve a bajar.
[[nodiscard]] Core::Expected<void, std::string> StepIcarusSeenObject(World::WorldObject& object, std::int32_t selector,
                                                                     std::int32_t& count,
                                                                     const IcarusObjectServices& services);
} // namespace Client::Scenes
