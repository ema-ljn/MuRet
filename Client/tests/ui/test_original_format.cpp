#include "Crt/OriginalFormat.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The CRT's sprintf (0x69fd9b) and wsprintfA ([0x6b851c]) with the arguments the original passes, for the Text.bmd
// texts the event packets use as a format.
namespace
{
using Client::Crt::FormatArgument;
using Client::Crt::FormatFunction;
using Client::Crt::FormatOriginal;
using Client::Crt::TextArgument;

std::string Formatted(FormatFunction function, std::string_view format, const std::vector<FormatArgument>& arguments)
{
    const auto formatted = FormatOriginal(function, format, arguments, "test");
    EXPECT_TRUE(formatted.HasValue()) << formatted.Error();
    return formatted.HasValue() ? formatted.Value() : std::string{};
}

// Chaos Castle's text 828 with 1147: the name by pointer and the seconds by value.
TEST(OriginalFormatTest, ATextGoesByPointerAndANumberByValue)
{
    const std::vector<FormatArgument> arguments{TextArgument{.index = 1147, .text = "Chaos Castle"}, 18};
    EXPECT_EQ(Formatted(FormatFunction::Wsprintf, "%s abrira (en %d segundos)", arguments),
              "Chaos Castle abrira (en 18 segundos)");
    EXPECT_EQ(Formatted(FormatFunction::Sprintf, "%s abrira (en %d segundos)", arguments),
              "Chaos Castle abrira (en 18 segundos)");
}

// 0x69fd9b: "% d" puts a space before an unsigned number and "% s" changes nothing.
TEST(OriginalFormatTest, SprintfTakesTheSpaceFlag)
{
    const std::vector<FormatArgument> arguments{TextArgument{.index = 1146, .text = "Blood Castle"}, 10};
    EXPECT_EQ(Formatted(FormatFunction::Sprintf, "% s cierra en% d", arguments), "Blood Castle cierra en 10");
    EXPECT_EQ(Formatted(FormatFunction::Sprintf, "% d", {-3}), "-3");
}

// P-C290 (inferred): wsprintfA has no ' ' flag: it copies the space, takes no argument and the letter comes out as
// text. "% s ... (en% d segundos)" of texts 821..825 is left without the name and the seconds.
TEST(OriginalFormatTest, WsprintfCopiesTheSpaceAndTakesNoArgument)
{
    const std::vector<FormatArgument> arguments{TextArgument{.index = 1146, .text = "Blood Castle"}, 30};
    EXPECT_EQ(Formatted(FormatFunction::Wsprintf, "% s invasion (en% d segundos) ", arguments),
              " s invasion (en d segundos) ");
}

// Q-C1321: a "%d" that takes a text prints its address, 0x76bd1dc + index · 300 (853 with 1146 in the 9A).
TEST(OriginalFormatTest, ANumberConversionOfATextPrintsItsAddress)
{
    const std::vector<FormatArgument> arguments{TextArgument{.index = 1146, .text = "Blood Castle"}, 10};
    const std::uint32_t address = Client::Crt::TextTableAddress + 1146u * Client::Crt::TextRecordBytes;
    EXPECT_EQ(Formatted(FormatFunction::Sprintf, "%d y %d", arguments), std::to_string(address) + " y 10");
    EXPECT_EQ(Formatted(FormatFunction::Sprintf, "%X", arguments), "77110D4");
    EXPECT_EQ(address, 0x077110d4u);
}

TEST(OriginalFormatTest, KeepsWidthPrecisionAndFlags)
{
    EXPECT_EQ(Formatted(FormatFunction::Sprintf, "[%5d][%-4d][%03d][%.3d][%#x][%%]", {42, 7, 5, 9, 255}),
              "[   42][7   ][005][009][0xff][%]");
    EXPECT_EQ(Formatted(FormatFunction::Wsprintf, "[%.2s][%c]", {TextArgument{.index = 0, .text = "abc"}, 0x41}),
              "[ab][A]");
}

// Where the original would read the stack or a pointer that is not one, this stops with an error.
TEST(OriginalFormatTest, StopsWhereTheOriginalReadsGarbage)
{
    const auto missing = FormatOriginal(FormatFunction::Sprintf, "%d %d", std::vector<FormatArgument>{1}, "aquí");
    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("aquí"), std::string::npos) << missing.Error();
    const auto pointer = FormatOriginal(FormatFunction::Sprintf, "%s", std::vector<FormatArgument>{1}, "aquí");
    ASSERT_FALSE(pointer.HasValue());
    const auto unknown = FormatOriginal(FormatFunction::Sprintf, "%f", std::vector<FormatArgument>{1}, "aquí");
    ASSERT_FALSE(unknown.HasValue());
    EXPECT_EQ(Formatted(FormatFunction::Wsprintf, "%f", {}), "f");
}
} // namespace
