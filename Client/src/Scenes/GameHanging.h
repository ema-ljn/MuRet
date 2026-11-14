#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/Models/HalfLifeMath.h"
#include "Scenes/GameDrawTail.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <span>
#include <string>

namespace Client::Render
{
class ModelBatch;
namespace Models
{
class CrtRandom;
}
} // namespace Client::Render

namespace Client::World
{
class FrameSprites;
class Particles;
} // namespace Client::World

// Lo que cuelga de un jugador en el dibujo del juego (0x50ba30, de 0x50db57 a 0x50e9fa): el arma a la
// espalda, la capa de los GM, las alas, Helper02 y el lazo de las manos, cada cosa salvo la capa con 0x509340 (posada
// sobre el buffer global de la pose, dibujada con 0x60b3d0 y con los sprites de su modelo). Lo que pide un efecto o un
// joint que MuRet no tiene corta con su dirección. Contrato: 06 § El dibujo del juego, "Lo que cuelga en
// el juego".
namespace Client::Scenes
{
class IModelLibrary;
struct ActorState;
struct FaunaDrawCarry;
struct ItemDrawContext;

struct GameHangingPass
{
    Render::ModelBatch& batch;
    const IModelLibrary& models;
    // Los huesos del cuerpo de este frame: el buffer propio del personaje (+0x120).
    std::span<const Render::Models::BoneTransform> body;
    // El rand() global de 0x69fd70.
    Render::Models::CrtRandom& random;
    // El buffer global de la pose (0x68b181c) y la tabla de sprites del frame (0x5491e0). Hacen falta
    // solo si algo cuelga.
    FaunaDrawCarry* carry = nullptr;
    World::FrameSprites* sprites = nullptr;
    // Los joints de 0x530860 y las partículas de 0x53fe70 del frame. Hacen falta solo si algo los crea: el
    // 0x617 de la acción 0x8c (0x50a110), el 0x618/0x11 de 0xc1e, el 0x610/4 de 0x223 y el 0x614/10 de 0x224.
    // Los 0x610/4 y 0x618/0x11 guardan el personaje que cuelga como dueño (+0x54): un ActorState.
    std::span<JointState> joints{};
    World::Particles* particles = nullptr;
    // Los globales de 0x60b3d0 que presta el dueño del frame (sin él, los de un ItemDrawContext vacío).
    const ItemDrawContext* itemContext = nullptr;
    Core::Duration frameClock{};
    // DAT_05767b88: el reloj del mundo en ms, en float.
    float worldClock = 0.0f;
    std::int32_t map = 0; // 0x6c9dd0
    // +0x1a1 del héroe (0x743a170), que 0x5ffdf0 lee en los mapas 18 a 23.
    std::uint8_t heroByte1a1 = 0;
    // 0x4294b0, el byte del asedio (0x7239ec), que 0x4142f0 mira con las capas 6 a 13.
    bool siegeActive = false;
    std::int32_t logicSteps = 0; // 0x7d3a0f0
    // En un frame del original avanza la animación de lo que cuelga; entre frames, la dibuja como quedó.
    bool originalFrame = true;
};

// 0x50db57..0x50e9fa sobre un 0x20a ya dibujado (cuerpo y tela): el rand() de la luz de lo que cuelga,
// la espalda (un arma o el carcaj del hueso 47 con su ángulo), la capa de los GM (MuSign en el hueso 20,
// que crea y avanza en +0x360 y +0x364), las alas y Helper02 (fuera de los mapas 18 a 23), y fuera de la
// espalda las dos manos, cada una con +0x38e abierto y cerrado antes. Escribe el
// hueso, la acción, la velocidad y la animación de cada parte que cuelga, como el original, y +0x3b4 ya
// tiene que tener la luz de lo que cuelga (0x50db94). Con un monstruo o un NPC (0x50dcc2 salta a 0x50e060), solo el
// rand(), la espalda (que corta lo que sigue) y las manos, sin el efecto de +0x78 & 0x800, que es del 0x20a; `body`
// son sus huesos propios.
[[nodiscard]] Core::Expected<void, std::string> DrawGameHanging(ActorState& actor, const GameHangingPass& pass);

// 0x509340 desde lo que 0x50ba30 agrega por el +0x376 de un monstruo (0x50c79a, 0x50c803 y 0x50c8b8): el modelo del
// lugar de las alas (+0x2f8), que quien llama ya armó (el modelo, el hueso, las dos acciones y la velocidad), sin
// nivel ni excelente. Sin `placed`, desde su hueso del cuerpo con `offset`; con él, por el lugar de 0x5093d2 (el de
// las estatuas 0x84 a 0x86: el 0xc1c, el 0x225 y el 0xa24; otro modelo corta en 0x5095e0). `bodyScale` es la escala
// con que se posó el cuerpo (+0x68 del modelo del tipo, que lee 0x4ec190), y la del personaje, la del modelo que
// cuelga. Con el 0xef, su origen y su giro (0x5096ac). Saca los rand() de 0x509340 (los de 0x607b60, el cierre de
// +0x38e y los de los sprites del modelo), no el de 0x50db57; `pass.body` son los huesos propios del personaje. Entre
// frames del original no avanza la animación de lo que cuelga.
[[nodiscard]] Core::Expected<void, std::string> HangFromTheWingsSlot(ActorState& actor, const GameHangingPass& pass,
                                                                     const glm::vec3& offset, bool placed,
                                                                     float bodyScale);
} // namespace Client::Scenes
