#include "UI/Emotes.h"

#include "Data/Text/TextTable.h"

#include "support/ChatTexts.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// UI::FindTheEmote: la búsqueda de 0x576eb0 (06 § El chat, "El emote"). La tabla esperada sale del ASM
// (0x576ede..0x577f88, los 22 casos por su bloque); los textos de Text.bmd son los inventados de ChatTexts ("#e270" y
// así), que no se contienen entre ellos.
namespace
{
using Client::UI::EmoteCase;
using Client::UI::FindTheEmote;

const Client::Data::Text::TextTable& Texts()
{
    static const Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable(Client::Tests::ChatTexts());
    return texts;
}

// Un disparador: un texto de Text.bmd (por su índice) o un literal del binario (por su dirección), con su caso.
struct Trigger
{
    std::string name;
    std::string said;
    EmoteCase expected;
};

// Los 95, en el orden en que 0x576eb0 los prueba.
std::vector<Trigger> OriginalOrder()
{
    std::vector<Trigger> rows;
    const auto texts = [&rows](std::size_t first, std::size_t last, EmoteCase expected)
    {
        for (std::size_t index = first; index <= last; ++index)
            rows.push_back({"Text" + std::to_string(index), "#e" + std::to_string(index), expected});
    };
    const auto literal = [&rows](std::string_view address, std::string_view bytes, EmoteCase expected)
    { rows.push_back({"Literal" + std::string(address), std::string(bytes), expected}); };
    texts(270, 277, {0x99, 0x83});                         // 0x57ca85
    texts(278, 280, {0x9b, 0x84});                         // 0x57c686
    texts(281, 286, {0x9d, 0x85});                         // 0x57c2a2
    texts(287, 290, {0xa3, 0x86});                         // 0x57bea3
    texts(292, 295, {0xa1, 0x87});                         // 0x57babf
    texts(296, 302, {0xa5, 0x88});                         // 0x57b6c0
    literal("6c90e0", ";", {0xa9, 0x8b});                  // 0x57b2dc
    texts(303, 305, {0xa9, 0x8b});                         // 0x57b2dc
    literal("6c90e4", "\xa4\xd0.\xa4\xd0", {0xa7, 0x89});  // 0x57aef9
    literal("6c90ec", "\xa4\xcc.\xa4\xcc", {0xa7, 0x89});  // 0x57aef9
    literal("6c90f4", "T_T", {0xa7, 0x89});                // 0x57aef9
    texts(306, 309, {0xa7, 0x89});                         // 0x57aef9
    literal("6c90f8", "\xa4\xd1.\xa4\xd1", {0xab, 0x8c});  // 0x57ab15
    literal("6c9100", "\xa4\xd1.,\xa4\xd1", {0xab, 0x8c}); // 0x57ab15
    literal("6c9108", "\xa4\xd1,.\xa4\xd1", {0xab, 0x8c}); // 0x57ab15
    literal("6c9110", "-.-", {0xab, 0x8c});                // 0x57ab15
    literal("6c9114", "-_-", {0xab, 0x8c});                // 0x57ab15
    texts(310, 311, {0xab, 0x8c});                         // 0x57ab15
    literal("6c9118", "^^", {0xaf, 0x8e});                 // 0x57a731
    literal("6c911c", "^.^", {0xaf, 0x8e});                // 0x57a731
    literal("6c9120", "^_^", {0xaf, 0x8e});                // 0x57a731
    texts(312, 316, {0xaf, 0x8e});                         // 0x57a731
    texts(318, 321, {0x9f, 0x8a});                         // 0x57a333
    texts(322, 325, {0xad, 0x8d});                         // 0x579f35
    texts(326, 329, {0xb1, 0x8f});                         // 0x579b36
    texts(330, 334, {0xb3, 0x90});                         // 0x579737
    texts(335, 338, {0xb5, 0x91});                         // 0x579353
    texts(339, 341, {0xb7, 0x92});                         // 0x578f54
    texts(342, 343, {0xb8, 0x93});                         // 0x578b70
    literal("6c9124", "/\xa4\xd1", {0xb8, 0x93});          // 0x578b70
    literal("6c9128", "\xa4\xd1^", {0xb8, 0x93});          // 0x578b70
    texts(344, 347, {0xc0, 0x94});                         // 0x578771
    texts(783, 783, {0xbc, 0x98});                         // 0x57838d
    literal("6c912c", "hustle", {0xbc, 0x98});             // 0x57838d
    texts(291, 291, {0xbd, 0x99});                         // 0x577796
    texts(317, 317, {0xbf, 0x9b});                         // 0x577b92
    texts(348, 348, {0xbe, 0x9a});                         // 0x577f8e
    return rows;
}

class EveryTrigger : public ::testing::TestWithParam<Trigger>
{
};

// Cada uno, solo en una frase, da su caso.
TEST_P(EveryTrigger, FindsItsCase)
{
    const Trigger& trigger = GetParam();
    const auto found = FindTheEmote(Texts(), "dijo " + trigger.said + " y nada mas");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, trigger.expected);
}

