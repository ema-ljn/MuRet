#include "Scenes/ChaosMix.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/ItemPrice.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/ItemState.h"

#include <array>
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
// El tipo vacío de las grillas.
constexpr std::uint16_t NoItem = 0xffff;
// 0x5fd165..0x5fd1a3: los precios fijos de 0x5fd120.
constexpr std::int32_t BlessValue = 100'000;
constexpr std::int32_t SoulValue = 70'000;
constexpr std::int32_t ChaosValue = 40'000;
constexpr std::int32_t LifeValue = 450'000;
constexpr std::int32_t CreationValue = 450'000;
// Los tipos que cuenta 0x5fd4f0.
constexpr std::int32_t Bless = 0x1c0d;
constexpr std::int32_t Soul = 0x1c0e;
constexpr std::int32_t Life = 0x1c10;
constexpr std::int32_t Eye = 0x1c11;
constexpr std::int32_t Key = 0x1c12;
constexpr std::int32_t Creation = 0x1c16;
constexpr std::int32_t Guardian = 0x1c1f;
constexpr std::int32_t Chaos = 0x180f;
constexpr std::int32_t Uniria = 0x1a02;
constexpr std::int32_t Feather = 0x1a0e;
constexpr std::int32_t Scroll = 0x1a10;
constexpr std::int32_t Bone = 0x1a11;
constexpr std::int32_t Item1a1e = 0x1a1e;
constexpr std::int32_t Item1a20 = 0x1a20;
constexpr std::int32_t Item1a21 = 0x1a21;
constexpr std::int32_t Item1a22 = 0x1a22;
constexpr std::int32_t Item1a23 = 0x1a23;
constexpr std::int32_t Item1a24 = 0x1a24;
constexpr std::int32_t Fenrir = 0x1a25;
constexpr std::int32_t FirstWings = 0x1800;
constexpr std::int32_t LastFirstWings = 0x1802;
constexpr std::int32_t FirstSpecialWings = 0x1803;
constexpr std::int32_t LastSpecialWings = 0x1806;
constexpr std::int32_t FirstArmor = 0xc00;
// Las opciones excelentes que cuentan (0x50 a 0x53), las de las alas (0x50, 0x51 y 0x55) y la suerte (0x54).
constexpr std::uint8_t ExcellentOptionFrom = 0x50;
constexpr std::uint8_t ExcellentOptionTo = 0x53;
constexpr std::uint8_t LastWingValueOption = 0x51;
constexpr std::uint8_t WingTimesFourOption = 0x55;
constexpr std::uint8_t LuckOption = 0x54;
// 0x5fd681: el tope de 0x7d2dc88.
constexpr std::int32_t MaxExcellentValue = 1'000'000;
// El nivel de los ítems (D+4 >> 3 & 0xf) desde el que cuentan las opciones.
constexpr std::int32_t ExcellentFromLevel = 4;
constexpr std::int32_t FirstCountedLevel = 9;
constexpr std::int32_t LastCountedLevel = 12;
// 0x5fdf1a: el nivel del registro propio que piden las alas del 0x18.
constexpr std::uint16_t CapeHeroLevel = 10;
// 0x5fe30d..0x5fe33a: las divisiones de la chance de las alas y su tope.
constexpr std::uint32_t ExcellentRateDivisor = 40'000;
constexpr std::uint32_t WingsRateDivisor = 4'000'000;
constexpr std::int32_t MaxWingRate = 90;

// Los tipos de mezcla que devuelve 0x5fd4f0.
constexpr std::int32_t ChaosWeaponMix = 1;

