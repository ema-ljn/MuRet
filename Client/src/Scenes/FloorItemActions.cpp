#include "Scenes/FloorItemActions.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/RayPicking.h"
#include "Scenes/FloorItemPackets.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameText.h"
#include "UI/MessageArea.h"
#include "UI/SprintfFormat.h"
#include "World/ItemState.h"
#include "World/TerrainHeight.h"
#include "World/TerrainPickingSegment.h"
#include "World/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
// 0x59e30e y 0x59e37f: la luz de los ítems a la vista (0x3e4ccccd) y la del elegido (0x3fc00000).
constexpr float SeenLight = 0.2f;
constexpr float PickedLight = 1.5f;
// 0x566767..0x56679b: el centro de la celda (· 100 + 50, 0x6b86b4 y 0x6b86b0) y los 150 de 0x6b87ac.
constexpr float CellSize = 100.0f;
constexpr float CellHalf = 50.0f;
constexpr double ArrivalDistance = 150.0;
// 0x5667ac: la espera del mouse.
constexpr std::int32_t PickUpMouseUpdateTimeMax = 6;
// 0x566e29: "no hay lugar" (0x76d8950); 0x566e69 y 0x566e76: 3 (0x6b86b8) sobre el terreno y la caída en 50.
constexpr std::size_t NoRoomText = 375;
constexpr double NoRoomHeight = 3.0;
constexpr float NoRoomDropSpeed = 50.0f;
// 0x5ac970: los 300 de 0x6b8900.
constexpr double SpaceDistance = 300.0;
// 0x5650ef: el caso 1 de la tabla de 0x56ab20.
constexpr std::uint8_t PickUpMovement = 1;

// 0x5667d7..0x566b40 y 0x566e90..: el pedido y el C1 05 22, si no hay otro.
Core::Expected<void, std::string> RequestThePickUp(const InventoryMoveContext& context)
{
    FloorItemGlobals& floor = context.world.Runtime().floor;
    if (floor.pendingPickUp != -1)
        return {};
    floor.pendingPickUp = floor.chosen;
    context.random.Discard(2);
    if (!context.services.sendPickUp)
        return Core::Unexpected(std::string("0x566bd2: el C1 05 22 sin la sesión"));
    return context.services.sendPickUp(static_cast<std::uint16_t>(floor.chosen & 0xffff));
}
} // namespace

std::int32_t PickTheFloorItem(std::span<FloorItemState> items, const World::TerrainPickingSegment& ray)
{
    // 0x59e2f0..0x59e31f.
    for (FloorItemState& item : items)
    {
        if (!item.alive || !item.seen)
            continue;
        item.vertexLight = true;
        item.light = glm::vec3(SeenLight);
    }
    // 0x59e321..0x59e3a6.
    for (std::size_t index = 0; index < items.size(); ++index)
    {
        FloorItemState& item = items[index];
        if (!item.alive || !item.seen || !Render::Models::SegmentHitsBox(ray.origin, ray.end, item.pickBox))
            continue;
        item.vertexLight = false;
        item.light = glm::vec3(PickedLight);
        return static_cast<std::int32_t>(index);
    }
    return -1;
}

Core::Expected<bool, std::string> HasRoomFor(const Data::Tables::ItemTable* items,
                                             std::span<const World::ItemState> cells, std::int16_t type)
{
    const auto footprint = ItemFootprintOf(items, static_cast<std::uint16_t>(type), "0x5c9d20");
    if (!footprint.HasValue())
        return Core::Unexpected(footprint.Error());
    constexpr auto Columns = static_cast<std::int32_t>(InventoryColumns);
    const auto rows = static_cast<std::int32_t>(cells.size()) / Columns;
    const std::int32_t width = footprint.Value().width;
    const std::int32_t height = footprint.Value().height;
    for (std::int32_t row = rows - height; row >= 0; --row)
    {
        for (std::int32_t column = Columns - width; column >= 0; --column)
        {
            bool empty = true;
            for (std::int32_t y = row; y < row + height && empty; ++y)
            {
                for (std::int32_t x = column; x < column + width && empty; ++x)
                    empty = cells[static_cast<std::size_t>(y * Columns + x)].id == EmptyOwnItemId;
            }
            if (empty)
                return true;
        }
    }
    return false;
}

