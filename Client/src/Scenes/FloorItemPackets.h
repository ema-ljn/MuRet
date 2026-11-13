#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Protocol/FloorItems.h"
#include "Protocol/Inventory.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

// Los paquetes de los ítems del piso (CL9 hito 5; 06 § Los ítems del piso): el 20 (0x4d0ba0, con 0x608350, 0x607b60
// y 0x607fb0), el 21 (0x4e3849), el 22 (0x4d0cc0) y la fruta de stats, el 2C (0x4de500). El 23 está en
// InventoryMoves (ApplyDropResult): es del ítem del cursor.
namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::Data::Text
{
class TextTable;
}

namespace Client::Scenes
{
class WorldState;
struct FloorItemState;
struct WorldEntryResources;

// 0x20: la entrada de 0x220 bytes de `key & 0x7fff` (0 si pasa de 999) en 0x77f46c8.
inline constexpr std::uint16_t FloorItemIndexMask = 0x7fff;
inline constexpr std::int32_t FloorItemCount = 1000;
// 0x608369: el tipo del zen.
inline constexpr std::int16_t ZenItem = 0x1c0f;

// Lo que 0x608350 lee de afuera.
struct FloorItemCreation
{
    Render::Models::CrtRandom& random;
    // 0x5f93c0, la altura del terreno en (x, y): la de World::TerrainHeightAt con la del mapa.
    std::function<Core::Expected<double, std::string>(float x, float y)> height{};
    // El reloj del mundo (0x5767b88) de la luz que late de algunos modelos en 0x607b60.
    Core::Duration clock{};
    // 0x41ed10 con el ítem recién caído (CL10): queda el último en lastSoundId.
    std::optional<std::int32_t>* sound = nullptr;
};

// 0x608350(entrada, ítem, posición, recién caído): el tipo (0x54b3a0) en +4, el valor, la durabilidad y las opciones,
// el modelo (tipo + 0x212, con los que cambian por nivel), 0x607b60 con sus rand() (CharacterDrawing::
// HangingRandomCount), la caja de ±30, la posición y la altura: con `dropped`, 180 por encima del terreno y la
// velocidad 20 (los fuegos 0x2242 y 0x2243, 3 y 50, con sus dos efectos de la CL11, que cortan); si no, en el terreno.
// Termina con 0x607fb0.
[[nodiscard]] Core::Expected<void, std::string> CreateFloorItem(FloorItemState& item,
                                                                const Protocol::Inventory::ItemData& bytes,
                                                                const glm::vec3& position, bool dropped,
                                                                const FloorItemCreation& creation);

// 0x4d0ba0: cada entrada en (x + 0,5, y + 0,5) · 100, en orden; el 0x54b3a0 que sigue no deja nada.
[[nodiscard]] Core::Expected<void, std::string>
ApplyFloorItemsCreate(const Protocol::FloorItems::FloorItemsCreate& create, const WorldEntryResources& resources);

// 0x4e3849: apaga (+0x4c en 0) la entrada de cada clave, con las de 1000 o más en la 0.
void ApplyFloorItemsDelete(const Protocol::FloorItems::FloorItemsDelete& deleted, WorldState& world);

// 0x4d0cc0: con 0xfe, el zen en el registro propio (3.848 rand() al cerrarlo sin accesos) y, con el elegido
// (0x77f27b0) vivo, el aviso "%d %s %s" con su valor y los textos 224 y 918; con un lugar, 0x5bc740 (salvo 0xfd) y el
// aviso "%s %s" con el nombre del elegido (0x5b1c70) y el 918. Después el sonido por el tipo de pkt[4..] (0x45 o 0x1d,
// CL10) y, también con 0xff, el pedido (0x6c82e8) en −1.
[[nodiscard]] Core::Expected<void, std::string> ApplyPickUpResult(const Protocol::FloorItems::PickUpResult& result,
                                                                  const WorldEntryResources& resources);

// 0x4de500, el 2C: con 0 y un stat de 0 a 4, el stat del registro propio y +0x44 suben los puntos; con 3, el stat baja
// y +0x68 y +0x48 suben; cada uno con su aviso (0x632e30) armado con los textos 379, el del stat y 1412 o 1903. Los
// resultados 1, 2, 4, 5, 0x10, 0x21, 0x25 y 0x26 abren su aviso. Siempre, 0x5767954 en 0.
[[nodiscard]] Core::Expected<void, std::string> ApplyStatFruit(const Protocol::FloorItems::StatFruit& fruit,
                                                               WorldState& world, Render::Models::CrtRandom& random,
                                                               const Data::Text::TextTable* texts);
} // namespace Client::Scenes