INSTANTIATE_TEST_SUITE_P(OriginalOrder, EveryTrigger, ::testing::ValuesIn(OriginalOrder()),
                         [](const ::testing::TestParamInfo<Trigger>& row) { return row.param.name; });

TEST(EmotesTest, TheTableHasTheNinetyFiveTriggers)
{
    EXPECT_EQ(OriginalOrder().size(), Client::UI::EmoteTriggerCount);
}

// El primero en el orden gana, aunque esté más adelante en lo dicho o tenga un índice mayor.
TEST(EmotesTest, TheEarlierTriggerWins)
{
    const std::vector<Trigger> rows = OriginalOrder();
    for (std::size_t first = 0; first < rows.size(); ++first)
    {
        for (std::size_t later = first + 1; later < rows.size(); ++later)
        {
            SCOPED_TRACE(rows[first].name + " antes que " + rows[later].name);
            const auto found = FindTheEmote(Texts(), rows[later].said + " " + rows[first].said);
            ASSERT_TRUE(found.has_value());
            EXPECT_EQ(*found, rows[first].expected);
        }
    }
}

// 0x577780, 0x577b7c y 0x577f78: el 291, el 317 y el 348 se prueban después de todos, también del 783 y "hustle".
TEST(EmotesTest, TheLonelyTextsGoLast)
{
    EXPECT_EQ(FindTheEmote(Texts(), "#e291 #e347"), (EmoteCase{0xc0, 0x94}));
    EXPECT_EQ(FindTheEmote(Texts(), "#e317 hustle"), (EmoteCase{0xbc, 0x98}));
    EXPECT_EQ(FindTheEmote(Texts(), "#e348 #e783"), (EmoteCase{0xbc, 0x98}));
    EXPECT_EQ(FindTheEmote(Texts(), "#e348 #e317 #e291"), (EmoteCase{0xbd, 0x99}));
    EXPECT_EQ(FindTheEmote(Texts(), "#e348 #e317"), (EmoteCase{0xbf, 0x9b}));
}

// El ';' y el '^^' de 0x6c90e0 y 0x6c9118, que el corte de la CL8 no miraba.
TEST(EmotesTest, TheSemicolonAndTheSmileAreEmotes)
{
    EXPECT_EQ(FindTheEmote(Texts(), "bueno;"), (EmoteCase{0xa9, 0x8b}));
    EXPECT_EQ(FindTheEmote(Texts(), "hola ^^"), (EmoteCase{0xaf, 0x8e}));
    EXPECT_EQ(FindTheEmote(Texts(), "hola ^^;"), (EmoteCase{0xa9, 0x8b}));
}

TEST(EmotesTest, NothingFoundIsEmpty)
{
    EXPECT_EQ(FindTheEmote(Texts(), "hola, que tal"), std::nullopt);
    EXPECT_EQ(FindTheEmote(Texts(), ""), std::nullopt);
    // Medio literal no alcanza, y las mayúsculas cuentan.
    EXPECT_EQ(FindTheEmote(Texts(), "^ ^ T-T HUSTLE \xa4\xd0"), std::nullopt);
}

// Un texto que Text.bmd no tiene queda vacío, y 0x576960 encuentra el vacío en cualquier mensaje (Q-C117).
TEST(EmotesTest, AMissingTextIsFoundInAnyMessage)
{
    std::map<std::size_t, std::string> texts = Client::Tests::ChatTexts();
    texts.erase(287);
    const auto table = Client::Tests::MakeTextTable(texts);
    EXPECT_EQ(FindTheEmote(table, "hola"), (EmoteCase{0xa3, 0x86}));
    EXPECT_EQ(FindTheEmote(table, "#e270"), (EmoteCase{0x99, 0x83}));
}
} // namespace
