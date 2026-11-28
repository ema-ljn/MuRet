#include "Scenes/FaunaDrawing.h"

#include "Assets/ModelIds.h"
#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Math/Angles.h"
#include "Math/X87.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Models/Cloth.h"
#include "Models/IModelLibrary.h"
#include "Models/ObjectState.h"
#include "Math/OriginalRotation.h"
#include "Models/Pose.h"
#include "Models/Shine.h"
#include "Models/Skinning.h"
#include "Render/SpriteVertex.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Actors/Drawing/PosePublishing.h"
#include "Kernel/PoseBuffer.h"
#include "Scenes/FaunaObject.h"
#include "Scenes/FaunaState.h"
#include "World/Birds.h"
#include "Math/Ftol.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/TerrainHeight.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Client::Scenes
{
namespace
{
constexpr float QuarterTurn = Math::QuarterTurnAsFloat;
constexpr float VisibleAlpha = .01f;
constexpr std::int16_t InvisibleBird = Assets::RidingSpearModel;
constexpr std::int16_t StormBird = Assets::Monster32Model;
constexpr std::int32_t Icarus = 10;
using Math::F32;
Models::AnimationState AnimationOf(const FaunaObject& object)
{
    return {object.action, object.frame, object.previousAction,
            static_cast<std::int16_t>(Math::FtolLow(object.previousFrame))};
}
Actors::Drawing::DrawnObject Drawn(const FaunaObject& object, const Terrain::TerrainLighting& lighting)
{
    Actors::Drawing::DrawnObject drawn;
    drawn.model = static_cast<std::uint16_t>(object.model);
    drawn.animation = AnimationOf(object);
    drawn.transform = {object.position, object.angles, object.bodyScale};
    drawn.state = {
        static_cast<std::int16_t>(object.blendMesh), object.ceiling, {}, static_cast<std::int16_t>(object.hiddenMesh)};
    const auto light = Terrain::LightAt(lighting, object.position.x, object.position.y);
    const double factor = object.vertexLight != 0 ? 1 : static_cast<double>(.1f);
    for (int k = 0; k < 3; ++k)
        drawn.color[k] = F32(static_cast<double>(light[k]) * factor + object.light[k]);
    drawn.alpha = object.alpha;
    drawn.vertexLight = object.vertexLight != 0;
    return drawn;
}
bool Seen(const FaunaObject& object, const Terrain::TerrainPolygon& polygon)
{
    return Terrain::PolygonContains(polygon, F32(static_cast<double>(object.position.x) * .01f),
                                    F32(static_cast<double>(object.position.y) * .01f), -20);
}
glm::vec3 Flatten(const glm::vec3& world, const glm::vec3& origin, std::int32_t map)
{
    // 4f0040..4f0089: x/z retenidos, diferencia Y guardada en f32 antes de reponer el origen.
    const double x = static_cast<double>(world.x) - origin.x, z = static_cast<double>(world.z) - origin.z;
    const float y = F32(static_cast<double>(world.y) - origin.y);
    const double spread = map == 30 ? 2500 : 2000;
    const double projected = ((x + spread) * z) / (z - 4000);
    return {F32((projected + x) + origin.x), F32(static_cast<double>(y) + origin.y),
            F32(static_cast<double>(origin.z) + 5)};
}
Core::Expected<void, std::string> Shadow(Render::ModelBatch& batch, const FaunaObject& object,
                                         FaunaDrawContext& context)
{
    // 620a00(1) precede la consulta del número de mallas de 4eff40, incluso si no hay geometría.
    auto state = batch.State();
    state.blend = Render::ModelBlend::Alpha;
    state.cullEnabled = false;
    state.depthWrite = true;
    state.alphaTest = true;
    batch.ApplyState(state);
    if (context.terrain.heights == nullptr || context.terrain.attributes == nullptr)
        return Core::Unexpected(std::string("4eff40: altura del terreno ausente"));
    glm::vec3 origin = object.position;
    origin.z = F32(Terrain::TerrainHeightAt(*context.terrain.heights, *context.terrain.attributes, origin.x, origin.y,
                                            context.fixedHeight));
    // 446e9d..446eb3: sólo XYZ; la escala del último cuerpo permanece retenida.
    context.carry.models[static_cast<std::uint16_t>(object.model)].position = origin;
    const auto* model = context.models.Find(Models::ModelId{static_cast<std::uint16_t>(object.model)});
    if (model == nullptr || model->meshes.empty())
        return {};
    state.blend = Render::ModelBlend::BlendedShadow;
    state.depthWrite = false;
    state.alphaTest = false;
    state.texture2d = false;
    batch.ApplyState(state);
    for (std::size_t i = 0; i < model->meshes.size(); ++i)
    {
        for (const auto& triangle : model->meshes[i].triangles)
        {
            if (triangle.polygon != 3)
                return Core::Unexpected(std::string("4eff40: polígono no triangular"));
            std::array<Render::ModelVertex, 3> vertices{};
            for (std::size_t k = 0; k < vertices.size(); ++k)
            {
                const auto vertex = triangle.vertices[k];
                if (vertex < 0)
                    return Core::Unexpected(std::string("4eff40: índice de vértice negativo"));
                // El scratch frío original es .bss en cero. Los índices no escritos conservan ese cero.
                glm::vec3 position{};
                if (i < context.carry.meshes.size() &&
                    static_cast<std::size_t>(vertex) < context.carry.meshes[i].positions.size())
                    position = context.carry.meshes[i].positions[static_cast<std::size_t>(vertex)];
                vertices[k] = {Flatten(position, origin, context.map), {}, Render::Color{0, 0, 0, .2f}};
            }
            batch.Triangle(vertices);
        }
    }
    // 4f00f5→620890 sólo repone la escritura de profundidad, deja textura/alpha test apagados.
    state.depthWrite = true;
    batch.ApplyOperation(state, Render::ModelStateOperation::DepthWrite);
    return {};
}
glm::vec3 BonePoint(const FaunaDrawContext& context, const FaunaObject& object, std::size_t index,
                    const glm::vec3& offset, bool translated)
{
    Math::OriginalMatrix matrix{};
    if (index < context.carry.bones.size())
        matrix = Math::OriginalMatrixOf(context.carry.bones[index]);
    if (translated)
    {
        const auto& transform = context.carry.models.at(static_cast<std::uint16_t>(object.model));
        return Math::OriginalTransformPosition(matrix, offset, transform.scale, transform.position);
    }
    return Math::OriginalVectorTransform(offset, matrix);
}
Core::Expected<void, std::string> BirdExtras(FaunaObject& bird, FaunaDrawContext& context)
{
    if (bird.model == Assets::ButterflyModel)
    {
        const auto light = World::BirdGlowLight(bird.model, context.cloth.random);
        static_cast<void>(context.sprites.Create(0x5ac, bird.position, 1, *light, 0, 0));
    }
    if (bird.model == Assets::CrowModel)
    {
        if (!context.carry.models.contains(static_cast<std::uint16_t>(bird.model)))
            return Core::Unexpected(std::string("446d51: origen/escala retenidos del modelo BC sin evidencia"));
        const auto left = BonePoint(context, bird, 1, {-5, 0, 0}, true);
        const auto light = World::BirdGlowLight(bird.model, context.cloth.random);
        static_cast<void>(context.sprites.Create(0x5ac, left, .1f, *light, 0, 0));
        const auto right = BonePoint(context, bird, 1, {5, 0, 0}, true);
        static_cast<void>(context.sprites.Create(0x5ac, right, .1f, *light, 0, 0));
    }
    if (bird.model != StormBird)
        return {};
    if (context.storm == 0)
        return {};
    const auto position = BonePoint(context, bird, 11, {0, -50, 0}, false);
    static_cast<void>(context.sprites.Create(0x5d5, position, 1, {1, 0, 0}, 0, 0));
    if (context.particles == nullptr)
        return Core::Unexpected(std::string("446d1e: pool de partículas de fauna ausente"));
    const float clock =
        static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(context.frameClock).count());
    auto created =
        context.particles->CreateChecked(0x5d9, position, bird.angles, glm::vec3(1), 0, 1, clock, context.cloth.random);
    if (!created.HasValue())
        return Core::Unexpected(std::string("446d1e: partícula 5d9/0 no disponible"));
    return {};
}
Core::Expected<void, std::string> DrawOne(Render::ModelBatch& batch, FaunaObject& object, FaunaDrawContext& context,
                                          bool bird)
{
    if (object.active == 0)
        return {};
    object.seen = static_cast<std::uint8_t>(Seen(object, context.polygon));
    if (object.seen == 0 || (bird ? object.model == InvisibleBird : object.model == -1))
        return {};
    const auto drawn = Drawn(object, context.lighting);
    if (const auto* model = context.models.Find(Models::ModelId{drawn.model}))
    {
        if (context.carry.meshes.size() < model->meshes.size())
            context.carry.meshes.resize(model->meshes.size());
        for (std::size_t i = 0; i < model->meshes.size(); ++i)
        {
            auto& scratch = context.carry.meshes[i];
            if (scratch.positions.size() < model->meshes[i].vertices.size())
                scratch.positions.resize(model->meshes[i].vertices.size());
            if (scratch.normals.size() < model->meshes[i].normals.size())
                scratch.normals.resize(model->meshes[i].normals.size());
        }
    }
    if (object.alpha >= VisibleAlpha)
    {
        auto pose = Actors::Drawing::PublishPose(context.carry, context.models, drawn, !bird);
        if (!pose.HasValue())
            return pose;
    }
    Actors::Drawing::DrawObjectFromMeshes(batch, context.models, drawn, context.frameClock, context.carry.meshes);
    // 446c35..446c80 son draws puros, también visibles en la proyección entre pasos nativos.
    if (bird && object.model == StormBird && object.counter == 1)
    {
        Actors::Drawing::DrawObjectFromMeshes(batch, context.models, drawn, context.frameClock, context.carry.meshes,
                                              Models::ShinePass::Shiny);
        Actors::Drawing::DrawObjectFromMeshes(batch, context.models, drawn, context.frameClock, context.carry.meshes,
                                              Models::ShinePass::Chrome);
    }
    if (bird && context.originalFrame)
    {
        auto extras = BirdExtras(object, context);
        if (!extras.HasValue())
            return extras;
    }
    const bool shadow =
        context.map != Icarus && (bird || (object.model != Assets::Fish08Model && object.model != Assets::Fish09Model));
    return shadow ? Shadow(batch, object, context) : Core::Expected<void, std::string>{};
}
Core::Expected<void, std::string> DrawPool(Render::ModelBatch& batch, std::span<FaunaObject> pool,
                                           FaunaDrawContext& context, bool birds)
{
    if (!context.originalFrame)
    {
        if (context.projectedCarry)
        {
            for (const auto& retained : pool)
            {
                FaunaObject object = retained;
                if (!birds || object.model != InvisibleBird)
                    object.angles.z = F32(static_cast<double>(object.angles.z) + QuarterTurn);
                if (auto result = DrawOne(batch, object, context, birds); !result.HasValue())
                    return result;
            }
            return {};
        }
        Kernel::PoseBuffer carry = context.carry;
        FaunaDrawContext projected{context.fauna,   context.models,     context.terrain,    context.lighting,
                                   context.polygon, context.cloth,      context.sprites,    carry,
                                   context.map,     context.storm,      context.frameClock, context.particles,
                                   false,           context.fixedHeight};
        for (const auto& retained : pool)
        {
            FaunaObject object = retained;
            if (!birds || object.model != InvisibleBird)
                object.angles.z = F32(static_cast<double>(object.angles.z) + QuarterTurn);
            if (auto result = DrawOne(batch, object, projected, birds); !result.HasValue())
                return result;
        }
        return {};
    }
    for (auto& object : pool)
    {
        const bool rotate = !birds || object.model != InvisibleBird;
        if (rotate)
            object.angles.z = F32(static_cast<double>(object.angles.z) + QuarterTurn);
        auto result = DrawOne(batch, object, context, birds);
        // Fallo del binding: conservar el prefijo del slot actual, incluido el yaw aún sumado.
        if (!result.HasValue())
            return result;
        if (rotate)
            object.angles.z = F32(static_cast<double>(object.angles.z) - QuarterTurn);
    }
    return {};
}
} // namespace
Core::Expected<void, std::string> DrawGameBirds(Render::ModelBatch& batch, FaunaDrawContext& context)
{
    return DrawPool(batch, context.fauna.Birds(), context, true);
}
Core::Expected<void, std::string> DrawGameFish(Render::ModelBatch& batch, FaunaDrawContext& context)
{
    return DrawPool(batch, context.fauna.Fish(), context, false);
}
} // namespace Client::Scenes
