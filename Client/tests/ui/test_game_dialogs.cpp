#include "UI/GameDialogs.h"

#include "UI/Dialogs.h"
#include "UI/Widget.h"

#include "support/SceneDataFiles.h"

#include <cstdint>
#include <string>
#include <utility>

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
    // Sin el ítem (0x96 y 0xa8) o sin lo que leen sus formatos (0x99 y 0xa3), cortan con su dirección.
    for (const auto& [id, address] : {std::pair<std::int32_t, std::string>{0x96, "0x63330f"},
                                      {0xa8, "0x63330f"},
                                      {0x99, "0x63333d"},
                                      {0xa3, "0x63340d"}})
    {
        const auto opened = Client::UI::OpenTextDialog(box, dialogs, &texts, 1, 60, id);
        ASSERT_FALSE(opened.HasValue());
        EXPECT_NE(opened.Error().find(address), std::string::npos) << opened.Error();
    }
    const auto tooMany = Client::UI::OpenTextDialog(box, dialogs, &texts, 8, 60, 0x8b);
    ASSERT_FALSE(tooMany.HasValue());
    EXPECT_NE(tooMany.Error().find("0x633612"), std::string::npos) << tooMany.Error();
    EXPECT_EQ(dialogs.current, 0);
}

// 0x63332f..0x6333a3: el 0x99 pone el número con sus puntos en el primer texto y copia los siguientes como formato.
TEST(GameDialogsTest, TheMoneyTextFormatsTheFirstRowAndCopiesTheRest)
{
    const auto texts = MakeTextTable({{70, "Pagar %s zen?"}, {71, "Seguro"}, {72, "Del todo 100%"}});
    DialogBoxState box;
    DialogState dialogs;
    const Client::UI::DialogTextArguments arguments{.money = "1.234.567"};
    ASSERT_TRUE(
        Client::UI::OpenTextDialog(box, dialogs, &texts, 3, 70, Client::UI::MoneyTextDialog, nullptr, &arguments)
            .HasValue());
    EXPECT_EQ(box.lineCount, 3);
    EXPECT_EQ(DialogRow(box, 0), "Pagar 1.234.567 zen?");
    EXPECT_EQ(DialogRow(box, 1), "Seguro");
    // 0x69fd9b con el texto como formato: el '%' del final no escribe nada, como el CRT.
    EXPECT_EQ(DialogRow(box, 2), "Del todo 100");
    EXPECT_EQ(dialogs.current, Client::UI::MoneyTextDialog);
}

// 0x633419..0x633525: la pregunta de la mezcla de joyas partida en dos renglones de 0x1e y el precio debajo; al
// deshacer, el nivel + 1 y el otro precio.
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
    const Client::UI::DialogTextArguments mix{
        .jewelMode = 0, .jewelKind = 1, .jewelCount = 20, .jewelLevel = -1, .jewelPrice = 1000000};
    ASSERT_TRUE(
        Client::UI::OpenTextDialog(box, dialogs, &texts, 0, 0, Client::UI::ServerTimeDialog, nullptr, &mix).HasValue());
    EXPECT_EQ(box.lineCount, 3);
    EXPECT_EQ(DialogRow(box, 0), "Juntar de verdad Joya Dos en ");
    EXPECT_EQ(DialogRow(box, 1), "una cantidad de 20?");
    EXPECT_EQ(DialogRow(box, 2), "Cuesta 1000000");
    EXPECT_EQ(box.buttons[0].y, 0x5a);
    EXPECT_EQ(dialogs.current, Client::UI::ServerTimeDialog);

    DialogBoxState unmixBox;
    DialogState unmixDialogs;
    const Client::UI::DialogTextArguments unmix{
        .jewelMode = 1, .jewelKind = 0, .jewelCount = 1, .jewelLevel = 2, .jewelPrice = 1000000};
    ASSERT_TRUE(
        Client::UI::OpenTextDialog(unmixBox, unmixDialogs, &texts, 0, 0, Client::UI::ServerTimeDialog, nullptr, &unmix)
            .HasValue());
    EXPECT_EQ(unmixBox.lineCount, 2);
    EXPECT_EQ(DialogRow(unmixBox, 0), "Deshacer Joya Uno 3?");
    EXPECT_EQ(DialogRow(unmixBox, 1), "Deshacer cuesta 1000000");
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
