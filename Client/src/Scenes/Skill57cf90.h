#pragma once

#include <cstdint>

// 0x57cf90(&clave), CL9 hito 4, fase 2c: la clave de la próxima magia, del contador 0x77f3bad (.bss 0): uno más y, al
// pasar de 0x32, 1 (el INC de un byte y el JBE sin signo). La deja en `key` y la devuelve. El original, con un puntero
// nulo, devuelve 0 sin tocar el contador; acá la referencia no lo es (la 0x43 de la cola, 0x59497a, la llama así:
// HeroSkillsTail.cpp manda el 0 sin llamarla). En línea, la repiten las
// ramas de 0x57cfc0 (0x581348, 0x589246, 0x58e6d9 y 0x58f822) con el +0x1bd del héroe, que después va al 1E.
namespace Client::Scenes
{
struct GameMovementState;

std::uint8_t NextMagicKey(GameMovementState& state, std::uint8_t& key);
} // namespace Client::Scenes