struct Tally
{
    std::int32_t total = 0;                // [EBP − 0xb0]
    std::int32_t chaos = 0;                // [EBP − 0xac], 0x180f
    std::int32_t plainExcellent = 0;       // [EBP − 0xa8], los de la cola con 0x50..0x53
    std::int32_t eye = 0;                  // [EBP − 0xa4], 0x1c11
    std::int32_t key = 0;                  // [EBP − 0xa0], 0x1c12
    std::array<std::int32_t, 4> byLevel{}; // [EBP − 0x9c..−0x90], niveles 9 a 12
    std::int32_t bless = 0;                // [EBP − 0x8c]
    std::int32_t soul = 0;                 // [EBP − 0x88]
    std::int32_t fullUniria = 0;           // [EBP − 0x84], 0x1a02 con la durabilidad en 0xff
    std::int32_t others = 0;               // [EBP − 0x80]
    std::int32_t creation = 0;             // [EBP − 0x7c], 0x1c16
    std::int32_t feather = 0;              // [EBP − 0x78], 0x1a0e
    std::int32_t wings = 0;                // [EBP − 0x74], 0x1800..0x1802
    std::int32_t excellent = 0;            // [EBP − 0x70]
    std::int32_t excellentSpecial = 0;     // [EBP − 0x6c], 0x1803..0x1806 excelentes
    std::int32_t excellentStaffs = 0;      // [EBP − 0x68], 0x406, 0x806 y 0xa07
    std::int32_t scroll = 0;               // [EBP − 0x64], 0x1a10
    std::int32_t bone = 0;                 // [EBP − 0x60], 0x1a11
    std::int32_t guardian = 0;             // [EBP − 0x50], 0x1c1f
    std::int32_t item1a20 = 0;             // [EBP − 0x4c]
    std::int32_t item1a21 = 0;             // [EBP − 0x48]
    std::int32_t item1a22 = 0;             // [EBP − 0x44]
    std::int32_t item1a23 = 0;             // [EBP − 0x40]
    std::int32_t item1a24 = 0;             // [EBP − 0x3c]
    std::int32_t plainFenrir = 0;          // [EBP − 0x38], 0x1a25 sin excelente
    std::int32_t life = 0;                 // [EBP − 0x34], 0x1c10
    std::int32_t excellentWeapons = 0;     // [EBP − 0x30]
    std::int32_t excellentArmors = 0;      // [EBP − 0x2c]
    std::int32_t notExcellent = 0;         // [EBP − 0x28]
    std::uint16_t eyeLevel = 0;            // [EBP − 0x24]
    std::uint16_t keyLevel = 0;            // [EBP − 0x22]
    std::uint16_t scrollLevel = 0;         // [EBP − 0x1e]
    std::uint16_t boneLevel = 0;           // [EBP − 0x1c]
    std::uint16_t featherLevel = 0;        // [EBP − 0x16]
    std::uint32_t excellentPrices = 0;     // [EBP − 0xc8], 0x54d0f0 en el modo 0
    std::uint32_t wingsPrice = 0;          // [EBP − 0xc4]
};

std::int32_t Signed(std::uint16_t id)
{
    return static_cast<std::int16_t>(id);
}

bool InRange(std::int32_t value, std::int32_t first, std::int32_t last)
{
    return value >= first && value <= last;
}

std::int32_t LevelOf(const World::ItemState& item)
{
    // 0x5fd612..0x5fd61b: SAR y AND 0xf.
    return static_cast<std::int32_t>((item.scalar1Low >> 3u) & 0xfu);
}

// D+0x2b + offset: las opciones desde +0x2b y sus valores desde +0x33. MuRet tiene hasta +0x3b.
Core::Expected<std::uint8_t, std::string> OptionByte(const World::ItemState& item, std::size_t offset,
                                                     std::string_view address)
{
    if (offset >= item.optionBytes.size())
        return Core::Unexpected(std::format("{}: la caja del caos lee D+{:#x}, fuera de las opciones "
                                            "(D+0x2b..D+0x3b) que tiene MuRet",
                                            address, 0x2b + offset));
    return item.optionBytes[offset];
}

Core::Expected<std::uint32_t, std::string> PriceOf(const World::ItemState& item, const Data::Tables::ItemTable* items,
                                                   std::int32_t mode, const WorldRuntime& runtime,
                                                   std::string_view address)
{
    if (items == nullptr)
        return Core::Unexpected(std::format("{}: 0x54d0f0 sin Item.bmd ([0x7758550])", address));
    const PetPriceLevels pets{.raven = runtime.ravenLevel, .horse = runtime.darkHorseLevel};
    auto price = ItemPrice(item, *items, mode, pets);
    if (!price.HasValue())
        return Core::Unexpected(std::format("{}: {}", address, price.Error()));
    return price.Value();
}

