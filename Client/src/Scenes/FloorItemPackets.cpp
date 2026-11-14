#include "Scenes/FloorItemPackets.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Protocol/FloorItems.h"
#include "Protocol/Inventory.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/ObjectState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GameFloorItems.h"
#include "Scenes/GameMessages.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/ItemNames.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnItemPlacement.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameDialogs.h"
#include "UI/GameText.h"
#include "UI/MessageArea.h"
#include "UI/SprintfFormat.h"
#include "World/GameCamera.h"
#include "World/ModelEffects.h"
#include "World/TerrainHeight.h"
#include "World/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
// 0x608417: el modelo es el tipo más 0x212.
constexpr std::int32_t ItemModelBase = 0x212;
// 0x6085c4..0x6085ef: la caja de ±30 (0xc1f00000 y 0x41f00000).
constexpr float BoundingHalf = 30.0f;
// 0x607b68..0x607bfd: la luz de 0,3, la escala de 0,8 (0,7 de 0x812 a 0xe11), el alpha y la luz del pase en 1.
constexpr float ObjectLight = 0.3f;
constexpr float ItemScale = 0.8f;
constexpr float SmallItemScale = 0.7f;
constexpr std::int16_t FirstSmallModel = 0x812;
constexpr std::int16_t EndSmallModel = 0xe12;
// 0x608633 y 0x60863c: 180 (0x6b86bc) por encima del terreno y la velocidad 20.
constexpr double DropHeight = 180.0;
constexpr float DropSpeed = 20.0f;
// 0x608614..0x608722: las medallas de plata y de oro (el 0x1c0b de nivel 5 y 6 por la tabla de 0x608750; el GS 0.97 las
// suelta con gMedalEvent) recién caídas: 3 (0x6b86b8) sobre el terreno, las grietas 0x100 (0x6086ce) y 0xfe (0x6086f1)
// de tamaño 0x37 y la velocidad 50 (0x60870f).
constexpr std::int16_t SilverMedalModel = 0x2242;
constexpr std::int16_t GoldMedalModel = 0x2243;
constexpr double MedalHeight = 3.0;
constexpr float MedalDropSpeed = 50.0f;
constexpr std::array<std::int16_t, 2> MedalCracks{0x100, 0xfe};
constexpr std::int16_t MedalCrackSize = 0x37;
// 0x4d0bdd y 0x4d0be9: (celda + 0,5) · 100.
constexpr float CellHalf = 0.5f;
constexpr float CellSize = 100.0f;
// Los sonidos de 0x41ed10 (CL10): 0x6083a5 el zen, 0x60840a las joyas y 0x608400 lo demás; 0x4d10af y 0x4d10a2 los
// del 22.
constexpr std::int32_t ZenDropSound = 0x1f;
constexpr std::int32_t JewelSound = 0x45;
constexpr std::int32_t ItemDropSound = 0x1e;
constexpr std::int32_t ItemPickUpSound = 0x1d;
// 0x4d0f85..0x4d0f9c y 0x4d102a..0x4d1036: los textos de Text.bmd del aviso del 22.
constexpr std::size_t ZenText = 224;
constexpr std::size_t PickedUpText = 918;
// 0x4de7a5..0x4de7ca y 0x4dea18..0x4dea32: el aviso de la fruta, con 1412 al sumar y 1903 al restar.
constexpr std::size_t FruitText = 379;
constexpr std::size_t FruitAddedText = 1412;
constexpr std::size_t FruitRemovedText = 1903;

std::int32_t LevelOf(std::int32_t value)
{
    return value >> 3;
}

// 0x6083ca..0x608402 y 0x4d1060..0x4d1097: los tipos que suenan con 0x45 al caer (con 0x181e y 0x181f, también al
// levantarlos).
bool IsAJewel(std::int32_t type, bool pickingUp)
{
    return type == 0x1c0d || type == 0x1c0e || type == 0x1c10 || type == 0x180f || type == 0x1c16 || type == 0x1c1f ||
           (pickingUp && (type == 0x181e || type == 0x181f));
}

