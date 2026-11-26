#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Terrain/TerrainGrid.h"
#include "EngineApi.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

// TerrainHeight.OZB (Client/docs/research/05-formatos-de-datos.md, "Terreno de un mundo"; the loader at 0x5f9020).
// Unencrypted: a 256 × 256 grayscale BMP with 4 leading bytes the original skips (its exporter, 0x63b280, repeats the
// BMP's first 4 there). Q-C02: the original ignores bfOffBits; the lab's 8-bit BMPs say 0x436 and the original reads
// 2 bytes further on, so each row is shifted two cells. Reproduced, because the world's water and attributes were
// edited over the shifted terrain, as the game sees it (005f9020-terrainheight-ozb.md §6). A BMP library does not fit:
// SDL_LoadBMP_IO jumps to bfOffBits and flips the rows (64,500 heights differ on Devias) and does not load World11
// (cut) or World19 (no "BM").
namespace Client::Data::Terrain
{
// The original skips them with fseek before reading the BMP (0x5f91d3).
inline constexpr std::size_t TerrainHeightsPrefixSize = 4;
// What the original copies as the BMP header: 0x10e dwords (005f9020 §2). An 8-bit BMP with a palette has 0x436
// (14 + 40 + 256 colors of 4 bytes); the original takes 2 bytes more. Nothing of it is kept: in the original only the
// saver at 0x5f92b0, a development tool, reads it (005f9020 §2).
inline constexpr std::size_t TerrainHeightsBitmapHeaderSize = 0x438;
inline constexpr std::size_t TerrainHeightsValuesOffset = TerrainHeightsPrefixSize + TerrainHeightsBitmapHeaderSize;
static_assert(TerrainHeightsValuesOffset == 0x43C, "el origen que mide el research (005f9020 §2)");
// The prefix and the 0x10438 bytes the original reads with a single fread.
inline constexpr std::size_t TerrainHeightsSize = TerrainHeightsValuesOffset + TerrainCellCount;
static_assert(TerrainHeightsSize == 66620, "el tamaño que mide el research (005f9020 §6)");

// The constant at 0x6b8b94. Every byte times 1.5 or 3 gives an exact float, without rounding.
inline constexpr float TerrainHeightsScale = 1.5f;
// The original's world 0x37 alone (0x6b86b8), which keeps its terrain in World56.
inline constexpr float TerrainHeightsScaleOfWorld56 = 3.0f;
// No other world shares it (05, Terreno de un mundo).
inline constexpr std::uint8_t TerrainHeightsWorldWithDoubleScale = 56;

struct TerrainHeights
{
    // In world units: the byte times TerrainHeightsScale.
    TerrainLayer<float> cells;
};

// The heights take 256 KB and go on the heap, like the .map mapping (TerrainMapping.h). The BMP header is not checked,
// as in the original: in the lab World11's is a 24-bit BMP's and World19's is all zeros, and both load (Q-C02). A
// `file` shorter than TerrainHeightsSize is DataError::FileTooShort: the original ignores what its fread read and the
// missing cells keep what was on the heap (Q-C24). Anything longer is ignored, as in the original.
ENGINE_API Core::Expected<std::unique_ptr<TerrainHeights>, DataError>
LoadTerrainHeights(std::span<const std::byte> file, std::uint8_t world);
} // namespace Client::Data::Terrain
