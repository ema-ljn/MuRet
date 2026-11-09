#include "Scenes/OwnItemOptions.h"

#include "World/ItemState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <gtest/gtest.h>

namespace
{
using Client::Scenes::AddOwnItemOptionValues;
using Client::Scenes::ApplyOwnItemOptionPercent;
using Client::Scenes::OwnItemOptionsView;

OwnItemOptionsView Options(const Client::World::ItemState& item)
{
    return {item.id, item.durability, item.optionCount, item.optionBytes};
}

TEST(OwnItemOptionsTest, AddsAllUnsignedValuesWithWrapAndFullSelector)
{
    Client::World::ItemState item;
    item.id = 0xfffe;
    item.durability = 0x80;
    item.optionCount = 4;
    item.optionBytes = {0x90, 0x91, 0x90, 0x90, 0, 0, 0, 0, 255, 71, 7, 128};
    std::array destination{std::byte{0xfa}, std::byte{0xff}};
    AddOwnItemOptionValues(destination, 0x190, Options(item));
    EXPECT_EQ(destination[0], std::byte{0xfa});
    AddOwnItemOptionValues(destination, 0x90, Options(item));
    EXPECT_EQ(destination[0], std::byte{0x80}); // 65530+255+7+128 = 65920 -> 384
    EXPECT_EQ(destination[1], std::byte{1});
}

TEST(OwnItemOptionsTest, PreservesResidualsOnSentinelEmptyMissAndDisabledAdd)
{
    Client::World::ItemState item;
    item.id = 0xffff;
    item.durability = 1;
    item.optionCount = 1;
    item.optionBytes[0] = 0x90;
    item.optionBytes[8] = 99;
    std::array destination{std::byte{17}, std::byte{23}};
    AddOwnItemOptionValues(destination, 0x90, Options(item));
    ApplyOwnItemOptionPercent(destination, 0x90, Options(item), 77);
    item.id = 0;
    item.optionCount = 0;
    AddOwnItemOptionValues(destination, 0x90, Options(item));
    ApplyOwnItemOptionPercent(destination, 0x90, Options(item), 77);
    item.optionCount = 1;
    item.durability = 0;
    AddOwnItemOptionValues(destination, 0x90, Options(item)); // durability=0
    ApplyOwnItemOptionPercent(destination, 0x190, Options(item), 77);
    EXPECT_EQ(destination, (std::array{std::byte{17}, std::byte{23}}));
}

TEST(OwnItemOptionsTest, AppliesSequentialPercentWithoutDurabilityOrValueReads)
{
    Client::World::ItemState item;
    item.optionCount = 4;
    item.optionBytes = {0x90, 0x91, 0x90, 0x90}; // durability=0, values=0
    std::array destination{std::byte{101}, std::byte{0}};
    ApplyOwnItemOptionPercent(destination, 0x90, Options(item), 0x10032);
    // 101+50=151; 151+75=226; 226+113=339 (low16 factor=50).
    EXPECT_EQ(destination, (std::array{std::byte{83}, std::byte{1}}));
}

TEST(OwnItemOptionsTest, UsesSignedLow32ProductAndTruncatesNegativeTowardZero)
{
    Client::World::ItemState item;
    item.optionCount = 1;
    item.optionBytes[0] = 0x90;
    std::array destination{std::byte{0xff}, std::byte{0xff}};
    ApplyOwnItemOptionPercent(destination, 0x90, Options(item), 0xffff);
    // 65535*65535 low32 = -131071; /100 -> -1310; 65535-1310=64225.
    EXPECT_EQ(destination, (std::array{std::byte{0xe1}, std::byte{0xfa}}));
    destination = {std::byte{0x50}, std::byte{0xc3}}; // 50000
    ApplyOwnItemOptionPercent(destination, 0x90, Options(item), 100);
    EXPECT_EQ(destination, (std::array{std::byte{0xa0}, std::byte{0x86}})); // 100000 mod 65536
}

TEST(OwnItemOptionsTest, RereadsCountAndFutureTagsAfterAliasedAdds)
{
    const std::uint16_t id = 1;
    const std::uint8_t durability = 1;
    std::array<std::uint8_t, 20> storage{2, 0, 0};
    // Destino sobre count y primer tag: 0002 + 255 -> 0101; count baja a 1.
    storage[9] = 255;
    storage[10] = 7; // un count congelado procesaría indebidamente esta segunda suma.
    OwnItemOptionsView options{id, durability, storage[0], std::span(storage).subspan(1)};
    AddOwnItemOptionValues(std::as_writable_bytes(std::span(storage)).first<2>(), 0, options);
    EXPECT_EQ(storage[0], 1);
    EXPECT_EQ(storage[1], 1);
    // Destino sobre tags 0/1; la primera suma cambia el siguiente tag de 0 a 1.
    storage.fill(0);
    storage[0] = 2;
    storage[1] = 1;
    storage[9] = 255;
    storage[10] = 7;
    AddOwnItemOptionValues(std::as_writable_bytes(std::span(storage)).subspan<1, 2>(), 1, options);
    EXPECT_EQ(storage[1], 7);
    EXPECT_EQ(storage[2], 1);
}

TEST(OwnItemOptionsTest, RereadsCountAndFutureTagsAfterAliasedPercent)
{
    const std::uint16_t id = 1;
    const std::uint8_t durability = 0;
    std::array<std::uint8_t, 20> storage{2, 1, 1, 1};
    OwnItemOptionsView options{id, durability, storage[0], std::span(storage).subspan(1)};
    // 0x0102 se duplica a 0x0204; count sube de 2 a 4 y el primer tag pasa a 2.
    ApplyOwnItemOptionPercent(std::as_writable_bytes(std::span(storage)).first<2>(), 1, options, 100);
    EXPECT_EQ(storage[0], 16); // el tercer match está fuera del count inicial: 0408 -> 0810.
    EXPECT_EQ(storage[1], 8);
    storage.fill(0);
    storage[0] = 2;
    storage[1] = 1;
    // 0x0001 + 25600% -> 0x0101; el siguiente tag pasa a coincidir también.
    ApplyOwnItemOptionPercent(std::as_writable_bytes(std::span(storage)).subspan<1, 2>(), 1, options, 25600);
    EXPECT_EQ(storage[1], 1);
    EXPECT_EQ(storage[2], 2); // 257 + 65792 = 66049 -> 513
}

TEST(OwnItemOptionsTest, DoesNotClampHighUnsignedCountsOrSeparateOverlappingTagValues)
{
    const std::uint16_t id = 0;
    const std::uint8_t durability = 1;
    const std::uint8_t count = 255;
    std::array<std::uint8_t, 263> bytes{};
    bytes[254] = 0x90;
    bytes[262] = 201;
    std::array destination{std::byte{3}, std::byte{0}};
    AddOwnItemOptionValues(destination, 0x90, {id, durability, count, bytes});
    EXPECT_EQ(destination[0], std::byte{204});
    ApplyOwnItemOptionPercent(destination, 0x90, {id, durability, count, bytes}, 50);
    EXPECT_EQ(destination, (std::array{std::byte{50}, std::byte{1}})); // 204 + 102
    Client::World::ItemState item;
    item.durability = 1;
    item.optionCount = 9;
    item.optionBytes[0] = 0x90;
    item.optionBytes[8] = 0x90; // tag8 y valor0 son el mismo byte del productor.
    item.optionBytes[16] = 3;
    destination = {std::byte{0}, std::byte{0}};
    AddOwnItemOptionValues(destination, 0x90, Options(item));
    EXPECT_EQ(destination[0], std::byte{147});
}
} // namespace

