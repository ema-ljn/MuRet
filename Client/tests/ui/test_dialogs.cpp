#include "Ui/Dialogs.h"

#include <gtest/gtest.h>

// Client::UI: el par de diálogos del juego y 0x6236f0 (CL8 hito 1; 06 § HUD y ventanas, "Los diálogos del juego").
namespace
{
using Client::Ui::DialogState;
using Client::Ui::PushDialog;

TEST(DialogsTest, TheFirstOneOpensAndTheNextOneWaits)
{
    DialogState dialogs;
    PushDialog(dialogs, Client::Ui::EscapeMenuDialog);
    EXPECT_EQ(dialogs.current, Client::Ui::EscapeMenuDialog);
    EXPECT_EQ(dialogs.pending, 0);
    PushDialog(dialogs, 0x71);
    EXPECT_EQ(dialogs.current, Client::Ui::EscapeMenuDialog);
    EXPECT_EQ(dialogs.pending, 0x71);
}

TEST(DialogsTest, ZeroTakesThePendingOne)
{
    DialogState dialogs{.current = 0x6e, .pending = 0x71};
    PushDialog(dialogs, 0);
    EXPECT_EQ(dialogs.current, 0x71);
    EXPECT_EQ(dialogs.pending, 0);
    PushDialog(dialogs, 0);
    EXPECT_EQ(dialogs.current, 0);
}

TEST(DialogsTest, TheForcedOneReplacesTheOpenOne)
{
    DialogState dialogs{.current = 0x6e, .pending = 0x71};
    PushDialog(dialogs, Client::Ui::ForcedDialog);
    EXPECT_EQ(dialogs.current, Client::Ui::ForcedDialog);
    EXPECT_EQ(dialogs.pending, 0x71);
}

TEST(DialogsTest, ChaosCastleIgnoresThe7f)
{
    DialogState dialogs{.current = Client::Ui::ChaosCastleDialog};
    PushDialog(dialogs, Client::Ui::IgnoredOverChaosCastle);
    EXPECT_EQ(dialogs.current, Client::Ui::ChaosCastleDialog);
    EXPECT_EQ(dialogs.pending, 0);
}
} // namespace
