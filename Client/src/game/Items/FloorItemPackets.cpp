#include "Items/FloorItemPackets.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Protocol/Items/FloorItems.h"
#include "Protocol/Items/Inventory.h"
#include "Models/LogicalAccess.h"
#include "Models/ObjectState.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Kernel/DrawTailState.h"
#include "Items/GameFloorItems.h"
#include "Chat/GameMessages.h"
#include "Items/InventoryMoves.h"
#include "Items/ItemNames.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Items/OwnItemPlacement.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameDialogs.h"
#include "Ui/GameText.h"
#include "Ui/MessageArea.h"
#include "Crt/SprintfFormat.h"
#include "Camera/GameCamera.h"
#include "Effects/ModelEffects.h"
#include "Terrain/TerrainHeight.h"
#include "Terrain/WorldTerrain.h"
#include "Terrain/TerrainDefines.h"
#include "Audio/SoundId.h"
#include "Sound/SoundIds.h"
#include "Ui/GameUiState.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Items
{
namespace
{
// 0x608417.
constexpr std::int32_t ItemModelBase = 0x212;
// 0x6085c4..0x6085ef: the ±30 box (0xc1f00000 and 0x41f00000).
constexpr float BoundingHalf = 30.0f;
// 0x607b68..0x607bfd: the light of 0.3, the scale of 0.8 (0.7 from 0x812 to 0xe11), the alpha and the pass light at 1.
constexpr float ObjectLight = 0.3f;
constexpr float ItemScale = 0.8f;
constexpr float SmallItemScale = 0.7f;
constexpr std::int16_t FirstSmallModel = 0x812;
constexpr std::int16_t EndSmallModel = 0xe12;
// 0x608633 and 0x60863c: 180 (0x6b86bc) above the terrain and the speed 20.
constexpr double DropHeight = 180.0;
constexpr float DropSpeed = 20.0f;
// 0x608614..0x608722: the silver and gold medals (0x1c0b of level 5 and 6 through 0x608750's table; the GS 0.97 drops
// them with gMedalEvent) just dropped: 3 (0x6b86b8) above the terrain, the cracks 0x100 (0x6086ce) and 0xfe (0x6086f1)
// of size 0x37 and the speed 50 (0x60870f).
constexpr std::int16_t SilverMedalModel = 0x2242;
constexpr std::int16_t GoldMedalModel = 0x2243;
constexpr double MedalHeight = 3.0;
constexpr float MedalDropSpeed = 50.0f;
constexpr std::array<std::int16_t, 2> MedalCracks{0x100, 0xfe};
constexpr std::int16_t MedalCrackSize = 0x37;
// 0x4d0bdd and 0x4d0be9: (cell + 0.5) · 100.
constexpr float CellHalf = 0.5f;
using Terrain::CellSize;
// 0x41ed10's sounds (CL10): 0x6083a5 zen, 0x60840a the jewels and 0x608400 the rest; 0x4d10af and 0x4d10a2 the 22's.
constexpr Audio::SoundId ZenDropSound{0x1f};
constexpr Audio::SoundId JewelSound = Sound::GemSound;
constexpr Audio::SoundId ItemDropSound{0x1e};
constexpr Audio::SoundId ItemPickUpSound = Sound::GetItemSound;
// 0x4d0f85..0x4d0f9c and 0x4d102a..0x4d1036: the Text.bmd texts of the 22's notice.
constexpr std::size_t ZenText = 224;
constexpr std::size_t PickedUpText = 918;
// 0x4de7a5..0x4de7ca and 0x4dea18..0x4dea32: the fruit's notice, with 1412 when adding and 1903 when removing.
constexpr std::size_t FruitText = 379;
constexpr std::size_t FruitAddedText = 1412;
constexpr std::size_t FruitRemovedText = 1903;

std::int32_t LevelOf(std::int32_t value)
{
    return value >> 3;
}

// 0x6083ca..0x608402 and 0x4d1060..0x4d1097: the types that sound with 0x45 when dropped (with 0x181e and 0x181f, also
// when picked up).
bool IsAJewel(std::int32_t type, bool pickingUp)
{
    return type == 0x1c0d || type == 0x1c0e || type == 0x1c10 || type == 0x180f || type == 0x1c16 || type == 0x1c1f ||
           (pickingUp && (type == 0x181e || type == 0x181f));
}

// 0x608423..0x6085b8: the models that change by type and level (value >> 3, without the 4-bit mask). The 0x1a13 (the
// quest's archangel weapon, level 0 to 2: the staff, the sword and the crossbow) also sets the value to 0.
std::int16_t ModelOf(std::int32_t type, std::int32_t& value)
{
    const auto model = static_cast<std::int16_t>(type + ItemModelBase);
    const std::int32_t level = LevelOf(value);
    switch (type)
    {
    case 0x1c0b:
    {
        // 0x608439..0x608497, 0x608750's table through 0x608770 with the level − 1: 13 writes 0x2240 and then 0x223f
        // (0x60848b falls into 0x608491).
        constexpr std::array<std::int16_t, 15> Boxes{0x223e, 0x223f, 0x2240, 0,      0x2242, 0x2243, 0,     0x2244,
                                                     0x2244, 0x2244, 0x2244, 0x2244, 0x223f, 0x223f, 0x223f};
        if (level < 1 || level > 15 || Boxes[static_cast<std::size_t>(level - 1)] == 0)
            return model;
        return Boxes[static_cast<std::size_t>(level - 1)];
    }
    case 0x1c15:
        return level >= 1 && level <= 2 ? std::int16_t{0x2245} : model;
    case 0x1a13:
    {
        constexpr std::array<std::int16_t, 3> ArchangelWeapons{0xc1c, 0x225, 0xa24};
        if (level < 0 || level > 2)
            return model;
        value = 0;
        return ArchangelWeapons[static_cast<std::size_t>(level)];
    }
    case 0x1c17:
        return level == 1 ? std::int16_t{0x2246} : model;
    case 0x1c18:
        return level == 1 ? std::int16_t{0x2247} : model;
    case 0x1a14:
        if (level == 0)
            return 0x2249;
        return level == 1 || level == 3 ? std::int16_t{0x2248} : model;
    case 0x1c09:
        return level == 1 ? std::int16_t{0x2241} : model;
    case 0x1a0e:
        // 0x608596: AND AL, 0xf8 on the integer value.
        return (value & ~7) == 8 ? std::int16_t{0x224a} : model;
    case 0x1a0b:
        return (value & ~7) == 8 ? std::int16_t{0x224c} : model;
    default:
        return model;
    }
}

// 0x607b60 on the item's object: what the floor draws and steps on afterwards.
void InitializeTheObject(Kernel::FloorItemState& item, const FloorItemCreation& creation)
{
    item.light = glm::vec3(ObjectLight);
    item.vertexLight = true;
    item.animation = {};
    item.dropSpeed = 0.0f;
    item.alpha = 1.0f;
    item.scale = item.type >= FirstSmallModel && item.type < EndSmallModel ? SmallItemScale : ItemScale;
    const auto model = static_cast<std::uint16_t>(item.type);
    Models::ObjectRandoms randoms{};
    for (std::size_t index = 0; index < Actors::Drawing::HangingRandomCount(model); ++index)
        randoms[index] = creation.random.Next();
    item.objectState = Models::NativeItemObjectState(model, creation.clock, randoms);
}

Core::Expected<std::string_view, std::string> TextOf(const Tables::TextTable* texts, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = Ui::TextAt(texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: falta el texto {} de Text.bmd", where, index));
    return *text;
}

// 0x555190(name, text, 1): the names the 22 passes (0x5767994 and 0x5767998) are buffers nobody writes.
Core::Expected<void, std::string> SystemLine(const Scenes::WorldEntryResources& resources, std::string_view text,
                                             std::string_view where)
{
    if (resources.textMeasure == nullptr)
        return Core::Unexpected(std::format("{}: 0x555190 sin el medidor de los textos", where));
    const Core::PixelSize screen = resources.screen ? resources.screen() : Core::PixelSize{640, 480};
    return Chat::AddGameMessage(resources.world.Runtime(), *resources.textMeasure, screen, {}, text, Ui::SystemMessage);
}

Core::Expected<void, std::string> PickUpMessage(const Protocol::Items::PickUpResult& result,
                                                const Scenes::WorldEntryResources& resources)
{
    const Kernel::WorldRuntime& runtime = resources.world.Runtime();
    if (runtime.floor.chosen < 0 || runtime.floor.chosen >= FloorItemCount)
        return Core::Unexpected(
            std::format("0x4d0f5f: el elegido {} está fuera de la tabla 0x77f46c8", runtime.floor.chosen));
    const Kernel::FloorItemState& chosen =
        resources.world.DrawTail().floorItems[static_cast<std::size_t>(runtime.floor.chosen)];
    const auto picked = TextOf(resources.texts, PickedUpText, "0x4d0f85");
    if (!picked.HasValue())
        return Core::Unexpected(picked.Error());
    if (result.result == Protocol::Items::ZenPickedUp)
    {
        // 0x4d0f5f..0x4d0fb4: with the picked one alive.
        if (!chosen.alive)
            return {};
        const auto zen = TextOf(resources.texts, ZenText, "0x4d0f8a");
        if (!zen.HasValue())
            return Core::Unexpected(zen.Error());
        return SystemLine(resources, Crt::Sprintf("%d %s %s", chosen.value, zen.Value(), picked.Value()), "0x4d0faf");
    }
    // 0x4d0fdd..0x4d1049: the picked one's name by its type and level, alive or not.
    const auto name = ItemNameOf(resources.items, resources.texts, &resources.world.Runtime().npcNames,
                                 static_cast<std::uint16_t>(chosen.itemType), (chosen.value >> 3) & 15);
    if (!name.HasValue())
        return Core::Unexpected(std::format("0x4d1019: {}", name.Error()));
    return SystemLine(resources, Crt::Sprintf("%s %s", std::string_view(name.Value()), picked.Value()), "0x4d1049");
}

Core::Expected<void, std::string> FruitBox(Kernel::WorldState& world, const Tables::TextTable* texts, std::size_t text,
                                           std::string_view where)
{
    const auto line = TextOf(texts, text, where);
    if (!line.HasValue())
        return Core::Unexpected(line.Error());
    Kernel::WorldRuntime& runtime = world.Runtime();
    Ui::OpenMessageBox(runtime.dialogBox, runtime.dialogs, line.Value());
    return {};
}

// 0x4deab1..0x4deb04: sprintf(buffer, text) and 0x632e30.
Core::Expected<void, std::string> FormattedFruitBox(Kernel::WorldState& world, const Tables::TextTable* texts,
                                                    std::size_t text, std::string_view where)
{
    const auto line = TextOf(texts, text, where);
    if (!line.HasValue())
        return Core::Unexpected(line.Error());
    Kernel::WorldRuntime& runtime = world.Runtime();
    Ui::OpenMessageBox(runtime.dialogBox, runtime.dialogs, Crt::Sprintf(line.Value()));
    return {};
}

// 0x4de813 and 0x4dea7b: sprintf(buffer, 1904, the adding or removing text) and 0x632e30.
Core::Expected<void, std::string> FruitFailureBox(Kernel::WorldState& world, const Tables::TextTable* texts,
                                                  std::size_t kind, std::string_view where)
{
    const auto format = TextOf(texts, 1904, where);
    const auto argument = TextOf(texts, kind, where);
    if (!format.HasValue())
        return Core::Unexpected(format.Error());
    if (!argument.HasValue())
        return Core::Unexpected(argument.Error());
    Kernel::WorldRuntime& runtime = world.Runtime();
    Ui::OpenMessageBox(runtime.dialogBox, runtime.dialogs, Crt::Sprintf(format.Value(), argument.Value()));
    return {};
}

// 0x4de53a..0x4de7e4 (adding) and 0x4de849..0x4dea4c (removing): the own record's stat, the points and the notice.
Core::Expected<void, std::string> EatTheFruit(const Protocol::Items::StatFruit& fruit, Kernel::WorldState& world,
                                              Crt::CrtRandom& random, const Tables::TextTable* texts, bool adding)
{
    // 0x4deb74 and 0x4deb88: the slot's stat and its text (energy, vitality, agility, strength and command).
    constexpr std::array<std::size_t, 5> StatTexts{168, 169, 167, 166, 1900};
    Kernel::WorldRuntime& runtime = world.Runtime();
    {
        const Models::LogicalAccess own(runtime.ownRecordDepth, random, Models::OwnRecordCloseRandoms);
        Kernel::OwnCharacterRecord& record = world.OwnCharacter();
        const std::array<std::uint16_t*, 5> stats{&record.energy, &record.vitality, &record.agility, &record.strength,
                                                  &record.leadership};
        std::uint16_t& stat = *stats[fruit.stat];
        const auto add = [](std::uint16_t& word, std::int32_t amount)
        { word = static_cast<std::uint16_t>(static_cast<std::int32_t>(word) + amount); };
        if (adding)
        {
            add(stat, fruit.points);
            add(record.addedPoints, fruit.points);
        }
        else
        {
            add(stat, -static_cast<std::int32_t>(fruit.points));
            add(record.levelUpPoints, fruit.points);
            add(record.word48, fruit.points);
        }
    }
    const auto format = TextOf(texts, FruitText, "0x4de7ca");
    const auto statName = TextOf(texts, StatTexts[fruit.stat], "0x4de7c2");
    const auto tail = TextOf(texts, adding ? FruitAddedText : FruitRemovedText, "0x4de7a5");
    if (!format.HasValue())
        return Core::Unexpected(format.Error());
    if (!statName.HasValue())
        return Core::Unexpected(statName.Error());
    if (!tail.HasValue())
        return Core::Unexpected(tail.Error());
    Ui::OpenMessageBox(
        runtime.dialogBox, runtime.dialogs,
        Crt::Sprintf(format.Value(), statName.Value(), static_cast<std::int32_t>(fruit.points), tail.Value()));
    return {};
}
} // namespace

Core::Expected<void, std::string> CreateFloorItem(Kernel::FloorItemState& item, const Protocol::Items::ItemData& bytes,
                                                  const glm::vec3& position, bool dropped,
                                                  const FloorItemCreation& creation)
{
    const auto byte = [&bytes](std::size_t index) { return std::to_integer<std::int32_t>(bytes[index]); };
    // 0x60835d..0x608370: 0x54b3a0's type in +4.
    const std::int32_t type = InventoryItemType(InventoryItemBytes(bytes));
    item.itemType = static_cast<std::int16_t>(type);
    std::optional<Audio::SoundId> sound;
    if (type == ZenItem)
    {
        // 0x608376..0x6083a7: zen in b1, b2 and b4; +0x1e and +0x1f at 0 (+0x20 stays).
        item.value = (((byte(1) << 8) + byte(2)) << 8) + byte(4);
        item.durability = 0;
        item.excellent = 0;
        if (dropped)
            sound = ZenDropSound;
    }
    else
    {
        // 0x6083a9..0x608402.
        item.value = byte(1);
        item.durability = static_cast<std::uint8_t>(byte(2));
        item.excellent = static_cast<std::uint8_t>(byte(3));
        item.ancient = static_cast<std::uint8_t>(byte(4));
        if (dropped)
            sound = IsAJewel(type, false) ? JewelSound : ItemDropSound;
    }
    if (sound)
    {
        if (creation.soundRequest)
            creation.soundRequest(*sound, position);
        else if (creation.sound != nullptr)
            *creation.sound = sound;
    }
    // 0x608414..0x60842a: alive, the model and +4 at 1.
    item.alive = true;
    item.type = ModelOf(type, item.value);
    item.effectCounter = 1;
    // 0x6085be: 0x607b60.
    InitializeTheObject(item, creation);
    item.boundingMin = glm::vec3(-BoundingHalf);
    item.boundingMax = glm::vec3(BoundingHalf);
    item.position = position;
    if (!creation.height)
        return Core::Unexpected(std::string("0x5f93c0: la altura del ítem del piso sin el terreno del mapa"));
    const auto height = creation.height(item.position.x, item.position.y);
    if (!height.HasValue())
        return Core::Unexpected(height.Error());
    if (dropped && (item.type == SilverMedalModel || item.type == GoldMedalModel))
    {
        if (creation.effects == nullptr)
            return Core::Unexpected(
                std::format("0x6086ce: la medalla {:#x} recién caída pide la tabla de efectos (0x518cf0)", item.type));
        // 0x608659..0x6086b8: the point at the requested position's x and y (the item's) with z = 0x5f93c0(x, y) + 3,
        // the angles at 0 and the light (1, 1, 1).
        const glm::vec3 point{position.x, position.y, static_cast<float>(height.Value() + MedalHeight)};
        // The owner is the item's object (the entry's +0x4c, ESI): its WORD +0x2 is the model, which MuRet keeps
        // signed; reading it as std::uint16_t is the access through the unsigned type it has. The step of 0x100 and
        // 0xfe (0x5227d5 and 0x522b04) does not read it.
        const Effects::ModelEffectOwner owner{
            .type = reinterpret_cast<const std::uint16_t*>(&item.type), .effect = nullptr, .angles = &item.angles};
        // 0x6086ce and 0x6086f1: 0x518cf0(type, point, angles, light, 0, item, 0x37, 0, 0, 0, 0), with no rand(); with
        // the table full it creates nothing and goes on.
        for (const std::int16_t crack : MedalCracks)
            static_cast<void>(creation.effects->CreateEarthquakeCrack(crack, point, glm::vec3(0.0f), glm::vec3(1.0f), 0,
                                                                      owner, MedalCrackSize));
        // 0x6086f6..0x60870f: the item 3 above the terrain at its (x, y) and the speed 50.
        item.position.z = static_cast<float>(height.Value() + MedalHeight);
        item.dropSpeed = MedalDropSpeed;
    }
    else if (dropped)
    {
        item.position.z = static_cast<float>(height.Value() + DropHeight);
        item.dropSpeed = DropSpeed;
    }
    else
        item.position.z = static_cast<float>(height.Value());
    SettleFloorItem(item);
    return {};
}

Core::Expected<void, std::string> ApplyFloorItemsCreate(const Protocol::Items::FloorItemsCreate& create,
                                                        const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    const Terrain::WorldTerrain& terrain = Scenes::TerrainOf(resources);
    const float fixedHeight = resources.camera.fixedHeight;
    const FloorItemCreation creation{
        .random = resources.cloth.random,
        .height = [&terrain, fixedHeight](float x, float y) -> Core::Expected<double, std::string>
        {
            if (!terrain.heights || !terrain.attributes)
                return Core::Unexpected(std::string("0x5f93c0: la altura del ítem del piso sin el terreno del mapa"));
            return Terrain::TerrainHeightAt(*terrain.heights, *terrain.attributes, x, y, fixedHeight);
        },
        .clock = resources.clock != nullptr ? resources.clock->Now() : Core::Duration{},
        .soundRequest = [&runtime = world.Runtime()](Audio::SoundId sound, const glm::vec3& position)
        { runtime.RequestSound(sound, position); },
        .effects = &world.Map().effects};
    auto& items = world.DrawTail().floorItems;
    for (const Protocol::Items::FloorItemEntry& entry : create.items)
    {
        // 0x4d0be6..0x4d0c19: the index, with those of 1000 or more at 0, and bit 15 for "just dropped".
        std::int32_t index = entry.key & FloorItemIndexMask;
        if (index >= FloorItemCount)
            index = 0;
        const bool dropped = (entry.key >> 15) != 0;
        const glm::vec3 position{(static_cast<float>(entry.x) + CellHalf) * CellSize,
                                 (static_cast<float>(entry.y) + CellHalf) * CellSize, 0.0f};
        if (auto created =
                CreateFloorItem(items[static_cast<std::size_t>(index)], entry.item, position, dropped, creation);
            !created.HasValue())
            return created;
    }
    return {};
}

void ApplyFloorItemsDelete(const Protocol::Items::FloorItemsDelete& deleted, Kernel::WorldState& world)
{
    auto& items = world.DrawTail().floorItems;
    for (const std::uint16_t key : deleted.keys)
    {
        // 0x4e385e..0x4e3885: the whole key, without the 20's mask.
        const std::int32_t index = key >= FloorItemCount ? 0 : key;
        items[static_cast<std::size_t>(index)].alive = false;
    }
}

Core::Expected<void, std::string> ApplyPickUpResult(const Protocol::Items::PickUpResult& result,
                                                    const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    if (result.result != Protocol::Items::NothingPickedUp)
    {
        if (result.result == Protocol::Items::ZenPickedUp)
        {
            // 0x4d0ce0..0x4d0f5f: zen in +0xecc with the record open.
            const Models::LogicalAccess own(runtime.ownRecordDepth, resources.cloth.random,
                                            Models::OwnRecordCloseRandoms);
            world.OwnCharacter().zen = result.zen;
        }
        else if (result.result != Protocol::Items::PickedUpElsewhere)
        {
            // 0x4d0fc0..0x4d0fd5: 0x5bc740(0x78a9bb0, 8, 8, lugar, pkt + 4, 0).
            if (auto placed = PlaceMovedItem(result.result, result.item, resources); !placed.HasValue())
                return placed;
        }
        if (auto said = PickUpMessage(result, resources); !said.HasValue())
            return said;
        // 0x4d1051..0x4d10b1: the sound by the type in pkt[4]'s bytes, also with zen.
        const std::int32_t type = InventoryItemType(InventoryItemBytes(result.item));
        runtime.RequestSound(IsAJewel(type, true) ? JewelSound : ItemPickUpSound);
    }
    // 0x4d10b9.
    runtime.floor.pendingPickUp = -1;
    return {};
}

Core::Expected<void, std::string> ApplyShortZenPickUp(const Protocol::Items::ShortZenPickUp& result,
                                                      const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    {
        // 0x4d0ce0..0x4d0f5f: zen in +0xecc with the record open.
        const Models::LogicalAccess own(runtime.ownRecordDepth, resources.cloth.random, Models::OwnRecordCloseRandoms);
        world.OwnCharacter().zen = result.zen;
    }
    const Protocol::Items::PickUpResult zen{.result = Protocol::Items::ZenPickedUp, .zen = result.zen};
    if (auto said = PickUpMessage(zen, resources); !said.HasValue())
        return said;
    // 0x4d10b9.
    runtime.floor.pendingPickUp = -1;
    return {};
}

void ApplyShortPickUpResult(Kernel::WorldState& world)
{
    // 0x4d10b9.
    world.Runtime().floor.pendingPickUp = -1;
}

Core::Expected<void, std::string> ApplyStatFruit(const Protocol::Items::StatFruit& fruit, Kernel::WorldState& world,
                                                 Crt::CrtRandom& random, const Tables::TextTable* texts)
{
    // 0x4de522..0x4de533, 0x4deb4c's table through 0x4deb20.
    constexpr std::uint8_t LastStat = 4;
    Core::Expected<void, std::string> shown{};
    switch (fruit.result)
    {
    case 0:
        if (fruit.stat <= LastStat)
            shown = EatTheFruit(fruit, world, random, texts, true);
        break;
    case 1:
        shown = FruitBox(world, texts, 378, "0x4de7f5");
        break;
    case 2:
        shown = FruitFailureBox(world, texts, FruitAddedText, "0x4de813");
        break;
    case 3:
        if (fruit.stat <= LastStat)
            shown = EatTheFruit(fruit, world, random, texts, false);
        break;
    case 4:
        shown = FruitBox(world, texts, 1906, "0x4dea5d");
        break;
    case 5:
        shown = FruitFailureBox(world, texts, FruitRemovedText, "0x4dea7b");
        break;
    case 0x10:
        shown = FormattedFruitBox(world, texts, 1909, "0x4deab1");
        break;
    case 0x21:
    case 0x25:
        shown = FormattedFruitBox(world, texts, 1911, "0x4deab8");
        break;
    case 0x26:
        shown = FormattedFruitBox(world, texts, 1912, "0x4deaec");
        break;
    default:
        break;
    }
    // 0x4de7e4, 0x4de802, 0x4de838, 0x4dea4c, 0x4dea6a, 0x4deaa0 and 0x4deb0e: the wait, on every path.
    world.Runtime().ui.field5767954 = 0;
    return shown;
}

void ApplyShortStatFruit(Kernel::WorldState& world)
{
    // 0x4de7e4, 0x4de802, 0x4de838, 0x4dea4c, 0x4dea6a, 0x4deaa0 and 0x4deb0e.
    world.Runtime().ui.field5767954 = 0;
}
} // namespace Client::Items
