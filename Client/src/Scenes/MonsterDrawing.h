#pragma once

#include "Core/Expected.h"
#include "Scenes/ActorState.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Render
{
class ModelBatch;
}

// Los monstruos y los NPC en el dibujo del juego (0x50ba30 con un tipo que no es el 0x20a, CL9 hito 2). Contrato: 06
// § El dibujo del juego, "El dibujo de los monstruos y los NPC".
namespace Client::Scenes
{
class IModelLibrary;
class WorldState;
struct GameCharacterPass;

// 0x50ba9e..0x50bae0: los +0x376 que 0x50ba30 manda a la tela sobre la malla (0x50bae7, la tabla de 0x510adc sobre
// 0x510ad4): 0x59, 0x5f, 0x70, 0x76, 0x7c, 0x82 y 0x8f. Con cualquier tipo, también el 0x20a.
[[nodiscard]] bool WearsClothOnTheMesh(std::uint16_t monster);

// 0x50bf45..0x50c904: lo que 0x50ba30 le agrega al cuerpo por el +0x376, con cualquier tipo (también el 0x20a): los
// pases de brillo de 0x50c671 (0x26, 0x2b, 0x34, 0x3b, 0x43 y 0x4e a 0x53), los sprites y las partículas del 0x45
// (0x50bf98), el 0x46 (0x50c0fb), las telas del 0x47, el 0x4a y el 0x12d (0x50c59f), el 0x49 y el 0x4b (0x50c36b), el
// 0x4d (0x50c177), el brillo del 0x35 y el 0x36 (0x50c71b), las armas colgadas del 0x2a (0x50c762) y del 0x84 al 0x86
// (0x50c83b) y el brillo del 0x87 (0x50c8ce). Ninguno está portado: con uno de ellos, corta con su dirección.
[[nodiscard]] Core::Expected<void, std::string> CheckMonsterDecorations(std::uint16_t monster);

// 0x50db57..0x50dc58 sin su rand(): la luz de lo que cuelga (+0x3b4). Con +0x375 desde 6, (1; 0,1; 0,1). Si no, la
// del terreno (la de 0x50cc55, o 0,4 en la escena 4) más la propia (+0xf4), y en Blood Castle (los mapas 10 a 17: el
// mapa − 9 entre 1 y 8) con el +0x376 en 0x56..0x59, 0x5c..0x5f, 0x73..0x76, 0x79..0x7c, 0x7f..0x82 o 0x8a..0x8f, si
// (mapa − 9) / 3 (con signo) no es 0, (ese cociente · 0,5; 0,1; 0,1).
[[nodiscard]] glm::vec3 HangingLightOf(const ActorState& actor, const glm::vec3& terrainLight, std::int32_t map);

// 0x50ba30 con un monstruo o un NPC (el tipo no es el 0x20a) en el lugar `slot`, en el orden del ASM:
// - la tela sobre la malla corta (0x50bae7, P-C22 f);
// - 0x602e90(objeto, 1, contorno, +0x376 de la lista de 0x50bd05 o 0): 0x5ffdf0 baja 0,15f el alpha en Chaos Castle con
//   +0x1a1 del héroe, por debajo de 0,01 no posa ni dibuja; posa en los huesos propios (+0x120, actor.bodyPose) con los
//   ángulos de la cabeza, con el contorno (los dos pases de 0x60cd60 con los colores y las escalas de 0x5fffbd..
//   0x600135) y el cuerpo de 0x6002e0 sobre el color del modelo de 0x5ffce0, con sus casos por tipo y por mapa, su
//   cadena y lo que agrega después del cuerpo (DrawMonsterBody, MonsterBody.h);
// - la sombra de 0x50bd89 con sus condiciones (DrawObjectShadow) y, en Blood Castle, la altura del terreno de los que
//   mueren; la marca del 0xe8 y el 0xe9 (0x5fb2f0) corta;
// - lo de 0x50bf45 por el +0x376 (CheckMonsterDecorations);
// - las partes (0x50d3e3), el Angel (0x50d383), la marca del gremio (0x50c99b) y la tela del 0x37 (0x50d599) cortan si
//   el registro las tiene;
// - 0x50db57: el rand() y la luz de lo que cuelga (HangingLightOf) y, fuera de la espalda, las manos (DrawGameHanging);
// - el switch por modelo de 0x50e9fa: los sprites de 0x50b870 y 0x50b920 del 0x152 (con +0x1d6 en 1), el 0x170, el
//   0x186, el 0x177, el 0x174, el 0x176, el 0x173, el 0x1d8 y el 0x1f2; los del 0x1d9 (0x510763) con su rand() % 30 y,
//   si da 0, sus tres rand() % 100 y el joint 0x618/6 (que CreateFaunaJoint todavía no crea: corta); el 0x16e
//   (0x5108b6) corta, y los demás van por la cadena de 0x510a1e, cuyos tipos cortan con la dirección de su helper;
// - 0x510ef0 en la escena 5 con +0x88 en 1 y +0x369.
// `models` tiene los ítems y Player.bmd (las manos); el modelo del tipo sale de WorldState::FaunaResources. Entre
// frames del original, todo sobre una copia del personaje y del rand().
[[nodiscard]] Core::Expected<void, std::string> DrawGameMonster(Render::ModelBatch& batch, const IModelLibrary& models,
                                                                WorldState& world, std::size_t slot,
                                                                const GameCharacterPass& pass, bool originalFrame);
} // namespace Client::Scenes
