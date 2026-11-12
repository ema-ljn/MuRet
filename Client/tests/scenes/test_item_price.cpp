#include "Scenes/ItemPrice.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "World/ItemState.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// El precio de un ítem (0x54d0f0) y el de repararlo (0x5b13e0), CL8 hito 5 (06 § El precio de un ítem). Lo que se
// espera sale de las cuentas del ASM hechas a mano (y con un oráculo chico en Python, con la x87 en PC53 sobre
// fracciones exactas), con registros de Item.bmd inventados y, al final, con el Item.bmd real del repo.
namespace
{
using Client::Scenes::ItemPrice;
using Client::Scenes::PetPriceLevels;
using Client::Scenes::RepairPriceOf;
using Client::World::ItemState;

constexpr std::int32_t ShopGrid = Client::Scenes::ShopGridPrice;
constexpr std::int32_t Inventory = Client::Scenes::InventoryPrice;
constexpr std::int32_t Repair = Client::Scenes::RepairPriceMode;

// Los tipos inventados de esta prueba.
constexpr std::uint16_t OneHanded = 0x0010;
constexpr std::uint16_t TwoHanded = 0x0011;
constexpr std::uint16_t Armor = 0x0e10;
constexpr std::uint16_t EarlyWing = 0x1800;
constexpr std::uint16_t TallWing = 0x1801;
constexpr std::uint16_t WrappedWing = 0x1802;
constexpr std::uint16_t SkillWing = 0x1803;
constexpr std::uint16_t LateWing = 0x1814;
constexpr std::uint16_t Helper = 0x1a02;
constexpr std::uint16_t ZeroHelper = 0x1a06;
constexpr std::uint16_t Scroll = 0x1e10;
constexpr std::uint16_t Potion = 0x1c01;
constexpr std::uint16_t LastPotion = 0x1c08;

struct Option
{
    std::uint8_t tag = 0;
    std::uint8_t value = 0;
};

// Los registros de Item.bmd en claro, con los campos que lee 0x54d0f0.
class PriceRecords
{
public:
    PriceRecords() : m_plain(Client::Data::Tables::ItemTableSize) {}

    PriceRecords& Byte(std::uint16_t type, std::size_t offset, std::uint8_t value)
    {
        At(type, offset) = std::byte{value};
        return *this;
    }

    // +0x20: el nivel del ítem.
    PriceRecords& Level(std::uint16_t type, std::uint16_t level)
    {
        Byte(type, 0x20, static_cast<std::uint8_t>(level & 0xff));
        return Byte(type, 0x21, static_cast<std::uint8_t>(level >> 8));
    }

    // +0x40: el precio fijo.
    PriceRecords& Fixed(std::uint16_t type, std::int32_t price)
    {
        const auto bits = static_cast<std::uint32_t>(price);
        for (std::size_t i = 0; i < 4; ++i)
            Byte(type, 0x40 + i, static_cast<std::uint8_t>((bits >> (8 * i)) & 0xff));
        return *this;
    }

    [[nodiscard]] Client::Data::Tables::ItemTable Table() const
    {
        const std::vector<std::byte> encrypted =
            Client::Tests::EncryptRecords(m_plain, Client::Data::Tables::ItemRecordSize);
        return Client::Data::Tables::ItemTable(std::span<const std::byte>(encrypted));
    }

private:
    std::byte& At(std::uint16_t type, std::size_t offset)
    {
        return m_plain[std::size_t{type} * Client::Data::Tables::ItemRecordSize + offset];
    }

