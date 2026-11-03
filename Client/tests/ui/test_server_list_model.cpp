#include "UI/ServerListModel.h"

#include "Protocol/Connection/ServerList.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Client::UI::GroupServers y lo que muestra cada botón de la lista (CL5.5; 06 § La lista de
// servidores; Q-C62 y Q-C63). 0x4c1860 reparte cada servidor en el grupo `código / 20`, y el
// grupo va a un lugar de la pantalla por una tabla de 25: del lugar 4 al 24 hay un botón, en el
// orden 20, 16, 15, 14, 13, 11, 19, 18, 17, 10, 9, ..., 0 y 12.
namespace
{
using Client::Protocol::Connection::ServerEntry;
using Client::UI::GroupNameText;
using Client::UI::GroupOf;
using Client::UI::GroupServers;
using Client::UI::GroupSlot;
using Client::UI::IsNonPvp;
using Client::UI::LoadText;
using Client::UI::LoginTitleText;
using Client::UI::ServerGroups;
using Client::UI::ServerLabel;
using Client::UI::ServerNumber;
using Client::UI::SingleServerLabel;

// Los grupos de la lista, en el orden en que salen.
std::vector<std::uint16_t> GroupsOf(const ServerGroups& grouped)
{
    std::vector<std::uint16_t> groups;
    for (const auto& group : grouped.groups)
    {
        groups.push_back(group.group);
    }
    return groups;
}

// El 400 (el 0 del laboratorio con FoxAnti) y el 401 son del grupo 20, en el lugar 4, el primero;
// el 380 es del 19, en el lugar 10. El original no mostraría el 19: corta en el lugar 5, vacío
// (Q-C62); MuRet muestra todos los lugares con servidores. El 420 es del grupo 21, que cae en el
// lugar 2 y no tiene botón: se ignora y se cuenta.
TEST(ServerListModelTest, GroupsByCodeOverTwenty)
{
    const std::vector<ServerEntry> list{{400, 10}, {380, 20}, {401, 30}, {420, 40}};

    const ServerGroups grouped = GroupServers(list);

    ASSERT_EQ(grouped.groups.size(), 2u);
    EXPECT_EQ(grouped.groups[0].group, 20);
    EXPECT_EQ(grouped.groups[0].slot, 4);
    ASSERT_EQ(grouped.groups[0].servers.size(), 2u);
    EXPECT_EQ(grouped.groups[0].servers[0].code, 400);
    EXPECT_EQ(grouped.groups[0].servers[0].load, 10);
    EXPECT_EQ(grouped.groups[0].servers[1].code, 401);
    EXPECT_EQ(grouped.groups[1].group, 19);
    EXPECT_EQ(grouped.groups[1].slot, 10);
    ASSERT_EQ(grouped.groups[1].servers.size(), 1u);
    EXPECT_EQ(grouped.groups[1].servers[0].code, 380);
    EXPECT_EQ(grouped.ignored, 1u);
}

TEST(ServerListModelTest, AnEmptyListHasNoGroups)
{
    const ServerGroups grouped = GroupServers({});

    EXPECT_TRUE(grouped.groups.empty());
    EXPECT_EQ(grouped.ignored, 0u);
}

// La tabla de 0x4c1860: el lugar es 23 menos la tabla, salvo el 12, que va al 24. Un servidor por
// grupo, del 0 al 20, en cualquier orden: salen como los lugares.
TEST(ServerListModelTest, KeepsTheOrderOfTheOriginal)
{
    std::vector<ServerEntry> list;
    for (std::uint16_t group = 0; group <= 20; ++group)
    {
        list.push_back(ServerEntry{static_cast<std::uint16_t>(group * 20), 0});
    }

    const ServerGroups grouped = GroupServers(list);

    const std::vector<std::uint16_t> expected{20, 16, 15, 14, 13, 11, 19, 18, 17, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 12};
    EXPECT_EQ(GroupsOf(grouped), expected);
    for (std::size_t i = 0; i < grouped.groups.size(); ++i)
    {
        EXPECT_EQ(grouped.groups[i].slot, static_cast<int>(i) + 4) << grouped.groups[i].group;
    }
}

// Sin el grupo 20, el lugar 4 queda vacío y el original no muestra ningún botón (Q-C62). MuRet
// muestra los tres, en sus lugares: el 1 en el 22, el 0 en el 23 y el 12 en el 24.
TEST(ServerListModelTest, AListWithoutGroupTwentyShowsItsGroups)
{
    const std::vector<ServerEntry> list{{0, 0}, {240, 0}, {20, 0}};

    EXPECT_EQ(GroupsOf(GroupServers(list)), (std::vector<std::uint16_t>{1, 0, 12}));
}

// Los grupos 21 a 23 caen en los lugares 2 a 0 y el 24 en el -1; del 500 en adelante, la tabla se
// lee fuera de la pila (Q-C63). Ninguno tiene botón.
TEST(ServerListModelTest, IgnoresTheGroupsWithoutAButton)
{
    const std::vector<ServerEntry> list{{420, 0}, {440, 0}, {460, 0}, {480, 0}, {499, 0}, {500, 0}, {65535, 0}};

    const ServerGroups grouped = GroupServers(list);

    EXPECT_TRUE(grouped.groups.empty());
    EXPECT_EQ(grouped.ignored, list.size());
    EXPECT_EQ(GroupSlot(21), std::optional<int>{2});
    EXPECT_EQ(GroupSlot(24), std::optional<int>{-1});
    EXPECT_EQ(GroupSlot(25), std::nullopt);
}

// Un grupo tiene 20 números (código % 20). Con códigos repetidos el original sigue y pisa el grupo
// que sigue (Q-C63); MuRet guarda 20 e ignora el resto.
TEST(ServerListModelTest, AGroupKeepsTwentyServers)
{
    std::vector<ServerEntry> list;
    for (std::uint16_t i = 0; i < 22; ++i)
    {
        list.push_back(ServerEntry{static_cast<std::uint16_t>(400 + i % 20), 0});
    }

    const ServerGroups grouped = GroupServers(list);

    ASSERT_EQ(grouped.groups.size(), 1u);
    EXPECT_EQ(grouped.groups[0].servers.size(), 20u);
    EXPECT_EQ(grouped.ignored, 2u);
}

// 0x4c1860: el nombre del grupo g es el texto 540 + g, salvo el 12 (559), el 19 (552) y el 20
// (1829).
TEST(ServerListModelTest, NamesEachGroupByItsText)
{
    EXPECT_EQ(GroupNameText(0), 540u);
    EXPECT_EQ(GroupNameText(11), 551u);
    EXPECT_EQ(GroupNameText(12), 559u);
    EXPECT_EQ(GroupNameText(13), 553u);
    EXPECT_EQ(GroupNameText(18), 558u);
    EXPECT_EQ(GroupNameText(19), 552u);
    EXPECT_EQ(GroupNameText(20), 1829u);
}

// 0x4c1860: el grupo de un servidor es código / 20, con la división entera. La cuenta lo usa para
// su título (0x641990).
TEST(ServerListModelTest, TheGroupIsTheCodeOverTwenty)
{
    EXPECT_EQ(GroupOf(400), 20u);
    EXPECT_EQ(GroupOf(419), 20u);
    EXPECT_EQ(GroupOf(420), 21u);
    EXPECT_EQ(GroupOf(19), 0u);
}

// 0x645860: el número es código % 20 + 1, y la carga elige el texto: 562 debajo de 100, 561 de
// 100 a 127 y 560 de 128 en adelante.
TEST(ServerListModelTest, TheNumberAndTheLoadText)
{
    EXPECT_EQ(ServerNumber(400), 1);
    EXPECT_EQ(ServerNumber(419), 20);
    EXPECT_EQ(ServerNumber(380), 1);
    EXPECT_EQ(LoadText(0), 562u);
    EXPECT_EQ(LoadText(99), 562u);
    EXPECT_EQ(LoadText(100), 561u);
    EXPECT_EQ(LoadText(127), 561u);
    EXPECT_EQ(LoadText(128), 560u);
    EXPECT_EQ(LoadText(255), 560u);
}

// 0x447c70: el 1 y el 15 son sin PvP, salvo en el lugar 4 (el grupo 20). Con eso elige 0x641990
// el título de la cuenta: el 460 sin PvP, el 461 si no.
TEST(ServerListModelTest, TheFirstAndTheFifteenthAreNonPvp)
{
    EXPECT_TRUE(IsNonPvp(10, 1));
    EXPECT_TRUE(IsNonPvp(10, 15));
    EXPECT_FALSE(IsNonPvp(10, 2));
    EXPECT_FALSE(IsNonPvp(4, 1));
    EXPECT_FALSE(IsNonPvp(4, 15));
    EXPECT_EQ(LoginTitleText(10, 1), 460u);
    EXPECT_EQ(LoginTitleText(4, 1), 461u);
}

// Los formatos del binario (no de Text.bmd): "%s%s %s" con un servidor en el grupo (0x6d6724) y
// "%s-%d%s %s" con más (0x6d672c), con la marca "(Non-PVP)" de 0x6d6738 o nada.
TEST(ServerListModelTest, TheLabelsOfTheServerButtons)
{
    EXPECT_EQ(SingleServerLabel("Group", false, "Load"), "Group Load");
    EXPECT_EQ(SingleServerLabel("Group", true, "Load"), "Group(Non-PVP) Load");
    EXPECT_EQ(ServerLabel("Group", 3, false, "Load"), "Group-3 Load");
    EXPECT_EQ(ServerLabel("Group", 15, true, "Load"), "Group-15(Non-PVP) Load");
}
} // namespace
