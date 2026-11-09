#pragma once

#include "Core/Expected.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainLight.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

// La luz del terreno de un mundo con la sombra de sus alturas, y la luz en un punto del mundo, como
// la usan los modelos que no van por la luz de cada vértice (Client/docs/research/
// 06-render-escenas-y-ui.md, "La luz del terreno en un punto"). Nace en la CL6.4, porque el logo y
// el personaje del login la toman de su lugar en el mapa 55; la CL7 la usa para el terreno.
namespace Client::World
{
// El mapa 30 (0x1e) sombrea con −1 y 1 (0x6b8b9c y 0x6b87d4) donde los demás usan −0,5 y 0,5.
inline constexpr std::uint8_t BrightShadingMap = 30;

// La tabla de 0x7c0ccc4: la luz de cada celda (TerrainLight.OZJ) por la sombra de su normal.
struct TerrainLighting
{
    Data::Terrain::TerrainLayer<Data::Terrain::TerrainLightColor> cells;
};

// 0x5f8b10 y 0x5f8d90, al cargar un mundo (0x5f8f40):
//   - la normal de cada celda (x, y) es la del triángulo (x + 1, y), (x + 1, y + 1), (x, y + 1), con
//     las celdas a 100 unidades y las alturas de `heights`: (p2 − p1) × (p3 − p1), normalizada. Las
//     alturas se buscan con x + 1 e y + 1 en un byte (la columna 255 toma la altura de la 0, y la fila
//     255 la de la 0), y las posiciones no;
//   - la sombra es n.x · 0,5 + a · n.y + b · n.z + 0,5, con a = −0,5 y b = 0,5 (−1 y 1 en el mapa 30),
//     de 0 a 1, y multiplica cada canal de la luz.
// `map` es el número del mapa (el de SceneWorld), no el de su carpeta.
[[nodiscard]] std::unique_ptr<TerrainLighting> ShadeTerrainLight(const Data::Terrain::TerrainLight& light,
                                                                 const Data::Terrain::TerrainHeights& heights,
                                                                 std::uint8_t map);

// 0x5f8f40 con el mundo de `map`: lee TerrainHeight.OZB y TerrainLight.OZJ de su carpeta
// (Data::WorldFolder) en `dataRoot` y los sombrea con ShadeTerrainLight. Un archivo que falta o que no
// se lee es un error que lo nombra.
[[nodiscard]] Core::Expected<std::unique_ptr<TerrainLighting>, std::string>
LoadTerrainLighting(const std::filesystem::path& dataRoot, std::uint8_t map);

// 0x5f99e0, núcleo sobre una tabla explícita y estable: escala x/y por 0,01f y guarda F32;
// convierte ambos a signed64 truncado y conserva low32 firmado, en orden X/Y. Un low32 fuera
// de [0,254] provoca rechazo con RGB +0; en la banda ordinaria acepta −1 < coordenada escalada < 255, y también
// coordenadas grandes cuyo low32 pasa. Invalid enmascarada proyecta 0 y sigue interpolando.
// Perfil portable elegido: binary32/binary64, PC53/RN, sin FMA; pesos y columnas quedan en double,
// primero Y y después X, hasta el store F32 por canal. No fija el CW nativo ni reproduce flags,
// traps o payloads NaN de x87, ni promete equivalencia para otros PC/RC o su stack anidado.
// Devuelve un valor: no modela out3 aliasando tabla/stack ni interferencias entre canales. Los
// guards de globals del original pertenecen al binding; esta API no recibe esos globals.
[[nodiscard]] glm::vec3 LightAt(const TerrainLighting& lighting, float x, float y);
} // namespace Client::World
