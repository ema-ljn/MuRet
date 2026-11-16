#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "World/ItemState.h"

#include <cstdint>
#include <optional>
#include <string>

// El precio de un ítem (0x54d0f0) y el de repararlo (0x5b13e0), CL8 hito 5 (06 § El precio de un ítem). Son
// funciones de datos: no abren el registro propio ni sacan rand(). Las usan el cuerpo de reparar (0x5b8150) y, en la
// CL9.3, el precio de la tienda de NPC del cuerpo del ítem (0x5b2d23, P-C125 (a)).
namespace Client::Scenes
{
// Los modos con que lo llaman: 0 desde la grilla de la tienda de NPC (0x5b2d2e), 1 desde el inventario con la tienda
// abierta (0x5b2e46) y 2 para reparar (0x5b838a). Cualquiera distinto de 0 divide por 3; solo el 1 descuenta la
// durabilidad que falta.
inline constexpr std::int32_t ShopGridPrice = 0;
inline constexpr std::int32_t InventoryPrice = 1;
inline constexpr std::int32_t RepairPriceMode = 2;

// Los niveles de las dos mascotas, los words de 0x723968 + 0x14 · tipo que escribe 0x421db0 con el A9: el 0 (el
// cuervo) para el 0x1a05 y el 1 (el caballo, WorldRuntime::pets) para el 0x1a04. Solo los lee 0x422930.
struct PetPriceLevels
{
    std::uint16_t raven = 0; // [0x723968]
    std::uint16_t horse = 0; // [0x72397c]
};

// 0x54d0f0(ítem, modo): el dword bajo del precio, que es lo que leen los que llaman. El tipo 0xffff vale 0. Corta con
// la dirección del original: un tipo fuera de Item.bmd (0x54d133), una opción leída fuera de D+0x2b..D+0x3b, las
// flechas y los virotes de nivel 3 o más (0x54d283, multiplican por la dirección del ítem), una cuenta de la x87
// sobre un entero de más de 53 bits, el 0x1a04 y el 0x1a05 sin `pets` (0x54db28) y un infinito en el _ftol de la
// durabilidad del modo 1 (0x54dc04).
[[nodiscard]] Core::Expected<std::uint32_t, std::string> ItemPrice(const World::ItemState& item,
                                                                   const Data::Tables::ItemTable& items,
                                                                   std::int32_t mode,
                                                                   const std::optional<PetPriceLevels>& pets);

struct RepairPrice
{
    // Lo que devuelve en EAX, ya redondeado.
    std::int32_t gold = 0;
    // Lo que escribe en el buffer de quien llama, con los miles separados por comas.
    std::string text;
};

// 0x5b13e0(precio, durabilidad, máxima, tipo, buffer): el precio de reparar, desde el de 0x54d0f0 (con tope de
// 400.000.000), lo que falta de la durabilidad y [0x78abb10] (WorldRuntime::field78abb10, 1 con el botón de reparar
// del inventario), que suma 0,05 por cada unidad de su byte bajo. El tipo no lo lee.
[[nodiscard]] RepairPrice RepairPriceOf(std::uint32_t price, std::int32_t durability, std::int32_t maxDurability,
                                        std::int32_t field78abb10);
} // namespace Client::Scenes
