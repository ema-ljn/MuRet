#pragma once

#include <glm/ext/vector_float2.hpp>

namespace Client::Render::Models
{
class CrtRandom;
} // namespace Client::Render::Models

// La cabeza de un personaje (cierre del login): el tramo de 0x505de0 que la mueve (0x5068eb..0x5069ab), en
// cada paso de 0x508ea0, después de 0x4fd5f0 (el cuerpo y el cuervo). La pose la lee en 0x4eb860
// (Render::Models::PoseSkeleton, `headAngles`). Contrato: 06 § Lo demás del login. Verificado bit a bit
// contra el código real emulado (tests/vectors/world55/head.txt).
namespace Client::Scenes
{
struct CharacterHead
{
    glm::vec2 angles{}; // +0x28, +0x2c: los de la cabeza, en grados
    glm::vec2 target{}; // +0x34, +0x38: hacia dónde gira
};

// Con `sorts` (el personaje no es el héroe de 0x743a170 ni se está muriendo, +0x389), rand() % 32 en 0
// sortea el blanco: x = rand() % 128 − 64 e y = rand() % 32 − 16, con 360 más si da negativo. Después, cada
// ángulo gira hacia su blanco (0x4e9560) a lo sumo el 20 % de lo que los separa (0x4e96f0 sin signo, por
// 0,2f y guardado en float). Lo que se saltea antes (las banderas 0x20 y 0x1000 de +0x78, y que se vea,
// +0x16c) lo mira el que llama. Es el tramo del modelo 0x20a, el de los jugadores: con otro modelo, 0x505de0
// llama antes a 0x4ea190 (0x5068da..0x5068e8), que según +0x111 sortea otro blanco (con 0: rand() % 32 en 0,
// x = rand() % 128 − 64 e y = rand() % 48 − 16), mira a otro personaje (con 2 y +0x3a4 distinto de −1,
// 0x4ea0e0), no hace nada (con 6) o lo pone en 0; un NPC puede sortear dos veces por paso (08, P-C17).
void StepCharacterHead(CharacterHead& head, bool sorts, Render::Models::CrtRandom& random);
} // namespace Client::Scenes
