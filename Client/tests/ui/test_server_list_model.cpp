#include "FrontEnd/ServerListModel.h"

#include "Protocol/Session/ServerList.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Client::FrontEnd::GroupServers and what each button of the list shows (CL5.5; 06 § La lista de servidores; Q-C62
// and Q-C63). 0x4c1860 puts each server in group `code / 20`, and the group goes to a screen slot through a table of
// 25: from slot 4 to 24 there is a button, in the order 20, 16, 15, 14, 13, 11, 19, 18, 17, 10, 9, ..., 0 and 12.
namespace
{
using Client::FrontEnd::GroupNameText;
using Client::FrontEnd::GroupOf;
using Client::FrontEnd::GroupServers;
using Client::FrontEnd::GroupSlot;
using Client::FrontEnd::IsNonPvp;
using Client::FrontEnd::LoadText;
using Client::FrontEnd::LoginTitleText;
using Client::FrontEnd::ServerGroups;
using Client::FrontEnd::ServerLabel;
using Client::FrontEnd::ServerNumber;
using Client::FrontEnd::SingleServerLabel;
using Client::Protocol::Session::ServerEntry;

// The list's groups, in the order they come out.
std::vector<std::uint16_t> GroupsOf(const ServerGroups& grouped)
{
    std::vector<std::uint16_t> groups;
    for (const auto& group : grouped.groups)
    {
        groups.push_back(group.group);
    }
    return groups;
}

// 400 (the lab's 0, with FoxAnti) and 401 are in group 20, at slot 4, the first; 380 is in 19, at slot 10. The
// original would not show 19: it stops at slot 5, empty (Q-C62); MuRet shows every slot with servers. 420 is in
// group 21, which falls on slot 2 and has no button: it is ignored and counted.
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

// 0x4c1860's table: the slot is 23 minus the table, except 12, which goes to 24. One server per group, 0 to 20, in
// any order: they come out as the slots.
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

// Without group 20, slot 4 stays empty and the original shows no button (Q-C62). MuRet shows all three, in their
// slots: 1 at 22, 0 at 23 and 12 at 24.
TEST(ServerListModelTest, AListWithoutGroupTwentyShowsItsGroups)
{
    const std::vector<ServerEntry> list{{0, 0}, {240, 0}, {20, 0}};

    EXPECT_EQ(GroupsOf(GroupServers(list)), (std::vector<std::uint16_t>{1, 0, 12}));
}

// Groups 21 to 23 fall on slots 2 to 0 and 24 on -1; from 500 on, the table is read outside the stack (Q-C63). None
// has a button.
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

// A group has 20 numbers (code % 20). With repeated codes the original goes on and overwrites the next group
// (Q-C63); MuRet keeps 20 and ignores the rest.
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

// 0x4c1860: group g's name is text 540 + g, except 12 (559), 19 (552) and 20 (1829).
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

// 0x4c1860: a server's group is code / 20, with integer division. The account uses it for its title (0x641990).
TEST(ServerListModelTest, TheGroupIsTheCodeOverTwenty)
{
    EXPECT_EQ(GroupOf(400), 20u);
    EXPECT_EQ(GroupOf(419), 20u);
    EXPECT_EQ(GroupOf(420), 21u);
    EXPECT_EQ(GroupOf(19), 0u);
}

// 0x645860: the number is code % 20 + 1, and the load picks the text: 562 under 100, 561 from 100 to 127 and 560 from
// 128 on.
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

// 0x447c70: 1 and 15 are non-PvP, except at slot 4 (group 20). With that 0x641990 picks the account's title: 460
// non-PvP, 461 otherwise.
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

// The binary's formats (not Text.bmd's): "%s%s %s" with one server in the group (0x6d6724) and "%s-%d%s %s" with more
// (0x6d672c), with the mark "(Non-PVP)" from 0x6d6738 or nothing.
TEST(ServerListModelTest, TheLabelsOfTheServerButtons)
{
    EXPECT_EQ(SingleServerLabel("Group", false, "Load"), "Group Load");
    EXPECT_EQ(SingleServerLabel("Group", true, "Load"), "Group(Non-PVP) Load");
    EXPECT_EQ(ServerLabel("Group", 3, false, "Load"), "Group-3 Load");
    EXPECT_EQ(ServerLabel("Group", 15, true, "Load"), "Group-15(Non-PVP) Load");
}
} // namespace
