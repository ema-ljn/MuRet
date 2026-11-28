#include "FrontEnd/DeleteChecks.h"

#include "Protocol/FrontEnd/CharacterList.h"
#include "Ui/Notices.h"

#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

// Client::FrontEnd::NoticeOfDeleteButton and FreeDeletionGroup: the delete button's checks (0x63edf0), with the
// chosen one and the chosen server's group.
namespace
{
using Client::FrontEnd::FreeDeletionGroup;
using Client::FrontEnd::NoticeOfDeleteButton;
using Client::Protocol::FrontEnd::CharacterEntry;
using Client::Ui::BlockedAccountNotice;
using Client::Ui::ConfirmDeletionNotice;
using Client::Ui::GuildMemberNotice;
using Client::Ui::HighLevelNotice;

CharacterEntry Deletable()
{
    CharacterEntry entry;
    entry.slot = 1;
    entry.level = 79;
    entry.flags = 0;
    entry.guildStatus = 0xff;
    return entry;
}

// No guild (0xff), under 80, without the 0x12 flags: confirmation 0x5e.
TEST(DeleteChecksTest, AFreeCharacterAsksForConfirmation)
{
    EXPECT_EQ(NoticeOfDeleteButton(Deletable(), false), ConfirmDeletionNotice);
    EXPECT_EQ(NoticeOfDeleteButton(Deletable(), true), ConfirmDeletionNotice);
}

// 1: with a guild (+0x368 other than 0xff), 0x92, before everything else.
TEST(DeleteChecksTest, AGuildMemberIsNotDeleted)
{
    for (const std::uint8_t guild : {std::uint8_t{0}, std::uint8_t{0x20}, std::uint8_t{0x80}, std::uint8_t{0xfe}})
    {
        CharacterEntry entry = Deletable();
        entry.guildStatus = guild;
        entry.level = 400;
        entry.flags = 0x12;
        EXPECT_EQ(NoticeOfDeleteButton(entry, false), GuildMemberNotice) << static_cast<int>(guild);
    }
}

// 2: level 80 or more (the 0x50 of 0x6d5e24) and outside text 559's group, 0x83, before the flags. In 559's group, it
// goes on.
TEST(DeleteChecksTest, AHighLevelOneIsDeletedOnlyInTheFreeGroup)
{
    CharacterEntry entry = Deletable();
    entry.level = 80;
    entry.flags = 0x02;
    EXPECT_EQ(NoticeOfDeleteButton(entry, false), HighLevelNotice);
    EXPECT_EQ(NoticeOfDeleteButton(entry, true), BlockedAccountNotice);
    entry.flags = 0;
    EXPECT_EQ(NoticeOfDeleteButton(entry, true), ConfirmDeletionNotice);
    entry.level = 0xffff;
    EXPECT_EQ(NoticeOfDeleteButton(entry, false), HighLevelNotice);
}

// 3: with bit 0x02 or 0x10 of the flags, 0x61. 0x01 (blocked from entering) and the others do not count.
TEST(DeleteChecksTest, TheBlockingFlagsStopTheDeletion)
{
    for (const std::uint8_t flags : {std::uint8_t{0x02}, std::uint8_t{0x10}, std::uint8_t{0x12}, std::uint8_t{0xff}})
    {
        CharacterEntry entry = Deletable();
        entry.flags = flags;
        EXPECT_EQ(NoticeOfDeleteButton(entry, false), BlockedAccountNotice) << static_cast<int>(flags);
    }
    for (const std::uint8_t flags : {std::uint8_t{0x01}, std::uint8_t{0x04}, std::uint8_t{0x08}, std::uint8_t{0xed}})
    {
        CharacterEntry entry = Deletable();
        entry.flags = flags;
        EXPECT_EQ(NoticeOfDeleteButton(entry, false), ConfirmDeletionNotice) << static_cast<int>(flags);
    }
}

// strcmp of the chosen server's group name (code / 20) with text 559: what counts is the text, not the number. With
// the lab's Text.bmd, all but 540 say the same.
TEST(DeleteChecksTest, TheFreeGroupIsTheOneNamedLikeText559)
{
    const auto texts = Client::Tests::MakeTextTable({{540, "Synthetic A"},
                                                     {541, "Synthetic B"},
                                                     {559, "Synthetic B"},
                                                     {552, "Synthetic C"},
                                                     {1829, "Synthetic B"}});
    EXPECT_TRUE(FreeDeletionGroup(&texts, std::uint16_t{12 * 20}));
    EXPECT_TRUE(FreeDeletionGroup(&texts, std::uint16_t{12 * 20 + 19}));
    EXPECT_TRUE(FreeDeletionGroup(&texts, std::uint16_t{1 * 20 + 3}));
    EXPECT_TRUE(FreeDeletionGroup(&texts, std::uint16_t{20 * 20}));
    EXPECT_FALSE(FreeDeletionGroup(&texts, std::uint16_t{0}));
    EXPECT_FALSE(FreeDeletionGroup(&texts, std::uint16_t{19 * 20}));
    // 542 is missing: empty against "Synthetic B".
    EXPECT_FALSE(FreeDeletionGroup(&texts, std::uint16_t{2 * 20}));
}

// Without a chosen server (without ConnectServer) or without texts, MuRet does not look at the group: the original
// always goes through the list.
TEST(DeleteChecksTest, WithoutAChosenServerTheGroupDoesNotCount)
{
    const auto texts = Client::Tests::MakeTextTable({{540, "Synthetic A"}, {559, "Synthetic B"}});
    EXPECT_TRUE(FreeDeletionGroup(&texts, std::nullopt));
    EXPECT_TRUE(FreeDeletionGroup(nullptr, std::uint16_t{0}));
}
} // namespace
