#include "Scenes/GameFloorItems.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/GameDrawTail.h"
#include "World/Particles.h"
#include "World/TerrainHeight.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <span>
#include <string>

namespace Client::Scenes
{
namespace
{
bool Between(std::int16_t type, std::int16_t first, std::int16_t end)
{
    return type >= first && type < end;
}
} // namespace

// 607fb0..608348: ninguna rama consume RNG ni pone la velocidad en cero.
void SettleFloorItem(FloorItemState& item)
{
    const auto type = item.type;
    item.angles = {0, 0, -45};
    if (Between(type, 0x212, 0x612))
    {
        item.angles.x = 60;
        if (type == 0x225)
            item.scale = 0.7f;
        return;
    }
    if (Between(type, 0xa1a, 0xa23) || Between(type, 0xa24, 0xc12) || type == 0x223e || type == 0x1e23 ||
        type == 0x1c34)
        item.angles.x = 90;
    else if (Between(type, 0x612, 0xe12))
        item.angles.y = 270;
    else if (Between(type, 0xe12, 0x1012))
        item.angles = {0, 270, 225};
    else if (Between(type, 0x1212, 0x1812))
        item.angles.x = 270;
    else if (Between(type, 0x1a12, 0x1c12) || type == 0x2242 || type == 0x2243)
        item.angles = {270, 0, 45};
    else if (Between(type, 0x1e32, 0x1e35))
    {
        item.angles.z = 90;
        item.scale = 0.7f;
    }
    else if (type == 0x2244)
        item.scale = 0.2f;
    else if (type == 0x223f)
    {
        item.angles.z = 45;
        item.scale = 1.1f;
    }
    else if (type == 0x1e29)
        item.angles = {0, 45, 45};
    else if (type == 0x1e2a || Between(type, 0x1e2b, 0x1e2d) || type == 0x1c20 || type == 0x2241 || type == 0x1e26 ||
             type == 0x224a)
    {
        item.angles.z = 45;
        if (type == 0x224a)
            item.scale = 0.5f;
    }
    else if (type == 0x1e24)
        item.angles = {270, 0, 270};
    else if (type == 0x1e25 || type == 0x1e27)
        item.angles = {270, 0, 90};
    else if (type == 0x1e2f)
        item.angles = {90, 0, 70};
    else if (type == 0x2245)
    {
        item.angles = {115, 75, 8};
        item.scale = 0.4f;
    }
    else if (type == 0x1c22 || type == 0x1c23)
    {
        item.angles = {-45, -5, 18};
        item.scale = 0.48f;
    }
    else if (type == 0x1c24)
    {
        item.angles = {165, -168, 198};
        item.scale = 0.48f;
    }
    else if (type == 0x1c30)
    {
        item.angles = {-45, 0, 45};
        item.scale = 0.5f;
    }
    else if (type == 0x2246 || type == 0x2247)
    {
        item.angles = {160, -183, 198};
        item.scale = type == 0x2246 ? 0.38f : 0.54f;
    }
    else if (Between(type, 0x1c27, 0x1c2b))
        item.angles.z = 20;
    else if (type == 0x1c33)
    {
        item.angles.z = 45;
        item.scale = 1.2f;
    }
    else if (type == 0x1c35 || type == 0x1c36 || type == 0x1c37)
    {
        item.angles.z = type == 0x1c37 ? 180.0f : 90.0f;
        if (type == 0x1c36)
            item.scale = 1.3f;
    }
}

namespace
{
Core::Expected<void, std::string> Spark(FloorItemState& item, const FloorItemStepContext& context)
{
    constexpr std::int32_t Interval = 48;
    const auto old = item.effectCounter;
    item.effectCounter = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(old) + 1u);
    if (old % Interval != 0)
        return {};
    const auto matrix = Render::Models::OriginalAngleMatrix(item.angles);
    const auto x = static_cast<float>(context.random.Next() % 32 + 16);
    const auto z = static_cast<float>(context.random.Next() % 32 + 16);
    const auto rotated = Render::Models::OriginalVectorRotate({x, 0, z}, matrix);
    const glm::vec3 position{static_cast<float>(static_cast<double>(rotated.x) + item.position.x),
                             static_cast<float>(static_cast<double>(rotated.y) + item.position.y),
                             static_cast<float>(static_cast<double>(rotated.z) + item.position.z)};
    for (const std::int32_t subtype : {0, 1})
        if (auto created = context.particles.CreateChecked(0x5fc, position, item.angles, glm::vec3(1), subtype, 1,
                                                           context.worldClock, context.random);
            !created.HasValue())
            return Core::Unexpected(std::format("{}: la chispa 0x5fc/{} del piso falla ({})",
                                                subtype == 0 ? "0x60884a" : "0x608866", subtype,
                                                static_cast<int>(created.Error())));
    return {};
}
} // namespace

Core::Expected<void, std::string> StepFloorItems(std::span<FloorItemState> items, const FloorItemStepContext& context)
{
    for (auto& item : items)
    {
        if (!item.alive)
            continue;
        item.position.z = static_cast<float>(static_cast<double>(item.dropSpeed) + item.position.z);
        item.dropSpeed = static_cast<float>(static_cast<double>(item.dropSpeed) - 6.0);
        constexpr float GroundHeight = 30.0f; // 6088c5:6b8858=00 00 f0 41.
        double ground = World::TerrainHeightAt(context.heights, context.attributes, item.position.x, item.position.y,
                                               context.fixedHeight) +
                        GroundHeight;
        if (Between(item.type, 0x212, 0xe12))
            ground += 40.0;
        if (ground >= item.position.z)
        {
            item.position.z = static_cast<float>(ground);
            SettleFloorItem(item);
        }
        else if (Between(item.type, 0xe12, 0x1012))
            item.angles.z = static_cast<float>(static_cast<double>(item.dropSpeed) * -10.0);
        else
            item.angles.y = static_cast<float>(static_cast<double>(item.dropSpeed) * -10.0);
        if (auto emitted = Spark(item, context); !emitted.HasValue())
            return emitted;
    }
    return {};
}
} // namespace Client::Scenes
