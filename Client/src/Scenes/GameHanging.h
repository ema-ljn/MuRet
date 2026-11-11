#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/Models/HalfLifeMath.h"

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
}

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
    // Los globales de 0x60b3d0 que presta el dueño del frame (sin él, los de un ItemDrawContext vacío).
    const ItemDrawContext* itemContext = nullptr;
    Core::Duration frameClock{};
    // DAT_05767b88: el reloj del mundo en ms, en float.
    float worldClock = 0.0f;
    std::int32_t map = 0;        // 0x6c9dd0
    std::int32_t logicSteps = 0; // 0x7d3a0f0
    // En un frame del original avanza la animación de lo que cuelga; entre frames, la dibuja como quedó.
    bool originalFrame = true;
};

// 0x50db57..0x50e9fa sobre un 0x20a ya dibujado (cuerpo y tela): el rand() de la luz de lo que cuelga,
// la espalda (un arma o el carcaj del hueso 47 con su ángulo), la capa de los GM (MuSign en el hueso 20,
// que crea y avanza en +0x360 y +0x364), las alas y Helper02 (fuera de los mapas 18 a 23), y fuera de la
// espalda las dos manos, cada una con +0x38e abierto y cerrado antes. Escribe el
// hueso, la acción, la velocidad y la animación de cada parte que cuelga, como el original, y +0x3b4 ya
// tiene que tener la luz de lo que cuelga (0x50db94).
[[nodiscard]] Core::Expected<void, std::string> DrawGameHanging(ActorState& actor, const GameHangingPass& pass);
} // namespace Client::Scenes