    std::vector<std::byte> m_plain;
};

constexpr std::size_t TwoHandedAt = 0x1e;
constexpr std::size_t DurabilityAt = 0x2d;
constexpr std::size_t PotionValueAt = 0x3c;

Client::Data::Tables::ItemTable Items()
{
    PriceRecords records;
    records.Level(0x0005, 0).Byte(0x0005, DurabilityAt, 22);
    records.Level(OneHanded, 30).Byte(OneHanded, DurabilityAt, 40);
    records.Level(TwoHanded, 30).Byte(TwoHanded, DurabilityAt, 40).Byte(TwoHanded, TwoHandedAt, 1);
    records.Level(Armor, 30).Byte(Armor, DurabilityAt, 40);
    records.Level(EarlyWing, 100).Byte(EarlyWing, DurabilityAt, 200);
    records.Level(TallWing, 2080).Level(WrappedWing, 822).Level(SkillWing, 490);
    records.Level(LateWing, 20);
    records.Level(Helper, 25).Byte(Helper, DurabilityAt, 255).Level(ZeroHelper, 25);
    records.Level(Scroll, 10);
    records.Level(0x1a00, 23).Level(0x1a1e, 180).Byte(0x1a1e, DurabilityAt, 200);
    records.Fixed(0x1e00, 17000).Fixed(0x1e01, 1234).Fixed(0x1e02, 123).Fixed(0x1e03, 99);
    records.Fixed(0x1e04, -500).Fixed(0x1e05, -7);
    records.Byte(0x0807, DurabilityAt, 255);
    records.Level(Potion, 10).Byte(Potion, PotionValueAt, 10).Byte(LastPotion, PotionValueAt, 255);
    records.Level(0x1c09, 15).Byte(0x1c09, PotionValueAt, 30);
    records.Byte(0x1a14, DurabilityAt, 30);
    return records.Table();
}

ItemState Item(std::uint16_t type, std::uint32_t level = 0, std::uint8_t durability = 0,
               std::initializer_list<Option> options = {})
{
    ItemState item;
    item.id = type;
    item.scalar1Low = level << 3u;
    item.durability = durability;
    std::size_t index = 0;
    for (const Option& option : options)
    {
        item.optionBytes[index] = option.tag;
        item.optionBytes[Client::World::ItemOptionValueOffset + index] = option.value;
        ++index;
    }
    item.optionCount = static_cast<std::uint8_t>(index);
    return item;
}

// Las opciones como bytes de D+0x2b..D+0x3b, con su cuenta aparte: la etiqueta 8 pisa el valor 0.
ItemState RawItem(std::uint16_t type, std::uint8_t durability, std::uint8_t count, std::vector<std::uint8_t> bytes)
{
    ItemState item = Item(type, 0, durability);
    for (std::size_t i = 0; i < bytes.size(); ++i)
        item.optionBytes[i] = bytes[i];
    item.optionCount = count;
    return item;
}

// El arma +9 excelente del tooltip: 0x50 con 4 y 0x5f.
ItemState ExcellentWeapon(std::uint8_t durability = 25)
{
    ItemState item = Item(0x0005, 9, durability, {{.tag = 0x50, .value = 4}, {.tag = 0x5f}});
    item.flags1b = 0x02;
    return item;
}

class ItemPriceTest : public ::testing::Test
{
protected:
    std::uint32_t Price(const ItemState& item, std::int32_t mode, std::optional<PetPriceLevels> pets = std::nullopt)
    {
        auto price = ItemPrice(item, items, mode, pets);
        EXPECT_TRUE(price.HasValue()) << price.Error();
        return price.HasValue() ? price.Value() : 0;
    }

    std::string Cut(const ItemState& item, std::int32_t mode, std::optional<PetPriceLevels> pets = std::nullopt)
    {
        auto price = ItemPrice(item, items, mode, pets);
        EXPECT_FALSE(price.HasValue());
        return price.HasValue() ? std::string() : price.Error();
    }

    Client::Data::Tables::ItemTable items = Items();
};

// Lo que pasa de 2^31 vuelve como el dword bajo: −x es 2^32 − x.
constexpr std::uint32_t LowDword(std::int64_t value)
{
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(value));
}

TEST_F(ItemPriceTest, TheEmptyItemIsFreeAndATypeOutsideItemBmdCuts)
{
    EXPECT_EQ(Price(Item(0xffff), ShopGrid), 0u);
    // 0x54d10e: movsx del tipo y [0x7758550] + tipo · 0x54, sin mirar el rango.
    EXPECT_NE(Cut(Item(0x2000), ShopGrid).find("0x54d133"), std::string::npos);
    EXPECT_NE(Cut(Item(0x8000), ShopGrid).find("0x54d133"), std::string::npos);
}

TEST_F(ItemPriceTest, AFixedPriceIsRoundedAndDividedByThreeOutsideModeZero)
{
    // 0x54d13a..0x54d1b1: a 100 desde 1.000 y a 10 desde 100; los otros modos, por 3 antes.
    EXPECT_EQ(Price(Item(0x1e00), ShopGrid), 17000u);
    EXPECT_EQ(Price(Item(0x1e00), Repair), 5600u); // 5.666
    EXPECT_EQ(Price(Item(0x1e01), ShopGrid), 1200u);
    EXPECT_EQ(Price(Item(0x1e01), Inventory), 410u); // 411
    EXPECT_EQ(Price(Item(0x1e02), ShopGrid), 120u);
    EXPECT_EQ(Price(Item(0x1e02), Repair), 41u);
    EXPECT_EQ(Price(Item(0x1e03), ShopGrid), 99u);
    EXPECT_EQ(Price(Item(0x1e03), Repair), 33u);
    // Uno negativo sale por 0x54dc5e sin redondear: −500 y −500 / 3 = −166; −7 / 3 = −2.
    EXPECT_EQ(Price(Item(0x1e04), ShopGrid), LowDword(-500));
    EXPECT_EQ(Price(Item(0x1e04), Repair), LowDword(-166));
    EXPECT_EQ(Price(Item(0x1e05), ShopGrid), LowDword(-7));
    EXPECT_EQ(Price(Item(0x1e05), Inventory), LowDword(-2));
}