// 0x5fd63c..0x5fd6b1 (y 0x5fd9eb..0x5fda2c): si alguna opción es excelente (0x50 a 0x53); `each` corre con la
// primera.
template <typename Found>
Core::Expected<bool, std::string> WithTheFirstExcellent(const World::ItemState& item, std::string_view address,
                                                        Found&& each)
{
    bool found = false;
    for (std::size_t i = 0; i < item.optionCount; ++i)
    {
        auto tag = OptionByte(item, i, address);
        if (!tag.HasValue())
            return Core::Unexpected(tag.Error());
        if (tag.Value() < ExcellentOptionFrom || tag.Value() > ExcellentOptionTo || found)
            continue;
        if (auto done = each(); !done.HasValue())
            return Core::Unexpected(done.Error());
        found = true;
    }
    return found;
}

// 0x5fd9c3..0x5fda7b: lo que no tiene un caso propio.
Core::Expected<void, std::string> CountTheRest(Tally& tally, NpcWindowState& npc, const World::ItemState& item,
                                               std::int32_t level, bool& checksLuck,
                                               const Data::Tables::ItemTable* items, const WorldRuntime& runtime)
{
    const std::int32_t type = Signed(item.id);
    bool found = false;
    if (type < FirstWings && level >= ExcellentFromLevel)
    {
        npc.mixLuck = 0;
        checksLuck = true;
        auto excellent = WithTheFirstExcellent(item, "0x5fd9eb",
                                               [&]() -> Core::Expected<void, std::string>
                                               {
                                                   ++tally.plainExcellent;
                                                   if (type == 0x406 || type == 0x806 || type == 0xa07)
                                                       ++tally.excellentStaffs;
                                                   return {};
                                               });
        if (!excellent.HasValue())
            return Core::Unexpected(excellent.Error());
        found = excellent.Value();
    }
    if (!found)
        ++tally.others;
    // 0x5fda31..0x5fda7b: con excelente (+0x1b & 0x3f) y nivel 4 o más.
    if ((item.flags1b & 0x3fu) == 0 || level < ExcellentFromLevel)
        return {};
    if (InRange(type, FirstSpecialWings, LastSpecialWings))
    {
        ++tally.excellentSpecial;
        return {};
    }
    ++tally.excellent;
    auto price = PriceOf(item, items, ShopGridPrice, runtime, "0x5fda65");
    if (!price.HasValue())
        return Core::Unexpected(price.Error());
    tally.excellentPrices += price.Value();
    return {};
}

// 0x5fd7f7..0x5fda99: las alas de primer nivel, con su precio y la opción de las alas. Después mira la suerte sin
// mirar [EBP − 8], en la variable local [EBP − 0x11] (y en 0x7d2dc9c si el nivel la prendió).
Core::Expected<void, std::string> CountTheWings(Tally& tally, NpcWindowState& npc, const World::ItemState& item,
                                                const Data::Tables::ItemTable* items, const WorldRuntime& runtime)
{
    ++tally.wings;
    npc.mixWingOption = 0;
    auto price = PriceOf(item, items, ShopGridPrice, runtime, "0x5fd814");
    if (!price.HasValue())
        return Core::Unexpected(price.Error());
    tally.wingsPrice = price.Value();
    for (std::size_t i = 0; i < item.optionCount; ++i)
    {
        auto tag = OptionByte(item, i, "0x5fd82f");
        if (!tag.HasValue())
            return Core::Unexpected(tag.Error());
        if (tag.Value() < ExcellentOptionFrom)
            continue;
        if (tag.Value() > LastWingValueOption && tag.Value() != WingTimesFourOption)
            continue;
        auto value = OptionByte(item, World::ItemOptionValueOffset + i, "0x5fd84d");
        if (!value.HasValue())
            return Core::Unexpected(value.Error());
        npc.mixWingOption =
            tag.Value() == WingTimesFourOption ? std::int32_t{value.Value()} << 2 : std::int32_t{value.Value()};
    }
    return {};
}

