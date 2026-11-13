#pragma once

#include <cstdint>
#include <optional>

namespace Client::World
{
struct TerrainPickingSegment;
}

// El picking de los personajes del juego (CL9 hito 1): 0x59e700, que 0x59ec80 llama con las máscaras 0x22, 1 y 4.
// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "El picking de los personajes del juego". *Estático*.
namespace Client::Scenes
{
class WorldState;

// 0x59e700(máscara) en el juego (la escena 5: el héroe publicado no se elige, 0x59e70d..0x59e717 y 0x59e952). Los
// 400 primeros con +0x88 dentro de la máscara (+0x88 & máscara == +0x88), vivos, a la vista (+0x16c) y con el alpha
// sobre 0 se tiñen: +0xe8 en 1 (0x511290 ya lo dejó así, 0x511be6; MuRet no lo guarda) y la luz propia (+0xf4) por el
// WORD de +0x1d6, de 0 a 4: (0,2; 0,2; 0,2), (−0,4; −0,4; −0,4), (0,2; −0,6; −0,6), (1,5; 1,5; 1,5) y (0,3; 0,2;
// −0,5); con 5 o más la deja. Con el PK (+0x375) en 6 o más, (−0,4; −0,4; −0,4). Después, de los mismos, sin el que se
// muere (+0x389), sin +0x7c & 0x10000000, sin el héroe publicado y con +0x42c, el de menor distancia² entre su
// posición y la cámara (0x7d36884) cuya caja toca el segmento del mouse (0x622900, de 0x7d36834 a 0x7d366c0): la del
// 0x20a desde su posición (Q-C100). En Lorencia (0) un candidato sobre la capa 4 del .map, y en Devias (2) sobre la 3,
// cuenta solo si es la capa de la celda del héroe (0x77f36d0). El primero gana los empates. −1 si ninguno. La party
// (0x78aba48) está vacía en MuRet (hito 7): sus dos recorridos no hacen nada. Sin segmento (antes del primer dibujo),
// solo tiñe. Sin rand().
[[nodiscard]] std::int32_t PickTheCharacter(WorldState& world, std::uint8_t mask,
                                            const std::optional<World::TerrainPickingSegment>& ray);
} // namespace Client::Scenes
