#include "Scenes/FaunaDrawing.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Render/Models/Shine.h"
#include "Render/Models/Skinning.h"
#include "Render/SpriteVertex.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaObject.h"
#include "Scenes/FaunaState.h"
#include "World/Birds.h"
#include "World/Ftol.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/TerrainCulling.h"
#include "World/TerrainHeight.h"
#include "World/TerrainLighting.h"
#include "World/WorldTerrain.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
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
constexpr float QuarterTurn = 90;
constexpr float VisibleAlpha = .01f;
constexpr std::int16_t InvisibleBird = 0x10f;
constexpr std::int16_t StormBird = 0x171;
constexpr std::int32_t Icarus = 10;
float F32(double value)
{
    return static_cast<float>(value);
}
Render::Models::AnimationState AnimationOf(const FaunaObject& object)
{
    return {object.action, object.frame, object.previousAction,
            static_cast<std::int16_t>(World::FtolLow(object.previousFrame))};
}
DrawnObject Drawn(const FaunaObject& object, const World::TerrainLighting& lighting)
{
    DrawnObject drawn;
    drawn.model = static_cast<std::uint16_t>(object.type);
    drawn.animation = AnimationOf(object);
    drawn.transform = {object.position, object.angles, object.bodyScale};
    drawn.state = {
        static_cast<std::int16_t>(object.blendMesh), object.ceiling, {}, static_cast<std::int16_t>(object.hiddenMesh)};
    const auto light = World::LightAt(lighting, object.position.x, object.position.y);
    const double factor = object.vertexLight != 0 ? 1 : static_cast<double>(.1f);
    for (int k = 0; k < 3; ++k)
        drawn.color[k] = F32(static_cast<double>(light[k]) * factor + object.light[k]);
    drawn.alpha = object.alpha;
    drawn.vertexLight = object.vertexLight != 0;
    return drawn;
}
bool Seen(const FaunaObject& object, const World::TerrainPolygon& polygon)
{
    return World::PolygonContains(polygon, F32(static_cast<double>(object.position.x) * .01f),
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
    origin.z = F32(World::TerrainHeightAt(*context.terrain.heights, *context.terrain.attributes, origin.x, origin.y,
                                          context.fixedHeight));
    // 446e9d..446eb3: sólo XYZ; la escala del último cuerpo permanece retenida.
    context.carry.models[static_cast<std::uint16_t>(object.type)].position = origin;
    const auto* model = context.models.Find(static_cast<std::uint16_t>(object.type));
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
    Render::Models::OriginalMatrix matrix{};
    if (index < context.carry.bones.size())
        matrix = Render::Models::OriginalMatrixOf(context.carry.bones[index]);
    if (translated)
    {
        const auto& transform = context.carry.models.at(static_cast<std::uint16_t>(object.type));
        return Render::Models::OriginalTransformPosition(matrix, offset, transform.scale, transform.position);
    }
    return Render::Models::OriginalVectorTransform(offset, matrix);
}
Core::Expected<void, std::string> BirdExtras(FaunaObject& bird, FaunaDrawContext& context)
{
    if (bird.type == 0xae)
    {
        const auto light = World::BirdGlowLight(bird.type, context.cloth.random);
        static_cast<void>(context.sprites.Create(0x5ac, bird.position, 1, *light, 0, 0));
    }
    if (bird.type == 0xbc)
    {
        if (!context.carry.models.contains(static_cast<std::uint16_t>(bird.type)))
            return Core::Unexpected(std::string("446d51: origen/escala retenidos del modelo BC sin evidencia"));
        const auto left = BonePoint(context, bird, 1, {-5, 0, 0}, true);
        const auto light = World::BirdGlowLight(bird.type, context.cloth.random);
        static_cast<void>(context.sprites.Create(0x5ac, left, .1f, *light, 0, 0));
        const auto right = BonePoint(context, bird, 1, {5, 0, 0}, true);
        static_cast<void>(context.sprites.Create(0x5ac, right, .1f, *light, 0, 0));
    }
    if (bird.type != StormBird)
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
    if (object.seen == 0 || (bird ? object.type == InvisibleBird : object.type == -1))
        return {};
    const auto drawn = Drawn(object, context.lighting);
    if (const auto* model = context.models.Find(drawn.model))
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
        auto pose = PublishFaunaPose(context.carry, context.models, drawn, !bird);
        if (!pose.HasValue())
            return pose;
    }
    DrawObjectFromMeshes(batch, context.models, drawn, context.frameClock, context.carry.meshes);
    // 446c35..446c80 son draws puros, también visibles en la proyección entre pasos nativos.
    if (bird && object.type == StormBird && object.counter == 1)
    {
        DrawObjectFromMeshes(batch, context.models, drawn, context.frameClock, context.carry.meshes,
                             Render::Models::ShinePass::Shiny);
        DrawObjectFromMeshes(batch, context.models, drawn, context.frameClock, context.carry.meshes,
                             Render::Models::ShinePass::Chrome);
    }
    if (bird && context.originalFrame)
    {
        auto extras = BirdExtras(object, context);
        if (!extras.HasValue())
            return extras;
    }
    const bool shadow = context.map != Icarus && (bird || (object.type != 0xb9 && object.type != 0xba));
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
                if (!birds || object.type != InvisibleBird)
                    object.angles.z = F32(static_cast<double>(object.angles.z) + QuarterTurn);
                if (auto result = DrawOne(batch, object, context, birds); !result.HasValue())
                    return result;
            }
            return {};
        }
        FaunaDrawCarry carry = context.carry;
        FaunaDrawContext projected{context.fauna,   context.models,     context.terrain,    context.lighting,
                                   context.polygon, context.cloth,      context.sprites,    carry,
                                   context.map,     context.storm,      context.frameClock, context.particles,
                                   false,           context.fixedHeight};
        for (const auto& retained : pool)
        {
            FaunaObject object = retained;
            if (!birds || object.type != InvisibleBird)
                object.angles.z = F32(static_cast<double>(object.angles.z) + QuarterTurn);
            if (auto result = DrawOne(batch, object, projected, birds); !result.HasValue())
                return result;
        }
        return {};
    }
    for (auto& object : pool)
    {
        const bool rotate = !birds || object.type != InvisibleBird;
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
Core::Expected<void, std::string> PublishFaunaPose(FaunaDrawCarry& carry, const IModelLibrary& models,
                                                   const DrawnObject& object, bool translated)
{
    if (object.alpha < VisibleAlpha)
        return {};
    const auto* model = models.Find(object.model);
    const auto* skeleton = models.SkeletonOf(object.model);
    if (model == nullptr || skeleton == nullptr)
        return {};
    carry.models[object.model] = object.transform;
    const auto count = skeleton->bones.size();
    if (carry.bones.size() < count)
        carry.bones.resize(count, Render::Models::BoneTransform(0));
    auto bones = std::span(carry.bones).first(count);
    if (!Render::Models::PoseSkeleton(*skeleton, object.animation, object.transform, glm::vec2{}, bones))
        return Core::Unexpected(std::string("5ffdf0: pose de fauna inválida"));
    return PublishFaunaPoseRaw(carry, models, object.model, bones, object.transform, translated);
}
Core::Expected<void, std::string> PublishFaunaFloorPose(FaunaDrawCarry& carry, const IModelLibrary& models,
                                                        const DrawnObject& object)
{
    const auto* skeleton = models.SkeletonOf(object.model);
    if (skeleton == nullptr)
        return {};
    const auto count = skeleton->bones.size();
    if (carry.bones.size() < count)
        carry.bones.resize(count, Render::Models::BoneTransform(0));
    auto bones = std::span(carry.bones).first(count);
    if (!Render::Models::PoseSkeleton(*skeleton, object.animation, object.transform, {}, bones))
        return Core::Unexpected(std::string("608b0a: pose de ítem del piso inválida"));
    return PublishFaunaPoseRaw(carry, models, object.model, bones, object.transform, false,
                               object.alpha > VisibleAlpha);
}
Core::Expected<void, std::string> PublishFaunaPoseRaw(FaunaDrawCarry& carry, const IModelLibrary& models,
                                                      std::uint16_t modelId,
                                                      std::span<const Render::Models::BoneTransform> bones,
                                                      const Render::Models::ObjectTransform& transform, bool translated,
                                                      bool skin)
{
    const auto* model = models.Find(modelId);
    const auto* skeleton = models.SkeletonOf(modelId);
    const auto count = bones.size();
    carry.models[modelId] = transform;
    if (carry.bones.size() < count)
        carry.bones.resize(count, Render::Models::BoneTransform(0));
    for (std::size_t i = 0; i < count; ++i)
        if (skeleton == nullptr || i >= skeleton->bones.size() || !skeleton->bones[i].dummy)
            carry.bones[i] = bones[i];
    if (model == nullptr)
        return {};
    bones = std::span(carry.bones).first(count);
    if (skin)
    {
        if (carry.meshes.size() < model->meshes.size())
            carry.meshes.resize(model->meshes.size());
        for (std::size_t i = 0; i < model->meshes.size(); ++i)
        {
            Render::Models::SkinnedMesh mesh;
            if (!Render::Models::SkinMesh(model->meshes[i], bones, transform, mesh))
                return Core::Unexpected(std::string("4ebc80: vértices de fauna inválidos"));
            auto& output = carry.meshes[i];
            if (output.positions.size() < mesh.positions.size())
                output.positions.resize(mesh.positions.size());
            std::copy(mesh.positions.begin(), mesh.positions.end(), output.positions.begin());
            if (output.normals.size() < mesh.normals.size())
                output.normals.resize(mesh.normals.size());
            std::copy(mesh.normals.begin(), mesh.normals.end(), output.normals.begin());
        }
    }
    if (!translated)
        return {};
    // 5ffdf0 con argumento0 posa matrices en el mundo; los dummy no se vuelven a escribir.
    for (std::size_t i = 0; i < count; ++i)
    {
        if (skeleton != nullptr && i < skeleton->bones.size() && skeleton->bones[i].dummy)
            continue;
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 3; ++row)
            {
                auto& value = carry.bones[i][column][row];
                value = F32(static_cast<double>(value) * transform.scale);
                if (column == 3)
                    value = F32(static_cast<double>(value) + transform.position[row]);
            }
    }
    return {};
}
Core::Expected<void, std::string> DrawGameBirds(Render::ModelBatch& batch, FaunaDrawContext& context)
{
    return DrawPool(batch, context.fauna.Birds(), context, true);
}
Core::Expected<void, std::string> DrawGameFish(Render::ModelBatch& batch, FaunaDrawContext& context)
{
    return DrawPool(batch, context.fauna.Fish(), context, false);
}
} // namespace Client::Scenes
