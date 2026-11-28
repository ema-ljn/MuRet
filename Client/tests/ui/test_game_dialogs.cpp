#include "Ui/GameDialogs.h"

#include "Ui/Dialogs.h"
#include "Ui/Widget.h"

#include "support/SceneDataFiles.h"

#include <cstdint>
#include <string>
#include <utility>

#include <gtest/gtest.h>

// The game's old boxes (CL8 milestone 6): 0x632e30, 0x633300 and the step of their buttons and of the 0x8c's arrows.
// Made-up texts.
namespace
{
using Client::Tests::MakeTextTable;
using Client::Ui::DialogBoxState;
using Client::Ui::DialogRow;
using Client::Ui::DialogState;
using Client::Ui::Point;

// 0x632e30: the text split into lines shorter than 0x26, one button and the 0x8a, open or waiting.
TEST(GameDialogsTest, TheMessageBoxSplitsItsTextAndOpensTheFriendlyDialog)
{
    DialogBoxState box;
    box.buttons[1].state = 2;
    DialogState dialogs;
    Client::Ui::OpenMessageBox(box, dialogs, "una caja con un texto bastante largo para dos renglones");

    EXPECT_EQ(box.lineCount, 2);
    EXPECT_EQ(DialogRow(box, 0), "una caja con un texto bastante largo ");
    EXPECT_EQ(DialogRow(box, 1), "para dos renglones");
    EXPECT_EQ(box.buttons[0].state, 1);
    EXPECT_EQ(box.buttons[0].x, 0x47);
    EXPECT_EQ(box.buttons[0].y, 0x8c);
    EXPECT_EQ(box.buttons[0].width, 0x46);
    EXPECT_EQ(box.buttons[0].height, 0x15);
    EXPECT_EQ(box.buttons[1].state, 0);
    EXPECT_EQ(dialogs.current, 0x8a);

    dialogs.current = Client::Ui::EscapeMenuDialog;
    Client::Ui::OpenMessageBox(box, dialogs, "otra");
    EXPECT_EQ(dialogs.current, Client::Ui::EscapeMenuDialog);
    EXPECT_EQ(dialogs.pending, 0x8a);
}

// 0x633300: one strcpy per text into 0x26 rows; a long one runs into the next row until that row's text overwrites
// it.
TEST(GameDialogsTest, TheTextDialogCopiesEachTextIntoItsRow)
{
    const auto texts = MakeTextTable(
        {{40, "primer renglon"}, {41, "un renglon que no entra en los treinta y ocho bytes"}, {42, "tercero"}});
    DialogBoxState box;
    DialogState dialogs;
    const auto opened = Client::Ui::OpenTextDialog(box, dialogs, &texts, 3, 40, 0x8b);
    ASSERT_TRUE(opened.HasValue()) << opened.Error();

    EXPECT_EQ(box.lineCount, 3);
    EXPECT_EQ(DialogRow(box, 0), "primer renglon");
    // Without a '\0' in its 0x26 bytes, the second one reads up to the one the third left.
    EXPECT_EQ(DialogRow(box, 1), "un renglon que no entra en los treintatercero");
    EXPECT_EQ(DialogRow(box, 2), "tercero");
    EXPECT_EQ(box.buttons[0].state, 1);
    EXPECT_EQ(box.buttons[0].x, 0x15);
    EXPECT_EQ(box.buttons[0].y, 0x5a);
    EXPECT_EQ(box.buttons[1].state, 3);
    EXPECT_EQ(box.buttons[1].x, 0x78);
    EXPECT_EQ(box.buttons[1].y, 0x5a);
    EXPECT_EQ(dialogs.current, 0x8b);
}

// The 0x9a splits each text with 0x632c90 after the previous one and lowers the buttons to 0x78; the 0x7f does not
// get in over it.
TEST(GameDialogsTest, TheChaosCastleDialogSplitsItsTextsOneAfterTheOther)
{
    const auto texts = MakeTextTable({{50, "una linea"}, {51, "otra linea que pasa de los treinta y ocho bytes"}});
    DialogBoxState box;
    DialogState dialogs;
    ASSERT_TRUE(Client::Ui::OpenTextDialog(box, dialogs, &texts, 2, 50, Client::Ui::ChaosCastleDialog).HasValue());

    EXPECT_EQ(box.lineCount, 3);
    EXPECT_EQ(DialogRow(box, 0), "una linea");
    EXPECT_EQ(DialogRow(box, 1), "otra linea que pasa de los treinta y ");
    EXPECT_EQ(DialogRow(box, 2), "ocho bytes");
    EXPECT_EQ(box.buttons[0].y, 0x78);
    EXPECT_EQ(box.buttons[1].y, 0x78);
    EXPECT_EQ(dialogs.current, Client::Ui::ChaosCastleDialog);

    ASSERT_TRUE(Client::Ui::OpenTextDialog(box, dialogs, &texts, 1, 50, Client::Ui::IgnoredOverChaosCastle).HasValue());
    EXPECT_EQ(dialogs.current, Client::Ui::ChaosCastleDialog);
    EXPECT_EQ(dialogs.pending, 0);
}

// The formatted texts are CL9.3's (OpenTheMoneyTextDialog opens the 0x99), and what would pass the 7 rows bails out.
TEST(GameDialogsTest, TheFormattedTextsAndTheOverflowsAreCut)
{
    const auto texts = MakeTextTable({{60, "corto"}});
    DialogBoxState box;
    DialogState dialogs;
    // Without the item (0x96 and 0xa8) or without what its format reads (0xa3), they bail out with their address.
    for (const auto& [id, address] :
         {std::pair<std::int32_t, std::string>{0x96, "0x63330f"}, {0xa8, "0x63330f"}, {0xa3, "0x63340d"}})
    {
        const auto opened = Client::Ui::OpenTextDialog(box, dialogs, &texts, 1, 60, id);
        ASSERT_FALSE(opened.HasValue());
        EXPECT_NE(opened.Error().find(address), std::string::npos) << opened.Error();
    }
    const auto money = Client::Ui::OpenTextDialog(box, dialogs, &texts, 1, 60, Client::Ui::MoneyTextDialog);
    ASSERT_FALSE(money.HasValue());
    EXPECT_NE(money.Error().find("OpenTheMoneyTextDialog"), std::string::npos) << money.Error();
    const auto tooMany = Client::Ui::OpenTextDialog(box, dialogs, &texts, 8, 60, 0x8b);
    ASSERT_FALSE(tooMany.HasValue());
    EXPECT_NE(tooMany.Error().find("0x633612"), std::string::npos) << tooMany.Error();
    EXPECT_EQ(dialogs.current, 0);
}

// 0x63332f..0x6333a3 (CL9 milestone 7): the 0x99 writes the first row with the sprintf of its text and the zen,
// uncapped (the long part runs below and the second's sprintf overwrites it), passes the following ones through the
// same sprintf without arguments (the "%%" comes out as "%") and opens with the two buttons. With one row, only the
// first; what would pass the 7 bails out, as without Text.bmd.
TEST(GameDialogsTest, TheMoneyDialogFormatsItsFirstRow)
{
    const auto texts = MakeTextTable({{70, "venta : %s zen, un renglon largo"}, {71, "seguro? 100%%"}});
    DialogBoxState box;
    DialogState dialogs;
    ASSERT_TRUE(Client::Ui::OpenTheMoneyTextDialog(box, dialogs, &texts, 2, 70, "1,234,567").HasValue());
    EXPECT_EQ(box.lineCount, 2);
    // Without a '\0' in its 0x26 bytes, the first one reads up to the one the second left.
    EXPECT_EQ(DialogRow(box, 0), "venta : 1,234,567 zen, un renglon largseguro? 100%");
    EXPECT_EQ(DialogRow(box, 1), "seguro? 100%");
    EXPECT_EQ(box.buttons[0].state, 1);
    EXPECT_EQ(box.buttons[0].x, 0x15);
    EXPECT_EQ(box.buttons[0].y, 0x5a);
    EXPECT_EQ(box.buttons[1].state, 3);
    EXPECT_EQ(box.buttons[1].x, 0x78);
    EXPECT_EQ(box.buttons[1].y, 0x5a);
    EXPECT_EQ(dialogs.current, Client::Ui::MoneyTextDialog);

    DialogBoxState single;
    DialogState closed;
    ASSERT_TRUE(Client::Ui::OpenTheMoneyTextDialog(single, closed, &texts, 1, 70, "9").HasValue());
    EXPECT_EQ(single.lineCount, 1);
    EXPECT_EQ(DialogRow(single, 0), "venta : 9 zen, un renglon largo");
    EXPECT_EQ(DialogRow(single, 1), "");

    const std::string wide(7 * 0x26, '9');
    const auto overflow = Client::Ui::OpenTheMoneyTextDialog(single, closed, &texts, 1, 70, wide);
    ASSERT_FALSE(overflow.HasValue());
    EXPECT_NE(overflow.Error().find("0x633367"), std::string::npos) << overflow.Error();
    const auto missing = Client::Ui::OpenTheMoneyTextDialog(single, closed, nullptr, 2, 70, "9");
    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("0x633367"), std::string::npos) << missing.Error();
    EXPECT_EQ(closed.current, Client::Ui::MoneyTextDialog);
}

