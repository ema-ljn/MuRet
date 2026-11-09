#pragma once

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Data/Textures/Sampling.h"
#include "World/TerrainLighting.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Lo que 0x61a560 lee de un mundo para el terreno (CL7.1): el mapeo, los atributos y las alturas de
// World<N>\ (EncTerrain<N>.map y .att, TerrainHeight.OZB), la luz sombreada (TerrainLighting) y la
// tabla de sus tiles. Contrato: 06 § Terreno y § El terreno del login.
namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::World
{
struct WorldTerrain
{
    std::unique_ptr<Data::Terrain::TerrainMapping> mapping;
    std::unique_ptr<Data::Terrain::TerrainLayer<float>> grassOffsets;
    std::unique_ptr<Data::Terrain::TerrainAttributes> attributes;
    std::unique_ptr<Data::Terrain::TerrainHeights> heights;
    std::unique_ptr<TerrainLighting> lighting;
};

// Hook síncrono concreto del orden61a560: crear objetos después de .att y antes de alturas.
// Un error conserva los efectos ya aplicados y aborta la carga, sin presentar otro frame.
struct WorldTerrainLoad
{
    Render::Models::CrtRandom* random = nullptr;
    std::function<Core::Expected<void, std::string>()> objectsBeforeHeights;
};

// Los tres archivos y la luz del mapa `map` (el número de la carpeta, Data::WorldFolderNumber). El
// error nombra el archivo como lo pide el original. Los mapas 30 y 34, con otro .att, no van.
[[nodiscard]] Core::Expected<WorldTerrain, std::string>
LoadWorldTerrain(const std::filesystem::path& dataRoot, std::uint8_t map, WorldTerrainLoad context = {});

// Los objetos de EncTerrain<N>.obj del mapa `map` (0x607370), en el orden del archivo (CL7.2). El
// error nombra el archivo como lo pide el original.
[[nodiscard]] Core::Expected<std::vector<Data::Terrain::TerrainObject>, std::string>
LoadWorldObjects(const std::filesystem::path& dataRoot, std::uint8_t map);

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
// (0x32..0x34), las hojas (0x64, 0x65) y la lluvia (0x66..0x68) que carga después quedan para lo que
// los dibuje.
[[nodiscard]] std::vector<WorldTileTexture> WorldTileTextures(std::uint8_t map);

// El slot 0x122 que pide una capa 2 de 255 (Q-C19): Interface\accept_box01.jpg, que el original sube
// en la carga (0x61ae60) y queda para siempre; MuRet lo sube con el mundo.
[[nodiscard]] WorldTileTexture AcceptBoxTexture();
} // namespace Client::World