// 0x5fd725..0x5fd9be: el caso de cada tipo. Devuelve falso para lo que sigue en 0x5fd9c3.
bool CountByType(Tally& tally, const World::ItemState& item, std::int32_t level)
{
    const auto word = static_cast<std::uint16_t>(level);
    switch (Signed(item.id))
    {
    case Item1a23:
        ++tally.item1a23;
        return true;
    case Item1a1e:
        // [EBP − 0x54]: nadie lo lee.
        return true;
    case Item1a20:
        if (item.durability == 0x14)
            ++tally.item1a20;
        return true;
    case Item1a21:
        if (item.durability == 0x14)
            ++tally.item1a21;
        return true;
    case Item1a22:
        if (item.durability == 10)
            ++tally.item1a22;
        return true;
    case Bone:
        ++tally.bone;
        tally.boneLevel = word;
        return true;
    case Feather:
        ++tally.feather;
        tally.featherLevel = word;
        return true;
    case Scroll:
        ++tally.scroll;
        tally.scrollLevel = word;
        return true;
    case Uniria:
        if (item.durability >= 0xff)
            ++tally.fullUniria;
        return true;
    case Chaos:
        ++tally.chaos;
        return true;
    case Life:
        ++tally.life;
        return true;
    case Bless:
        ++tally.bless;
        return true;
    case Soul:
        ++tally.soul;
        return true;
    case Item1a24:
        ++tally.item1a24;
        return true;
    case Fenrir:
        if ((item.flags1b & 0x3fu) == 0)
            ++tally.plainFenrir;
        return true;
    case Eye:
        ++tally.eye;
        tally.eyeLevel = word;
        return true;
    case Key:
        ++tally.key;
        tally.keyLevel = word;
        return true;
    case Creation:
        ++tally.creation;
        return true;
    case Guardian:
        ++tally.guardian;
        return true;
    default:
        return false;
    }
}

// Una casilla con tipo y +0x3c (0x5fd5bd..0x5fd7b7).
Core::Expected<void, std::string> CountTheItem(Tally& tally, NpcWindowState& npc, const World::ItemState& item,
                                               const Data::Tables::ItemTable* items, const WorldRuntime& runtime)
{
    ++tally.total;
    // 0x5fd5ca..0x5fd60d: el ancho y el alto más grandes.
    auto footprint = ItemFootprintOf(items, item.id, "0x5fd5ef");
    if (!footprint.HasValue())
        return Core::Unexpected(footprint.Error());
    if (npc.mixWidth <= footprint.Value().width)
        npc.mixWidth = footprint.Value().width;
    if (npc.mixHeight <= footprint.Value().height)
        npc.mixHeight = footprint.Value().height;
    const std::int32_t level = LevelOf(item);
    const std::int32_t type = Signed(item.id);
    // [EBP − 8], [EBP − 0x10] y [EBP − 0xb8]: si mira la suerte, si también la escribe en 0x7d2dc9c por el nivel y
    // dónde la escribe (0x7d2dc9c, o la variable local de las alas).
    bool checksLuck = false;
    bool luckByLevel = false;
    bool luckIsGlobal = true;
    if (type < FirstWings && level >= ExcellentFromLevel)
    {
        // 0x5fd631..0x5fd6b1: la primera opción excelente: el arma o la armadura y su precio en el modo 1.
        auto excellent =
            WithTheFirstExcellent(item, "0x5fd63c",
                                  [&]() -> Core::Expected<void, std::string>
                                  {
                                      if (type >= 0 && type < FirstArmor)
                                          ++tally.excellentWeapons;
                                      else if (type >= FirstArmor)
                                          ++tally.excellentArmors;
                                      auto price = PriceOf(item, items, InventoryPrice, runtime, "0x5fd671");
                                      if (!price.HasValue())
                                          return Core::Unexpected(price.Error());
                                      npc.mixExcellentValue = static_cast<std::int32_t>(
                                          static_cast<std::uint32_t>(npc.mixExcellentValue) + price.Value());
                                      if (npc.mixExcellentValue > MaxExcellentValue)
                                          npc.mixExcellentValue = MaxExcellentValue;
                                      return {};
                                  });
        if (!excellent.HasValue())
            return Core::Unexpected(excellent.Error());
        if (!excellent.Value())
            ++tally.notExcellent;
    }
    else if (type != Chaos && type != Life && (type != Fenrir || (item.flags1b & 0x3fu) != 0))
    {
        // 0x5fd6e5..0x5fd700.
        ++tally.notExcellent;
    }
    // 0x5fd6b9..0x5fd722: los niveles 9 a 12 de lo que está debajo de 0x1807 y del 0x1a1e.
    if ((type < 0x1807 || type == Item1a1e) && InRange(level, FirstCountedLevel, LastCountedLevel))
    {
        ++tally.byLevel[static_cast<std::size_t>(level - FirstCountedLevel)];
        npc.mixLuck = 0;
        checksLuck = true;
        luckByLevel = true;
    }
    if (InRange(type, FirstWings, LastFirstWings))
    {
        if (auto wings = CountTheWings(tally, npc, item, items, runtime); !wings.HasValue())
            return wings;
        checksLuck = true;
        luckIsGlobal = false;
    }
    else if (!CountByType(tally, item, level))
    {
        if (auto rest = CountTheRest(tally, npc, item, level, checksLuck, items, runtime); !rest.HasValue())
            return rest;
    }
    // 0x5fd785..0x5fd7b7: la suerte.
    if (!checksLuck)
        return {};
    for (std::size_t i = 0; i < item.optionCount; ++i)
    {
        auto tag = OptionByte(item, i, "0x5fd795");
        if (!tag.HasValue())
            return Core::Unexpected(tag.Error());
        if (tag.Value() != LuckOption)
            continue;
        if (luckIsGlobal || luckByLevel)
            npc.mixLuck = 1;
    }
    return {};
}

