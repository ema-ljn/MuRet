#pragma once

#include "Core/Expected.h"
#include "Scenes/SceneId.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
}
namespace Client::World
{
class Particles;
}

// CL9 hito 3, fase 2a: el paso del que se muere (+0x389 distinto de 0), 0x4fb9e0(c, o, b), que el cuerpo de los
// personajes (0x4fd5f0) llama en 0x4fd722. Contrato: Client/docs/research/06-render-escenas-y-ui.md, "Morirse".
// *Estático*.
namespace Client::Scenes
{
struct ActorState;

struct ActorDyingContext
{
    std::int32_t map = 0;
    // DAT_006d5dc8: las burbujas del mapa 7 son solo de la escena 5.
    SceneId scene = SceneId::Game;
    // c == [0x743a170]: el héroe se hunde pero no se borra.
    bool hero = false;
    Render::Models::CrtRandom& random;
    World::Particles* particles = nullptr;
    float worldClock = 0.0f;
};

// 0x4fb9e0: apaga 0x1000, 0x4000 y 0x20 de +0x78; +0x3c8 sube 0,02 por paso (0,05 el 0x172 en el mapa 7); desde 1, el
// alpha es 2 − +0x3c8 y, mientras no baje de 0,01, la Z baja 0,4; debajo, el que no es el héroe se borra (+0 en 0 y
// +0x391, +0x1a1 y +0x1bc en 0; 0x401040 y las telas de 0x4fb1a0 no tienen nada que soltar en el mundo de MuRet,
// P-C161). Después, el vuelo del empuje: en Blood Castle (11 a 17) con +0x1a1, 0x4fb2b0; con la skill 0x28 o 0x3b
// (fuera de los castillos), antes del paso 15, 0x51fd20 con +0xd0 que baja +0xd8 y +0xd8 que sube 1. Las burbujas
// 0x607 del mapa 7 en la escena 5 (cuatro, con tres rand() cada una). Corta, con lo de antes hecho, en el 0x192 (que
// lee +0x60 del objeto), en Chaos Castle (las chispas de 0x51fe10 y el vuelo de 0x4fb2b0 con +0x1a1) y en las diez
// partículas 0x5ac de la skill 0x28 hasta el paso 30 (CL11).
[[nodiscard]] Core::Expected<void, std::string> StepActorDying(ActorState& actor, std::size_t slot,
                                                               const ActorDyingContext& context);

// 0x4fb2b0(c, o): el vuelo del que muere en un castillo: (0, +0xd0, 0) girado por +0x1a4 (0x5fc8f0 y 0x5fcbd0) desde
// +0x1b0, +0xd0 más +0xcc, +0xe4 más +0xd8, +0xd8 menos +0xd4, el ángulo X menos 5 y la Z en +0x1b8 + +0xe4.
void FlyFromTheCastle(ActorState& actor);

// 0x51fd20(o, o + 0x28): la posición más (+0xcc, +0xd0, +0xd4) girado por los ángulos de +0x28 (los de la cabeza y
// +0x30).
void FlyFromThePush(ActorState& actor);
} // namespace Client::Scenes
