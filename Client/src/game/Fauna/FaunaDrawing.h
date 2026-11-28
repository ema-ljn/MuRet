#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"

#include <cstdint>
#include <string>

namespace Client::Render
{
class ModelBatch;
}
namespace Client::Models
{
struct ClothWorld;
class IModelLibrary;
} // namespace Client::Models
namespace Client::Terrain
{
struct WorldTerrain;
struct TerrainLighting;
struct TerrainPolygon;
} // namespace Client::Terrain

namespace Client::World
{
class FrameSprites;
class Particles;
} // namespace Client::World
namespace Client::Kernel
{
struct PoseBuffer;
} // namespace Client::Kernel

namespace Client::Scenes
{
class FaunaState;
struct FaunaDrawContext
{
    FaunaState& fauna;
    const Models::IModelLibrary& models;
    const Terrain::WorldTerrain& terrain;
    const Terrain::TerrainLighting& lighting;
    const Terrain::TerrainPolygon& polygon;
    Models::ClothWorld& cloth;
    World::FrameSprites& sprites;
    Kernel::PoseBuffer& carry;
    std::int32_t map;
    std::int32_t storm;
    Core::Duration frameClock;
    World::Particles* particles = nullptr;
    bool originalFrame = true;
    float fixedHeight = 1200;
    bool projectedCarry = false;
};
[[nodiscard]] Core::Expected<void, std::string> DrawGameBirds(Render::ModelBatch& batch, FaunaDrawContext& context);
[[nodiscard]] Core::Expected<void, std::string> DrawGameFish(Render::ModelBatch& batch, FaunaDrawContext& context);
} // namespace Client::Scenes