// 0x5fdac0..0x5fdc30: los que se deciden por la cuenta total.
std::optional<std::int32_t> KindByTotal(const Tally& t, NpcWindowState& npc)
{
    if (t.total == 3)
    {
        if (t.chaos == 1)
        {
            if (t.eye == 1 && t.key == 1)
            {
                if (t.eyeLevel != t.keyLevel)
                    return -2;
                npc.mixLevel = std::int32_t{t.eyeLevel} + 1;
                return 2;
            }
            if (t.scroll == 1 && t.bone == 1)
            {
                if (t.scrollLevel != t.boneLevel)
                    return -8;
                npc.mixLevel = std::int32_t{t.scrollLevel} + 1;
                return 8;
            }
        }
        if (t.item1a20 == 1 && t.item1a21 == 1 && t.chaos == 1)
            return 0x19;
    }
    else if (t.total == 7)
    {
        if (t.item1a22 == 1 && t.item1a23 == 5 && t.chaos == 1)
            return 0x1a;
    }
    else if (t.total == 5)
    {
        if (t.item1a24 == 1 && t.life == 3 && t.chaos == 1)
            return 0x1b;
    }
    else if (t.total >= 8 && t.plainFenrir == 1)
    {
        if (t.life == 5 && t.chaos == 1 && t.excellentWeapons >= 1 && t.excellentArmors == 0 && t.notExcellent == 0)
        {
            npc.mixCapeKind = 3;
            return 0x1c;
        }
        if (t.life == 5 && t.chaos == 1 && t.excellentWeapons == 0 && t.excellentArmors >= 1 && t.notExcellent == 0)
        {
            npc.mixCapeKind = 4;
            return 0x1c;
        }
    }
    return std::nullopt;
}

// 0x5fe30d..0x5fe33a y 0x5fe386..0x5fe3b1.
void RateTheWings(const Tally& t, NpcWindowState& npc)
{
    const std::uint32_t rate = t.excellentPrices / ExcellentRateDivisor + t.wingsPrice / WingsRateDivisor;
    const auto signedRate = static_cast<std::int32_t>(rate);
    npc.mixWingRate = signedRate < MaxWingRate ? signedRate : MaxWingRate;
}

// 0x5fe3c7..0x5fe5be: las joyas, la fruta, Uniria y la cola.
std::int32_t KindOfTheRest(const Tally& t)
{
    struct Jewels
    {
        std::int32_t total;
        std::size_t level;
        std::int32_t count;
        std::int32_t kind;
    };
    constexpr std::array<Jewels, 4> Upgrades{{{4, 0, 1, 3}, {6, 1, 2, 4}, {8, 2, 3, 0x16}, {10, 3, 4, 0x17}}};
    bool checkTheTail = t.chaos > 0;
    bool decided = false;
    for (const Jewels& upgrade : Upgrades)
    {
        if (t.total != upgrade.total)
            continue;
        decided = true;
        if (t.chaos != 1)
            break;
        if (t.byLevel[upgrade.level] == 1 && t.bless == upgrade.count && t.soul == upgrade.count)
            return upgrade.kind;
        // 0x5fe3da..: sigue en 0x5fe4e9 sin mirar la joya del caos otra vez.
        checkTheTail = true;
    }
    if (!decided && t.total == 11 && t.fullUniria == 10 && t.chaos == 1)
        return 5;
    if (!decided && t.total == 2 && t.creation == 1 && t.chaos == 1)
        return 6;
    // 0x5fe4e9..0x5fe558.
    if (checkTheTail && t.plainExcellent > 0 && t.eye == 0 && t.key == 0 && t.fullUniria == 0 && t.others == 0 &&
        t.creation == 0 && t.feather == 0 && t.wings == 0 && t.excellentSpecial == 0 && t.scroll == 0 && t.bone == 0 &&
        t.guardian == 0)
        return t.excellentStaffs <= 0 ? 1 : 11;
    // 0x5fe559..0x5fe5b5.
    if (t.total != 0)
    {
        if (t.total == t.bless)
            return 0xf;
        if (t.total == t.soul)
            return 0x10;
    }
    if (t.total == 12 && t.bless == 5 && t.soul == 5 && t.chaos == 1 && t.guardian == 1)
        return 0x11;
    return 0;
}
} // namespace

