#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// El 0x628, la calavera (Skill\Skull.jpg como sprite 0x628; el efecto no tiene modelo ni caso en 0x52d620): su alta
// (0x51ee23) y su paso (0x52b59b), y lo que hacen sus llamadores con la bandera 0x40 (la skill 0x37). 06, "Los efectos
// con modelo de los tipos altos".
namespace Client::Render::Models
{
class CrtRandom;
} // namespace Client::Render::Models

namespace Client::World
{
struct ModelEffect;
} // namespace Client::World

namespace Client::Scenes
{
class WorldState;
struct ActorState;
struct HighEffectStepContext;

inline constexpr std::int16_t SkullEffect = 0x628;

// 0x51ee23..0x51ee95: vida 1.000; el /1, 940; el /2, 8 y la z 150 más; el /4, 20, +0xd8 en 2 y la velocidad
// (0, −10, 0). Sin rand().
void BeginTheSkull(World::ModelEffect& effect);

// 0x52b59b..0x52b8a1. El /4 cae (la velocidad y baja +0xd8, la x de los ángulos 1) y deja el sprite 0x628 de escala 10
// con su luz. Los demás miran a su dueño (+0x108, un personaje): muerto, la vida en 0 y el byte vivo en 0. Vivo, por
// subtipo (la tabla de 0x52d18c): el /0, con el bit 0x40 de su propio +0x78, la vida en 10 y, con 0x4000 también,
// +0x16c en 0 y nada más; si no, como el /1 y el /3, tres vueltas con la partícula 0x5de/5 y el sprite 0x628 girando
// sobre el dueño; el /2, el sprite 0x628 de escala 1,5f con la luz vida · 0,1f; el /5, tres partículas 0x5ac/5 apiladas
// que giran. Sin rand(). Corta en la primera partícula que MuRet no tiene, con lugar en la tabla (CL11).
[[nodiscard]] Core::Expected<void, std::string> StepTheSkull(World::ModelEffect& effect,
                                                             const HighEffectStepContext& context);

// 0x518ac0(0x628, personaje, 0) y 0x518cf0(0x628, +0x10, +0x1c, (1, 1, 1), 0, personaje, −1, 0, 0, 0, 0): apaga el
// 0x628/0 del personaje y crea otro con él de dueño (con la tabla llena, ninguno). Sin rand(). `address` es la del
// 0x518cf0 del llamador, para el error.
[[nodiscard]] Core::Expected<void, std::string> PutTheSkullOn(ActorState& actor, WorldState& world,
                                                              Render::Models::CrtRandom& random, std::uint32_t address);
} // namespace Client::Scenes
