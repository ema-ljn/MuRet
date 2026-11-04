#include "UI/NameChecks.h"

#include "UI/Notices.h"

#include "support/UiPrinters.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Client::UI::NoticeOfNewName: los chequeos del nombre del OK de crear (0x63d990), con las palabras
// de FilterName.bmd del laboratorio y los textos 457 y 458 de su Text.bmd. Los nombres son los de la
// emulación del contrato, sintéticos.
namespace
{
using Client::UI::ContainsLikeTheOriginal;
using Client::UI::ForbiddenNameNotice;
using Client::UI::HasForbiddenSymbol;
using Client::UI::IsFilteredName;
using Client::UI::NameFilters;
using Client::UI::NoticeOfNewName;
using Client::UI::ShortNameNotice;
using Client::UI::SymbolNameNotice;

const std::vector<std::string> LabFilterName{"ADMIN", "admin", "GM", "gm", "Gm", "gM"};

NameFilters LabFilters()
{
    return NameFilters{.filterName = LabFilterName, .filter = {}, .operation = "Operacion ", .server = "Mu Baires 3"};
}

// 0x576960: la subcadena, distinguiendo mayúsculas; una palabra vacía está en cualquier texto, y
// una más larga que el texto, en ninguno.
TEST(NameChecksTest, TheSearchIsASubstring)
{
    EXPECT_TRUE(ContainsLikeTheOriginal("xGMx", "GM"));
    EXPECT_TRUE(ContainsLikeTheOriginal("GM", "GM"));
    EXPECT_FALSE(ContainsLikeTheOriginal("xGmx", "GM"));
    EXPECT_FALSE(ContainsLikeTheOriginal("G", "GM"));
    EXPECT_TRUE(ContainsLikeTheOriginal("abc", ""));
    EXPECT_TRUE(ContainsLikeTheOriginal("", ""));
}

// La tabla de la emulación (contrato § 3.8): menos de 4 bytes, 0x79; una palabra filtrada, 0x72;
// un byte malo en un lugar par, 0x7a; si no, se manda.
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

// 0x622c70: FilterName.bmd contra el nombre sin espacios; las fijas, contra el nombre tal cual: el
// espacio, el punto, los tres de GB2312 y las cuatro de Webzen, y los textos 457 y 458. Que la
// primera vaya sin espacios no se ve: un nombre con un espacio ya lo filtra la fija " ".
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

// 0x622b40 con 1: Filter.bmd contra el nombre sin espacios, salvo si empieza con '/'.
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

// Con el texto 457 o el 458 vacíos (otro Text.bmd), 0x576960 los encuentra en cualquier nombre, y
// ninguno se puede crear.
TEST(NameChecksTest, AnEmptyTextFiltersEveryName)
{
    NameFilters filters = LabFilters();
    filters.server = "";
    EXPECT_TRUE(IsFilteredName("Synthetic", filters));
}

// 0x447ca0 parchado: mira los bytes 0, 2, 4…, mientras el de dos más adelante no sea 0. Malo, uno
// menor que 0x21, de 0x7f a 0xaf o mayor que 0xf7.
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
} // namespace