Core::Expected<std::int32_t, std::string>
ChaosBoxValue(std::span<const World::ItemState> grid, const Data::Tables::ItemTable* items, const PetPriceLevels& pets)
{
    std::int32_t value = 0;
    bool chaos = false;
    for (const World::ItemState& item : grid)
    {
        if (item.id == NoItem || static_cast<std::int32_t>(item.occupied) <= 0)
            continue;
        std::int32_t add = 0;
        switch (Signed(item.id))
        {
        case Bless:
            add = BlessValue;
            break;
        case Soul:
            add = SoulValue;
            break;
        case Chaos:
            add = ChaosValue;
            break;
        case Life:
            add = LifeValue;
            break;
        case Creation:
            add = CreationValue;
            break;
        default:
        {
            if (items == nullptr)
                return Core::Unexpected(std::string("0x5fd1ae: 0x54d0f0 sin Item.bmd ([0x7758550])"));
            auto price = ItemPrice(item, *items, ShopGridPrice, pets);
            if (!price.HasValue())
                return Core::Unexpected(std::format("0x5fd1ae: {}", price.Error()));
            add = static_cast<std::int32_t>(price.Value());
            break;
        }
        }
        value = static_cast<std::int32_t>(static_cast<std::uint32_t>(value) + static_cast<std::uint32_t>(add));
        if (Signed(item.id) == Chaos)
            chaos = true;
    }
    return chaos ? value : 0;
}

Core::Expected<std::int32_t, std::string> ChaosMixKind(WorldState& world, Render::Models::CrtRandom& random,
                                                       const Data::Tables::ItemTable* items)
{
    WorldRuntime& runtime = world.Runtime();
    NpcWindowState& npc = runtime.npcWindows;
    // 0x5fd4fc..0x5fd552.
    npc.mixExcellentValue = 0;
    npc.mixHeight = 1;
    npc.mixWidth = 1;
    Tally tally;
    for (const World::ItemState& item : npc.chaosBox)
    {
        if (item.id == NoItem || static_cast<std::int32_t>(item.occupied) <= 0)
            continue;
        if (auto counted = CountTheItem(tally, npc, item, items, runtime); !counted.HasValue())
            return Core::Unexpected(counted.Error());
    }
    if (const std::optional<std::int32_t> kind = KindByTotal(tally, npc))
        return *kind;
    // 0x5fdc31..0x5fe3c6: las alas de segundo nivel (0x1a0e de nivel 0) y la capa (0x1a0e de nivel 1).
    const bool wingsAndFeather = tally.wings == 1 && tally.excellentSpecial == 0 && tally.feather == 1;
    const bool everythingElseExcellent = tally.chaos == 1 && tally.total == tally.excellent + 3;
    if (wingsAndFeather && tally.featherLevel == 1)
    {
        if (everythingElseExcellent)
        {
            // 0x5fdc6f..0x5fe12e: el nivel del registro propio, abierto y cerrado.
            std::uint16_t level = 0;
            {
                const Render::Models::LogicalAccess own(runtime.ownRecordDepth, random,
                                                        Render::Models::OwnRecordCloseRandoms);
                level = world.OwnCharacter().level;
            }
            if (level < CapeHeroLevel)
                return -0x18;
            RateTheWings(tally, npc);
            return 0x18;
        }
    }
    else if (wingsAndFeather && tally.featherLevel == 0 && everythingElseExcellent)
    {
        RateTheWings(tally, npc);
        return 7;
    }
    return KindOfTheRest(tally);
}

