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

// Una tabla RGB de 256 × 256: la base sombreada de 0x7c0ccc4 o un destino explícito para la
// ventana y las sumas de 0x7b4ccc4. El tipo no decide cuándo se inicializa o se actualiza.
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

// Primer bloque RGB de 0x5fbf60: copia por bits la ventana inclusiva hasta lastX/lastY + 3,
// con ambas coordenadas envueltas a un byte. Las demás celdas del destino quedan intactas.
// Source y destination deben ser tablas disjuntas o la misma tabla (autocopia estable).
// Los límites + 3 y el incremento posterior al último índice deben caber en int32.
// No incluye el segundo bloque escalar de olas, ni establece el estado inicial o la cadencia.
void CopyTerrainLightWindow(const TerrainLighting& source, TerrainLighting& destination, std::int32_t firstX,
                            std::int32_t lastX, std::int32_t firstY, std::int32_t lastY);

// 0x5f9780: suma radial en orden filas, columnas, canales, con wrap en ambos ejes y piso en +0;
// no hay techo en 1. color puede referenciar una celda de destination: se relee en cada canal.
// El centro truncado sólo delimita el barrido: la distancia conserva las coordenadas escaladas
// float32 con su fracción. Un peso no positivo no toca la celda; un -0 exacto se conserva.
// Modelo numérico PC53/RN: operaciones double separadas, stores float y sin contracción FMA;
// no afirma el CW efectivo de todas las ejecuciones del original. Requiere radio positivo,
// coordenadas finitas cuyo producto almacenado en float por 0,01 trunque a int32, límites
// centro ± radio e incremento posterior representables en int32, colores y actualizaciones finitos.
void AddTerrainLight(TerrainLighting& destination, float worldX, float worldY,
                     const Data::Terrain::TerrainLightColor& color, std::int32_t radius);

// 0x5f99e0: la luz en (x, y) del mundo, entre las cuatro celdas de alrededor. La celda es x · 0,01
// truncado (de −1 a 0 todavía es la 0, y sale de la grilla de afuera para adentro); fuera de las
// celdas 0 a 254 en x o en y, (0, 0, 0).
[[nodiscard]] glm::vec3 LightAt(const TerrainLighting& lighting, float x, float y);
} // namespace Client::World
