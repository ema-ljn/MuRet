#include "UI/DeleteChecks.h"

#include "Protocol/Character/CharacterList.h"
#include "UI/Notices.h"

#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

// Client::UI::NoticeOfDeleteButton y FreeDeletionGroup: las revisiones del botón de borrar
// (0x63edf0), con el elegido y el grupo del servidor elegido.
namespace
{
using Client::Protocol::Character::CharacterEntry;
using Client::UI::BlockedAccountNotice;
using Client::UI::ConfirmDeletionNotice;
using Client::UI::FreeDeletionGroup;
using Client::UI::GuildMemberNotice;
using Client::UI::HighLevelNotice;
using Client::UI::NoticeOfDeleteButton;

CharacterEntry Deletable()
{
    CharacterEntry entry;
    entry.slot = 1;
    entry.level = 79;
    entry.flags = 0;
    entry.guildStatus = 0xff;
    return entry;
}

// Sin gremio (0xff), de menos de 80, sin las banderas 0x12: la confirmación 0x5e.
TEST(DeleteChecksTest, AFreeCharacterAsksForConfirmation)
{
    EXPECT_EQ(NoticeOfDeleteButton(Deletable(), false), ConfirmDeletionNotice);
    EXPECT_EQ(NoticeOfDeleteButton(Deletable(), true), ConfirmDeletionNotice);
}

// 1: con gremio (+0x368 distinto de 0xff), el 0x92, antes que todo lo demás.
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

// 2: de nivel 80 o más (el 0x50 de 0x6d5e24) y fuera del grupo del texto 559, el 0x83, antes que
// las banderas. En el grupo del 559, sigue.
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

// 3: con el bit 0x02 o el 0x10 de las banderas, el 0x61. El 0x01 (bloqueado para entrar) y los
// demás no cuentan.
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

// strcmp del nombre del grupo del servidor elegido (código / 20) con el texto 559: el que cuenta es
// el texto, no el número. Con el Text.bmd del laboratorio, todos menos el 540 dicen lo mismo.
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
    // El 542 no está: vacío contra "Synthetic B".
    EXPECT_FALSE(FreeDeletionGroup(&texts, std::uint16_t{2 * 20}));
}

// Sin servidor elegido (sin ConnectServer) o sin textos, MuRet no mira el grupo: el original pasa
// siempre por la lista.
TEST(DeleteChecksTest, WithoutAChosenServerTheGroupDoesNotCount)
{
    const auto texts = Client::Tests::MakeTextTable({{540, "Synthetic A"}, {559, "Synthetic B"}});
    EXPECT_TRUE(FreeDeletionGroup(&texts, std::nullopt));
    EXPECT_TRUE(FreeDeletionGroup(nullptr, std::uint16_t{0}));
}
} // namespace