std::int32_t ChaosMixKindForMode(std::int32_t kind, std::int32_t mode)
{
    if (mode == 0)
        return kind == ChaosWeaponMix ? 0 : kind;
    if (mode == 1)
        return kind == ChaosWeaponMix ? kind : 0;
    return kind;
}

Core::Expected<void, std::string> ScanTheChaosBox(WorldState& world, Render::Models::CrtRandom& random,
                                                  const Data::Tables::ItemTable* items)
{
    WorldRuntime& runtime = world.Runtime();
    NpcWindowState& npc = runtime.npcWindows;
    const PetPriceLevels pets{.raven = runtime.ravenLevel, .horse = runtime.darkHorseLevel};
    auto value = ChaosBoxValue(npc.chaosBox, items, pets);
    if (!value.HasValue())
        return Core::Unexpected(value.Error());
    npc.chaosBoxValue = value.Value();
    auto kind = ChaosMixKind(world, random, items);
    if (!kind.HasValue())
        return Core::Unexpected(kind.Error());
    npc.mixKind = ChaosMixKindForMode(kind.Value(), npc.chaosMixMode);
    return {};
}

bool HasRoomFor(std::span<const World::ItemState> grid, std::int32_t columns, std::int32_t rows, std::int32_t width,
                std::int32_t height)
{
    for (std::int32_t row = 0; row < rows - height + 1; ++row)
    {
        for (std::int32_t column = 0; column < columns - width + 1; ++column)
        {
            if (height <= 0)
                return true;
            bool empty = true;
            for (std::int32_t y = 0; y < height; ++y)
            {
                for (std::int32_t x = 0; x < width; ++x)
                {
                    const auto at = static_cast<std::size_t>((row + y) * columns + column + x);
                    if (at < grid.size() && grid[at].id != NoItem)
                        empty = false;
                }
            }
            if (empty)
                return true;
        }
    }
    return false;
}

MixRoom MixRoomFor(const NpcWindowState& npc, std::int32_t kind)
{
    // La tabla de 0x5febd4 con kind − 1 (0..0x1b) y sus casos de 0x5febc0.
    switch (kind)
    {
    case 1:
    case 7:
    case 11:
        return MixRoom{.width = 5, .height = 4};
    case 8:
    case 26:
    case 27:
    case 28:
        return MixRoom{.width = 2, .height = 2};
    case 15:
    case 16:
    case 17:
    case 25:
        return MixRoom{.width = 1, .height = 1};
    case 24:
        return MixRoom{.width = 3, .height = 3};
    default:
        return MixRoom{.width = npc.mixWidth, .height = npc.mixHeight};
    }
}

MixButtonAction MixButtonActionFor(std::int32_t kind)
{
    // La tabla de 0x5feb88 con kind + 0x18 (0..0x34) y sus casos de 0x5feb74.
    switch (kind)
    {
    case -0x18:
        return MixButtonAction::LevelTooLow;
    case -8:
    case -2:
        return MixButtonAction::NotTheSame;
    case 0:
        return MixButtonAction::NothingToMix;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 0xb:
    case 0xf:
    case 0x10:
    case 0x11:
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19:
    case 0x1a:
    case 0x1b:
    case 0x1c:
        return MixButtonAction::Mix;
    default:
        return MixButtonAction::Nothing;
    }
}

std::string ThousandsText(std::uint32_t value)
{
    constexpr std::uint32_t Thousand = 1000;
    constexpr std::uint32_t Million = 1'000'000;
    constexpr std::uint32_t Billion = 1'000'000'000;
    const std::uint32_t units = value % Thousand;
    const std::uint32_t thousands = value % Million / Thousand;
    const std::uint32_t millions = value % Billion / Million;
    if (value >= Billion)
        return std::format("{},{:03},{:03},{:03}", value / Billion, millions, thousands, units);
    if (value >= Million)
        return std::format("{},{:03},{:03}", millions, thousands, units);
    if (value >= Thousand)
        return std::format("{},{:03}", thousands, units);
    return std::format("{}", units);
}