TEST_F(ItemPriceTest, BoltsAndArrowsGoByTheirDurability)
{
    // 0x54d26a: 200 · 100 / 255 = 78; 200 · 1.400 / 255 = 1.098; 200 · 2.200 / 255 = 1.725.
    EXPECT_EQ(Price(Item(0x807, 0, 200), ShopGrid), 78u);
    EXPECT_EQ(Price(Item(0x807, 1, 200), ShopGrid), 1000u);
    EXPECT_EQ(Price(Item(0x807, 2, 200), ShopGrid), 1700u);
    // Desde el nivel 3, EBX es la dirección del ítem (Q-C735).
    EXPECT_NE(Cut(Item(0x807, 3, 200), ShopGrid).find("0x54d283"), std::string::npos);
    // Sin la durabilidad de Item.bmd no multiplica: 0, aunque sea de nivel 3.
    EXPECT_EQ(Price(Item(0x80f, 3, 200), ShopGrid), 0u);
}

TEST_F(ItemPriceTest, JewelBundlesAndNamedTypesHaveTheirPrices)
{
    // 0x402d60: (5 · nivel + 5) · 18.000.000 y (nivel + 1) · 60.000.000.
    EXPECT_EQ(Price(Item(0x181e), ShopGrid), 90'000'000u);
    EXPECT_EQ(Price(Item(0x181e, 15), ShopGrid), 1'440'000'000u);
    EXPECT_EQ(Price(Item(0x181f, 2), Repair), 60'000'000u);
    // 0x54d2b1..0x54d63a.
    const std::vector<std::pair<std::uint16_t, std::uint32_t>> constants{
        {0x1c0d, 9'000'000},  {0x1c0e, 6'000'000},  {0x180f, 810'000},   {0x1c10, 45'000'000},
        {0x1c16, 36'000'000}, {0x1a0f, 33'000'000}, {0x1c1c, 600'000},   {0x1a1d, 5'000},
        {0x1c14, 900},        {0x1c0a, 750},        {0x1c1f, 60'000'000}};
    for (const auto& [type, price] : constants)
        EXPECT_EQ(Price(Item(type), ShopGrid), price) << type;
    EXPECT_EQ(Price(Item(0x1c0d), Repair), 3'000'000u);
    // Los de los niveles 0 y 1; con otro, 0.
    EXPECT_EQ(Price(Item(0x1a0e, 0), ShopGrid), 180'000u);
    EXPECT_EQ(Price(Item(0x1a0e, 1), ShopGrid), 7'500'000u);
    EXPECT_EQ(Price(Item(0x1a0e, 2), ShopGrid), 0u);
    EXPECT_EQ(Price(Item(0x1a07, 0), ShopGrid), 1'500'000u);
    EXPECT_EQ(Price(Item(0x1a07, 1), ShopGrid), 1'200'000u);
    EXPECT_EQ(Price(Item(0x1a07, 2), ShopGrid), 0u);
    EXPECT_EQ(Price(Item(0x1a0b, 0), ShopGrid), 100'000u);
    EXPECT_EQ(Price(Item(0x1a0b, 1), ShopGrid), 2'400'000u);
    EXPECT_EQ(Price(Item(0x1a0b, 2), ShopGrid), 0u);
}

TEST_F(ItemPriceTest, TheDinorantAddsItsOptions)
{
    EXPECT_EQ(Price(Item(0x1a03), ShopGrid), 960'000u);
    // 0x61 y 0x67 suman 300.000 cada una; 0x62 no.
    EXPECT_EQ(Price(Item(0x1a03, 0, 0, {{.tag = 0x61}, {.tag = 0x67}, {.tag = 0x62}}), ShopGrid), 1'560'000u);
    EXPECT_EQ(Price(Item(0x1a03, 0, 0, {{.tag = 0x68}}), Repair), 420'000u);
}

TEST_F(ItemPriceTest, TheEventItemsGoByTheirLevelOrTheirDurability)
{
    // 0x1a10: la tabla del nivel 1 al 7, por 3; con 0 o con 8, 0.
    EXPECT_EQ(Price(Item(0x1a10, 0), ShopGrid), 0u);
    EXPECT_EQ(Price(Item(0x1a10, 1), ShopGrid), 15'000u);
    EXPECT_EQ(Price(Item(0x1a11, 3), ShopGrid), 30'000u);
    EXPECT_EQ(Price(Item(0x1a10, 7), ShopGrid), 75'000u);
    EXPECT_EQ(Price(Item(0x1a10, 8), ShopGrid), 0u);
    // 0x1a12: (5 · nivel + 45) · 4.000 por 3, salvo el nivel 1 (50.000 por 3).
    EXPECT_EQ(Price(Item(0x1a12, 0), ShopGrid), 540'000u);
    EXPECT_EQ(Price(Item(0x1a12, 1), ShopGrid), 150'000u);
    EXPECT_EQ(Price(Item(0x1a12, 2), ShopGrid), 660'000u);
    EXPECT_EQ(Price(Item(0x1c1d, 0, 3), ShopGrid), 90'000u);
    EXPECT_EQ(Price(Item(0x1c15, 0, 2), ShopGrid), 9'000u);
    EXPECT_EQ(Price(Item(0x1c15, 1, 2), ShopGrid), 9'000u);
    EXPECT_EQ(Price(Item(0x1c15, 2, 2), ShopGrid), 0u);
    EXPECT_EQ(Price(Item(0x1c15, 3, 2), ShopGrid), 7'800u);
    const std::vector<std::uint32_t> devil{30'000, 15'000, 21'000, 30'000, 45'000, 45'000};
    for (std::uint32_t level = 0; level < devil.size(); ++level)
        EXPECT_EQ(Price(Item(0x1c11, level), ShopGrid), devil[level]) << level;
    EXPECT_EQ(Price(Item(0x1c12, 2), ShopGrid), 21'000u);
    EXPECT_EQ(Price(Item(0x1c13, 2), ShopGrid), 84'000u);
    EXPECT_EQ(Price(Item(0x1c07, 0, 10), ShopGrid), 9'000'000u);
    EXPECT_EQ(Price(Item(0x1c07, 1, 10), ShopGrid), 4'500'000u);
    EXPECT_EQ(Price(Item(0x1c07, 2, 10), ShopGrid), 0u);
    // 0x1c09 solo tiene precio propio en el nivel 1; en el 0 va por su +0x3c (30): 10 · 900 / 12 = 750.
    EXPECT_EQ(Price(Item(0x1c09, 1), ShopGrid), 1'000u);
    EXPECT_EQ(Price(Item(0x1c09, 0), ShopGrid), 750u);
}

TEST_F(ItemPriceTest, PotionsReturnTheirPriceByTenWithoutTheModeAtTheEnd)
{
    // 10 · 10² / 12 = 83, por 3 de durabilidad = 249, / 10 · 10.
    EXPECT_EQ(Price(Item(Potion, 0, 3), ShopGrid), 240u);
    // El modo divide antes del / 10: 249 / 3 = 83.
    EXPECT_EQ(Price(Item(Potion, 0, 3), Repair), 80u);
    // Por 2^2: 996.
    EXPECT_EQ(Price(Item(Potion, 2, 3), ShopGrid), 990u);
    // 54.187 · 2^15 · 255 / 10 = 45.277.790.208: su dword bajo por 10, en 32 bits.
    EXPECT_EQ(Price(Item(LastPotion, 15, 255), ShopGrid), 1'806'336'000u);
}

TEST_F(ItemPriceTest, TheWizardRingAndTheSpiritComeAfterThePotionValue)
{
    EXPECT_EQ(Price(Item(0x1a14, 0), ShopGrid), 30'000u);
    EXPECT_EQ(Price(Item(0x1a14, 1), ShopGrid), 0u);
    EXPECT_EQ(Price(Item(0x1a14, 2), ShopGrid), 0u);
    EXPECT_EQ(Price(Item(0x1a1f, 0), ShopGrid), 30'000'000u);
    EXPECT_EQ(Price(Item(0x1a1f, 1), ShopGrid), 15'000'000u);
    EXPECT_EQ(Price(Item(0x1a1f, 2), ShopGrid), 0u);
    // Con un +0x3c, el anillo va por él (0x54d647 antes de 0x54d6ef).
    PriceRecords records;
    records.Byte(0x1a14, PotionValueAt, 30);
    const Client::Data::Tables::ItemTable withValue = records.Table();
    auto price = ItemPrice(Item(0x1a14, 0), withValue, ShopGrid, std::nullopt);
    ASSERT_TRUE(price.HasValue()) << price.Error();
    EXPECT_EQ(price.Value(), 750u);
}

TEST_F(ItemPriceTest, HelpersScrollsAndLateWingsAreCubes)
{
    // 0x54d963: b³ + 100 con b = 25 (+0x20) + 3 · nivel.
    EXPECT_EQ(Price(Item(Helper), ShopGrid), 15'700u);
    // b = 25 + 6 + 25 (la 0x56): 175.716, por 3 + 1 y por 1 + 1 con las dos 0x55.
    EXPECT_EQ(
        Price(Item(Helper, 2, 0, {{.tag = 0x55, .value = 3}, {.tag = 0x56}, {.tag = 0x55, .value = 1}}), ShopGrid),
        1'405'700u);
    EXPECT_EQ(Price(Item(LateWing, 1), ShopGrid), 12'200u);
    EXPECT_EQ(Price(Item(Scroll), ShopGrid), 1'100u);
    // El ángel no se divide por el modo (0x54da45).
    EXPECT_EQ(Price(Item(0x1a00), Repair), 12'200u);
    EXPECT_EQ(Price(Item(0x1a00), ShopGrid), 12'200u);
    // La capa es del grupo 13: va por acá y no por la cuenta de las alas que la nombra (Q-C737). b = 180 + 9.
    EXPECT_EQ(Price(Item(0x1a1e, 3), ShopGrid), 6'751'300u);
}

TEST_F(ItemPriceTest, WeaponsAndArmorsGoByTheirLevel)
{
    // b = 30: 70 · 900 / 8 + 100 = 7.975; las de una mano de los grupos 0 a 6, al 80 %.
    EXPECT_EQ(Price(Item(OneHanded), ShopGrid), 6'300u);
    EXPECT_EQ(Price(Item(TwoHanded), ShopGrid), 7'900u);
    EXPECT_EQ(Price(Item(Armor), ShopGrid), 7'900u);
    EXPECT_EQ(Price(Item(Armor, 4), ShopGrid), 18'100u);
    // Desde el 5, lo de la tabla de 0x54dc68: 4 ... 245 en el 13; el 14 y el 15 no suman.
    EXPECT_EQ(Price(Item(Armor, 5), ShopGrid), 26'800u);
    EXPECT_EQ(Price(Item(Armor, 13), ShopGrid), 4'362'900u);
    EXPECT_EQ(Price(Item(Armor, 14), ShopGrid), 72'600u);
    EXPECT_EQ(Price(Item(Armor, 15), ShopGrid), 80'900u);
    // Una opción de 0x56 a 0x63 suma 25 a b y dobla el precio: 36.021 · 2.
    EXPECT_EQ(Price(Item(Armor, 0, 0, {{.tag = 0x57}}), ShopGrid), 72'000u);
}

TEST_F(ItemPriceTest, EachOptionChangesThePriceOfAnArmor)
{
    const auto with = [this](std::uint8_t tag, std::uint8_t value = 0)
    { return Price(Item(Armor, 0, 0, {{.tag = tag, .value = value}}), ShopGrid); };
    // 7.975 + el dword bajo de 7.975 · 1,5f.
    EXPECT_EQ(with(0x12), 19'900u);
    EXPECT_EQ(with(0x38), 19'900u);
    // Los escalones, por los doubles de 0x6ba420..0x6ba408: + 4.785, 11.165, 22.330 y 44.660.
    EXPECT_EQ(with(0x51, 4), 12'700u);
    EXPECT_EQ(with(0x51, 8), 19'100u);
    EXPECT_EQ(with(0x51, 12), 30'300u);
    EXPECT_EQ(with(0x51, 16), 52'600u);
    EXPECT_EQ(with(0x51, 5), 7'900u);
    EXPECT_EQ(with(0x52, 5), 12'700u);
    EXPECT_EQ(with(0x52, 10), 19'100u);
    EXPECT_EQ(with(0x52, 15), 30'300u);
    EXPECT_EQ(with(0x52, 20), 52'600u);
    EXPECT_EQ(with(0x52, 4), 7'900u);
    // Fuera de las alas, el valor de la 0x55 no va por 4.
    EXPECT_EQ(with(0x55, 4), 12'700u);
    EXPECT_EQ(with(0x54), 9'900u);
    EXPECT_EQ(with(0x66), 9'900u);
    EXPECT_EQ(with(0x6e), 15'900u);
    EXPECT_EQ(with(0x19), 7'900u);
}

TEST_F(ItemPriceTest, EarlyWingsUseTheirOwnFormula)
{
    // 0x54d7b8: (b + 40) · b² · 11 + 40.000.000, sin el 80 %.
    EXPECT_EQ(Price(Item(EarlyWing), ShopGrid), 55'400'000u);
    // En las alas, el valor de la 0x55 va por 4: 1 es el escalón de 4 (0,6) y 4 el de 16 (5,6).
    EXPECT_EQ(Price(Item(EarlyWing, 0, 0, {{.tag = 0x55, .value = 1}}), ShopGrid), 88'640'000u);
    EXPECT_EQ(Price(Item(EarlyWing, 0, 0, {{.tag = 0x55, .value = 4}}), ShopGrid), 365'640'000u);
    EXPECT_EQ(Price(Item(EarlyWing, 0, 0, {{.tag = 0x50, .value = 4}}), ShopGrid), 88'640'000u);
}

TEST_F(ItemPriceTest, ThePriceIsCappedAtThreeBillionBeforeTheMode)
{
    // b = 2.080: 2.147.400.192, doblado.
    EXPECT_EQ(Price(Item(TallWing, 0, 0, {{.tag = 0x6e}}), ShopGrid), 3'000'000'000u);
    EXPECT_EQ(Price(Item(TallWing, 0, 0, {{.tag = 0x6e}}), Repair), 1'000'000'000u);
    // Con nueve opciones (0x6e y ocho 0x52 con 20: la novena etiqueta es el valor de la primera), 2^53 no se pasa.
    std::vector<std::uint8_t> bytes{0x6e, 0x52, 0x52, 0x52, 0x52, 0x52, 0x52, 0x52, 0x52};
    bytes.insert(bytes.end(), 8, 20);
    EXPECT_EQ(Price(RawItem(TallWing, 0, 9, bytes), ShopGrid), 3'000'000'000u);
    // Con diez, la décima etiqueta es el valor de la segunda (20, una 0x14) y multiplica un precio de más de 53 bits.
    EXPECT_NE(Cut(RawItem(TallWing, 0, 10, bytes), ShopGrid).find("0x54d862"), std::string::npos);
}

TEST_F(ItemPriceTest, AWrappedNegativePriceIsNeitherCappedNorRounded)
{
    // b = 822: la cuenta en 32 bits da −2.143.098.904.
    EXPECT_EQ(Price(Item(WrappedWing), ShopGrid), LowDword(-2'143'098'904));
    EXPECT_EQ(Price(Item(WrappedWing), Repair), LowDword(-714'366'301));
}

TEST_F(ItemPriceTest, TheSkillOptionAddsOnlyTheLowDword)
{
    // b = 490: 1.439.783.000, doblado: 2.879.566.000. Por 1,5 da 4.319.349.000, y suma solo su dword bajo,
    // 24.381.704 (Q-C736): 2.903.947.704, debajo del tope.
    EXPECT_EQ(Price(Item(SkillWing, 0, 0, {{.tag = 0x6e}, {.tag = 0x12}}), ShopGrid), 2'903'947'700u);
}

TEST_F(ItemPriceTest, AnOptionReadBeyondTheItemCuts)
{
    // 0x54d1dc: la etiqueta 17 es D+0x3c.
    EXPECT_NE(Cut(RawItem(Armor, 0, 18, std::vector<std::uint8_t>(17, 0x19)), ShopGrid).find("0x54d1dc"),
              std::string::npos);
    // La décima etiqueta (el valor de la segunda) pide un valor en D+0x3c.
    const auto tenth = [](std::uint8_t tag)
    {
        std::vector<std::uint8_t> bytes(17, 0);
        bytes[0] = 0x19;
        bytes[1] = 0x19;
        bytes[9] = tag;
        return bytes;
    };
    EXPECT_NE(Cut(RawItem(Armor, 0, 10, tenth(0x50)), ShopGrid).find("0x54d8bb"), std::string::npos);
    EXPECT_NE(Cut(RawItem(EarlyWing, 0, 10, tenth(0x50)), ShopGrid).find("0x54d892"), std::string::npos);
    EXPECT_NE(Cut(RawItem(Armor, 0, 10, tenth(0x52)), ShopGrid).find("0x54d8df"), std::string::npos);
    EXPECT_NE(Cut(RawItem(Helper, 0, 10, tenth(0x55)), ShopGrid).find("0x54d99e"), std::string::npos);
}

TEST_F(ItemPriceTest, TheLateHelpersOverrideThePriceAfterTheMode)
{
    // 0x54da69..0x54db12: 50, 100, 1.000 y 10.000 por la durabilidad, 30.000 y 50.000, sin el / 3.
    const std::vector<std::uint32_t> prices{350, 700, 7'000, 70'000, 30'000, 50'000};
    for (std::uint16_t i = 0; i < prices.size(); ++i)
        EXPECT_EQ(Price(Item(static_cast<std::uint16_t>(0x1a20 + i), 0, 7), Repair), prices[i]) << i;
}

TEST_F(ItemPriceTest, ThePetsReadTheirLevels)
{
    const PetPriceLevels pets{.raven = 3, .horse = 7};
    EXPECT_EQ(Price(Item(0x1a04), ShopGrid, pets), 14'000'000u);
    // Después del modo: no se divide.
    EXPECT_EQ(Price(Item(0x1a05), Repair, pets), 3'000'000u);
    // 65.535 · 2.000.000 en 32 bits: 2.220.981.120.
    EXPECT_EQ(Price(Item(0x1a04), ShopGrid, PetPriceLevels{.raven = 0, .horse = 65535}), 2'220'981'100u);
    EXPECT_NE(Cut(Item(0x1a04), ShopGrid).find("0x54db28"), std::string::npos);
}

TEST_F(ItemPriceTest, TheInventoryModeDiscountsTheMissingDurability)
{
    // 687.986 / 3 = 229.328; (1 − 25 / 51) · 229.328 · 0,6f = 70.147,2: 159.181.
    EXPECT_EQ(Price(ExcellentWeapon(), Inventory), 159'100u);
    EXPECT_EQ(Price(ExcellentWeapon(), Repair), 229'300u);
    EXPECT_EQ(Price(ExcellentWeapon(), ShopGrid), 687'900u);
    // El 0x1a1f calcula y no descuenta.
    EXPECT_EQ(Price(Item(0x1a1f, 0, 3), Inventory), 10'000'000u);
}

TEST_F(ItemPriceTest, AZeroMaximumDurabilityDiscountsNothingOrCuts)
{
    // Un ala tardía de b = 20 sin durabilidad: 8.100 / 3 = 2.700. 0 / 0 es NaN y el _ftol da 0.
    EXPECT_EQ(Price(Item(LateWing), Inventory), 2'700u);
    EXPECT_NE(Cut(Item(LateWing, 0, 5), Inventory).find("0x54dc04"), std::string::npos);
    // Del 0x1c00 en adelante no se descuenta: el pergamino no llega a la FIDIV.
    EXPECT_EQ(Price(Item(Scroll, 0, 5), Inventory), 360u);
    // Ocho 0x55 con 255: el precio por 256^8 es 0 en 64 bits, y −infinito · 0 es NaN.
    std::vector<std::uint8_t> bytes(8, 0x55);
    bytes.insert(bytes.end(), 8, 255);
    EXPECT_EQ(Price(RawItem(ZeroHelper, 5, 8, bytes), Inventory), 0u);
    EXPECT_EQ(Price(Item(ZeroHelper), Inventory), 5'200u);
    EXPECT_NE(Cut(Item(ZeroHelper, 0, 5), Inventory).find("0x54dc04"), std::string::npos);
}

TEST_F(ItemPriceTest, ADurabilityAboveItsMaximumSkipsTheRounding)
{
    // (1 − 60 / 51) · 229.328 · 0,6f = −24.281,8: resta 2^32 − 24.281 y el precio queda negativo, sin redondear
    // (Q-C736).
    EXPECT_EQ(Price(ExcellentWeapon(60), Inventory), 253'609u);
}

TEST_F(ItemPriceTest, AHugeHelperPriceWrapsAndCutsInTheDiscount)
{
    // Nueve 0x55 (la novena etiqueta es el valor de la primera, 0x55 = 85) y ocho valores 251: el producto en
    // 64 bits es negativo y no tiene tope.
    std::vector<std::uint8_t> bytes(9, 0x55);
    bytes.insert(bytes.end(), 8, 251);
    EXPECT_EQ(Price(RawItem(Helper, 100, 9, bytes), ShopGrid), 3'869'114'368u);
    EXPECT_NE(Cut(RawItem(Helper, 100, 9, bytes), Inventory).find("0x54dbf9"), std::string::npos);
    // Con 250, positivo: el tope, / 3 y el descuento.
    bytes.assign(9, 0x55);
    bytes.insert(bytes.end(), 8, 250);
    EXPECT_EQ(Price(RawItem(Helper, 100, 9, bytes), ShopGrid), 3'000'000'000u);
    EXPECT_EQ(Price(RawItem(Helper, 100, 9, bytes), Inventory), 635'294'100u);
}

TEST(RepairPriceTest, AWornWeaponPaysForWhatItLacks)
{
    // 3 · 229.300^(3/4) · (26 / 51 en float) + 1 = 16.027,1.
    auto repair = RepairPriceOf(229'300, 25, 51, 0);
    EXPECT_EQ(repair.gold, 16'000);
    EXPECT_EQ(repair.text, "16,000");
    // [0x78abb10] en 1 suma el 5 %; solo cuenta su byte bajo.
    EXPECT_EQ(RepairPriceOf(229'300, 25, 51, 1).text, "16,800");
    EXPECT_EQ(RepairPriceOf(229'300, 25, 51, 0x101).text, "16,800");
    EXPECT_EQ(RepairPriceOf(229'300, 25, 51, 0x100).text, "16,000");
    // Sin durabilidad, por 1,4f.
    EXPECT_EQ(RepairPriceOf(229'300, 0, 51, 0).gold, 44'000);
}

TEST(RepairPriceTest, NothingMissingCostsNothing)
{
    for (const auto& [durability, maximum] :
         std::vector<std::pair<std::int32_t, std::int32_t>>{{51, 51}, {60, 51}, {5, 0}})
    {
        auto repair = RepairPriceOf(229'300, durability, maximum, 0);
        EXPECT_EQ(repair.gold, 0) << durability << "/" << maximum;
        EXPECT_EQ(repair.text, "0");
    }
    // Un precio de 0 cuesta 1.
    EXPECT_EQ(RepairPriceOf(0, 1, 2, 0).text, "1");
    EXPECT_EQ(RepairPriceOf(0, 0, 2, 0).text, "1");
}

TEST(RepairPriceTest, ThePriceIsCappedAndANegativeOneIsZero)
{
    // 400.000.000 por 1,4f y por 1 + 255 · 0,05f.
    EXPECT_EQ(RepairPriceOf(1'000'000'000, 0, 255, 255).text, "163,341,600");
    EXPECT_EQ(RepairPriceOf(400'000'000, 0, 255, 255).text, "163,341,600");
    // Con signo, es menos que el tope: la FSQRT da NaN y el _ftol, 0.
    EXPECT_EQ(RepairPriceOf(0x80000000u, 1, 2, 0).text, "0");
    EXPECT_EQ(RepairPriceOf(0xffffffffu, 1, 2, 0).text, "0");
}

TEST(RepairPriceTest, TheGoldIsRoundedAndGroupedByThousands)
{
    EXPECT_EQ(RepairPriceOf(50, 1, 2, 0).text, "29");
    EXPECT_EQ(RepairPriceOf(900, 1, 2, 0).text, "240");
    EXPECT_EQ(RepairPriceOf(90'000, 1, 2, 0).text, "7,700");
    EXPECT_EQ(RepairPriceOf(5'000'000, 1, 2, 0).text, "158,600");
}

// Con el Item.bmd del repo (Client/assets/Data).
class GoldenItemPriceTest : public Client::Tests::GoldenDataTest
{
protected:
    void SetUp() override
    {
        GoldenDataTest::SetUp();
        const auto path = DataFile("Local\\Item.bmd");
        ASSERT_TRUE(path.has_value());
        const auto file = Client::Tests::ReadFileBytes(*path);
        ASSERT_TRUE(file.has_value());
        auto loaded = Client::Data::Tables::LoadItemTable(std::as_bytes(std::span(*file)));
        ASSERT_TRUE(loaded.HasValue());
        m_items.emplace(std::move(loaded).Value().table);
    }

    std::uint32_t Price(const ItemState& item, std::int32_t mode)
    {
        auto price = ItemPrice(item, *m_items, mode, std::nullopt);
        EXPECT_TRUE(price.HasValue()) << price.Error();
        return price.HasValue() ? price.Value() : 0;
    }

    std::optional<Client::Data::Tables::ItemTable> m_items;
};

TEST_F(GoldenItemPriceTest, TheRealItemsHaveTheirPrices)
{
    // Kris: +0x20 6, de una mano: (46 · 36 / 8 + 100) · 0,8 = 245.
    EXPECT_EQ(Price(Item(0x0000), ShopGrid), 240u);
    // Orb of Twisting Slash: +0x40 29.000.
    EXPECT_EQ(Price(Item(0x1807), ShopGrid), 29'000u);
    EXPECT_EQ(Price(Item(0x1807), Inventory), 9'600u);
    // Healing Potion (+0x3c 20) por 5: 333 · 5.
    EXPECT_EQ(Price(Item(0x1c02, 0, 5), ShopGrid), 1'660u);
    // Wings of Elf (+0x20 100), y +7 con la 0x50 en 8 (1,4).
    EXPECT_EQ(Price(Item(0x1800), ShopGrid), 55'400'000u);
    EXPECT_EQ(Price(Item(0x1800, 7, 0, {{.tag = 0x50, .value = 8}}), Repair), 66'890'000u);
    EXPECT_EQ(Price(Item(0x1c0d), ShopGrid), 9'000'000u);
    // Cape of Lord +3 (+0x20 180): 189³ + 100.
    EXPECT_EQ(Price(Item(0x1a1e, 3), ShopGrid), 6'751'300u);
    EXPECT_EQ(Price(Item(0x1a00), Repair), 12'200u);
}

TEST_F(GoldenItemPriceTest, ARealSwordPaysItsRepair)
{
    // Legendary Sword +9 excelente con la skill y la 0x57, con 30 de 83 de durabilidad.
    ItemState sword = Item(0x000b, 9, 30, {{.tag = 0x12}, {.tag = 0x57}});
    sword.flags1b = 0x04;
    const std::uint32_t price = Price(sword, Repair);
    EXPECT_EQ(price, 1'085'600u);
    EXPECT_EQ(RepairPriceOf(price, 30, 83, 1).text, "67,600");
}
} // namespace
