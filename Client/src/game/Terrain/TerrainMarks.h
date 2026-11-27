#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace Client::Data::Terrain
{
struct TerrainAttributes;
}

// Las marcas que los eventos ponen y sacan en el .att del mapa cargado (0x7d0d348, un WORD por celda): los paquetes
// 46 (04 § Los eventos) y los derrumbes de Blood Castle y Chaos Castle (06 § Los dibujos de los eventos).
namespace Client::Scenes
{
// Un rectángulo de celdas de 0x5f8750: desde (x, y), `width` columnas y `height` filas.
struct TerrainArea
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
};

// 0x5f8710 (OR, con `add`) y 0x5f8730 (AND con el complemento) sobre el WORD (y << 8) + x. Sin el .att, o con la
// celda fuera de los 0x10000 WORD, corta en `where` sin tocar nada.
[[nodiscard]] Core::Expected<void, std::string> MarkTheTerrainCell(Data::Terrain::TerrainAttributes* attributes,
                                                                   std::int32_t x, std::int32_t y,
                                                                   std::uint8_t attribute, bool add,
                                                                   std::string_view where);

// 0x5f8750(x, y, ancho, alto, atributo, agregar): fila por fila, de y a y + alto − 1, y en cada una de x a x + ancho −
// 1; con alto o ancho en 0 o menos, nada. Corta en la primera celda que no puede marcar, con las anteriores hechas.
[[nodiscard]] Core::Expected<void, std::string> MarkTheTerrainArea(Data::Terrain::TerrainAttributes* attributes,
                                                                   const TerrainArea& area, std::uint8_t attribute,
                                                                   bool add, std::string_view where);
} // namespace Client::Scenes
