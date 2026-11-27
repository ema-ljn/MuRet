#pragma once

#include "Core/Expected.h"
#include "Actors/ActorActions.h"

#include <string>

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// La ropa de la clase nueva: 0x513100, que llama el premio de una misión (A3 0xc9..0xcb, 0x4ddb80) después de los
// joints y el calco. Contrato: Client/docs/research/04-protocolo.md, "Las misiones y los NPC" (el A3), y 07, Q-C1024.
namespace Client::Actors
{

// 0x513100: sólo con el tipo 0x20a. Con la acción (+0x111) fuera de [0x22, 0x97] y de [0xc5, 0xcc], 0x4f2250 con
// `stop`. Después, cada parte del cuerpo con el modelo de la clase base (la clase & 7) pasa al de la clase entera, con
// el nivel y los bytes +3 y +4 en 0; la del casco compara su propio modelo, y las otras cuatro, el de la parte
// siguiente (la última, el de la mano derecha, +0x2b8): Q-C1024. Al final, 0x512930 (la cabeza y la escala). Devuelve
// lo que pidió 0x4f2250, si corrió; su error, sin tocar las partes.
[[nodiscard]] Core::Expected<ActorStopResult, std::string> ApplyTheClassParts(Kernel::ActorState& actor,
                                                                              const ActorStopContext& stop);
} // namespace Client::Actors
