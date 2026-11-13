#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

namespace Client::Data::Terrain
{
struct TerrainAttributes;
}

namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::World
{
class Particles;
struct WorldObject;
} // namespace Client::World

// El derrumbe de los objetos de Blood Castle y de Chaos Castle (CL9 hito 11, fase 3; 06 § El derrumbe de los
// objetos): lo pide 0x5ff930 desde los paquetes de los eventos (EventGlobals::objectCollapse) y lo hace 0x5ff960 con
// cada objeto que recorre el paso de los objetos (0x606560).
namespace Client::Scenes
{
struct ObjectCollapse;
struct WorldRuntime;

// Lo que 0x5ff960 y 0x405630 leen y escriben fuera del objeto.
struct ObjectCollapseContext
{
    // El pedido (0x6c9dd4..0x6c9de0), el mapa en curso (0x6c9dd0), el área de Chaos Castle (0x6c634c) y el último
    // sonido (0x41ed10, CL10).
    WorldRuntime& runtime;
    Render::Models::CrtRandom& random;
    // 0x53fe70: el polvo 5f3/0 de la puerta de Blood Castle y la piedra 5f6/0 de Chaos Castle. Sin el pool, corta
    // donde lo pide.
    World::Particles* particles = nullptr;
    // El reloj del mundo (0x5767b88) con que 0x53fe70 crea.
    float worldClock = 0.0f;
    // +0x18 del héroe ([0x743a170]): la z de la piedra.
    float heroZ = 0.0f;
    // 0x7d325c8, el temblor de la cámara. nullptr: corta si la rama lo pide.
    float* cameraShake = nullptr;
    // El .att del mapa (0x7d0d348), que la puerta de Blood Castle abre al terminar (0x5f8750). nullptr: corta ahí.
    Data::Terrain::TerrainAttributes* attributes = nullptr;
};

// 0x5ff966..0x5ff997: el pedido vale con el mapa, el tipo y la cuenta en 0 o más y el mapa pedido igual al de ahora.
[[nodiscard]] bool ObjectCollapseActive(const WorldRuntime& runtime);

// 0x5ff910: el pedido a −1, −1, −1 y −1,0f.
void ResetTheObjectCollapse(ObjectCollapse& collapse);

// 0x606610..0x606618: la cuenta del derrumbe baja uno al final de cada paso de los objetos, también desde −1 (un
// DEC de 32 bits, que da la vuelta).
void CountDownTheObjectCollapse(WorldRuntime& runtime);

// 0x5ff960(objeto): sin el pedido activo, nada. En los mapas 18 a 23, 0x405630 (los escalones de Chaos Castle por el
// área de 0x6c634c, siempre sin seguir); si no, los objetos 9 y 10 con la cuenta en 0 y los del tipo pedido (la puerta
// de Blood Castle: el giro de +0x1c con la demora, el sonido 0x89 con la cuenta en 20, el polvo con +0x1c en 80 justo y
// el final con la cuenta en 0, que saca el atributo 8 de (13..15, 70..75) y borra el pedido). Corta donde MuRet no
// tiene lo que la rama pide, con lo anterior hecho.
[[nodiscard]] Core::Expected<void, std::string> CollapseTheObject(World::WorldObject& object,
                                                                  const ObjectCollapseContext& context);
} // namespace Client::Scenes