// 0x608423..0x6085b8: los modelos que cambian por el tipo y el nivel (value >> 3, sin la máscara de 4 bits). El 0x1a13
// (el arma del arcángel de la misión, del nivel 0 al 2: el bastón, la espada y la ballesta) además deja el valor en 0.
std::int16_t ModelOf(std::int32_t type, std::int32_t& value)
{
    const auto model = static_cast<std::int16_t>(type + ItemModelBase);
    const std::int32_t level = LevelOf(value);
    switch (type)
    {
    case 0x1c0b:
    {
        // 0x608439..0x608497, la tabla de 0x608750 por 0x608770 con el nivel − 1: el 13 escribe 0x2240 y después
        // 0x223f (0x60848b cae en 0x608491).
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
        // 0x608596: AND AL, 0xf8 sobre el valor entero.
        return (value & ~7) == 8 ? std::int16_t{0x224a} : model;
    case 0x1a0b:
        return (value & ~7) == 8 ? std::int16_t{0x224c} : model;
    default:
        return model;
    }
}

// 0x607b60 sobre el objeto del ítem: lo que el piso dibuja y pisa después.
void InitializeTheObject(FloorItemState& item, const FloorItemCreation& creation)
{
    item.light = glm::vec3(ObjectLight);
    item.vertexLight = true;
    item.animation = {};
    item.dropSpeed = 0.0f;
    item.alpha = 1.0f;
    item.scale = item.type >= FirstSmallModel && item.type < EndSmallModel ? SmallItemScale : ItemScale;
    const auto model = static_cast<std::uint16_t>(item.type);
    Render::Models::ObjectRandoms randoms{};
    for (std::size_t index = 0; index < HangingRandomCount(model); ++index)
        randoms[index] = creation.random.Next();
    item.objectState = Render::Models::NativeItemObjectState(model, creation.clock, randoms);
}

Core::Expected<std::string_view, std::string> TextOf(const Data::Text::TextTable* texts, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: falta el texto {} de Text.bmd", where, index));
    return *text;
}

// 0x555190(nombre, texto, 1): los nombres que pasa el 22 (0x5767994 y 0x5767998) son buffers que nadie escribe.
Core::Expected<void, std::string> SystemLine(const WorldEntryResources& resources, std::string_view text,
                                             std::string_view where)
{
    if (resources.textMeasure == nullptr)
        return Core::Unexpected(std::format("{}: 0x555190 sin el medidor de los textos", where));
    const App::PixelSize screen = resources.screen ? resources.screen() : App::PixelSize{640, 480};
    return AddGameMessage(resources.world.Runtime(), *resources.textMeasure, screen, {}, text, UI::SystemMessage);
}

Core::Expected<void, std::string> PickUpMessage(const Protocol::FloorItems::PickUpResult& result,
                                                const WorldEntryResources& resources)
{
    const WorldRuntime& runtime = resources.world.Runtime();
    if (runtime.floor.chosen < 0 || runtime.floor.chosen >= FloorItemCount)
        return Core::Unexpected(
            std::format("0x4d0f5f: el elegido {} está fuera de la tabla 0x77f46c8", runtime.floor.chosen));
    const FloorItemState& chosen =
        resources.world.DrawTail().floorItems[static_cast<std::size_t>(runtime.floor.chosen)];
    const auto picked = TextOf(resources.texts, PickedUpText, "0x4d0f85");
    if (!picked.HasValue())
        return Core::Unexpected(picked.Error());
    if (result.result == Protocol::FloorItems::ZenPickedUp)
    {
        // 0x4d0f5f..0x4d0fb4: con el elegido vivo.
        if (!chosen.alive)
            return {};
        const auto zen = TextOf(resources.texts, ZenText, "0x4d0f8a");
        if (!zen.HasValue())
            return Core::Unexpected(zen.Error());
        return SystemLine(resources, UI::Sprintf("%d %s %s", chosen.value, zen.Value(), picked.Value()), "0x4d0faf");
    }
    // 0x4d0fdd..0x4d1049: el nombre del elegido por su tipo y su nivel, vivo o no.
    const auto name = ItemNameOf(resources.items, resources.texts, static_cast<std::uint16_t>(chosen.itemType),
                                 (chosen.value >> 3) & 15);
    if (!name.HasValue())
        return Core::Unexpected(std::format("0x4d1019: {}", name.Error()));
    return SystemLine(resources, UI::Sprintf("%s %s", std::string_view(name.Value()), picked.Value()), "0x4d1049");
}

Core::Expected<void, std::string> FruitBox(WorldState& world, const Data::Text::TextTable* texts, std::size_t text,
                                           std::string_view where)
{
    const auto line = TextOf(texts, text, where);
    if (!line.HasValue())
        return Core::Unexpected(line.Error());
    WorldRuntime& runtime = world.Runtime();
    UI::OpenMessageBox(runtime.dialogBox, runtime.dialogs, line.Value());
    return {};
}

// 0x4deab1..0x4deb04: sprintf(buffer, texto) y 0x632e30.
Core::Expected<void, std::string> FormattedFruitBox(WorldState& world, const Data::Text::TextTable* texts,
                                                    std::size_t text, std::string_view where)
{
    const auto line = TextOf(texts, text, where);
    if (!line.HasValue())
        return Core::Unexpected(line.Error());
    WorldRuntime& runtime = world.Runtime();
    UI::OpenMessageBox(runtime.dialogBox, runtime.dialogs, UI::Sprintf(line.Value()));
    return {};
}

// 0x4de813 y 0x4dea7b: sprintf(buffer, 1904, el texto de sumar o restar) y 0x632e30.
Core::Expected<void, std::string> FruitFailureBox(WorldState& world, const Data::Text::TextTable* texts,
                                                  std::size_t kind, std::string_view where)
{
    const auto format = TextOf(texts, 1904, where);
    const auto argument = TextOf(texts, kind, where);
    if (!format.HasValue())
        return Core::Unexpected(format.Error());
    if (!argument.HasValue())
        return Core::Unexpected(argument.Error());
    WorldRuntime& runtime = world.Runtime();
    UI::OpenMessageBox(runtime.dialogBox, runtime.dialogs, UI::Sprintf(format.Value(), argument.Value()));
    return {};
}

// 0x4de53a..0x4de7e4 (sumar) y 0x4de849..0x4dea4c (restar): el stat del registro propio, los puntos y el aviso.
Core::Expected<void, std::string> EatTheFruit(const Protocol::FloorItems::StatFruit& fruit, WorldState& world,
                                              Render::Models::CrtRandom& random, const Data::Text::TextTable* texts,
                                              bool adding)
{
    // 0x4deb74 y 0x4deb88: el stat del lugar y su texto (la energía, la vitalidad, la agilidad, la fuerza y el
    // comando).
    constexpr std::array<std::size_t, 5> StatTexts{168, 169, 167, 166, 1900};
    WorldRuntime& runtime = world.Runtime();
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, random, Render::Models::OwnRecordCloseRandoms);
        OwnCharacterRecord& record = world.OwnCharacter();
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
    UI::OpenMessageBox(
        runtime.dialogBox, runtime.dialogs,
        UI::Sprintf(format.Value(), statName.Value(), static_cast<std::int32_t>(fruit.points), tail.Value()));
    return {};
}
} // namespace

