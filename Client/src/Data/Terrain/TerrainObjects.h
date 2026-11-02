#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// EncTerrain<N>.obj, los objetos de un mundo: árboles, casas, rocas (Client/docs/research/
// 05-formatos-de-datos.md, "Terreno de un mundo"; en el original, el cargador de 0x607370). Se lee
// de Data\World<N>\EncTerrain<N>.obj.
//
// Cifrado con K16 de punta a punta, en un solo flujo y sin XOR3, como el .map. En claro:
//
//   offset        qué es
//   0             no se lee (¿la versión?)
//   1             el número del mundo: el N de la carpeta World<N>
//   2             cuántos objetos hay: un int16 little-endian
//   4 + 30 * k    el objeto k, sin relleno entre uno y otro
//
// Cada objeto: el tipo (int16), la posición (3 floats), el ángulo (3 floats) y la escala (1 float),
// todo little-endian. Qué modelo es cada tipo y qué hace el original con el ángulo es de la CL7.
namespace Client::Data::Terrain
{
// El byte con el número del mundo, el que devuelve el cargador del original (0x6074ad).
inline constexpr std::size_t TerrainObjectsWorldOffset = 1;
// Dónde está la cantidad de objetos. El original la lee con signo (`movsx` en 0x6074a9).
inline constexpr std::size_t TerrainObjectsCountOffset = 2;
// La cabecera entera: el byte que no se lee, el mundo y la cantidad. Los objetos van después
// (00607370-encterrain-obj.md §2).
inline constexpr std::size_t TerrainObjectsHeaderSize = TerrainObjectsCountOffset + sizeof(std::int16_t);
static_assert(TerrainObjectsHeaderSize == 4, "la cabecera que mide el research (00607370 §2)");
// Lo que ocupa un objeto en el archivo: un int16 y siete floats. El original avanza de a 30 bytes
// (00607370-encterrain-obj.md §2).
inline constexpr std::size_t TerrainObjectSize = sizeof(std::int16_t) + 7 * sizeof(float);
static_assert(TerrainObjectSize == 30, "el registro que mide el research (00607370 §2)");

struct TerrainObject
{
    // Qué objeto es. En los archivos reales va de 0 a 153; World7 trae uno en -515 (Q-C28).
    std::int16_t type = 0;
    // Dónde está, en las unidades del mundo: 100 por celda. x e y van sobre la grilla y z es la
    // altura.
    std::array<float, 3> position{};
    // Cuánto está girado sobre cada eje, probablemente en grados.
    std::array<float, 3> angle{};
    // Cuánto se agranda el modelo: 1.0f es su tamaño. Los archivos reales traen alguna negativa o
    // en cero (Q-C28).
    float scale = 0.0f;
};

// Descifra `file`, el EncTerrain.obj de la carpeta World<world>, y devuelve sus objetos en el orden
// del archivo.
//   - Cada float queda con los bits del archivo, aunque sea un NaN: Devias trae dos (Q-C28). Qué
//     objetos se crean y cuáles se descartan lo decide la CL7, como el 0x606660 del original.
//   - Si la cantidad es cero o negativa, no hay objetos. El original guarda igual el número
//     negativo (0x6074b0), y el research no encontró quién lo lee (00607370 §1 y §7).
//   - Si `file` no llega a la cabecera o a todos los objetos que dice, devuelve
//     DataError::FileTooShort. El original no mira el tamaño y leería después del buffer (Q-C24).
//   - Si es más largo, el resto se ignora, como en el original.
//   - Si el byte 1 no es `world`, devuelve DataError::WrongWorld. El original crea los objetos
//     igual, y es el que llama el que muestra "%s file corrupted." y cierra el juego.
Core::Expected<std::vector<TerrainObject>, DataError> LoadTerrainObjects(std::span<const std::byte> file,
                                                                         std::uint8_t world);
} // namespace Client::Data::Terrain
