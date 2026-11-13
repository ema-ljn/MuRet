#include "Scenes/GameFloorItemDrawing.h"

#include "Core/Expected.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/Skinning.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GameItemSpecialDrawing.h"
#include "World/SphereFrustum.h"
#include "World/TerrainLighting.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>

namespace Client::Scenes
{
namespace
{
constexpr std::int16_t FirstArmor = 0x1012;
constexpr std::int16_t EndArmor = 0x1a12;
constexpr std::int16_t Money = 0x1e21;
constexpr std::uint16_t Player = 0x20a;
constexpr float VisibleAlpha = 0.01f;
float F32(double value)
{
    return static_cast<float>(value);
}
std::uint8_t Level(const FloorItemState& item)
{
    return static_cast<std::uint8_t>((item.value >> 3) & 15);
}
std::uint16_t Model(const FloorItemState& item)
{
    if (item.type == 0x1e1e && Level(item) == 0)
        return 0x223a;
    if (item.type == 0x1e1e && Level(item) == 2)
        return 0x223b;
    return static_cast<std::uint16_t>(item.type);
}
float RootHeight(std::int16_t type)
{
    if (type < FirstArmor || type >= EndArmor)
        return 0;
    if (type < 0x1212)
        return -160;
    if (type < 0x1412)
        return -100;
    if (type < 0x1612)
        return -50;
    if (type < 0x1812)
        return -70;
    return 0;
}
DrawnObject Object(const FloorItemState& item, const FloorItemDrawContext& context)
{
    DrawnObject object;
    object.model = Model(item);
    object.animation = item.animation;
    object.transform = {.position = item.position, .angles = item.angles, .scale = item.scale};
    object.state = item.objectState;
    object.alpha = item.alpha;
    object.vertexLight = item.vertexLight;
    const auto terrain =
        context.lighting ? World::LightAt(*context.lighting, item.position.x, item.position.y) : glm::vec3{};
    for (int component = 0; component < 3; ++component)
        object.color[component] = F32(static_cast<double>(terrain[component]) + item.light[component]);
    return object;
}
Core::Expected<void, std::string> Pose(FaunaDrawCarry& local, const FloorItemState& item,
                                       const FloorItemDrawContext& context, DrawnObject object)
{
    if (item.type >= FirstArmor && item.type < EndArmor)
        object.model = Player;
    object.transform.lockedRootHeight = RootHeight(item.type);
    if (context.onSkin || !context.onPose)
        object.alpha = 0;
    if (!context.models.Find(object.model) || !context.models.SkeletonOf(object.model))
        return Core::Unexpected(std::format("0x608a98: faltan modelo o pose del ítem {:#x}", object.model));
    if (context.onPose)
        return context.onPose(object);
    return PublishFaunaFloorPose(local, context.models, object);
}
Core::Expected<void, std::string> RenderItem(FaunaDrawCarry& local, FloorItemState& item,
                                             const FloorItemDrawContext& context, DrawnObject object,
                                             std::span<const Render::Models::SkinnedMesh> meshes)
{
    auto resources = context.itemContext ? *context.itemContext : ItemDrawContext{};
    auto special = resources.special ? *resources.special : ItemSpecialDrawContext{};
    special.worldClock = context.worldClock;
    special.objectFlags = std::bit_cast<std::uint32_t>(item.effectCounter);
    special.objectLight = &item.light;
    special.publishedObject = &object;
    resources.special = &special;
    resources.map = context.map;
    resources.ownerContext = &item;
    if (!context.onPose)
        resources.poseBones = [&local]() -> std::span<const Render::Models::BoneTransform> { return local.bones; };
    const auto result = DrawItemFromMeshes(context.batch, context.models, object, context.frameClock, meshes,
                                           item.type == Money ? 0 : Level(item), item.excellent, item.ancient,
                                           context.glowDetail, resources);
    item.objectState = object.state;
    item.alpha = object.alpha;
    return result;
}
Core::Expected<void, std::string> Body(FaunaDrawCarry& local, FloorItemState& item, const FloorItemDrawContext& context,
                                       DrawnObject object)
{
    if (!(item.alpha > VisibleAlpha))
        return {};
    // 0x60cf70 → 0x4ebc80: la caja del picking (+0x13c) desde la posición de este cuerpo, que en el último (el del
    // ítem, después del agua) es la que lee 0x59e2e0.
    item.pickBox = {item.position + item.boundingMin, item.position + item.boundingMax};
    const auto* model = context.models.Find(object.model);
    if (!model)
        return Core::Unexpected(std::format("0x60cfc8: falta el modelo del ítem {:#x}", object.model));
    const bool separateSkin = (item.type >= FirstArmor && item.type < EndArmor) || item.type == Money ||
                              context.map == 10 || (context.map >= 24 && context.map <= 29) || context.map == 36;
    if (separateSkin && context.onPose && !context.onSkin)
        return Core::Unexpected(std::string("0x60d186: falta el skin separado de la pose del piso"));
    if (context.onSkin)
    {
        if (auto result = context.onSkin(object); !result.HasValue())
            return result;
    }
    else if (!context.onPose)
    {
        if (auto result = TransformFaunaPose(local, context.models, object); !result.HasValue())
            return result;
    }
    if (context.onPose && !context.posedMeshes)
        return Core::Unexpected(std::string("0x4ebc80: falta el scratch retenido del piso"));
    const auto meshes =
        context.onPose ? context.posedMeshes() : std::span<const Render::Models::SkinnedMesh>(local.meshes);
    return RenderItem(local, item, context, object, meshes);
}

Core::Expected<void, std::string> Project(FloorItemState& item, const FloorItemDrawContext& context)
{
    if (!context.project)
        return Core::Unexpected(std::string("0x608da5: falta la proyección del ítem del piso"));
    auto point = item.position;
    constexpr float NameHeight = 30;
    point.z = F32(static_cast<double>(point.z) + NameHeight);
    const auto projected = context.project(point);
    if (!projected.HasValue())
        return Core::Unexpected(projected.Error());
    item.screenX = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(projected.Value().x));
    item.screenY = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(projected.Value().y));
    return {};
}
Core::Expected<void, std::string> Coins(FaunaDrawCarry& local, FloorItemState& item,
                                        const FloorItemDrawContext& context, const DrawnObject& object,
                                        std::size_t slot)
{
    if (item.type != Money)
        return {};
    constexpr std::size_t TableSize = 100;
    if (context.randomTable.size() < TableSize)
        return Core::Unexpected(std::string("0x608bf5: falta la tabla global de cien valores de monedas"));
    const auto position = item.position;
    const auto count =
        std::clamp(item.value >= 0 ? static_cast<int>(std::sqrt(static_cast<double>(item.value))) / 2 : 0, 3, 80);
    for (int coin = 1; coin < count; ++coin)
    {
        constexpr std::size_t Start = 1;
        constexpr std::size_t Stride = 20;
        const auto rotationIndex = (Start + Stride * slot + static_cast<std::size_t>(coin - 1)) % TableSize;
        const auto radiusIndex = (slot + static_cast<std::size_t>(coin)) % TableSize;
        const glm::vec3 angles{0, 0, static_cast<float>(context.randomTable[rotationIndex] % 360)};
        const glm::vec3 offset{static_cast<float>(context.randomTable[radiusIndex] % (count + 20)), 0, 0};
        const auto rotated = Render::Models::OriginalVectorRotate(offset, Render::Models::OriginalAngleMatrix(angles));
        auto coinObject = object;
        for (int component = 0; component < 3; ++component)
            coinObject.transform.position[component] =
                F32(static_cast<double>(rotated[component]) + position[component]);
        item.position = coinObject.transform.position;
        if (auto result = Body(local, item, context, coinObject); !result.HasValue())
            return result;
    }
    item.position = position;
    return {};
}
Core::Expected<void, std::string> Water(DrawnObject& object, const FloorItemDrawContext& context, std::size_t slot)
{
    if (context.map == 10)
    {
        constexpr float Frequency = 0.002f;
        constexpr float Amplitude = 10;
        constexpr std::size_t SlotPhase = 0x4d5;
        const auto wave = Render::Models::Detail::SineProductPC53(
            (static_cast<double>(slot * SlotPhase) + context.worldClock) * Frequency, Amplitude);
        if (!wave)
            return Core::Unexpected(std::string("0x608d05: fase acuática fuera del perfil portable"));
        object.transform.position.z = F32(*wave + object.transform.position.z);
        return {};
    }
    if ((context.map < 24 || context.map > 29) && context.map != 36)
        return {};
    if (!context.waterHeight)
        return Core::Unexpected(std::string("0x608d29: falta la altura de agua Kalima"));
    const auto height = context.waterHeight(object.transform.position.x, object.transform.position.y);
    if (!height.HasValue())
        return Core::Unexpected(height.Error());
    constexpr float FloatingHeight = 180;
    object.transform.position.z = F32(static_cast<double>(height.Value()) + FloatingHeight);
    return {};
}
} // namespace
Core::Expected<void, std::string> DrawFloorItemDrawing(std::span<FloorItemState> items,
                                                       const FloorItemDrawContext& context)
{
    FaunaDrawCarry local;
    constexpr float Radius = 400;
    for (std::size_t slot = 0; slot < items.size(); ++slot)
    {
        auto& item = items[slot];
        if (!item.alive)
            continue;
        item.seen = World::SphereVisible(context.frustum, item.position, Radius);
        if (!item.seen)
            continue;
        auto object = Object(item, context);
        if (auto result = Pose(local, item, context, object); !result.HasValue())
            return result;
        if (auto result = Coins(local, item, context, object, slot); !result.HasValue())
            return result;
        const auto position = item.position;
        if (auto result = Water(object, context, slot); !result.HasValue())
            return result;
        item.position = object.transform.position;
        if (auto result = Body(local, item, context, object); !result.HasValue())
            return result;
        item.position = position;
        if (auto result = Project(item, context); !result.HasValue())
            return result;
    }
    return {};
}
} // namespace Client::Scenes
