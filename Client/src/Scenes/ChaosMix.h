#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Scenes/ItemPrice.h"
#include "World/ItemState.h"

#include <cstdint>
#include <span>
#include <string>

// La caja del caos (CL9 hito 6, fase 3; 06 § La caja del caos): lo que vale (0x5fd120), el tipo de la mezcla
// (0x5fd4f0 y el modo de 0x5fd4b0), lo que la mezcla pide de lugar en el inventario (0x5fd200) y la chance y el precio
// que calcula su dibujo (0x5febf0). Las llama 0x5d8d50 con la caja abierta (0x5d8db6..0x5d8dda), el botón de mezclar
// (0x5fe690) y el dibujo. Los globales quedan en NpcWindowState.
namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::Scenes
{
class WorldState;
struct NpcWindowState;

// 0x5fd120(grilla, columnas, filas): la suma de lo que vale cada ítem con tipo y +0x3c (0x54d0f0 en el modo 0, con
// los precios fijos de las joyas: 100.000 la de Bless 0x1c0d, 70.000 la de Soul 0x1c0e, 40.000 la del caos 0x180f y
// 450.000 la de la vida 0x1c10 y la de la creación 0x1c16), en el int que da la vuelta. Sin una joya del caos, 0.
// `pets` son los niveles de las mascotas que lee 0x54d0f0 con el 0x1a04 y el 0x1a05.
[[nodiscard]] Core::Expected<std::int32_t, std::string>
ChaosBoxValue(std::span<const World::ItemState> grid, const Data::Tables::ItemTable* items, const PetPriceLevels& pets);

// 0x5fd4f0(0x7d2d380, 8, 4): cuenta lo que hay en la caja y devuelve el tipo de la mezcla (0 sin ninguna; negativo
// el error que avisa el botón de mezclar). Deja en `npc` los anchos y altos más grandes (0x6c9da0, 0x6c9da4), el
// valor de los excelentes (0x7d2dc88), la suerte (0x7d2dc9c), la opción de las alas (0x7d2dc90) y, según el tipo, el
// nivel (0x7d2dc8c), la capa (0x7d2dc84) y la chance de las alas (0x7d2dc94). Las alas de segundo nivel (y la capa
// 0x18) abren el registro propio para leer el nivel (+0xe): 3.848 rand() al cerrarlo sin otros accesos.
[[nodiscard]] Core::Expected<std::int32_t, std::string>
ChaosMixKind(WorldState& world, Render::Models::CrtRandom& random, const Data::Tables::ItemTable* items);

// 0x5fd4b0: el tipo de 0x5fd4f0 filtrado por el modo de la caja (0x7d3a114): en el 0, la mezcla 1 (el arma del caos)
// da 0; en el 1, solo la 1; en los demás, cualquiera.
[[nodiscard]] std::int32_t ChaosMixKindForMode(std::int32_t kind, std::int32_t mode);

// 0x5d8db6..0x5d8dda: con la caja abierta, en cada paso de 0x5d8d50, lo que vale la caja (0x7d2d378) y el tipo de la
// mezcla (0x7d2dc98).
[[nodiscard]] Core::Expected<void, std::string> ScanTheChaosBox(WorldState& world, Render::Models::CrtRandom& random,
                                                                const Data::Tables::ItemTable* items);

// 0x5fd200(grilla, columnas, filas, ancho, alto): si hay un lugar de ancho × alto con todas las casillas sin tipo
// (0xffff). Con el alto en 0 o menos, sí.
[[nodiscard]] bool HasRoomFor(std::span<const World::ItemState> grid, std::int32_t columns, std::int32_t rows,
                              std::int32_t width, std::int32_t height);

// Lo que el botón de mezclar pide de lugar para el tipo `kind` (0x5fe9bd..0x5fea04): 5 × 4 para el arma del caos, las
// alas y la capa del 0x0b; 2 × 2, 1 × 1 o 3 × 3 para otras; los anchos y altos más grandes de la caja (0x6c9da0 y
// 0x6c9da4) para el resto.
struct MixRoom
{
    std::int32_t width = 0;
    std::int32_t height = 0;
};
[[nodiscard]] MixRoom MixRoomFor(const NpcWindowState& npc, std::int32_t kind);

// Lo que el botón de mezclar hace con el tipo `kind` (0x5fe9a2..0x5fe9b6, la tabla de 0x5feb88).
enum class MixButtonAction
{
    LevelTooLow,  // −0x18: el texto 1171 con 10.
    NotTheSame,   // −8 y −2: el texto 600.
    NothingToMix, // 0: el 580 con el 591.
    Mix,          // los que mezclan: el lugar y el diálogo 0x98.
    Nothing,      // los demás: solo el sonido.
};
[[nodiscard]] MixButtonAction MixButtonActionFor(std::int32_t kind);

// 0x5fedac..0x5ff0c2: la chance (ESI) y el precio (0x7d2dca0) que el dibujo calcula del tipo de la mezcla, con el
// texto del precio de 0x5b1130. Sin un caso propio, la chance es lo que vale la caja / 20.000 (tope 100) y el precio
// la chance · 10.000.
struct MixQuote
{
    std::int32_t rate = 0;
    std::uint32_t price = 0;
    std::string priceText;
};
[[nodiscard]] Core::Expected<MixQuote, std::string> MixQuoteOf(const NpcWindowState& npc);

// 0x5b1130(n, buffer): n sin signo con los miles separados por comas ("%d,%03d" y sus hermanos).
[[nodiscard]] std::string ThousandsText(std::uint32_t value);

// 0x5b12f0(precio, buffer): el precio más el impuesto del castillo (0x78aba34 · precio / 100, en 64 bits con signo),
// con los miles separados por comas.
[[nodiscard]] std::string TaxedPriceText(std::uint32_t price, std::int32_t taxRate);
} // namespace Client::Scenes
