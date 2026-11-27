#pragma once

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Data/Textures/Sampling.h"
#include "Terrain/TerrainLighting.h"
#include "Data/Terrain/TerrainLight.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What 0x61a560 reads from a world for the terrain: the mapping, the attributes and the heights of
// World<N>\ (EncTerrain<N>.map and .att, TerrainHeight.OZB), the shaded light (TerrainLighting) and its tile table.
// Contract: 06 § Terreno and § El terreno del login.
namespace Client::Terrain
{
struct WorldTerrain
{
    std::unique_ptr<Data::Terrain::TerrainMapping> mapping;
    std::unique_ptr<Data::Terrain::TerrainAttributes> attributes;
    std::unique_ptr<Data::Terrain::TerrainHeights> heights;
    std::unique_ptr<TerrainLighting> lighting;
    // 0x78ecc10: RGB light without shade; 0x5f8e40 reads it again after deforming the heights.
    std::unique_ptr<Data::Terrain::TerrainLight> rawLight;
};

// A terrain with no table, as before any load: whoever needs one stops where it reads it.
[[nodiscard]] const WorldTerrain& EmptyTerrain();
// The terrain a context carries, or EmptyTerrain() without one.
[[nodiscard]] const WorldTerrain& TerrainOrEmpty(const WorldTerrain* terrain);

// The three files and the light of map `map` (the folder number, Maps::WorldFolderNumber). The error names the file as
// the original asks for it. Maps 30 and 34, with another .att, do not go here.
[[nodiscard]] Core::Expected<WorldTerrain, std::string> LoadWorldTerrain(const std::filesystem::path& dataRoot,
                                                                         std::uint8_t map);

// A staged load that keeps the MAP/ATT → OBJ → heights/light order (0x61a751..0x61a910). They publish each table
// already read and the first fatal notice, without UI. An error is still an error; a missing MAP only defers its
// diagnostic until the ATT is checked (Q-C27/Q-C180). `attributesNumber` is the number of EncTerrain<N>.att (the
// world's if missing: Crywolf asks for 351 or 352, 0x61a710) and `lightName` the light's (TerrainLight1 or
// TerrainLight2 in Crywolf, 0x61a8dd); the .att header is still compared with the world. `reuseMappingForAttributes`
// reads the .map as the .att and keeps the table if its size does not fit. `retainLightOnFailure` shades the retained
// light again when the requested light cannot be opened or decoded.
[[nodiscard]] Core::Expected<void, std::string>
LoadWorldTerrainLayout(const std::filesystem::path& dataRoot, std::uint8_t map, WorldTerrain& terrain,
                       std::optional<std::string>& fatalMessage, std::optional<std::int32_t> attributesNumber = {},
                       bool reuseMappingForAttributes = false);
[[nodiscard]] Core::Expected<void, std::string> LoadWorldTerrainSurface(const std::filesystem::path& dataRoot,
                                                                        std::uint8_t map, WorldTerrain& terrain,
                                                                        std::optional<std::string>& fatalMessage,
                                                                        std::string_view lightName = "TerrainLight",
                                                                        bool retainLightOnFailure = false);

// 0x5f8f40 with a light from `folder` (the folder as the caller builds it, "World%d"), outside the load: Crywolf's BD
// 00 (0x42ff81). Reads <folder>\<lightName>.OZJ and shades it with the loaded heights and map `map`. If it does not
// open, the notice " - File not exist." (fatal) and the light as it was; if it does not read, the light as it was
// (Q-C32): the original shades the previous light again with the same heights. Without heights it fails.
[[nodiscard]] Core::Expected<void, std::string>
ReloadWorldTerrainLight(const std::filesystem::path& dataRoot, const std::string& folder, std::string_view lightName,
                        std::uint8_t map, WorldTerrain& terrain, std::optional<std::string>& fatalMessage);

// 0x5f8390 outside the load (0x42ffe8): Data\<folder>\EncTerrain<number>.att, without comparing the header's world.
// If it does not open, the notice " file not found." (fatal); with another size, nothing (Q-C25); with a bad header or
// attribute, the notice "Data error" (fatal). In all three cases the attributes stay as they were.
[[nodiscard]] Core::Expected<void, std::string> ReloadWorldTerrainAttributes(const std::filesystem::path& dataRoot,
                                                                             const std::string& folder,
                                                                             std::int32_t number, WorldTerrain& terrain,
                                                                             std::optional<std::string>& fatalMessage);

// The objects of EncTerrain<N>.obj of map `map` (0x607370), in file order. The error names the file as the original
// asks for it.
[[nodiscard]] Core::Expected<std::vector<Data::Terrain::TerrainObject>, std::string>
LoadWorldObjects(const std::filesystem::path& dataRoot, std::uint8_t map,
                 std::optional<std::string>* fatalMessage = nullptr);

// A tile of 0x61a560: the path as the original asks for it, the slot, the sampling and whether it warns when missing
// (the sixth argument of 0x63b870: only TileGrass01). Those that do not warn are skipped.
struct WorldTileTexture
{
    std::string path;
    std::uint16_t slot = 0;
    Data::Textures::Sampling sampling{};
    bool required = false;
};

// The 14 terrain tiles of `map`, from World<N>\, at 0x23..0x30, with GL_NEAREST and repeating, in the order of
// 0x61a560. Maps 24 to 29 and 36, which build their own (0x4377a0), have none. The grass (0x32..0x34) goes in
// WorldGrassTextures; the leaves (0x64, 0x65) and the rain (0x66..0x68), in WorldWeatherTextures.
[[nodiscard]] std::vector<WorldTileTexture> WorldTileTextures(std::uint8_t map);

// The three grass textures of `map` that 0x61a560 uploads after the tiles (0x61abb4..0x61ac36):
// World<N>\TileGrass01..03.tga at 0x32..0x34 (Terrain/TerrainCells.h), with 0x63bd00, GL_NEAREST and repeating, without
// warning if missing. The maps that build their tiles in 0x4377a0 do not load them.
[[nodiscard]] std::vector<WorldTileTexture> WorldGrassTextures(std::uint8_t map);

// 0x61ac3e..0x61ad7a: the alternatives of leaves/snow at 0x64/0x65 and rain/splashes at 0x66..0x68,
// GL_NEAREST/GL_CLAMP, all optional; the later JPG at 0x64 replaces the TGA at 0x64 if it exists.
[[nodiscard]] std::vector<WorldTileTexture> WorldWeatherTextures(std::uint8_t map);

// 0x616eeb..0x616f03: the first thing 0x616ec0 uploads on every map, Object8\\drop01.jpg at 0x607 with GL_NEAREST and
// GL_CLAMP, required: the texture of particles 0x607 (Atlans's bubbles).
[[nodiscard]] WorldTileTexture DropTexture();

// The 32 caustics Object8\\wt00..wt31.jpg, slots 65..96 (0x41..0x60), LINEAR/REPEAT, without warning: map 7 uploads
// them in its case of 0x616ec0 (0x617753..0x617788) and the others at 0x618dda..0x618e29, with the same arguments.
// Every byte map of 0x61a560, including selection 54/55 and castle 30 (NpcBarrier1ff, 0x42e626). The caller excludes
// the signed sentinel −1: the network zero-extends its byte; the scenes set 54/55.
[[nodiscard]] std::vector<WorldTileTexture> CausticTextures(std::uint8_t map);

// 0x618e2b..0x618e40, after the caustics loop: Object8\\light01.jpg at 0x68, LINEAR/REPEAT, optional. The rain of
// 0x61ad7a may replace it later; if that alternative is missing, this slot stays alive.
[[nodiscard]] WorldTileTexture ObjectLightTexture();

// 0x6177f9..0x61784b, map 8 of 0x616ec0 before the Bugs: 0x63b870 with GL_LINEAR, sixth argument 1 (it warns).
// Object9\sand01 and sand02 at 0x5c2 and 0x5c3, repeating (the HUD sands of 0x5a7520), and Impack03 at 0x716, without
// repeat (the sprites of 0x3f and 0x40 of 0x602ed0). The others, none.
[[nodiscard]] std::vector<WorldTileTexture> TarkanObjectTextures(std::uint8_t map);

// 0x6178da..0x6178f2, map 10 of the second switch of 0x616ec0 (the one that always runs): 0x63b870 with GL_LINEAR,
// GL_CLAMP and sixth argument 1. Effect\cloudLight.jpg at 0x623 (the sprite of 0x6063d0). The others, none.
[[nodiscard]] std::vector<WorldTileTexture> IcarusObjectTextures(std::uint8_t map);

// 0x617ff6/0x618016: World31\\Map1/2.jpg, the castle minimaps, only when loading map 30.
[[nodiscard]] std::vector<WorldTileTexture> CastleSiegeTextures(std::uint8_t map);

// Slot 0x122, which a layer 2 of 255 asks for (Q-C19): Interface\accept_box01.jpg, which the original uploads at load
// (0x61ae60) and keeps forever; MuRet uploads it with the world.
[[nodiscard]] WorldTileTexture AcceptBoxTexture();
} // namespace Client::Terrain
