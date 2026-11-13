#include "UI/GameDialogs.h"

#include "UI/Dialogs.h"
#include "UI/Widget.h"

#include "support/SceneDataFiles.h"

#include <cstdint>
#include <string>

#include <gtest/gtest.h>

// Las cajas viejas del juego (CL8 hito 6): 0x632e30, 0x633300 y el paso de sus botones y de las flechas del 0x8c.
// Textos inventados.
namespace
{
using Client::Tests::MakeTextTable;
using Client::UI::DialogBoxState;
using Client::UI::DialogRow;
using Client::UI::DialogState;
using Client::UI::Point;

// 0x632e30: el texto partido en renglones de menos de 0x26, un botón y el 0x8a, abierto o esperando.
TEST(GameDialogsTest, TheMessageBoxSplitsItsTextAndOpensTheFriendlyDialog)
{
    DialogBoxState box;
    box.buttons[1].state = 2;
    DialogState dialogs;
    Client::UI::OpenMessageBox(box, dialogs, "una caja con un texto bastante largo para dos renglones");

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

    dialogs.current = Client::UI::EscapeMenuDialog;
    Client::UI::OpenMessageBox(box, dialogs, "otra");
    EXPECT_EQ(dialogs.current, Client::UI::EscapeMenuDialog);
    EXPECT_EQ(dialogs.pending, 0x8a);
}

// 0x633300: un strcpy por texto en renglones de 0x26; uno largo sigue en el renglón siguiente hasta que el texto de
// ese renglón lo pisa.
TEST(GameDialogsTest, TheTextDialogCopiesEachTextIntoItsRow)
{
    const auto texts = MakeTextTable(
        {{40, "primer renglon"}, {41, "un renglon que no entra en los treinta y ocho bytes"}, {42, "tercero"}});
    DialogBoxState box;
    DialogState dialogs;
    const auto opened = Client::UI::OpenTextDialog(box, dialogs, &texts, 3, 40, 0x8b);
    ASSERT_TRUE(opened.HasValue()) << opened.Error();

    EXPECT_EQ(box.lineCount, 3);
    EXPECT_EQ(DialogRow(box, 0), "primer renglon");
    // Sin '\0' en sus 0x26 bytes, el segundo se lee hasta el que dejó el tercero.
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

// El 0x9a parte cada texto con 0x632c90 detrás del anterior y baja los botones a 0x78; el 0x7f no entra sobre él.
TEST(GameDialogsTest, TheChaosCastleDialogSplitsItsTextsOneAfterTheOther)
{
    const auto texts = MakeTextTable({{50, "una linea"}, {51, "otra linea que pasa de los treinta y ocho bytes"}});
    DialogBoxState box;
    DialogState dialogs;
    ASSERT_TRUE(Client::UI::OpenTextDialog(box, dialogs, &texts, 2, 50, Client::UI::ChaosCastleDialog).HasValue());

    EXPECT_EQ(box.lineCount, 3);
    EXPECT_EQ(DialogRow(box, 0), "una linea");
    EXPECT_EQ(DialogRow(box, 1), "otra linea que pasa de los treinta y ");
    EXPECT_EQ(DialogRow(box, 2), "ocho bytes");
    EXPECT_EQ(box.buttons[0].y, 0x78);
    EXPECT_EQ(box.buttons[1].y, 0x78);
    EXPECT_EQ(dialogs.current, Client::UI::ChaosCastleDialog);

    ASSERT_TRUE(Client::UI::OpenTextDialog(box, dialogs, &texts, 1, 50, Client::UI::IgnoredOverChaosCastle).HasValue());
    EXPECT_EQ(dialogs.current, Client::UI::ChaosCastleDialog);
    EXPECT_EQ(dialogs.pending, 0);
}

// Los textos con formato son de la CL9.3, y lo que pasaría los 7 renglones corta.
TEST(GameDialogsTest, TheFormattedTextsAndTheOverflowsAreCut)
{
    const auto texts = MakeTextTable({{60, "corto"}});
    DialogBoxState box;
    DialogState dialogs;
    for (const std::int32_t id : {0x96, 0xa8, 0x99, 0xa3})
    {
        const auto opened = Client::UI::OpenTextDialog(box, dialogs, &texts, 1, 60, id);
        ASSERT_FALSE(opened.HasValue());
        EXPECT_NE(opened.Error().find("CL9.3"), std::string::npos) << opened.Error();
    }
    const auto tooMany = Client::UI::OpenTextDialog(box, dialogs, &texts, 8, 60, 0x8b);
    ASSERT_FALSE(tooMany.HasValue());
    EXPECT_NE(tooMany.Error().find("0x633612"), std::string::npos) << tooMany.Error();
    EXPECT_EQ(dialogs.current, 0);
}

// 0x62d756: el rectángulo corrido (0xd5, 0x3c) con sus bordes; con el clic, elige y apaga el clic y el botón abajo.
TEST(GameDialogsTest, TheButtonsLightUpAndTakeTheClick)
{
    DialogBoxState box;
    DialogState dialogs;
    Client::UI::OpenMessageBox(box, dialogs, "aviso");
    bool click = false;
    bool held = true;

    EXPECT_EQ(Client::UI::StepTheDialogButtons(box, Point{0x47 + 0xd5 + 0x46, 0x8c + 0x3c + 0x15}, click, held), -1);
    EXPECT_EQ(box.buttons[0].state, 2);
    EXPECT_EQ(box.buttons[1].state, 0);
    EXPECT_TRUE(held);

    EXPECT_EQ(Client::UI::StepTheDialogButtons(box, Point{0x47 + 0xd5 - 1, 0x8c + 0x3c}, click, held), -1);
    EXPECT_EQ(box.buttons[0].state, 1);

    click = true;
    EXPECT_EQ(Client::UI::StepTheDialogButtons(box, Point{0x47 + 0xd5, 0x8c + 0x3c}, click, held), 0);
    EXPECT_FALSE(click);
    EXPECT_FALSE(held);
}

// 0x62d7eb: las flechas del 0x8c, con el clic y sin gastarlo, entre 0 y la última.
TEST(GameDialogsTest, TheArrowsTurnThePagesWithinTheirBounds)
{
    const auto texts = MakeTextTable({{70, "pagina cero"}, {71, "pagina uno"}, {72, "pagina dos"}});
    DialogBoxState box;
    box.pageText = 70;
    box.page = 1;
    box.lastPage = 2;

    ASSERT_TRUE(Client::UI::StepTheDialogPages(box, &texts, Point{0x179, 0xca}, true).HasValue());
    EXPECT_EQ(box.page, 2);
    EXPECT_EQ(DialogRow(box, 0), "pagina dos");
    EXPECT_EQ(box.lineCount, 1);
    ASSERT_TRUE(Client::UI::StepTheDialogPages(box, &texts, Point{0x188, 0xde}, true).HasValue());
    EXPECT_EQ(box.page, 2);

    ASSERT_TRUE(Client::UI::StepTheDialogPages(box, &texts, Point{0x108, 0xde}, true).HasValue());
    EXPECT_EQ(box.page, 1);
    ASSERT_TRUE(Client::UI::StepTheDialogPages(box, &texts, Point{0xf9, 0xca}, true).HasValue());
    EXPECT_EQ(box.page, 0);
    EXPECT_EQ(DialogRow(box, 0), "pagina cero");
    ASSERT_TRUE(Client::UI::StepTheDialogPages(box, &texts, Point{0xf9, 0xca}, true).HasValue());
    EXPECT_EQ(box.page, 0);

    // Sin el clic, o fuera de las flechas, nada.
    ASSERT_TRUE(Client::UI::StepTheDialogPages(box, &texts, Point{0x179, 0xca}, false).HasValue());
    ASSERT_TRUE(Client::UI::StepTheDialogPages(box, &texts, Point{0x179, 0xdf}, true).HasValue());
    EXPECT_EQ(box.page, 0);
}
} // namespace
