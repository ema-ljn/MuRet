#include "Data/Text/NpcNames.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// NpcName(%s).txt real (Data\Local, 0x54e7f0 desde 0x61ed05): los 287 registros, sin "end", el primero y el último,
// los tres nombres que no entran en los 25 bytes de +0x1d9..+0x1f1 del personaje, y que Kor y Chs dan lo mismo que Eng.
namespace
{
using Client::Data::Text::LoadNpcNames;
using Client::Data::Text::NpcNameRecord;
using Client::Tests::ReadFileBytes;
using GoldenNpcNames = Client::Tests::GoldenDataTest;

std::string_view NameOf(const NpcNameRecord& record)
{
    const std::string_view name(record.name.data(), record.name.size());
    return name.substr(0, name.find('\0'));
}

std::optional<std::vector<NpcNameRecord>> Load(const std::filesystem::path& path)
{
    const std::optional<std::vector<char>> file = ReadFileBytes(path);
    if (!file)
        return std::nullopt;
    auto loaded = LoadNpcNames(std::as_bytes(std::span(*file)));
    if (!loaded.HasValue())
        return std::nullopt;
    return std::move(loaded).Value();
}

TEST_F(GoldenNpcNames, TheEnglishFileHas287Names)
{
    const auto path = DataFile("Local\\NpcName(Eng).txt");
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<NpcNameRecord>> names = Load(*path);
    ASSERT_TRUE(names.has_value());

    ASSERT_EQ(names->size(), 287u);
    EXPECT_EQ(names->front().type, 84);
    EXPECT_EQ(NameOf(names->front()), "King Orc");
    EXPECT_EQ(names->back().type, 338);
    EXPECT_EQ(NameOf(names->back()), "Relics of KUNDUN");
    // Ningún tipo se repite: 0x54ebb0 devuelve el primero.
    std::set<std::uint16_t> types;
    for (const NpcNameRecord& record : *names)
        types.insert(record.type);
    EXPECT_EQ(types.size(), names->size());
    EXPECT_EQ(*types.rbegin(), 338);
    // Los que pasan los 24 caracteres (el strcpy de 0x5140c0 pisaría +0x1f2 y, con el 231, la clave).
    std::vector<std::uint16_t> longNames;
    for (const NpcNameRecord& record : *names)
    {
        if (NameOf(record).size() > 24)
            longNames.push_back(record.type);
    }
    EXPECT_EQ(longNames, (std::vector<std::uint16_t>{76, 231, 246}));
    const auto longest = std::ranges::max_element(*names, {}, [](const NpcNameRecord& r) { return NameOf(r).size(); });
    EXPECT_EQ(longest->type, 231);
    EXPECT_EQ(NameOf(*longest).size(), 34u);
}

TEST_F(GoldenNpcNames, KoreanAndChineseGiveTheSameTable)
{
    const auto english = DataFile("Local\\NpcName(Eng).txt");
    const auto korean = DataFile("Local\\NpcName(Kor).txt");
    const auto chinese = DataFile("Local\\NpcName(Chs).txt");
    ASSERT_TRUE(english.has_value());
    ASSERT_TRUE(korean.has_value());
    ASSERT_TRUE(chinese.has_value());
    const auto expected = Load(*english);
    ASSERT_TRUE(expected.has_value());
    EXPECT_EQ(Load(*korean), expected);
    EXPECT_EQ(Load(*chinese), expected);
}
} // namespace
