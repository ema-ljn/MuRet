#include "Data/Text/MoveRequirements.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// movereq.bmd real (Data\Local, 0x448440 desde 0x61ccb0): los 28 destinos, el primero, el de la torre con el nombre
// de +4 distinto del de +0x24, el último, y los 640 bytes que sobran.
namespace
{
using Client::Data::Text::LoadMoveRequirements;
using Client::Data::Text::MoveRecordSize;
using Client::Data::Text::MoveRequirement;
using Client::Tests::ReadFileBytes;
using GoldenMoveRequirements = Client::Tests::GoldenDataTest;

std::string_view SearchNameOf(const MoveRequirement& move)
{
    const std::string_view name(move.searchName.data(), move.searchName.size());
    return name.substr(0, name.find('\0'));
}

TEST_F(GoldenMoveRequirements, TheRealFileHas28Destinations)
{
    const auto path = DataFile("Local\\movereq.bmd");
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());

    const auto loaded = LoadMoveRequirements(std::as_bytes(std::span(*file)));

    ASSERT_TRUE(loaded.HasValue());
    const std::vector<MoveRequirement>& moves = loaded.Value();
    ASSERT_EQ(moves.size(), 28U);
    EXPECT_EQ(file->size() - 4 - moves.size() * MoveRecordSize, 640U);
    for (std::size_t i = 0; i < moves.size(); ++i)
        EXPECT_EQ(moves[i].index, static_cast<std::int32_t>(i + 1));
    EXPECT_EQ(moves[0].name, "Arena");
    EXPECT_EQ(moves[0].level, 50);
    EXPECT_EQ(moves[0].zen, 2000U);
    EXPECT_EQ(moves[0].gate, 200);
    // El 14: lo que dibuja y dice la ventana (+4) no es lo que busca 0x4497c0 (+0x24).
    EXPECT_EQ(moves[13].name, "Losttower");
    EXPECT_EQ(SearchNameOf(moves[13]), "LostTower");
    EXPECT_EQ(moves[13].level, 90);
    EXPECT_EQ(moves[27].name, "Crywolf Fortress2");
    EXPECT_EQ(moves[27].level, 300);
    EXPECT_EQ(moves[27].zen, 10000U);
    EXPECT_EQ(moves[27].gate, 111);
    // Los niveles suben de a tramos; el más largo de los nombres tiene 17 letras.
    EXPECT_TRUE(std::ranges::is_sorted(moves, {}, &MoveRequirement::level));
    const std::size_t longest =
        std::ranges::max(moves, {}, [](const MoveRequirement& move) { return move.name.size(); }).name.size();
    EXPECT_EQ(longest, 17U);
}
} // namespace
