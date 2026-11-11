#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
}

// Lo de los mapas de Chaos Castle (18 a 23) que no depende de sus paquetes: lo que cada celda dibujada saca
// (0x405e00) y el alpha de 0x5ffdf0. Contrato:
// Client/docs/research/06-render-escenas-y-ui.md, "Los objetos de Chaos Castle".
namespace Client::World
{
class Particles;
struct FishEffectRequest;
struct WorldObject;

// Un rectángulo de celdas de 0x405e00, con los bordes incluidos: x0, y0, x1, y1.
struct ChaosCastleCellRect
{
    std::int32_t x0 = 0;
    std::int32_t y0 = 0;
    std::int32_t x1 = 0;
    std::int32_t y1 = 0;
};

// 0x6b86d4, 0x6b8714 y 0x6b8754: los cuatro rectángulos de las áreas 0, 3 y 6 de 0x6c634c.
inline constexpr std::array<ChaosCastleCellRect, 4> ChaosCastleAreaZero{
    {{23, 75, 44, 76}, {43, 77, 44, 108}, {23, 107, 42, 108}, {23, 77, 24, 106}}};
inline constexpr std::array<ChaosCastleCellRect, 4> ChaosCastleAreaThree{
    {{25, 77, 42, 78}, {41, 79, 42, 106}, {25, 105, 40, 106}, {25, 79, 26, 104}}};
inline constexpr std::array<ChaosCastleCellRect, 4> ChaosCastleAreaSix{
    {{27, 79, 40, 80}, {39, 81, 40, 104}, {27, 103, 38, 104}, {27, 81, 28, 102}}};

// 0x53fe70(0x5f6, ...): la piedra que cae en la celda de 0x405e00.
inline constexpr std::int32_t ChaosCastleFallingStone = 0x5f6;

// Lo que 0x405e00 lee y escribe fuera de la celda. Quien llama lo presta durante el pase.
struct ChaosCastleCellContext
{
    // 0x6c634c: el área (0xff en la .data y al armar el objeto de evento; la mueven los paquetes de la CL9.5).
    std::uint8_t area = 0xff;
    // +0x18 del héroe (0x743a170): la z de la piedra.
    float heroZ = 0.0f;
    Render::Models::CrtRandom* random = nullptr;
    Particles* particles = nullptr;
    std::reference_wrapper<const float> worldClock;
    // 0x7d325c8, el temblor de la cámara.
    float* cameraShake = nullptr;
};

// 0x405e00(x, y) en los mapas 18 a 23: rand() % 8 y, con 0 y el área en 0, 3 o 6, el primer rectángulo que
// tiene la celda: la piedra 0x5f6/0 en (x · 100 + rand() % 30 − 15; y · 100 + rand() % 30 − 15; z del héroe),
// ángulos en 0, luz 1, escala 1,5 y sin dueño, y con rand() % 5 en 0 el temblor en (rand() % 3 − 3) · 0,1f.
// Con otra área o fuera de los rectángulos, nada más. Corta sin el azar, sin el pool o sin la cámara cuando
// los lee.
[[nodiscard]] Core::Expected<void, std::string> StepChaosCastleCell(std::uint8_t x, std::uint8_t y,
                                                                    const ChaosCastleCellContext& context);

inline constexpr std::int32_t FirstChaosCastleMap = 18;
inline constexpr std::int32_t LastChaosCastleMap = 23;

[[nodiscard]] constexpr bool IsChaosCastleMap(std::int32_t map)
{
    return map >= FirstChaosCastleMap && map <= LastChaosCastleMap;
}

// Lo que 0x405490 mira del héroe (0x743a170) y la articulación 0x6ee/9 que pide (0x530860).
struct ChaosCastlePreludeContext
{
    Render::Models::CrtRandom* random = nullptr;
    glm::vec3 heroPosition{};
    glm::vec3 heroAngles{};
    // +0x3e2 del héroe: la zona segura.
    bool heroInSafeZone = false;
    std::function<Core::Expected<void, std::string>(const FishEffectRequest&)> createJoint{};
};

// 0x405490 en los mapas 18 a 23, primero en 0x6061c0, con la cuenta de los 3 vistos del pase anterior
// (0x7d365a0): con rand() % 10 en 0 y la cuenta distinta de 0, el selector es rand() % cuenta. Si no, con el
// héroe fuera de la zona segura y otro rand() % 10 en 0, la articulación 0x6ee/9 en (x + rand() % 800 − 400;
// y + rand() % 800 − 400; z − 150) del héroe, con sus ángulos, sin dueño y la escala rand() % 10 + 50. Devuelve el
// selector, que sin el primer caso queda en 0. Corta sin el azar o sin quien cree la articulación.
[[nodiscard]] Core::Expected<std::int32_t, std::string> StepChaosCastlePrelude(std::int32_t count,
                                                                               const ChaosCastlePreludeContext& context);

// 0x4055b0 por cada objeto visto, después de 0x604ab0: solo el 3, que suma 1 a la cuenta. Con la trampa armada
// (0x6c634d, 1 en la .data; solo el paquete de 0x404e20 la vuelve a armar) prende +0x8a, deja +0x60 en 10 y la
// desarma. Si no, con el selector distinto de 0 lo baja: +0x60 = 10 y +0x8a en 0, o en 1 si llegó a 0.
void StepChaosCastleSeenObject(WorldObject& object, std::int32_t& selector, std::int32_t& count,
                               std::uint8_t& trapArmed);

// 0x5ffdf0 (0x5ffdff..0x5ffe24): en los mapas 18 a 23, con +0x1a1 del héroe prendido, el alpha del objeto baja
// 0,15f (0x6b8ae8) con fsub y fstp, antes de compararlo con 0,01; queda guardado en el objeto. Si no, igual.
[[nodiscard]] float ChaosCastlePoseAlpha(std::int32_t map, std::uint8_t heroByte1a1, float alpha);
} // namespace Client::World
