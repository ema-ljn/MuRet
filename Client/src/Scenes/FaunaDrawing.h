#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Math/HalfLifeMath.h"
#include "Models/Skinning.h"
#include "Models/Pose.h"

#include <span>
#include <vector>
#include <cstdint>
#include <map>
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
namespace Client::Actors::Drawing
{
struct DrawnObject;
} // namespace Client::Actors::Drawing

namespace Client::Scenes
{
class FaunaState;
// El scratch global de la pose/los vértices que leen glows y sombras. El owner lo comparte
// con los objetos anteriores; publicar también cuando otra familia pisa ese scratch.
struct FaunaDrawCarry
{
    std::vector<Math::BoneTransform> bones;
    std::vector<Models::SkinnedMesh> meshes;
    std::map<std::uint16_t, Models::ObjectTransform> models;
    // One mesh skinned before it goes over the head of `meshes`, kept for its capacity.
    Models::SkinnedMesh skinned;
};
[[nodiscard]] Core::Expected<void, std::string> PublishFaunaPose(FaunaDrawCarry& carry,
                                                                 const Models::IModelLibrary& models,
                                                                 const Actors::Drawing::DrawnObject& object,
                                                                 bool translated);
[[nodiscard]] Core::Expected<void, std::string> PublishFaunaFloorPose(FaunaDrawCarry& carry,
                                                                      const Models::IModelLibrary& models,
                                                                      const Actors::Drawing::DrawnObject& object);
// Las matrices prestadas ya incluyen cabeza o raíz attached; no vuelve a posar. Con `translated`, `bones` ya están en
// el mundo (0x4eb860 con la bandera en 1, PoseWorldSkeleton) y el scratch sale de 0x4ebc80 con Translate en 0; sin él,
// están en el modelo y el scratch lleva la escala y el origen de `transform` (Translate en 1).
[[nodiscard]] Core::Expected<void, std::string>
PublishFaunaPoseRaw(FaunaDrawCarry& carry, const Models::IModelLibrary& models, std::uint16_t model,
                    std::span<const Math::BoneTransform> bones, const Models::ObjectTransform& transform,
                    bool translated, bool skin = true);
// 4ebc80: actualiza el scratch de mallas con los huesos retenidos, sin ejecutar 4eb860.
[[nodiscard]] Core::Expected<void, std::string> TransformFaunaPose(FaunaDrawCarry& carry,
                                                                   const Models::IModelLibrary& models,
                                                                   const Actors::Drawing::DrawnObject& object);
struct FaunaDrawContext
{
    FaunaState& fauna;
    const Models::IModelLibrary& models;
    const Terrain::WorldTerrain& terrain;
    const Terrain::TerrainLighting& lighting;
    const Terrain::TerrainPolygon& polygon;
    Models::ClothWorld& cloth;
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
