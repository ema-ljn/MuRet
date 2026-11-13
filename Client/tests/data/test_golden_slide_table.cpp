#include "Data/Text/SlideTable.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Slide.bmd real (Data\Local, 0x61ed18): el timer cada 60 segundos, la velocidad 2,5 y tres tramos con textos. Mira
// la forma, no los textos.
namespace
{
using Client::Data::Text::LoadSlideTable;
using Client::Tests::ReadFileBytes;
using GoldenSlideTable = Client::Tests::GoldenDataTest;

TEST_F(GoldenSlideTable, HasThreeGroupsWithTexts)
{
    const auto path = DataFile("Local\\Slide.bmd");
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());

    const auto table = LoadSlideTable(std::as_bytes(std::span(*file)));

    ASSERT_TRUE(table.HasValue());
    EXPECT_EQ(table.Value().periodSeconds, 60);
    EXPECT_EQ(table.Value().speed, 2.5f);
    const std::vector<std::pair<int, int>> groups{{15, 28}, {30, 20}, {50, 19}, {0, -1}, {0, -1}};
    for (std::size_t group = 0; group < groups.size(); ++group)
    {
        const auto& read = table.Value().groups[group];
        EXPECT_EQ(read.maxLevel, groups[group].first) << group;
        EXPECT_EQ(read.count, groups[group].second) << group;
        EXPECT_EQ(read.texts.size(), static_cast<std::size_t>(std::max(groups[group].second, 0))) << group;
        for (const std::string& text : read.texts)
            EXPECT_FALSE(text.empty()) << group;
    }
}
} // namespace
