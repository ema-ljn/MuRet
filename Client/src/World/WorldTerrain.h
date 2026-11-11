#pragma once

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Data/Textures/Sampling.h"
#include "World/TerrainLighting.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Lo que 0x61a560 lee de un mundo para el terreno (CL7.1): el mapeo, los atributos y las alturas de
// World<N>\ (EncTerrain<N>.map y .att, TerrainHeight.OZB), la luz sombreada (TerrainLighting) y la
// tabla de sus tiles. Contrato: 06 § Terreno y § El terreno del login.
namespace Client::World
{
struct WorldTerrain
{
    std::unique_ptr<Data::Terrain::TerrainMapping> mapping;
    std::unique_ptr<Data::Terrain::TerrainAttributes> attributes;
    std::unique_ptr<Data::Terrain::TerrainHeights> heights;
    std::unique_ptr<TerrainLighting> lighting;
};

// Los tres archivos y la luz del mapa `map` (el número de la carpeta, Data::WorldFolderNumber). El
// error nombra el archivo como lo pide el original. Los mapas 30 y 34, con otro .att, no van.
[[nodiscard]] Core::Expected<WorldTerrain, std::string> LoadWorldTerrain(const std::filesystem::path& dataRoot,
                                                                         std::uint8_t map);

// Carga por etapas para respetar MAP/ATT → OBJ → alturas/luz (61a751..61a910).
// Publican cada tabla ya leída y el primer aviso fatal, sin UI. Un error sigue siendo error;
// el MAP ausente sólo difiere su diagnóstico hasta comprobar el ATT (Q-C27/Q-C180).
[[nodiscard]] Core::Expected<void, std::string> LoadWorldTerrainLayout(const std::filesystem::path& dataRoot,
                                                                       std::uint8_t map, WorldTerrain& terrain,
                                                                       std::optional<std::string>& fatalMessage);
[[nodiscard]] Core::Expected<void, std::string> LoadWorldTerrainSurface(const std::filesystem::path& dataRoot,
                                                                        std::uint8_t map, WorldTerrain& terrain,
                                                                        std::optional<std::string>& fatalMessage);

// Los objetos de EncTerrain<N>.obj del mapa `map` (0x607370), en el orden del archivo (CL7.2). El
// error nombra el archivo como lo pide el original.
[[nodiscard]] Core::Expected<std::vector<Data::Terrain::TerrainObject>, std::string>
LoadWorldObjects(const std::filesystem::path& dataRoot, std::uint8_t map,
                 std::optional<std::string>* fatalMessage = nullptr);

// Un tile de 0x61a560: la ruta como la pide el original, el slot, el muestreo y si avisa cuando falta
// (el sexto argumento de 0x63b870: solo TileGrass01). Los que no avisan se saltean.
struct WorldTileTexture
{
    std::string path;
    std::uint16_t slot = 0;
    Data::Textures::Sampling sampling{};
    bool required = false;
};

// Los 14 tiles del terreno de `map`, de World<N>\, en 0x23..0x30, con GL_NEAREST y repitiendo, en el
// orden de 0x61a560. Los mapas 24 a 29 y 36, que arman los suyos (0x4377a0), no tienen. El pasto
// (0x32..0x34) va en WorldGrassTextures; las hojas (0x64, 0x65) y la lluvia (0x66..0x68),
// en WorldWeatherTextures.
[[nodiscard]] std::vector<WorldTileTexture> WorldTileTextures(std::uint8_t map);

// Las tres texturas del pasto de `map` que sube 0x61a560 después de los tiles (0x61abb4..0x61ac36):
// World<N>\TileGrass01..03.tga en 0x32..0x34 (World/TerrainCells.h), con 0x63bd00, GL_NEAREST y
// repitiendo, sin avisar si faltan. Los mapas que arman sus tiles en 0x4377a0 no las cargan.
[[nodiscard]] std::vector<WorldTileTexture> WorldGrassTextures(std::uint8_t map);

// 61ac3e..61ad7a: alternativas de hojas/nieve64/65 y lluvia/salpicaduras66..68,
// GL_NEAREST/GL_CLAMP, todas opcionales; la JPG64 posterior reemplaza a la TGA64 si existe.
[[nodiscard]] std::vector<WorldTileTexture> WorldWeatherTextures(std::uint8_t map);

// 618dda..618e29: las 32 cáusticas Object8\\wt00..wt31.jpg, slots65..96,
// LINEAR/REPEAT, sin aviso. Aquí se integra sólo la familia Kalima24..29/36.
[[nodiscard]] std::vector<WorldTileTexture> KalimaObjectTextures(std::uint8_t map);

// 0x6177f9..0x61784b, el mapa 8 de 0x616ec0 antes de los Bug: 0x63b870 con GL_LINEAR, sexto 1 (avisa).
// Object9\sand01 y sand02 en 0x5c2 y 0x5c3, repitiendo (las arenas del HUD de 0x5a7520, CL8), e
// Impack03 en 0x716, sin repetir (los sprites de los 0x3f y 0x40 de 0x602ed0). Los demás, ninguna.
[[nodiscard]] std::vector<WorldTileTexture> TarkanObjectTextures(std::uint8_t map);

// El slot 0x122 que pide una capa 2 de 255 (Q-C19): Interface\accept_box01.jpg, que el original sube
// en la carga (0x61ae60) y queda para siempre; MuRet lo sube con el mundo.
[[nodiscard]] WorldTileTexture AcceptBoxTexture();
} // namespace Client::World