// 0x633419..0x633525: the jewel mix question split into two 0x1e rows and the price below; when undoing, the level
// + 1 and the other price.
TEST(GameDialogsTest, TheJewelMixQuestionSplitsInTwoRowsAndAddsThePrice)
{
    const auto texts = MakeTextTable({{1806, "Joya Uno"},
                                      {1807, "Joya Dos"},
                                      {1809, "Juntar de verdad %s en una cantidad de %d?"},
                                      {1810, "Cuesta %d"},
                                      {1813, "Deshacer %s %d?"},
                                      {1814, "Deshacer cuesta %d"}});
    DialogBoxState box;
    DialogState dialogs;
    const Client::Ui::DialogTextArguments mix{
        .jewelMode = 0, .jewelKind = 1, .jewelCount = 20, .jewelLevel = -1, .jewelPrice = 1000000};
    ASSERT_TRUE(
        Client::Ui::OpenTextDialog(box, dialogs, &texts, 0, 0, Client::Ui::ServerTimeDialog, nullptr, &mix).HasValue());
    EXPECT_EQ(box.lineCount, 3);
    EXPECT_EQ(DialogRow(box, 0), "Juntar de verdad Joya Dos en ");
    EXPECT_EQ(DialogRow(box, 1), "una cantidad de 20?");
    EXPECT_EQ(DialogRow(box, 2), "Cuesta 1000000");
    EXPECT_EQ(box.buttons[0].y, 0x5a);
    EXPECT_EQ(dialogs.current, Client::Ui::ServerTimeDialog);

    DialogBoxState unmixBox;
    DialogState unmixDialogs;
    const Client::Ui::DialogTextArguments unmix{
        .jewelMode = 1, .jewelKind = 0, .jewelCount = 1, .jewelLevel = 2, .jewelPrice = 1000000};
    ASSERT_TRUE(
        Client::Ui::OpenTextDialog(unmixBox, unmixDialogs, &texts, 0, 0, Client::Ui::ServerTimeDialog, nullptr, &unmix)
            .HasValue());
    EXPECT_EQ(unmixBox.lineCount, 2);
    EXPECT_EQ(DialogRow(unmixBox, 0), "Deshacer Joya Uno 3?");
    EXPECT_EQ(DialogRow(unmixBox, 1), "Deshacer cuesta 1000000");
}

