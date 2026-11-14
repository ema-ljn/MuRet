#pragma once

#include "Core/Expected.h"
#include "Scenes/WorldTexts.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace Client::Data::Text
{
class TextTable;
}

namespace Client::Render::Models
{
struct Skeleton;
}

// Los casos por tipo del paso de los personajes que no son el 0x20a (0x4fd5f0 y 0x508ea0): el nombre del 0xc5, la
// caída del 0xf1 y los objetos del asedio de 0x42da90. Contrato: 06 § El cuerpo de los personajes (0x4fd5f0) y § Los
// objetos del mapa 30 en el paso (0x42da90).
namespace Client::Scenes
{
struct ActorState;
struct WorldRuntime;

// 0x4fd603..0x4fd698, el 0xc5 (las puertas de Kalima): "%s ." (0x6c88bc) con el nombre (+0x1d9, como cadena de C),
// Text.bmd[1176] (0x77133fc) con +0x1d6 sin signo, el strcat de los dos en el buffer de 100 bytes y 0x4381f0 con la
// posición (+0x10). Sin rand(). Sin Text.bmd, o si el strcat se pasara de los 100 bytes, corta.
[[nodiscard]] Core::Expected<void, std::string>
QueueTheGateName(const ActorState& actor, const Data::Text::TextTable* texts, WorldTextQueue& queue);

// 0x4fb910..0x4fb9c7, el 0xf1 desde 0x4fb380: la z sube +0xe4 y +0xe4 baja 6; si la altura del terreno (0x5f93c0) más
// 30 queda por encima, la z pasa a esa y +0xe4 a −0,4f veces; los ángulos suman +0xcc, +0xd0 y +0xd4, que se achican a
// 0,8f. `height` es 0x5f93c0(x, y).
void StepTheFallingBall(ActorState& actor, const std::function<double(float, float)>& height);

// 0x42a020(objeto, modo, rango): si algún jugador del pool (+0x88 en 1, el tipo 0x20a y +0x16c) está cerca del
// objeto. El modo 0: dx y dy (objeto menos jugador) debajo del rango y de 100; el 1: |dx| debajo de 1 y dy entre 0 y
// el rango; el 2: la distancia en el plano debajo del rango. Un NaN pasa las comparaciones con C0 (las de "debajo").
[[nodiscard]] bool PlayerNear(const ActorState& object, std::span<const ActorState> pool, std::uint8_t mode,
                              float range);

struct CastleObjectStep
{
    WorldRuntime& runtime;
    // El pool de 0x743a16c, para 0x42a020.
    std::span<const ActorState> pool;
    // 0x5f93c0(x, y); vacía, el 216 corta.
    std::function<double(float, float)> height{};
    // El modelo del personaje, para 0x4ea030; sin él, el 286 y el 287 cortan cuando cambian la acción.
    const Render::Models::Skeleton* skeleton = nullptr;
};

// 0x42da90(c, c), desde 0x508eed después de 0x505de0: fuera del mapa 30 nada. En el 30, por +0x376: el 216 (0xd8) se
// apoya 200 sobre el terreno, copia +0x78 a 0x7239e4 y +0x42c es +0x78 distinto de 0; el 277 (0x115) pone 0x7239d9
// en 1 (las puertas no se abren en 0x42da60); el 278 (0x116) con +0x42d en 0x80 guarda su x y su y en 0x7239dc y
// 0x7239e0; el 283 (0x11b) con el asedio +0x58 en −1 y +0x42c en 1, sin él −2 y 0; el 286 y el 287 (0x11e y 0x11f)
// abren con un jugador justo enfrente (0x42a020 con el modo 1 y 500) o con 0x7239d8 en 2 (la acción 4, 0x7239d8 en 2,
// +0x110 en 1 y +0x100 en 300) y cierran cuando se acaba +0x100 sin nadie (la acción 0), y +0x100 baja 1 en cada paso.
// Sin rand().
[[nodiscard]] Core::Expected<void, std::string> StepTheCastleObject(ActorState& actor, const CastleObjectStep& step);
} // namespace Client::Scenes
