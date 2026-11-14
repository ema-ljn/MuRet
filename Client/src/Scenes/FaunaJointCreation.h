#pragma once

#include "Core/Expected.h"
#include "Scenes/GameDrawTail.h"
#include "World/IFishStepServices.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
struct ClothWorld;
} // namespace Client::Render::Models

namespace Client::World
{
class FrameSprites;
class Particles;
struct TerrainLighting;
} // namespace Client::World

// 530860: ownerContext es FaunaObject durable salvo613/4, 614/10 y614/14, donde permanece opaco, y el 618/0x11 y el
// 610/4 de lo que cuelga de un jugador, el 60f/0, el 60f/0x12 y el 610/7 del conjunto completo, el 619/9 de su 5ac/3 y
// el 619/1 del calco de la mascota (0x518900), donde es el ActorState del personaje. Con `ownerKind` en Helper es el
// HelperState del caballo oscuro (0x444740), y en Raven el DarkSpirit del cuervo (0x418390 y 0x418400), sólo para el
// 60f/0 y el 60f/0x12: lee su luz (+0xf4) al nacer y su acción (+0x111) en el paso; con otro tipo corta antes de
// escribir (0x530860).
// Se escribe sobre el primer muerto sin reconstruirlo; pool lleno no lee los argumentos ni RNG.
namespace Client::Scenes
{
// El 618/0 y el 618/1 que el que mató lanza desde el blanco (0x505169, 0x5051a4): con el ActorState del que pega como
// dueño, giran 20 pasos y después vuelven hacia él (0x538313), con la luz dinámica, el rayo 0x5d5/0 de cada paso y, al
// llegar, el 0x5d5/1 y el sonido 0x23.
inline constexpr std::int32_t KillBeamJoint = 0x618;

[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateFaunaJoint(std::span<JointState> joints, const World::FishEffectRequest& request,
                 Render::Models::CrtRandom& random, JointOwnerKind ownerKind = JointOwnerKind::ByType);
// 535840: 697/0 y618/4 siguen el+40 del dueño;610/8 sigue position con z300;
// 10f/3 usa vida/RNG, seno y color del dueño.614/10 y 614/14 giran e iteran su cola sin leer al dueño;
// el 618/0x11 sigue el +0x40 del personaje y el 610/4 va de su +0x17c a su posición; el 60f/0 y el 60f/0x12 giran
// alrededor de su destino y, según la acción del dueño (la del personaje o la del caballo), pasan al 60f/1, que sólo
// se apaga; con el ancho en 40 o más no la leen (el 60f/0 del caballo nace con 40); el 610/7 se mece desde
// su partida y el 619/9 gira alrededor de la posición del personaje, leída en vivo; el 619/1 acelera y gira hacia el
// héroe, 120 más arriba, y en su última vida deja el sprite 0x5fd (pide el RNG y los sprites);
// sólo la llegada con width50 necesita particles, y el humo usa worldClock.
// El overload sin RNG conserva la frontera535f18 tras movimiento y comprobación del dueño. El 615/2 de Chaos
// Castle pide los sprites del frame (0x5491e0) y las partículas.
[[nodiscard]] Core::Expected<void, std::string> StepJoints(std::span<JointState> joints);
// El 618/0 y /1 piden además la luz dinámica (`lighting`, 0x7b4ccc4) y el sonido 0x23 (0x41ed10) al llegar.
[[nodiscard]] Core::Expected<void, std::string>
StepJoints(std::span<JointState> joints, Render::Models::CrtRandom& random, Render::Models::ClothWorld* cloth = nullptr,
           World::Particles* particles = nullptr, float worldClock = 0, World::FrameSprites* sprites = nullptr,
           World::TerrainLighting* lighting = nullptr, const std::function<void(std::int32_t)>& soundRequest = {});
} // namespace Client::Scenes
