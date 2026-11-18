#include "UI/FriendModel.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// La lista de amigos de [0x5763518] (CL9 hito 9; 06 § HUD y ventanas, "Los amigos"). Los nombres son inventados.
namespace
{
using Client::UI::AddAFriend;
using Client::UI::ByNameSort;
using Client::UI::ByStateSort;
using Client::UI::ClearTheFriends;
using Client::UI::FriendModel;
using Client::UI::NameComesFirst;
using Client::UI::RemoveAFriend;
using Client::UI::SameSort;
using Client::UI::SetAFriend;
using Client::UI::SetAllTheFriends;
using Client::UI::SortTheFriends;

std::vector<std::string> NamesOf(const FriendModel& model)
{
    std::vector<std::string> names;
    for (const auto& entry : model.entries)
        names.push_back(entry.name);
    return names;
}

std::vector<std::uint8_t> StatesOf(const FriendModel& model)
{
    std::vector<std::uint8_t> states;
    for (const auto& entry : model.entries)
        states.push_back(entry.state);
    return states;
}

// 0x49b220: al final, con el nombre cortado en 10.
TEST(FriendModelTest, AddingGoesAtTheEndWithTheNameCutAtTen)
{
    FriendModel model;
    AddAFriend(model, "Zorzal", 0, 2);
    AddAFriend(model, "Golondrinas", 0, 0xff);
    EXPECT_EQ(NamesOf(model), (std::vector<std::string>{"Zorzal", "Golondrina"}));
    EXPECT_EQ(StatesOf(model), (std::vector<std::uint8_t>{2, 0xff}));
    EXPECT_EQ(model.entries[0].selected, 0);
    ClearTheFriends(model);
    EXPECT_TRUE(model.entries.empty());
}

// 0x49b690 y 0x49c010: el primero con el nombre (strncmp de 10); sin él, nada.
TEST(FriendModelTest, RemovingAndSettingTouchTheFirstWithTheName)
{
    FriendModel model;
    AddAFriend(model, "Hornero", 0, 1);
    AddAFriend(model, "Tordo", 0, 2);
    AddAFriend(model, "Hornero", 0, 3);
    SetAFriend(model, "Hornero", 0, 7);
    EXPECT_EQ(StatesOf(model), (std::vector<std::uint8_t>{7, 2, 3}));
    RemoveAFriend(model, "Hornero");
    EXPECT_EQ(NamesOf(model), (std::vector<std::string>{"Tordo", "Hornero"}));
    EXPECT_EQ(StatesOf(model), (std::vector<std::uint8_t>{2, 3}));
    RemoveAFriend(model, "Carancho");
    SetAFriend(model, "Carancho", 0, 9);
    EXPECT_EQ(StatesOf(model), (std::vector<std::uint8_t>{2, 3}));
    SetAllTheFriends(model, 0, 0xfc);
    EXPECT_EQ(StatesOf(model), (std::vector<std::uint8_t>{0xfc, 0xfc}));
}

// 0x49c190: de la Z a la A, con los char con signo (lo de Latin-1 al final) y sin decidir entre uno que empieza como
// el otro (Q-C881).
TEST(FriendModelTest, TheNameOrderGoesBackwardsWithSignedChars)
{
    FriendModel model;
    for (const char* name : {"Benteveo", "Zorzal",
                             "\xd1"
                             "andu",
                             "Hornero", "Tordo", "Tordos", "Calandria"})
        AddAFriend(model, name, 0, 0);
    SortTheFriends(model, ByNameSort);
    EXPECT_EQ(model.sortMode, ByNameSort);
    EXPECT_EQ(NamesOf(model), (std::vector<std::string>{"Zorzal", "Tordo", "Tordos", "Hornero", "Calandria", "Benteveo",
                                                        "\xd1"
                                                        "andu"}));
    const Client::UI::FriendEntry tordo{.selected = 0, .name = "Tordo", .flag = 0, .state = 0};
    const Client::UI::FriendEntry tordos{.selected = 0, .name = "Tordos", .flag = 0, .state = 0};
    EXPECT_FALSE(NameComesFirst(tordo, tordos));
    EXPECT_FALSE(NameComesFirst(tordos, tordo));
}

// 0x49c240: el estado más alto primero; −1 ordena con el que quedó.
TEST(FriendModelTest, TheStateOrderPutsTheHighestFirstAndMinusOneKeepsTheMode)
{
    FriendModel model;
    AddAFriend(model, "Benteveo", 0, 0);
    AddAFriend(model, "Zorzal", 0, 0xfc);
    AddAFriend(model, "Hornero", 0, 3);
    SortTheFriends(model, ByStateSort);
    EXPECT_EQ(model.sortMode, ByStateSort);
    EXPECT_EQ(NamesOf(model), (std::vector<std::string>{"Zorzal", "Hornero", "Benteveo"}));
    AddAFriend(model, "Tordo", 0, 0xff);
    SortTheFriends(model, SameSort);
    EXPECT_EQ(model.sortMode, ByStateSort);
    EXPECT_EQ(NamesOf(model), (std::vector<std::string>{"Tordo", "Zorzal", "Hornero", "Benteveo"}));
}

// Con más de 16, el _Sort de VC6 deja los iguales en su propio orden, no en el de llegada (Q-C881). El orden esperado
// sale del <algorithm> de VC6 escrito aparte, en Python.
TEST(FriendModelTest, MoreThanSixteenLeaveTheEqualsWhereTheVc6SortLeavesThem)
{
    constexpr std::array<const char*, 20> Names{"Aguila",  "Benteveo",  "Calandria",  "Carancho", "Chimango",
                                                "Cotorra", "Gorrion",   "Golondrina", "Hornero",  "Jilguero",
                                                "Lechuza", "Martineta", "Nandu",      "Pirincho", "Ratona",
                                                "Tero",    "Tordo",     "Torcaza",    "Urraca",   "Zorzal"};
    constexpr std::array<std::uint8_t, 20> States{3, 1, 2, 3, 0, 2, 1, 3, 2, 0, 1, 2, 3, 1, 0, 2, 3, 1, 2, 0};
    FriendModel model;
    for (std::size_t at = 0; at < Names.size(); ++at)
        AddAFriend(model, Names[at], 0, States[at]);
    FriendModel byName = model;
    SortTheFriends(model, ByStateSort);
    EXPECT_EQ(NamesOf(model), (std::vector<std::string>{"Aguila",    "Carancho",  "Tordo",    "Golondrina", "Nandu",
                                                        "Urraca",    "Calandria", "Cotorra",  "Hornero",    "Tero",
                                                        "Martineta", "Torcaza",   "Pirincho", "Lechuza",    "Gorrion",
                                                        "Benteveo",  "Ratona",    "Jilguero", "Chimango",   "Zorzal"}));
    SortTheFriends(byName, ByNameSort);
    EXPECT_EQ(NamesOf(byName), (std::vector<std::string>{"Zorzal",   "Urraca",   "Tordo",     "Torcaza",    "Tero",
                                                         "Ratona",   "Pirincho", "Nandu",     "Martineta",  "Lechuza",
                                                         "Jilguero", "Hornero",  "Gorrion",   "Golondrina", "Cotorra",
                                                         "Chimango", "Carancho", "Calandria", "Benteveo",   "Aguila"}));
}
} // namespace
