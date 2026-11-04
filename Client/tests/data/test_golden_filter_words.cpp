#include "Data/Text/FilterWords.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// FilterName.bmd y Filter.bmd reales (Data\Local, 0x61ccb0): sus sumas dan, el primero trae las seis
// palabras que no puede tener un nombre y el segundo empieza vacío.
namespace
{
using Client::Data::Text::FilterKey;
using Client::Data::Text::FilterNameKey;
using Client::Data::Text::LoadFilterWords;
using Client::Tests::ReadFileBytes;
using GoldenFilterWords = Client::Tests::GoldenDataTest;

TEST_F(GoldenFilterWords, FilterNameHasSixWords)
{
    const auto path = DataFile("Local\\FilterName.bmd");
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());

    const auto words = LoadFilterWords(std::as_bytes(std::span(*file)), FilterNameKey);

    ASSERT_TRUE(words.HasValue());
    EXPECT_EQ(words.Value(), (std::vector<std::string>{"ADMIN", "admin", "GM", "gm", "Gm", "gM"}));
}

TEST_F(GoldenFilterWords, FilterIsEmpty)
{
    const auto path = DataFile("Local\\Filter.bmd");
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());

    const auto words = LoadFilterWords(std::as_bytes(std::span(*file)), FilterKey);

    ASSERT_TRUE(words.HasValue());
    EXPECT_TRUE(words.Value().empty());
}
} // namespace
