#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skinning.h"
#include "Render/Models/Pose.h"

#include <span>
#include <vector>
#include <cstdint>
#include <map>
#include <string>

namespace Client::Render
{
class ModelBatch;
}
namespace Client::Render::Models
{
struct ClothWorld;
}
namespace Client::World
{
struct WorldTerrain;
struct TerrainLighting;
struct TerrainPolygon;
class FrameSprites;
class Particles;
} // namespace Client::World
namespace Client::Scenes
{
class FaunaState;
class IModelLibrary;
struct DrawnObject;
// El scratch global de la pose/los vértices que leen glows y sombras. El owner lo comparte
// con los objetos anteriores; publicar también cuando otra familia pisa ese scratch.
struct FaunaDrawCarry
{
    std::vector<Render::Models::BoneTransform> bones;
    std::vector<Render::Models::SkinnedMesh> meshes;
    std::map<std::uint16_t, Render::Models::ObjectTransform> models;
};
[[nodiscard]] Core::Expected<void, std::string> PublishFaunaPose(FaunaDrawCarry& carry, const IModelLibrary& models,
                                                                 const DrawnObject& object, bool translated);
[[nodiscard]] Core::Expected<void, std::string>
PublishFaunaFloorPose(FaunaDrawCarry& carry, const IModelLibrary& models, const DrawnObject& object);
// Las matrices prestadas ya incluyen cabeza o raíz attached; no vuelve a posar.
[[nodiscard]] Core::Expected<void, std::string>
PublishFaunaPoseRaw(FaunaDrawCarry& carry, const IModelLibrary& models, std::uint16_t model,
                    std::span<const Render::Models::BoneTransform> bones,
                    const Render::Models::ObjectTransform& transform, bool translated, bool skin = true);
struct FaunaDrawContext
{
    FaunaState& fauna;
    const IModelLibrary& models;
    const World::WorldTerrain& terrain;
    const World::TerrainLighting& lighting;
    const World::TerrainPolygon& polygon;
    Render::Models::ClothWorld& cloth;
    World::FrameSprites& sprites;
    FaunaDrawCarry& carry;
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