// 0x62d756: the rectangle shifted by (0xd5, 0x3c) with its edges; with the click, it chooses and clears the click and
// the button down.
TEST(GameDialogsTest, TheButtonsLightUpAndTakeTheClick)
{
    DialogBoxState box;
    DialogState dialogs;
    Client::Ui::OpenMessageBox(box, dialogs, "aviso");
    bool click = false;
    bool held = true;

    EXPECT_EQ(Client::Ui::StepTheDialogButtons(box, Point{0x47 + 0xd5 + 0x46, 0x8c + 0x3c + 0x15}, click, held), -1);
    EXPECT_EQ(box.buttons[0].state, 2);
    EXPECT_EQ(box.buttons[1].state, 0);
    EXPECT_TRUE(held);

    EXPECT_EQ(Client::Ui::StepTheDialogButtons(box, Point{0x47 + 0xd5 - 1, 0x8c + 0x3c}, click, held), -1);
    EXPECT_EQ(box.buttons[0].state, 1);

    click = true;
    EXPECT_EQ(Client::Ui::StepTheDialogButtons(box, Point{0x47 + 0xd5, 0x8c + 0x3c}, click, held), 0);
    EXPECT_FALSE(click);
    EXPECT_FALSE(held);
}

// 0x62d7eb: the 0x8c's arrows, with the click and without consuming it, between 0 and the last one.
TEST(GameDialogsTest, TheArrowsTurnThePagesWithinTheirBounds)
{
    const auto texts = MakeTextTable({{70, "pagina cero"}, {71, "pagina uno"}, {72, "pagina dos"}});
    DialogBoxState box;
    box.pageText = 70;
    box.page = 1;
    box.lastPage = 2;

    ASSERT_TRUE(Client::Ui::StepTheDialogPages(box, &texts, Point{0x179, 0xca}, true).HasValue());
    EXPECT_EQ(box.page, 2);
    EXPECT_EQ(DialogRow(box, 0), "pagina dos");
    EXPECT_EQ(box.lineCount, 1);
    ASSERT_TRUE(Client::Ui::StepTheDialogPages(box, &texts, Point{0x188, 0xde}, true).HasValue());
    EXPECT_EQ(box.page, 2);

    ASSERT_TRUE(Client::Ui::StepTheDialogPages(box, &texts, Point{0x108, 0xde}, true).HasValue());
    EXPECT_EQ(box.page, 1);
    ASSERT_TRUE(Client::Ui::StepTheDialogPages(box, &texts, Point{0xf9, 0xca}, true).HasValue());
    EXPECT_EQ(box.page, 0);
    EXPECT_EQ(DialogRow(box, 0), "pagina cero");
    ASSERT_TRUE(Client::Ui::StepTheDialogPages(box, &texts, Point{0xf9, 0xca}, true).HasValue());
    EXPECT_EQ(box.page, 0);

    ASSERT_TRUE(Client::Ui::StepTheDialogPages(box, &texts, Point{0x179, 0xca}, false).HasValue());
    ASSERT_TRUE(Client::Ui::StepTheDialogPages(box, &texts, Point{0x179, 0xdf}, true).HasValue());
    EXPECT_EQ(box.page, 0);
}
} // namespace
