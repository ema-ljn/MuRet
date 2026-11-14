#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>

// El cuerpo de los modelos que no son el 0x20a (CL9 hito 2, P-C52 d): lo que 0x4f9580, 0x4fcd00, 0x4fcee0 y 0x505de0
// hacen según el tipo de un monstruo, un NPC o un NewFace. *Estático*. Contrato:
// Client/docs/research/06-render-escenas-y-ui.md, "El cuerpo de los monstruos y los NPC".
namespace Client::Render::Models
{
class CrtRandom;
struct Skeleton;
} // namespace Client::Render::Models

namespace Client::Scenes
{
struct ActorState;
struct WorldRuntime;

// 0x4f96d4..0x4f9737, después de los factores comunes: el NewFace (0xa8..0xac) por 2 (FADD), el 0x192 en la acción 6
// pasado el frame 6 por 4 (0x6b8a44) y el 0x1a9 en la acción 6 por 0,5. Cada uno se guarda en float.
[[nodiscard]] float ModelSpeedOf(std::uint16_t type, std::uint8_t action, float frame, float speed);

// Lo que pide 0x4fcbb0 después.
enum class ModelActionEnd : std::uint8_t
{
    Done,         // sale
    AdvanceAgain, // 0x42d890 devolvió 1: 0x4f9580 otra vez (0x4fcbe3)
    LeaveRemains, // la 6 de un monstruo con +0x38a en 0, que ya quedó en 1: 0x52e810 (0x4fcc3c, LeaveTheRemains)
};

// 0x4fcbb0(c, c), al terminar la acción de otro modelo que el 0x20a (0x4fce60). `skeleton` es el del modelo, que lee
// 0x4ea030. En orden: el 0xc5 pone +0x111 en 1; 0x42d890 en el mapa 30, por +0x376 (221 y 222 a la acción 0; 277 por
// +0x78; 286 y 287 por la acción, con el byte 0x7239d8, `runtime.castleByte7239d8`); en el mapa 1 el 0x28 a la 0; los
// 0x152..0x1c9 (los monstruos): la 6 con +0x38a en 0 lo pone en 1 y pide los restos (LeaveRemains: 0x52e810, CL9
// hito 3; con +0x38a en 1, nada), las 1, 3, 4, 5, 8, 9 y 0xb a la 0; el
// 0x1ce y el 0x1cf, rand() % 16 debajo de 12 a la 0 y si no a 1 + rand() % 2; el NewFace nada; los demás a rand() % 2.
[[nodiscard]] Core::Expected<ModelActionEnd, std::string> FinishTheModelAction(ActorState& actor,
                                                                               const Render::Models::Skeleton& skeleton,
                                                                               WorldRuntime& runtime,
                                                                               Render::Models::CrtRandom& random);

// 0x4ea190(c) desde 0x5068e3, con otro modelo que el 0x20a: la acción 6 no toca el blanco de la cabeza; la 0 sortea uno
// (rand() % 32 en 0: x = rand() % 128 − 64 e y = rand() % 48 − 16, con 360 más si da negativo); la 2 con +0x3a4
// distinto de −1 mira a ese personaje del pool (0x4ea0e0); el resto lo pone en 0. Un +0x3a4 fuera del pool corta.
[[nodiscard]] Core::Expected<void, std::string>
SetTheModelHeadTarget(ActorState& actor, std::span<const ActorState> pool, Render::Models::CrtRandom& random);

// 0x4ea0e0(c, blanco): con el rumbo de c al blanco (0x4e93f0, en float) a menos de 90 grados de +0x24 (0x4e96f0 sin
// signo), el blanco de la cabeza es +0x24 menos el rumbo y (z del blanco − (z + 50)) · 0,2f, con 360 más si da
// negativo; si no, (0, 0).
void LookAtTheTarget(ActorState& actor, const ActorState& target);

// 0x4fd10b..0x4fd5df, desde el frame 3 de otro modelo que el 0x20a: la dirección donde corta lo que hace según el
// tipo. La estela (0x52e470, desde 0x4fd233) con +0x376 de c en 0x47, 0x4a o 0x12d y las acciones 3 y 4, el 0x194 en la
// 3, el 0x195 y el 0x197 en la 3 y la 4, o con una espada (0x212..0x411) en la derecha en la 3 y la 4; y los helpers
// 0x441260 (el 0x1a7), 0x42fa40 (el 0x1b1 y el 0x1b3) y 0x426d90 (del 0x1b8 al 0x1ba). `beforeTheRandom` dice si corta
// antes de 0x432bd0 (Scenes::StepTheSwingsOf1abTo1b4, con su rand() de 0x432bf1) o después.
struct ModelTrailCut
{
    std::uint32_t address = 0;
    bool beforeTheRandom = true;
};
[[nodiscard]] std::optional<ModelTrailCut> ModelTrailCutOf(const ActorState& actor);
} // namespace Client::Scenes