Core::Expected<void, std::string> PickUpTheFloorItem(const InventoryMoveContext& context, const glm::vec3& actor,
                                                     float fixedHeight)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    FloorItemGlobals& floor = runtime.floor;
    if (floor.fromSpace == 0)
    {
        // 0x56675e..0x5667ac: en la x87, con el destino como entero con signo (FILD); con NaN sigue.
        const auto center = [](std::uint32_t cell)
        { return static_cast<double>(static_cast<std::int32_t>(cell)) * CellSize + static_cast<double>(CellHalf); };
        const double dx = static_cast<double>(actor.x) - center(runtime.movement.targetX);
        const double dy = static_cast<double>(actor.y) - center(runtime.movement.targetY);
        if (std::sqrt(dx * dx + dy * dy) > ArrivalDistance)
            return {};
        runtime.movement.mouseUpdateTimeMax = PickUpMouseUpdateTimeMax;
    }
    if (floor.chosen < 0 || floor.chosen >= FloorItemCount)
        return Core::Unexpected(
            std::format("0x5667c6: el ítem elegido {} está fuera de la tabla 0x77f46c8", floor.chosen));
    FloorItemState& item = world.DrawTail().floorItems[static_cast<std::size_t>(floor.chosen)];
    if (item.itemType == ZenItem)
        return RequestThePickUp(context);
    // 0x566e0a..0x566e1f.
    const auto room = HasRoomFor(context.services.items, world.Inventory().cells, item.itemType);
    if (!room.HasValue())
        return Core::Unexpected(room.Error());
    if (room.Value())
        return RequestThePickUp(context);
    // 0x566e23..0x566e42: sprintf(buffer, 375) y 0x555190 con el nombre vacío de 0x77f3bec.
    const std::optional<std::string_view> format = UI::TextAt(context.services.texts, NoRoomText);
    if (!format)
        return Core::Unexpected(std::format("0x566e2f: falta el texto {} de Text.bmd", NoRoomText));
    if (!context.services.chatLine)
        return Core::Unexpected(std::string("0x566e42: 0x555190 con el aviso 375 sin el chat"));
    if (auto said = context.services.chatLine(UI::Sprintf(*format), UI::SystemMessage); !said.HasValue())
        return Core::Unexpected(std::format("0x566e42, 0x555190: {}", said.Error()));
    // 0x566e47..0x566e76.
    const World::WorldTerrain& terrain = world.Map().terrain;
    if (!terrain.heights || !terrain.attributes)
        return Core::Unexpected(std::string("0x566e64: 0x5f93c0 sin el terreno del mapa"));
    const double ground =
        World::TerrainHeightAt(*terrain.heights, *terrain.attributes, item.position.x, item.position.y, fixedHeight);
    item.position.z = static_cast<float>(ground + NoRoomHeight);
    item.dropSpeed = NoRoomDropSpeed;
    return {};
}

Core::Expected<void, std::string> PickUpTheNearestFloorItem(const InventoryMoveContext& context, float fixedHeight)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    auto& items = world.DrawTail().floorItems;
    for (std::size_t index = 0; index < items.size(); ++index)
    {
        const FloorItemState& item = items[index];
        if (!item.alive || !item.seen)
            continue;
        // 0x5ac947..0x5ac97b: la distancia en 3D a la posición del héroe (+0x10), en la x87.
        const glm::vec3& hero = world.Actors()[runtime.hero].position;
        const double dx = static_cast<double>(item.position.x) - static_cast<double>(hero.x);
        const double dy = static_cast<double>(item.position.y) - static_cast<double>(hero.y);
        const double dz = static_cast<double>(item.position.z) - static_cast<double>(hero.z);
        // TEST AH, 1: menos de 300 o NaN.
        if (std::sqrt(dx * dx + dy * dy + dz * dz) >= SpaceDistance)
            continue;
        // 0x5ac996..0x5ac9c2.
        world.Actors()[runtime.hero].movementType = PickUpMovement;
        runtime.floor.chosen = static_cast<std::int32_t>(index);
        runtime.floor.fromSpace = 1;
        auto picked = PickUpTheFloorItem(context, item.position, fixedHeight);
        runtime.floor.fromSpace = 0;
        world.Actors()[runtime.hero].movementType = 0;
        return picked;
    }
    return {};
}
} // namespace Client::Scenes
