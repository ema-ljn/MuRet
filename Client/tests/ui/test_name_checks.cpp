#include "FrontEnd/NameChecks.h"

#include "Ui/Notices.h"

#include "support/UiPrinters.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Client::FrontEnd::NoticeOfNewName: the name checks of the create OK (0x63d990), with the lab's FilterName.bmd words
// and texts 457 and 458 of its Text.bmd. The names are those of the contract's emulation, synthetic.
namespace
{
using Client::FrontEnd::ContainsAFilteredWord;
using Client::FrontEnd::ContainsLikeTheOriginal;
using Client::FrontEnd::HasForbiddenSymbol;
using Client::FrontEnd::IsAShopTitle;
using Client::FrontEnd::IsFilteredName;
using Client::FrontEnd::NameFilters;
using Client::FrontEnd::NoticeOfNewName;
using Client::Ui::ForbiddenNameNotice;
using Client::Ui::ShortNameNotice;
using Client::Ui::SymbolNameNotice;

const std::vector<std::string> LabFilterName{"ADMIN", "admin", "GM", "gm", "Gm", "gM"};

NameFilters LabFilters()
{
    return NameFilters{.filterName = LabFilterName, .filter = {}, .operation = "Operacion ", .server = "Mu Baires 3"};
}

// 0x576960: the substring, case-sensitive; an empty word is in any text, and one longer than the text, in none.
TEST(NameChecksTest, TheSearchIsASubstring)
{
    EXPECT_TRUE(ContainsLikeTheOriginal("xGMx", "GM"));
    EXPECT_TRUE(ContainsLikeTheOriginal("GM", "GM"));
    EXPECT_FALSE(ContainsLikeTheOriginal("xGmx", "GM"));
    EXPECT_FALSE(ContainsLikeTheOriginal("G", "GM"));
    EXPECT_TRUE(ContainsLikeTheOriginal("abc", ""));
    EXPECT_TRUE(ContainsLikeTheOriginal("", ""));
}

// The emulation's table (contract § 3.8): under 4 bytes, 0x79; a filtered word, 0x72; a bad byte at an even place,
// 0x7a; otherwise, it is sent.
TEST(NameChecksTest, TheLabNamesGiveTheEmulatedNotices)
{
    const NameFilters filters = LabFilters();
    EXPECT_EQ(NoticeOfNewName("abc", filters), std::optional{ShortNameNotice});
    for (const std::string_view sent : {std::string_view{"abcd"}, std::string_view{"abcdefghij"},
                                        std::string_view{"Ab!d"}, std::string_view{"!@#$"}, std::string_view{"abc\x7f"},
                                        std::string_view{"a\x7f"
                                                         "bc"},
                                        std::string_view{"\xf1"
                                                         "and"}})
    {
        EXPECT_EQ(NoticeOfNewName(sent, filters), std::nullopt) << sent;
    }
    for (const std::string_view filtered :
         {std::string_view{"Sigma1"}, std::string_view{"xGMx"}, std::string_view{"ab cd"}, std::string_view{"a.bcd"},
          std::string_view{"Webzen1"}, std::string_view{"webzenX"}, std::string_view{"Mu Baires 3"}})
    {
        EXPECT_EQ(NoticeOfNewName(filtered, filters), std::optional{ForbiddenNameNotice}) << filtered;
    }
    for (const std::string_view symbol : {std::string_view{"\x7f"
                                                           "abc"},
                                          std::string_view{"\xa1"
                                                           "and"},
                                          std::string_view{"Mx\x01"
                                                           "b"}})
    {
        EXPECT_EQ(NoticeOfNewName(symbol, filters), std::optional{SymbolNameNotice}) << symbol;
    }
}

// 0x622c70: FilterName.bmd against the name without spaces; the fixed ones, against the name as is: the space, the
// dot, the three of GB2312 and the four of Webzen, and texts 457 and 458. That the first goes without spaces does not
// show: a name with a space is already filtered by the fixed " ".
TEST(NameChecksTest, TheWordsOfTheFilesAndTheFixedOnesAreFiltered)
{
    const NameFilters filters = LabFilters();
    EXPECT_TRUE(IsFilteredName("aG Mb", filters));
    EXPECT_TRUE(IsFilteredName("aWEBZENb", filters));
    EXPECT_TRUE(IsFilteredName("WebZen", filters));
    EXPECT_TRUE(IsFilteredName("a\xa1\xa1"
                               "b",
                               filters));
    EXPECT_TRUE(IsFilteredName("a\xa1\xa4"
                               "b",
                               filters));
    EXPECT_TRUE(IsFilteredName("a\xa1\xad"
                               "b",
                               filters));
    EXPECT_TRUE(IsFilteredName("xOperacion y", filters));
    EXPECT_FALSE(IsFilteredName("Webzeen", filters));
    EXPECT_FALSE(IsFilteredName("Synthetic", filters));
}

// 0x622b40 with 1: Filter.bmd against the name without spaces, unless it starts with '/'.
TEST(NameChecksTest, TheFilterWordsSkipANameThatStartsWithASlash)
{
    const std::vector<std::string> words{"bad"};
    NameFilters filters = LabFilters();
    filters.filter = words;
    EXPECT_TRUE(IsFilteredName("xbadx", filters));
    EXPECT_TRUE(IsFilteredName("xb adx", filters));
    EXPECT_FALSE(IsFilteredName("/bad", filters));
    EXPECT_TRUE(IsFilteredName("x/bad", filters));
}

// 0x622b40 from the chat (0x4b5734): with 1, a command ("/…") is not looked at; with 0 it is. Spaces do not separate.
TEST(NameChecksTest, TheChatFilterLooksAtTheTextWithoutSpaces)
{
    const std::vector<std::string> words{"bad"};
    EXPECT_TRUE(ContainsAFilteredWord("xb a dx", words, true));
    EXPECT_FALSE(ContainsAFilteredWord("/xbadx", words, true));
    EXPECT_TRUE(ContainsAFilteredWord("/xbadx", words, false));
    EXPECT_FALSE(ContainsAFilteredWord("good", words, false));
    EXPECT_FALSE(ContainsAFilteredWord("bad", {}, false));
}

// 0x5ebbd0, the personal shop title: no Filter.bmd word in the title without spaces; two consecutive spaces at the
// start (the count at letter 1) or at the end do not pass, and those in the middle do. Empty, it passes.
TEST(NameChecksTest, TheShopTitleLooksAtTheFilterAndTheSpaces)
{
    const std::vector<std::string> words{"bad"};
    EXPECT_TRUE(IsAShopTitle("Mi puesto", words));
    EXPECT_TRUE(IsAShopTitle("", words));
    EXPECT_TRUE(IsAShopTitle("Mi  puesto", words));
    EXPECT_TRUE(IsAShopTitle(" Mi puesto ", words));
    EXPECT_TRUE(IsAShopTitle("a ", words));
    EXPECT_FALSE(IsAShopTitle("  Mi puesto", words));
    EXPECT_FALSE(IsAShopTitle("Mi puesto  ", words));
    EXPECT_FALSE(IsAShopTitle("  ", words));
    EXPECT_FALSE(IsAShopTitle("b a d", words));
    EXPECT_FALSE(IsAShopTitle("/bad", words));
    // Three spaces from letter 0: at letter 1 the count is already 2.
    EXPECT_FALSE(IsAShopTitle("   x", words));
    // From letter 1, at letter 1 the count is 1: it passes.
    EXPECT_TRUE(IsAShopTitle("x  y", {}));
}

// With text 457 or 458 empty (another Text.bmd), 0x576960 finds them in any name, and none can be created.
TEST(NameChecksTest, AnEmptyTextFiltersEveryName)
{
    NameFilters filters = LabFilters();
    filters.server = "";
    EXPECT_TRUE(IsFilteredName("Synthetic", filters));
}

// 0x447ca0 patched: it looks at bytes 0, 2, 4…, while the one two ahead is not 0. Bad: one below 0x21, from 0x7f to
// 0xaf or above 0xf7.
TEST(NameChecksTest, OnlyTheEvenBytesAreChecked)
{
    EXPECT_FALSE(HasForbiddenSymbol(""));
    EXPECT_FALSE(HasForbiddenSymbol("a\x01"));
    EXPECT_TRUE(HasForbiddenSymbol("\x01"
                                   "a"));
    EXPECT_TRUE(HasForbiddenSymbol("ab\x20"));
    EXPECT_FALSE(HasForbiddenSymbol("ab\x21"));
    EXPECT_FALSE(HasForbiddenSymbol("ab\x7e"));
    EXPECT_TRUE(HasForbiddenSymbol("ab\x7f"));
    EXPECT_TRUE(HasForbiddenSymbol("ab\xaf"));
    EXPECT_FALSE(HasForbiddenSymbol("ab\xb0"));
    EXPECT_FALSE(HasForbiddenSymbol("ab\xf7"));
    EXPECT_TRUE(HasForbiddenSymbol("ab\xf8"));
    EXPECT_TRUE(HasForbiddenSymbol("abcd\xff"));
    EXPECT_FALSE(HasForbiddenSymbol("abc\xff"));
}

// The length goes before the filters: "GM" and "a b" are short (0x79) even if filtered.
TEST(NameChecksTest, TheLengthComesFirst)
{
    const NameFilters filters = LabFilters();
    EXPECT_EQ(NoticeOfNewName("GM", filters), std::optional{ShortNameNotice});
    EXPECT_EQ(NoticeOfNewName("a b", filters), std::optional{ShortNameNotice});
}

// 0x447ca0 reads the window's buffer: bytes from a longer earlier attempt can remain behind the name, and it looks at
// them (Q-C118). With "abcde" over "abcdef g", byte 6 is the space.
TEST(NameChecksTest, TheSymbolsSeeWhatIsLeftInTheBuffer)
{
    const NameFilters filters = LabFilters();
    const std::string buffer("abcde\0 g\0", 9);
    EXPECT_EQ(NoticeOfNewName("abcde", buffer, filters), std::optional{SymbolNameNotice});
    EXPECT_EQ(NoticeOfNewName("abcde", filters), std::nullopt);
}
} // namespace
