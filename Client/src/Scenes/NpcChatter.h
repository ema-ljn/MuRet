#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <string>

// Lo que dicen los NPC desde el paso de los personajes (CL9 hito 2): 0x4fcd00 llama a 0x4fc790 por cada uno, mira el
// reloj del mensaje del nombre (0x4fcd65..0x4fce15) y, al terminar la animación del 0x20a que giró, a 0x4fc8c0. Los
// tres hablan con 0x557170(nombre, texto, personaje, 0, −1): el globo del chat sin color. Contrato:
// Client/docs/research/06-render-escenas-y-ui.md, "Lo que dicen los NPC".
namespace Client::Scenes
{
struct ActorState;
struct CharacterStepContext;

// strcmp(+0x1d9, 0x6c88b0) en 0x4fcda2: "황금의 궁수" (el tipo 236). Los 10 bytes del nombre y +0x1e3 contra los 11
// primeros y, con los 11 iguales, +0x1e4 (ActorState::nameSpill) contra el 0 que la termina: igual solo con +0x1e4 en
// 0.
[[nodiscard]] bool HasTheMessageName(const ActorState& actor);

// 0x4fcd0c..0x4fcd55: con +0x88 en 4, en los mapas 0 y 2, el 0x20a de subtipo 0xd1 a 0xd3 mira para un lado fijo
// (+0x1c y +0x20 en 0, y +0x24 en 90 en el 0 y en 0 en el 2). Devuelve si giró: el local que 0x4fce47 lee al terminar.
[[nodiscard]] bool TurnTheNpc(ActorState& actor, std::int32_t map);

// 0x4fc790(c, o) desde 0x4fcd60: con +0x88 en 4, un rand() y, si es par, el mensaje del tipo: el 0x1cd (el tipo 253)
// fuera del mapa 30 el Text 1974, el 0x1d3 (el 242) el 1975, el 0x1d5 (el 241) el 1976 y el 0x20a con +0x376 en 0x101
// (el 257) el 1827. Sin el texto, el medidor o la tabla, el que tiene que hablar corta.
[[nodiscard]] Core::Expected<void, std::string> SayTheNpcLine(const ActorState& actor, std::size_t slot,
                                                              const CharacterStepContext& context);

// 0x4fcd65..0x4fce15, con cualquier personaje: la primera vez (el bit 0 de 0x743a174) 0x7437488 = timeGetTime() −
// 5.000; después, si pasaron más de 4.000 ms sin signo desde 0x7437488 y el nombre es el de 0x6c88b0, el Text 1167
// (con 0x743a190 en 1) o el 1168 (en 0), y 0x7437488 = ahora y 0x743a190 cambia. Sin `systemTime` no lee ni escribe el
// reloj, y el del nombre corta (Q-C248).
[[nodiscard]] Core::Expected<void, std::string> SayTheNamedLine(const ActorState& actor, std::size_t slot,
                                                                const CharacterStepContext& context);

// 0x4fc8c0(c, o) desde 0x4fce50, con el 0x20a que giró al terminar su animación: rand() % 100 y, fuera de la acción 1,
// la 1. En la 1, según el sorteo: debajo de 80 la 1, de 85 la 0x9d y de 90 la 0x9f, con un rand() par y en el mapa 2
// el Text 904; debajo de 95 la 0xab y de 100 la 0xa5, con rand() % 3 en 0 y en el mapa 2 el 905. El segundo rand()
// sale en cualquier mapa. Las acciones van con 0x4ea030 y las de Player.bmd.
[[nodiscard]] Core::Expected<void, std::string> FinishTheTurnedNpc(ActorState& actor, std::size_t slot,
                                                                   const CharacterStepContext& context);
} // namespace Client::Scenes
