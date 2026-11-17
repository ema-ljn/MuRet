#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Data::Terrain
{
struct TerrainHeights;
}

namespace Client::Render
{
class ModelBatch;
namespace Models
{
class CrtRandom;
}
} // namespace Client::Render

namespace Client::World
{
class FrameSprites;
class Particles;
} // namespace Client::World

// Los helpers por tipo del dibujo de los monstruos (0x441400, 0x434180 y 0x4271d0, de la cadena de 0x510a1e) y del
// paso desde el frame 3 (0x432bd0, de 0x4fcee0), CL9 hito 2, fase 6d. Contrato: 06 § El dibujo del juego, "Los
// helpers de los monstruos".
//
// Todo lo que es estado, sonido o rand() está portado, y las partículas (0x53fe70) que World::Particles crea (0x5ca/0,
// 0x5f2/11, 0x5f3/0, 0x5d9/0 y 0x729/0 y /1) se crean con sus rand(). Las que MuRet no tiene cortan con la dirección
// de la llamada solo si su tabla tiene lugar: llena, el original no crea nada, no saca rand() y sigue, y MuRet sigue
// con él. Los efectos (0x518cf0), los joints (0x530860), las estelas (0x52e470), 0x51fe10 y 0x4ee770 son de la CL11 y
// cortan con la dirección de la llamada, sin mirar si su tabla está llena (el pase no la lleva, P-C336). Un hueso con
// nombre (0x4010b0 y 0x4010d0 → 0x4016c0) que el alta del monstruo no registró en WorldRuntime::namedBones también
// corta: el original dejaría el punto sin escribir, con lo que hubiera en la pila.
namespace Client::Scenes
{
struct ActorState;
class WorldState;

// Lo que leen los helpers del dibujo: el personaje (el registro o su copia entre frames del original), su lugar en el
// pool (la clave de 0x4016c0), el rand() del dibujo, los sprites (0x5491e0) y las partículas (0x53fe70) del frame y el
// reloj del mundo ([0x5767b88], en ms). Los sonidos (0x41ed10, en WorldRuntime::lastSoundId para la CL10) se piden
// solo en un frame del original. El lote y las alturas del .OZB son los de las marcas 0x69b del piso (0x5fb2f0) del
// 0x204 al 0x208.
struct MonsterHelperPass
{
    ActorState& actor;
    std::size_t slot = 0;
    WorldState& world;
    Render::Models::CrtRandom& random;
    World::FrameSprites* sprites = nullptr;
    World::Particles* particles = nullptr;
    float worldClock = 0.0f;
    std::int32_t map = 0;
    bool originalFrame = true;
    Render::ModelBatch* batch = nullptr;
    const Data::Terrain::TerrainHeights* heights = nullptr;
};

// 0x441400(c, objeto, modelo), del 0x1a3 al 0x1a9 (la tabla de 0x442de8): los ojos, la cola y las manos de
// Monster81 a Monster87, sus sonidos y los efectos de la CL11. Siempre devuelve false (0x442dda); salvo el 0x1a8, que
// sale sin tocar nada, deja +4 en 0 en las acciones 0 y 1 (0x442dd2).
[[nodiscard]] Core::Expected<bool, std::string> DrawTheEffectsOf1a3To1a9(MonsterHelperPass& pass);

// 0x434180, solo en el mapa 34 (0x4303c0): del 0x1ab al 0x1b5 sus sonidos, los sprites del 0x1af, el 0x1b1 y el
// 0x1b2 y la partícula 0x5f3 del 0x1b1; del 0x204 al 0x208, con +0x7c & 0x40000 la luz propia y las partículas 0x729/0
// y /1, y las marcas 0x69b del piso (0x5fb2f0) entre 0x620a90 y 0x620980. La primera lleva el color de 0x40000 o de
// los bits 0x100000, 0x20000, 0x10000 u 0x80000; la segunda, el giro que solo escribe 0x40000. La marca cuyo color o
// giro es la pila sin escribir (sin ningún bit, o la segunda sin 0x40000) no se dibuja (Q-C1100): solo liga su
// textura. Devuelve true con el 0x1ad, del 0x1af al 0x1b4, que cortan la cadena de 0x510a1e.
[[nodiscard]] Core::Expected<bool, std::string> DrawTheCrywolfEffectsOf1abTo208(MonsterHelperPass& pass);

// 0x4271d0(c, objeto), del 0x1b6 al 0x1bb (la tabla de 0x428dd4): los sprites 0x5ad de Monster100 a Monster105, sus
// sonidos y sus partículas. Siempre devuelve false (0x428dc5); en su rango deja +4 en 0 en las acciones 0 y 1.
[[nodiscard]] Core::Expected<bool, std::string> DrawTheEffectsOf1b6To1bb(MonsterHelperPass& pass);

// 0x510a42..0x510a77: 0x441400, 0x42fa30 (siempre 0, no hace nada), 0x434180 y 0x4271d0, hasta el primero que
// devuelve distinto de 0. Los dos primeros de la cadena (0x43c150 y 0x42dfe0) van antes, en MonsterDrawing.
[[nodiscard]] Core::Expected<void, std::string> DrawTheMonsterHelpers(MonsterHelperPass& pass);

// 0x432bd0(c, objeto, modelo), desde 0x4fcee0 (0x4fd5d7) con cualquier tipo desde el frame 3 sin estela: el rand() de
// 0x432bf1 sin condición y, del 0x1ab al 0x1b4 en su acción, las poses de la estela en el buffer global (0x4eb860 en
// 0x68b181c, WorldState::PoseCarry). El 0x1b0 en la 4 da sus diez vueltas, con dos rand() cada una y, con el frame en
// (5, 7), la partícula 0x5d9/0 (en `particles`, con `worldClock`) del hueso 16 de cada pose; los demás cortan en su
// estela (0x52e470) o en su efecto después de la primera pose (el 0x1ab, con su partícula 0x640 antes si la tabla tiene
// lugar). El 0x1ae en la 8 no posa: su 0x5f2/0x1b corta con lugar. La acción, la escala y la posición del modelo
// (+0xa0, +0x68 y +0x6c, de su último 0x5ffdf0) son las del personaje (Q-C1102, *inferido*).
[[nodiscard]] Core::Expected<void, std::string> StepTheSwingsOf1abTo1b4(ActorState& actor, std::size_t slot,
                                                                        WorldState& world,
                                                                        Render::Models::CrtRandom& random,
                                                                        World::Particles* particles, float worldClock);
} // namespace Client::Scenes
