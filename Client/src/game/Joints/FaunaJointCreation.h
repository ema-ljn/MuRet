#pragma once

#include "Core/Expected.h"
#include "Kernel/DrawTailState.h"
#include "Joints/JointKernels.h"
#include "Kernel/JointRequest.h"
#include "Audio/SoundId.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
struct ClothWorld;
class IModelLibrary;
} // namespace Client::Models

namespace Client::Terrain
{
struct TerrainLighting;
} // namespace Client::Terrain

namespace Client::Effects
{
class ModelEffects;
} // namespace Client::Effects

namespace Client::Particles
{
class FrameSprites;
class Particles;
} // namespace Client::Particles

// 530860: ownerContext es FaunaObject durable salvo613/4, 614/10 y614/14, donde permanece opaco, y el 618/0x11 y el
// 610/4 de lo que cuelga de un jugador, el 60f/0, el 60f/0x12 y el 610/7 del conjunto completo, el 60f/0 y el 60f/0xa
// del premio de una misión (0x4ddb80), el 619/9 de su 5ac/3, el 619/1 del calco de la mascota (0x518900) y el 619/8 de
// la skill cargada (0x5189f0), donde es el ActorState del personaje, al igual que618/2y/3 del altarCrywolf. Con
// `ownerKind` en Helper es el HelperState del caballo oscuro (0x444740), y en Raven el DarkSpirit del cuervo (0x418390
// y 0x418400), sólo para el 60f/0 y el 60f/0x12: lee su luz (+0xf4) al nacer y su acción (+0x111) en el paso; con otro
// tipo corta antes de escribir (0x530860). ModelEffect identifica al dueño durable de619/1 y610/13, sin
// aliasActorState. Pool lleno no lee los argumentos ni RNG.
namespace Client::Joints
{

// El 618/0 y el 618/1 que el que mató lanza desde el blanco (0x505169, 0x5051a4): con el ActorState del que pega como
// dueño, giran 20 pasos y después vuelven hacia él (0x538313), con la luz dinámica, el rayo 0x5d5/0 de cada paso y, al
// llegar, el 0x5d5/1 y el sonido 0x23.
inline constexpr std::int32_t KillBeamJoint = 0x618;

[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateFaunaJoint(std::span<Kernel::JointState> joints, const Kernel::JointRequest& request, Crt::CrtRandom& random,
                 Kernel::JointOwnerKind ownerKind = Kernel::JointOwnerKind::ByType,
                 const JointKernels::CreationServices* services = nullptr);
// 535840: 697 y618/4 siguen el+40 del dueño;610/8 sigue position con z300;
// 10f/3 usa vida/RNG, seno y color del dueño.614/10 y 614/14 giran e iteran su cola sin leer al dueño;
// el 618/0x11 sigue el +0x40 del personaje y el 610/4 va de su +0x17c a su posición; el 60f/0, el 60f/0xa (que baja) y
// el 60f/0x12 giran alrededor de su destino y, según la acción del dueño (la del personaje o la del caballo), pasan al
// 60f/1, que sólo se apaga; con el ancho en 40 o más no la leen (el 60f/0 del caballo y los del premio nacen con 40);
// el 610/7 se mece desde su partida y el 619/9 gira alrededor de la posición del personaje, leída en vivo; el 619/1
// acelera y gira hacia el héroe, 120 más arriba, y en su última vida deja el sprite 0x5fd (pide el RNG y los sprites);
// el 619/8 acelera, gira hacia su destino con más de 10 de vida, se apaga con menos de 6 y deja el sprite 0x5ac en cada
// paso (pide los sprites, sin el RNG);
// sólo la llegada con width50 necesita particles, y el humo usa worldClock.
// El overload sin RNG conserva la frontera535f18 tras movimiento y comprobación del dueño. El 615/2 de Chaos
// Castle pide los sprites del frame (0x5491e0) y las partículas. El615/7 de la llegada comparte geometría sin esas
// emisiones. Los618/2y/3 siguen los dos puntos del dueño y emiten5d5/0 con el seno del reloj.
[[nodiscard]] Core::Expected<void, std::string> StepJoints(std::span<Kernel::JointState> joints);
// El 618/0 y /1 piden además la luz dinámica (`lighting`, 0x7b4ccc4) y el sonido 0x23 (0x41ed10) al llegar; el
// 613/0 y el /5, la altura del terreno (`terrainHeight`, 0x5f93c0); el 0x10f/4 y el /9, los 400 personajes
// (`actors`, 0x743a16c); el 0x60f/0xb y el /0x19 con skill 1, el choque de la flecha dueña (`effectHit`, 0x520170);
// el 0x613/0x13, la tabla de efectos con modelo (`effects`, 0x7491fd8). De la CL11: el 0x610/6 del 0x95 de Kalima pide
// la luz dinámica; el 0x615/6 del pulso del asedio, la altura del terreno y las partículas; el 0x632/5, los sprites, la
// altura del terreno y la tabla de efectos (la onda 0x6eb/4 al llegar al suelo); el 0x60f/4, /0xc y el 0x62b/4, /0xc
// dan otro paso entero mientras la vida no sea múltiplo de 12 (0x53c3c6); el 0x6e8/0 y /1 no se mueven (la lista de
// 0x53585e) y el 0x61c/1 sigue a su efecto de dueño. De la sexta vuelta: el 0x5ac agrega sus tramos sin moverse con
// el paso común; el 0x6ee/0x12 le escribe al 0x125 dueño el alpha y la posición (0x539494); la /0xe y la /0xf del
// 0x60f y del 0x62b con dueño piden el RNG; el 0x61c/0, el RNG y las partículas (y corta con dueño donde busca al
// héroe, 0x53b548); el 0x14f, el RNG, la luz dinámica con ancho de 50 o más y las partículas al llegar con 50. De la
// séptima: el 0x613 y el 0x6ee /0xb, /0xc, /0xe, /0xf, /0x10 y /0x12 piden el RNG y las partículas, el 0x613/0xf le
// escribe al 0x126 dueño los ángulos, el alpha y la posición (0x539249) y el 0x61c/0 con dueño pide el héroe (`hero`,
// [0x743a170], 0x53b548): sólo con él de dueño corta. De la octava: el 0x614/0xc y el 0x62c/5..7 con dueño piden los
// modelos compartidos (`world`, 0x5769ad8), y el 0x62c/5..7, los sprites. El 0x613/0, /5 y /0x13 de ancho 80 sueltan
// el 0xd0 en la tabla de efectos y piden `volley` (el asedio, los atributos de 0x5f82c0 y los blancos del héroe,
// 0x539a50). De la novena: el 0x61c/0 del héroe pide también `volley` (0x53b569); el 0x632/0 y /1 con dueño, al llegar
// al suelo, posan el 0x20b de `models` con el modelo compartido de `world` y sueltan las cáscaras 0x137 de sus vértices
// en la tabla de efectos (0x53c1c6).
[[nodiscard]] Core::Expected<void, std::string>
StepJoints(std::span<Kernel::JointState> joints, Crt::CrtRandom& random, Models::ClothWorld* cloth = nullptr,
           Particles::Particles* particles = nullptr, float worldClock = 0, Particles::FrameSprites* sprites = nullptr,
           Terrain::TerrainLighting* lighting = nullptr, const std::function<void(Audio::SoundId)>& soundRequest = {},
           const std::function<double(float, float)>& terrainHeight = {},
           std::span<const Kernel::ActorState> actors = {}, const JointKernels::EffectHit& effectHit = {},
           Effects::ModelEffects* effects = nullptr, const Kernel::ActorState* hero = nullptr,
           Kernel::WorldState* world = nullptr, const JointKernels::VolleyServices* volley = nullptr,
           const Models::IModelLibrary* models = nullptr);
} // namespace Client::Joints