std::string TaxedPriceText(std::uint32_t price, std::int32_t taxRate)
{
    // 0x5b12f3..0x5b1314: __allmul(impuesto con signo, precio sin signo) y __alldiv por 100; el dword bajo.
    const std::int64_t product = static_cast<std::int64_t>(taxRate) * static_cast<std::int64_t>(price);
    const auto tax = static_cast<std::uint32_t>(static_cast<std::uint64_t>(product / 100));
    return ThousandsText(price + tax);
}

Core::Expected<MixQuote, std::string> MixQuoteOf(const NpcWindowState& npc)
{
    // 0x5fedac..0x5fede8: la chance de lo que vale la caja (/ 20.000 con signo, tope 100) y su precio.
    std::int32_t rate = npc.chaosBoxValue / 20'000;
    if (rate > 100)
        rate = 100;
    auto quote = [](std::int32_t chance, std::uint32_t price)
    { return MixQuote{.rate = chance, .price = price, .priceText = ThousandsText(price)}; };
    MixQuote result = quote(rate, static_cast<std::uint32_t>(rate * 10'000));
    const auto luckRate = [&npc](std::int32_t base) { return base + (npc.mixLuck != 0 ? 25 : 0); };
    switch (npc.mixKind)
    {
    case -0x18:
        // 0x5ff0a0: la chance en 0 y el texto del precio de antes.
        result.rate = 0;
        return result;
    case -8:
    case -2:
    case 0:
        return quote(0, 0);
    case 2:
    {
        // 0x5fee23..0x5fee88: la chance de la tasa del nivel (0x6c9da0 + 4 · nivel) y el precio de la tabla.
        if (npc.mixLevel <= 0)
            return result;
        std::int32_t chance = 60;
        if (npc.mixLevel != 1)
        {
            const std::int32_t at = npc.mixLevel - 2;
            if (at >= static_cast<std::int32_t>(npc.chaosRates.size()))
                return Core::Unexpected(std::format("0x5fee3a: el nivel {} del 2 lee la .data después de las tasas "
                                                    "(0x6c9da0 + {:#x}: el formato de 0x6c9dc0 y el brillo de "
                                                    "0x6c9dcc), 08 P-C197",
                                                    npc.mixLevel, npc.mixLevel * 4));
            chance = npc.chaosRates[static_cast<std::size_t>(at)];
        }
        constexpr std::array<std::uint32_t, 7> Prices{100'000, 100'000,   200'000,  400'000,
                                                      700'000, 1'100'000, 1'600'000};
        const std::int32_t at = npc.mixLevel - 1 < 6 ? npc.mixLevel - 1 : 6;
        return quote(chance, Prices[static_cast<std::size_t>(at)]);
    }
    case 3:
        return quote(luckRate(50), 2'000'000);
    case 4:
        return quote(luckRate(45), 4'000'000);
    case 5:
        return quote(70, 500'000);
    case 6:
        return quote(90, 3'000'000);
    case 7:
    case 0x18:
        return quote(npc.mixWingRate, 5'000'000);
    case 8:
    {
        if (npc.mixLevel <= 0)
            return result;
        constexpr std::array<std::uint32_t, 8> Prices{50'000,  50'000,  80'000,  150'000,
                                                      250'000, 400'000, 600'000, 850'000};
        const std::int32_t at = npc.mixLevel - 1 < 7 ? npc.mixLevel - 1 : 7;
        return quote(80, Prices[static_cast<std::size_t>(at)]);
    }
    case 0xf:
        return quote(100, 100'000);
    case 0x10:
        return quote(100, 50'000);
    case 0x11:
        return quote(100, 5'000'000);
    case 0x16:
        return quote(luckRate(45), 6'000'000);
    case 0x17:
        return quote(luckRate(45), 8'000'000);
    case 0x19:
        return quote(70, 0);
    case 0x1a:
        return quote(50, 0);
    case 0x1b:
        return quote(30, 10'000'000);
    case 0x1c:
    {
        // 0x5fef82..0x5fefa8: lo que valen los excelentes · 100 / 1.000.000, con signo, tope 79.
        const auto scaled = static_cast<std::int32_t>(static_cast<std::uint32_t>(npc.mixExcellentValue) * 100u);
        const std::int32_t chance = scaled / 1'000'000;
        return quote(chance > 79 ? 79 : chance, 10'000'000);
    }
    default:
        return result;
    }
}
} // namespace Client::Scenes
