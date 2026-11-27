#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <string>

// El 0x628, la calavera (Skill\Skull.jpg como sprite 0x628; el efecto no tiene modelo ni caso en 0x52d620): su alta
// (0x51ee23) y su paso (0x52b59b), y lo que hacen sus llamadores con la bandera 0x40 (la skill 0x37). 06, "Los efectos
// con modelo de los tipos altos".
namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::World
{
struct ModelEffect;
} // namespace Client::World

namespace Client::Kernel
{
class WorldState;
struct ActorState;
} // namespace Client::Kernel

namespace Client::Scenes
{
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

// 0x52b795..0x52b829: el punto de la vuelta `turn` (0 a 2) del /5 con la vida `life` del efecto y T (`worldClock`,
// 0x5767b88): el ángulo turn · 2π/3 + vida · 0,17f + T · 0,003f, a 50 del dueño en x (FSIN) y en y (FCOS), con el
// perfil PC53, y a (turn + 1) · su escala · 50 sobre su z. Ahí nace su 0x5ac/5.
[[nodiscard]] Core::Expected<glm::vec3, std::string> SkullStackPoint(int turn, std::int32_t life, float worldClock,
                                                                     const Kernel::ActorState& owner);

// 0x518ac0(0x628, personaje, 0) y 0x518cf0(0x628, +0x10, +0x1c, (1, 1, 1), 0, personaje, −1, 0, 0, 0, 0): apaga el
// 0x628/0 del personaje y crea otro con él de dueño (con la tabla llena, ninguno). Sin rand(). `address` es la del
// 0x518cf0 del llamador, para el error.
[[nodiscard]] Core::Expected<void, std::string> PutTheSkullOn(Kernel::ActorState& actor, Kernel::WorldState& world,
                                                              Crt::CrtRandom& random, std::uint32_t address);
} // namespace Client::Scenes
