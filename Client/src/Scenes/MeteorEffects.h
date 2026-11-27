#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"

#include <cstdint>
#include <optional>
#include <string>

// El 0x13f que suelta el 0x1a9 (Monster87) en su acción 4 (0x442cb9) y los dos que deja al apagarse: el 0x13d, la
// piedra que late, sacude la cámara, tira piedritas y echa chispas por sus huesos, y el 0x13e, su cáscara. Sus altas
// (0x51efdc y 0x51c727) y sus pasos (0x525f6a, 0x5260bd; el 0x13e, sólo la cola). Los dos de abajo se dibujan por
// 0x442e10 (0x44357f y 0x4435e9, DrawTheMeteorBody). 06, "Los efectos con modelo de los tipos altos".
namespace Client::Render
{
class ModelBatch;
} // namespace Client::Render

namespace Client::Models
{
class IModelLibrary;
struct Skeleton;
} // namespace Client::Models

namespace Client::World
{
struct ModelEffect;
} // namespace Client::World

namespace Client::Actors::Drawing
{
struct DrawnObject;
} // namespace Client::Actors::Drawing

namespace Client::Scenes
{
struct HighEffectStepContext;

inline constexpr std::int16_t MeteorEffect = 0x13d;
inline constexpr std::int16_t MeteorShellEffect = 0x13e;
inline constexpr std::int16_t MeteorCallEffect = 0x13f;

// 0x51c727 (el 0x13d y el 0x13e: vida 120 y escala 0,8f) y 0x51efdc (el 0x13f: vida 20). Cualquier subtipo; sin rand().
void BeginTheMeteor(World::ModelEffect& effect);

// 0x525f6a..0x5260b8, el 0x13f. Con vida 8, dieciséis joints 0x60f/0x18 sin dueño, de ancho 90 (0x526028): cada uno con
// los ángulos (−10, 0, i · 10), en la posición con la z 100 más y la x más rand() % 600 − 200 y la y más rand() % 600 −
// 400. Con vida 2, los ángulos (0, 0, rand() % 360), el 0x13d (0x526090) y el 0x13e (0x5260ad) en su posición, con su
// luz y él de dueño (clave −1, escala 0), y el lugar libre (la cola sigue).
[[nodiscard]] Core::Expected<void, std::string> StepTheMeteorCall(World::ModelEffect& effect,
                                                                  const HighEffectStepContext& context);

// 0x5260bd..0x526337, el 0x13d:
// - la luz dinámica (v, v, v + 0,1) de radio 10 (0x5260fe), con v = FSIN(vida · π/120) · −0,5;
// - el temblor de la cámara en ((rand() & 3) − 2) · 0,1f (0x526126);
// - si rand() & 1 da 0, la piedrita 0xc8 o 0xc9 (0x526207): rand() % 150 + 300 adelante, girado rand() % 360 en z,
//   y el tipo con rand() & 1 (cinco rand() más los de la piedrita);
// - la pose del modelo 0x13d (0x4eb860) en el buffer global y, por cada hueso que no es dummy, rand() % 12; con 0, en
//   su punto (0x4ec110 con la escala y el origen del modelo) la partícula 0x6ec/3 con él de dueño (0x5262e8, de otro
//   lote: con lugar corta) y la 0x5f2/0x15 (0x526307).
// Sin la luz dinámica o el temblor en los servicios corta, después de los rand() de antes.
[[nodiscard]] Core::Expected<void, std::string>
StepTheMeteor(World::ModelEffect& effect, const Models::Skeleton* skeleton, const HighEffectStepContext& context);

// 0x6002e0 → 0x442e10 con el 0x13d (0x44357f) y el 0x13e (0x4435e9), después de la pose y de mirar la malla oculta −2:
// entre 0x4ecc10 y 0x4ecc20, sin RenderBody, el color del modelo en (1, 1, 1) y 0x4ecc30 sobre la malla 0 con las
// banderas 2, el alpha (+0x174) y la luz 1, sobre las mallas posadas (`object.retainedMeshes`) o, sin ellas, las que
// posa acá como 0x5ffdf0 (nada con el alpha por debajo de 0,01 o sin el modelo o su esqueleto):
// - el 0x13d, dos pases con la U y la V del efecto (+0x6c, +0x70): con el blendMesh −1 y su textura, y con el 0 y la
//   textura 0x63c;
// - el 0x13e, uno con el blendMesh 0 y su textura, la V del efecto y la U en FSIN(T · 0,00008f) · 2,5f, que queda en
//   +0x6c; después +0x6c vuelve a 1. T es el reloj del mundo (0x5767b88): sin él, corta.
// Devuelve true si es uno de los dos; false con otro tipo (sigue el pase común).
[[nodiscard]] Core::Expected<bool, std::string>
DrawTheMeteorBody(Render::ModelBatch& batch, const Models::IModelLibrary& models, World::ModelEffect& effect,
                  const Actors::Drawing::DrawnObject& object, Core::Duration frameClock,
                  std::optional<float> worldClock);
} // namespace Client::Scenes