Core::Expected<void, std::string> CreateFloorItem(FloorItemState& item, const Protocol::Inventory::ItemData& bytes,
                                                  const glm::vec3& position, bool dropped,
                                                  const FloorItemCreation& creation)
{
    const auto byte = [&bytes](std::size_t index) { return std::to_integer<std::int32_t>(bytes[index]); };
    // 0x60835d..0x608370: el tipo de 0x54b3a0 en +4.
    const std::int32_t type = InventoryItemType(InventoryItemBytes(bytes));
    item.itemType = static_cast<std::int16_t>(type);
    std::optional<std::int32_t> sound;
    if (type == ZenItem)
    {
        // 0x608376..0x6083a7: el zen en b1, b2 y b4; +0x1e y +0x1f en 0 (+0x20 queda).
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
    if (sound && creation.sound != nullptr)
        *creation.sound = sound;
    // 0x608414..0x60842a: vivo, el modelo y +4 en 1.
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
        // 0x608659..0x6086b8: el punto en la x y la y de la posición pedida (las del ítem) con z = 0x5f93c0(x, y) + 3,
        // los ángulos en 0 y la luz (1, 1, 1).
        const glm::vec3 point{position.x, position.y, static_cast<float>(height.Value() + MedalHeight)};
        // El dueño es el objeto del ítem (+0x4c de la entrada, ESI): su WORD +0x2 es el modelo, que MuRet guarda con
        // signo; leerlo como std::uint16_t es el acceso por el tipo sin signo que le corresponde. El paso de 0x100 y
        // 0xfe (0x5227d5 y 0x522b04) no lo lee.
        const World::ModelEffectOwner owner{
            .type = reinterpret_cast<const std::uint16_t*>(&item.type), .effect = nullptr, .angles = &item.angles};
        // 0x6086ce y 0x6086f1: 0x518cf0(tipo, punto, ángulos, luz, 0, ítem, 0x37, 0, 0, 0, 0), sin rand(); con la tabla
        // llena no crea nada y sigue.
        for (const std::int16_t crack : MedalCracks)
            static_cast<void>(creation.effects->CreateEarthquakeCrack(crack, point, glm::vec3(0.0f), glm::vec3(1.0f), 0,
                                                                      owner, MedalCrackSize));
        // 0x6086f6..0x60870f: el ítem a 3 sobre el terreno en su (x, y) y la velocidad 50.
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

Core::Expected<void, std::string> ApplyFloorItemsCreate(const Protocol::FloorItems::FloorItemsCreate& create,
                                                        const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    const World::WorldTerrain& terrain = world.Map().terrain;
    const float fixedHeight = resources.camera.fixedHeight;
    const FloorItemCreation creation{
        .random = resources.cloth.random,
        .height = [&terrain, fixedHeight](float x, float y) -> Core::Expected<double, std::string>
        {
            if (!terrain.heights || !terrain.attributes)
                return Core::Unexpected(std::string("0x5f93c0: la altura del ítem del piso sin el terreno del mapa"));
            return World::TerrainHeightAt(*terrain.heights, *terrain.attributes, x, y, fixedHeight);
        },
        .clock = resources.clock != nullptr ? resources.clock->Now() : Core::Duration{},
        .sound = &world.Runtime().lastSoundId,
        .effects = &world.Map().effects};
    auto& items = world.DrawTail().floorItems;
    for (const Protocol::FloorItems::FloorItemEntry& entry : create.items)
    {
        // 0x4d0be6..0x4d0c19: el índice, con los de 1000 o más en el 0, y el bit 15 de "recién caído".
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

void ApplyFloorItemsDelete(const Protocol::FloorItems::FloorItemsDelete& deleted, WorldState& world)
{
    auto& items = world.DrawTail().floorItems;
    for (const std::uint16_t key : deleted.keys)
    {
        // 0x4e385e..0x4e3885: la clave entera, sin la máscara del 20.
        const std::int32_t index = key >= FloorItemCount ? 0 : key;
        items[static_cast<std::size_t>(index)].alive = false;
    }
}

Core::Expected<void, std::string> ApplyPickUpResult(const Protocol::FloorItems::PickUpResult& result,
                                                    const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    if (result.result != Protocol::FloorItems::NothingPickedUp)
    {
        if (result.result == Protocol::FloorItems::ZenPickedUp)
        {
            // 0x4d0ce0..0x4d0f5f: el zen en +0xecc con el registro abierto.
            const Render::Models::LogicalAccess own(runtime.ownRecordDepth, resources.cloth.random,
                                                    Render::Models::OwnRecordCloseRandoms);
            world.OwnCharacter().zen = result.zen;
        }
        else if (result.result != Protocol::FloorItems::PickedUpElsewhere)
        {
            // 0x4d0fc0..0x4d0fd5: 0x5bc740(0x78a9bb0, 8, 8, lugar, pkt + 4, 0).
            if (auto placed = PlaceMovedItem(result.result, result.item, resources); !placed.HasValue())
                return placed;
        }
        if (auto said = PickUpMessage(result, resources); !said.HasValue())
            return said;
        // 0x4d1051..0x4d10b1: el sonido por el tipo de los bytes de pkt[4], también con el zen.
        const std::int32_t type = InventoryItemType(InventoryItemBytes(result.item));
        runtime.lastSoundId = IsAJewel(type, true) ? JewelSound : ItemPickUpSound;
    }
    // 0x4d10b9.
    runtime.floor.pendingPickUp = -1;
    return {};
}

void ApplyShortPickUpResult(WorldState& world)
{
    // 0x4d10b9.
    world.Runtime().floor.pendingPickUp = -1;
}

Core::Expected<void, std::string> ApplyStatFruit(const Protocol::FloorItems::StatFruit& fruit, WorldState& world,
                                                 Render::Models::CrtRandom& random, const Data::Text::TextTable* texts)
{
    // 0x4de522..0x4de533, la tabla de 0x4deb4c por 0x4deb20.
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
    // 0x4de7e4, 0x4de802, 0x4de838, 0x4dea4c, 0x4dea6a, 0x4deaa0 y 0x4deb0e: la espera, en todos los caminos.
    world.Runtime().ui.field5767954 = 0;
    return shown;
}

void ApplyShortStatFruit(WorldState& world)
{
    // 0x4de7e4, 0x4de802, 0x4de838, 0x4dea4c, 0x4dea6a, 0x4deaa0 y 0x4deb0e.
    world.Runtime().ui.field5767954 = 0;
}
} // namespace Client::Scenes