namespace
{
TEST(OwnItemOptionsTest, RereadsDurabilityAndDoesNotRecheckSentinelAfterWrites)
{
    std::uint16_t id = 0xfffe;
    std::uint8_t durability = 0xff;
    std::uint8_t count = 2;
    std::array<std::uint8_t, 10> bytes{0x90, 0x90, 0, 0, 0, 0, 0, 0, 1, 7};
    OwnItemOptionsView options{id, durability, count, bytes};
    // Primera suma produce ffff; el sentinel sólo se consulta antes del bucle.
    AddOwnItemOptionValues(std::as_writable_bytes(std::span(&id, 1)).first<2>(), 0x90, options);
    EXPECT_EQ(id, 6);
    std::array<std::uint8_t, 2> destination{0xff, 0};
    OwnItemOptionsView disabledAfterFirst{id, destination[0], count, bytes};
    AddOwnItemOptionValues(std::as_writable_bytes(std::span(destination)), 0x90, disabledAfterFirst);
    EXPECT_EQ(destination[0], 0);
    EXPECT_EQ(destination[1], 1); // primera suma apaga durability; segunda no suma 7.
    id = 0x7fff;
    ApplyOwnItemOptionPercent(std::as_writable_bytes(std::span(&id, 1)).first<2>(), 0x90, options, 100);
    EXPECT_EQ(id, 0xfffc); // 32767 -> 65534 -> 65532, wrap en cada store.
}
} // namespace
