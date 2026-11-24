#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Render::Models
{
struct Skeleton;
} // namespace Client::Render::Models

// Los efectos con modelo del lote "switch" de la CL11 (0x518cf0 y 0x5208a0): las piedras y las chispas de las skills
// de los magos y de los monstruos que no tienen su propio archivo. Cada alta y cada paso citan su dirección; lo que
// queda afuera (las partículas y los joints que MuRet todavía no crea) corta en la llamada que lo pide.
namespace Client::World
{
struct FishEffectRequest;
struct ModelEffect;
struct ModelEffectArguments;
struct ModelEffectOwner;
struct ModelEffectStepServices;
class ModelEffects;
class Particles;
class KalimaWorld;

// Lo que las altas de este lote piden afuera de su lugar: el registro propio (0x7681eb4: su profundidad, que gasta
// los 3.848 rand() del cierre, y su cuenta +0xf04), el héroe publicado (0x743a170), los blancos de 0x517a20 (el
// 0xf6 con el héroe de dueño), los joints de 0x530860 (`effectOwner` cuando el dueño es el efecto recién creado) y el
// sonido de 0x41ed10(id, 0, 0). Sin lo que una alta pide, corta en su dirección.
struct BlastEffectServices
{
    std::uint8_t* ownRecordDepth = nullptr;
    std::uint8_t* ownRecordCount = nullptr;
    const void* hero = nullptr;
    std::function<Core::Expected<void, std::string>(const ModelEffect&, float)> skillHits{};
    std::function<Core::Expected<void, std::string>(const FishEffectRequest&, bool effectOwner)> joint{};
    std::function<void(std::int32_t)> sound{};
};

// Los tipos cuya alta da CreateBlastModelEffect: 0xe4, 0xe5/0xe6 con el subtipo 5, 0xe7..0xe9, 0xec, 0xf2..0xf6,
// 0x105/1 (el hijo del 0x106), 0x106..0x109, 0x10c, 0x10d, 0x10f, 0x112, 0x113 y 0x5de con los subtipos 1 y 2.
[[nodiscard]] bool IsBlastModelEffect(std::int16_t type, std::int32_t subtype);

// 0x518cf0 con `arguments` para uno de los tipos de IsBlastModelEffect: el prefijo de ModelEffects::Begin, `owner`
// (lo que el paso lee en vivo de +0x108) y el caso del tipo (0x518e66 el 0x106, que vuelve a 0x518d0a con el 0x105 de
// su subtipo; 0x5192ed el 0xf5; 0x5198cd el 0xf6; …). Devuelve el lugar del efecto pedido (el del 0x106, no el de su
// 0x105), o nada con la tabla llena, sin rand(). Otro tipo, o lo que el caso pide y MuRet no tiene, corta.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateBlastModelEffect(ModelEffects& effects, const ModelEffectArguments& arguments, const ModelEffectOwner& owner,
                       Crt::CrtRandom& random, const BlastEffectServices& services);

// Los efectos cuyo paso da StepBlastModelEffect: los de IsBlastModelEffect, con el 0x105 sólo en el subtipo 1.
[[nodiscard]] bool StepsAsBlastModelEffect(const ModelEffect& effect);

// 0x5208a0 sobre uno de StepsAsBlastModelEffect, después de su primer rand() (`lightDraw`, la luz local) y con
// `before`, la posición antes del paso: el caso del tipo y la cola de 0x52cb98 (la acción publicada y la animación
// de los BMD de [0xad, 0x151), 0x51fc80 con los ángulos o, el 0x10f, sin ellos; el 0xf3 y el 0xf4 sólo pierden la
// vida; los 0xe7..0xe9 y el 0x113 sin dueño salen sin cola). Deja `before` en previousPosition.
[[nodiscard]] Core::Expected<void, std::string>
StepBlastModelEffect(ModelEffect& effect, const Render::Models::Skeleton* skeleton, Particles& particles,
                     float worldClock, Crt::CrtRandom& random, int lightDraw, KalimaWorld* kalima,
                     const ModelEffectStepServices& services);
} // namespace Client::World
