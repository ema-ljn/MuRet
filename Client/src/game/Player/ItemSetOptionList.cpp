#include "Player/ItemSetOptionList.h"

#include "Core/Expected.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

// 0x40d690 (151 bytes, equal to 0x40da20), 0x40d730 (185) and 0x40d7f0 (197), checked against the PE c32bbd5f: 06,
// "Los ocho helpers de los stats propios".
namespace Client::Player
{
namespace
{
// sub edi, 0x8c: the selector against each entry's first byte.
constexpr std::uint32_t SelectorBase = 0x8c;
// The divisor of 0x40d730 and 0x40d7f0 (imul by 0x51eb851f and sar 5).
constexpr std::int32_t PercentScale = 100;

// What the entries `take` chose add up to, how many there were and the last one's value.
struct Taken
{
    std::uint32_t sum = 0;
    std::int32_t last = 0;
    std::int32_t count = 0;
};

// The list's length, +0x9348 plus +0x9349, if it fits in the 16 entries.
Core::Expected<std::size_t, std::string> EntryCount(const ItemSetOptionList& list, std::string_view function)
{
    const std::size_t count = std::size_t{list.count9348} + list.count9349;
    if (count > ItemSetOptionCapacity)
    {
        return Core::Unexpected(std::format("{}: la lista de [0x6eed3c] dice {} entradas y en +0x934c caben {}; el "
                                            "original leería las marcas como entradas",
                                            function, count, ItemSetOptionCapacity));
    }
    return count;
}

// The common walk: each unused entry whose first byte is selector − 0x8c and that `wanted` accepts is marked and
// counts. The sum is 32 bits and wraps (add edi, edx).
template <typename Wanted>
Core::Expected<Taken, std::string> TakeEntries(ItemSetOptionList& list, std::uint32_t selector,
                                               std::string_view function, Wanted wanted)
{
    const auto count = EntryCount(list, function);
    if (!count.HasValue())
    {
        return Core::Unexpected(count.Error());
    }
    const std::uint32_t kind = selector - SelectorBase;
    Taken taken;
    for (std::size_t index = 0; index < count.Value(); ++index)
    {
        const auto& entry = list.entries[index];
        if (entry[0] != kind || !wanted(entry) || list.used[index] != 0)
        {
            continue;
        }
        list.used[index] = 1;
        taken.sum += std::bit_cast<std::uint32_t>(list.values[index]);
        taken.last = list.values[index];
        ++taken.count;
    }
    return taken;
}

std::uint16_t Low16(std::uint32_t value)
{
    return static_cast<std::uint16_t>(value);
}

// 32-bit imul: the product's low 32 bits, signed.
std::int32_t Multiply32(std::int32_t left, std::int32_t right)
{
    const auto product = std::bit_cast<std::uint32_t>(left) * std::bit_cast<std::uint32_t>(right);
    return std::bit_cast<std::int32_t>(product);
}
} // namespace

Core::Expected<void, std::string> AddItemSetOptionValues(ItemSetOptionList& list, std::uint16_t& word,
                                                         std::uint32_t selector)
{
    const auto taken = TakeEntries(list, selector, "0x40d690", [](const auto&) { return true; });
    if (!taken.HasValue())
    {
        return Core::Unexpected(taken.Error());
    }
    // test edi, edi / je: with the sum at 0 it does not write, which is the same as adding 0.
    word = Low16(word + taken.Value().sum);
    return {};
}

Core::Expected<void, std::string> ApplyItemSetOptionPercent(ItemSetOptionList& list, std::uint16_t& word,
                                                            std::uint32_t selector)
{
    const auto taken = TakeEntries(list, selector, "0x40d730", [](const auto&) { return true; });
    if (!taken.HasValue())
    {
        return Core::Unexpected(taken.Error());
    }
    const std::int32_t sum = std::bit_cast<std::int32_t>(taken.Value().sum);
    const std::int32_t adjustment = Multiply32(word, sum) / PercentScale;
    word = Low16(word + std::bit_cast<std::uint32_t>(adjustment));
    return {};
}

Core::Expected<void, std::string> AddItemSetOptionPercentOf(ItemSetOptionList& list, std::uint16_t& word,
                                                            std::uint16_t base, std::uint32_t selector)
{
    const auto taken = TakeEntries(list, selector, "0x40d7f0", [](const auto& entry) { return entry[1] != 0; });
    if (!taken.HasValue())
    {
        return Core::Unexpected(taken.Error());
    }
    const std::int32_t share = Multiply32(base, taken.Value().last) / PercentScale;
    word = Low16(word + std::bit_cast<std::uint32_t>(Multiply32(share, taken.Value().count)));
    return {};
}

Core::Expected<void, std::string> AddSkillSetOptionValues(ItemSetOptionList& list, std::int32_t& value,
                                                          std::uint32_t selector)
{
    const auto count = EntryCount(list, "0x40d8c0");
    if (!count.HasValue())
    {
        return Core::Unexpected(count.Error());
    }
    // 0x40d8ee..0x40d919: cmp 0x18 / jc, the byte minus 0x1d against selector & 0xff, the second byte and the mark.
    constexpr std::uint32_t LowestKind = 0x17;
    constexpr std::uint32_t KindBase = 0x1d;
    const std::uint32_t wanted = selector & 0xffu;
    std::uint32_t sum = 0;
    for (std::size_t index = 0; index < count.Value(); ++index)
    {
        const auto& entry = list.entries[index];
        const std::uint32_t kind = entry[0];
        if (kind <= LowestKind || kind - KindBase != wanted || entry[1] == 0 || list.used[index] != 0)
        {
            continue;
        }
        sum += std::bit_cast<std::uint32_t>(list.values[index]);
        list.used[index] = 1;
    }
    if (sum != 0)
    {
        value = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(value) + sum);
    }
    return {};
}
} // namespace Client::Player
