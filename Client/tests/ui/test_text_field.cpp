#include "UI/TextField.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

// Client::UI::TextField: el campo de la cuenta y el de la clave (CL5.5, el paso 2 del plan, la
// tabla del TODO(human)). El original usa dos EDIT de Windows con EM_LIMITTEXT 10, la clave con
// ES_PASSWORD; MuRet guarda bytes de ASCII imprimible, hasta 10, porque es lo que entra en el
// paquete del login (Q-C43). La tabla:
//
// | se teclea                   | el campo                                                    |
// | --------------------------- | ----------------------------------------------------------- |
// | una letra, un dígito        | se suma                                                     |
// | un espacio (0x20)           | se suma: es imprimible                                      |
// | "á" (0xC3 0xA1 en UTF-8)    | no se suma ningún byte: ninguno es ASCII                    |
// | un emoji (4 bytes en UTF-8) | tampoco                                                     |
// | el byte 11, con 10 adentro  | no se suma; lo que viene detrás de un texto largo, tampoco  |
// | Backspace, vacío            | nada                                                        |
// | Backspace, con algo         | borra el último byte                                        |
// | la clave, con lo que tenga  | se muestra un asterisco por byte                            |
namespace
{
using Client::UI::TextField;

constexpr std::size_t Ten = 10;

TEST(TextFieldTest, TakesPrintableAscii)
{
    TextField field(Ten, false);

    field.Type("a");
    field.Type("Z9");
    field.Type(" ");
    field.Type("~");

    EXPECT_EQ(field.Value(), "aZ9 ~");
    EXPECT_EQ(field.Shown(), "aZ9 ~");
}

// Cada byte de una secuencia de UTF-8 de más de uno es 0x80 o más: ninguno entra, y lo ASCII que
// viene al lado sí.
TEST(TextFieldTest, DropsWhatIsNotAscii)
{
    TextField field(Ten, false);

    field.Type("\xc3\xa1");
    field.Type("\xf0\x9f\x98\x80");
    field.Type("b\xc3\xa1"
               "c");

    EXPECT_EQ(field.Value(), "bc");
}

// Los de control tampoco: Tab, Enter y Retroceso llegan como teclas, no como texto.
TEST(TextFieldTest, DropsTheControlBytes)
{
    TextField field(Ten, false);

    field.Type("\t\r\n\x7f");

    EXPECT_EQ(field.Value(), "");
}

TEST(TextFieldTest, StopsAtTenBytes)
{
    TextField field(Ten, false);

    field.Type("user000");
    field.Type("1234");
    EXPECT_EQ(field.Value(), "user000123");

    field.Type("x");
    EXPECT_EQ(field.Value(), "user000123");
    EXPECT_EQ(field.Value().size(), Ten);
}

TEST(TextFieldTest, BackspaceErasesTheLastByte)
{
    TextField field(Ten, false);
    field.Type("ab");

    field.Backspace();
    EXPECT_EQ(field.Value(), "a");

    field.Type("c");
    EXPECT_EQ(field.Value(), "ac");
}

TEST(TextFieldTest, BackspaceOnEmptyDoesNothing)
{
    TextField field(Ten, false);

    field.Backspace();

    EXPECT_EQ(field.Value(), "");
}

TEST(TextFieldTest, TheMaskedFieldShowsStars)
{
    TextField password(Ten, true);
    password.Type("pass0002");

    EXPECT_EQ(password.Shown(), "********");
    EXPECT_EQ(password.Shown().find("pass"), std::string::npos);
    EXPECT_EQ(password.Value(), "pass0002");
    EXPECT_TRUE(password.Masked());
}

TEST(TextFieldTest, ClearEmptiesIt)
{
    TextField field(Ten, true);
    field.Type("pass0002");

    field.Clear();

    EXPECT_EQ(field.Value(), "");
    EXPECT_EQ(field.Shown(), "");
    EXPECT_EQ(field.Size(), 0u);
}

// El campo del nombre de un personaje nuevo (Latin1): el EDIT de 0x63d670 no tiene filtro, y en
// Windows de Occidente lo que se escribe llega en CP1252. MuRet toma, además de ASCII, las letras de
// U+00A0 a U+00FF como su byte de Latin-1, que en esa franja es el de CP1252; lo demás no entra.
TEST(TextFieldTest, ALatin1FieldTakesTheLettersOfItsPage)
{
    TextField field(10, false, TextField::Charset::Latin1);
    field.Type("Pe\xc3\xb1"
               "a");
    EXPECT_EQ(field.Value(), "Pe\xf1"
                             "a");
    field.Type("\xc2\xa0\xc3\xbf");
    EXPECT_EQ(field.Value(), "Pe\xf1"
                             "a\xa0\xff");
    // U+0080 a U+009F, lo de más de dos bytes y lo que no es UTF-8, no.
    field.Type("\xc2\x80\xe2\x82\xac\xf0\x9f\x98\x80\xc3\xff");
    EXPECT_EQ(field.Value(), "Pe\xf1"
                             "a\xa0\xff");
    // El de ASCII, como siempre.
    TextField ascii(10, false);
    ascii.Type("Pe\xc3\xb1"
               "a");
    EXPECT_EQ(ascii.Value(), "Pea");
}

// Los EDIT del chat (CL8 hito 3): EM_SETSEL(0, −1) elige todo y lo primero que entra lo reemplaza, aun con el campo
// lleno; lo que no entra no lo toca.
TEST(TextFieldTest, WhatIsTypedReplacesTheSelection)
{
    TextField field(4, false, TextField::Charset::Latin1);
    field.Type("abcd");
    field.SelectAll();
    EXPECT_TRUE(field.AllSelected());

    field.Type("\x01");
    EXPECT_EQ(field.Value(), "abcd");
    EXPECT_TRUE(field.AllSelected());

    field.Type("xy");
    EXPECT_EQ(field.Value(), "xy");
    EXPECT_FALSE(field.AllSelected());
}

TEST(TextFieldTest, BackspaceErasesTheSelection)
{
    TextField field(Ten, false);
    field.Type("abc");
    field.SelectAll();

    field.Backspace();

    EXPECT_EQ(field.Value(), "");
    EXPECT_FALSE(field.AllSelected());
}

// WM_SETTEXT no mira EM_LIMITTEXT ni el filtro de lo tecleado, y deja el cursor al final.
TEST(TextFieldTest, SetTextPutsTheBytesAsTheyAre)
{
    TextField field(4, false, TextField::Charset::Latin1);
    field.SelectAll();

    field.SetText("abcdef\x01");

    EXPECT_EQ(field.Value(), "abcdef\x01");
    EXPECT_FALSE(field.AllSelected());
    field.Type("g");
    EXPECT_EQ(field.Value(), "abcdef\x01");
}

TEST(TextFieldTest, DeselectLeavesTheCursorAtTheEnd)
{
    TextField field(Ten, false);
    field.Type("ab");
    field.SelectAll();

    field.Deselect();
    field.Type("c");

    EXPECT_EQ(field.Value(), "abc");
}

TEST(TextFieldTest, DeselectMovesPartialSelectionCaretToEnd)
{
    TextField field(10, false);
    field.SetText("abcd");
    field.Select(1, 2);

    field.Deselect();

    EXPECT_EQ(field.Cursor(), field.Size());
    EXPECT_FALSE(field.HasSelection());
    field.Type("e");
    EXPECT_EQ(field.Value(), "abcde");
}

TEST(TextFieldTest, InsertsAtCursorWithoutChangingLegacyAppend)
{
    TextField field(10, false);
    field.Type("ac");
    field.SetCursor(1);

    field.Type("b");

    EXPECT_EQ(field.Value(), "abc");
    EXPECT_EQ(field.Cursor(), 2u);
    EXPECT_FALSE(field.HasSelection());
}

TEST(TextFieldTest, SelectsInEitherDirectionAndReportsOrderedRange)
{
    TextField field(10, false);
    field.SetText("abcd");

    field.Select(3, 1);

    EXPECT_TRUE(field.HasSelection());
    EXPECT_EQ(field.SelectionBegin(), 1u);
    EXPECT_EQ(field.SelectionEnd(), 3u);
    EXPECT_EQ(field.Selected(), "bc");
    EXPECT_EQ(field.Cursor(), 1u);

    field.Select(1, 3);
    EXPECT_EQ(field.SelectionBegin(), 1u);
    EXPECT_EQ(field.SelectionEnd(), 3u);
    EXPECT_EQ(field.Selected(), "bc");
    EXPECT_EQ(field.Cursor(), 3u);
}

TEST(TextFieldTest, ShiftMovementExtendsAndContractsSelection)
{
    TextField field(10, false);
    field.SetText("abcd");
    field.SetCursor(1);

    field.MoveRight(true);
    field.MoveRight(true);
    EXPECT_EQ(field.Selected(), "bc");
    EXPECT_EQ(field.Cursor(), 3u);

    field.MoveLeft(true);
    EXPECT_EQ(field.Selected(), "b");
    EXPECT_EQ(field.Cursor(), 2u);
}

TEST(TextFieldTest, HomeAndEndRespectShiftAndClampPositions)
{
    TextField field(10, false);
    field.SetText("abcd");
    field.SetCursor(2);

    field.MoveHome(true);
    EXPECT_EQ(field.Selected(), "ab");
    EXPECT_EQ(field.Cursor(), 0u);

    field.MoveEnd();
    EXPECT_EQ(field.Cursor(), 4u);
    EXPECT_FALSE(field.HasSelection());

    field.SetCursor(100);
    EXPECT_EQ(field.Cursor(), 4u);
}

TEST(TextFieldTest, ArrowsMoveFromTheActiveCaretAndClearSelection)
{
    TextField field(10, false);
    field.SetText("abcd");
    field.Select(1, 3);

    field.MoveLeft();
    EXPECT_EQ(field.Cursor(), 2u);
    EXPECT_FALSE(field.HasSelection());

    field.Select(1, 3);
    field.MoveRight();
    EXPECT_EQ(field.Cursor(), 4u);
    EXPECT_FALSE(field.HasSelection());

    field.Select(3, 1);
    field.MoveLeft();
    EXPECT_EQ(field.Cursor(), 0u);
    EXPECT_FALSE(field.HasSelection());

    field.Select(3, 1);
    field.MoveRight();
    EXPECT_EQ(field.Cursor(), 2u);
    EXPECT_FALSE(field.HasSelection());
}

TEST(TextFieldTest, DeleteAndBackspaceEraseSelectionOrAdjacentByte)
{
    TextField field(10, false);
    field.SetText("abcd");
    field.SetCursor(2);
    field.Backspace();
    EXPECT_EQ(field.Value(), "acd");
    EXPECT_EQ(field.Cursor(), 1u);

    field.Delete();
    EXPECT_EQ(field.Value(), "ad");
    EXPECT_EQ(field.Cursor(), 1u);

    field.Select(0, 2);
    field.Delete();
    EXPECT_EQ(field.Value(), "");
    EXPECT_EQ(field.Cursor(), 0u);

    field.SetText("ab");
    field.SetCursor(0);
    field.Backspace();
    EXPECT_EQ(field.Value(), "ab");
    field.SetCursor(2);
    field.Delete();
    EXPECT_EQ(field.Value(), "ab");
}

TEST(TextFieldTest, SelectionReplacementUsesResultingLengthForLimit)
{
    TextField field(4, false);
    field.SetText("abcd");
    field.Select(1, 3);

    field.Type("XYZ");

    EXPECT_EQ(field.Value(), "aXYd");
    EXPECT_EQ(field.Cursor(), 3u);
}

TEST(TextFieldTest, RejectedUtf8KeepsSelectionUntilAValidCharacterArrives)
{
    TextField field(4, false);
    field.SetText("abcd");
    field.Select(1, 3);

    field.Type("\xc3\xa1");
    EXPECT_EQ(field.Value(), "abcd");
    EXPECT_EQ(field.Selected(), "bc");

    field.Type("Z");
    EXPECT_EQ(field.Value(), "aZd");
}

TEST(TextFieldTest, SetTextBeyondLimitLeavesCursorAtEndAndCanEditThere)
{
    TextField field(2, false);
    field.SetText("long");

    EXPECT_EQ(field.Cursor(), 4u);
    field.Type("x");
    EXPECT_EQ(field.Value(), "long");
    EXPECT_EQ(field.Cursor(), 4u);
}

TEST(TextFieldTest, SelectionAndCursorResetWithClear)
{
    TextField field(10, false);
    field.SetText("abcd");
    field.Select(1, 3);

    field.Clear();

    EXPECT_EQ(field.Cursor(), 0u);
    EXPECT_FALSE(field.HasSelection());
    EXPECT_TRUE(field.Selected().empty());
}

TEST(TextFieldTest, Windows1252XpTypingUsesBestFitAndQuestionMarkSubstitution)
{
    TextField field(10, false, TextField::Charset::Windows1252Xp);

    field.Type("e\xcc\x81\xe2\x82\xac\xef\xbc\xa1\xce\x91\xf0\x9f\x98\x80");

    EXPECT_EQ(field.Value(), std::string("e\xb4\x80"
                                         "A???"));
}

TEST(TextFieldTest, MalformedWindows1252XpTypingKeepsTheSelection)
{
    TextField field(10, false, TextField::Charset::Windows1252Xp);
    field.SetText("abcd");
    field.Select(1, 3);

    field.Type("X\xc3");

    EXPECT_EQ(field.Value(), "abcd");
    EXPECT_EQ(field.Selected(), "bc");
}

TEST(TextFieldTest, InsertBytesPreservesControlBytesAndReplacesSelectionWithinLimit)
{
    TextField field(4, false, TextField::Charset::Windows1252Xp);
    field.SetText("abcd");
    field.Select(1, 3);

    field.InsertBytes(std::string("\x7f\nZ", 3));

    EXPECT_EQ(field.Value(), std::string("a\x7f\nd", 4));
    EXPECT_EQ(field.Cursor(), 3u);
    EXPECT_FALSE(field.HasSelection());
}

TEST(TextFieldTest, InsertBytesCanReplaceUsingAViewIntoItsOwnSelection)
{
    TextField field(10, false, TextField::Charset::Windows1252Xp);
    field.SetText("abcd");
    field.Select(1, 3);
    const std::string_view selected = field.Selected();

    field.InsertBytes(selected);

    EXPECT_EQ(field.Value(), "abcd");
    EXPECT_EQ(field.Cursor(), 3u);
    EXPECT_FALSE(field.HasSelection());
}

TEST(TextFieldTest, InsertBytesWithNoBytesDoesNotReplaceASelection)
{
    TextField field(10, false, TextField::Charset::Windows1252Xp);
    field.SetText("abcd");
    field.Select(1, 3);

    field.InsertBytes("");

    EXPECT_EQ(field.Value(), "abcd");
    EXPECT_EQ(field.Selected(), "bc");
    EXPECT_EQ(field.Cursor(), 3u);
}

TEST(TextFieldTest, PasteUtf8ConvertsBeforeApplyingTheByteLimit)
{
    TextField field(3, false, TextField::Charset::Windows1252Xp);
    field.SetText("x");
    field.SelectAll();

    field.PasteUTF8("e\xcc\x81\xe2\x82\xacZ");

    EXPECT_EQ(field.Value(), std::string("e\xb4\x80", 3));
    EXPECT_EQ(field.Cursor(), 3u);
}

TEST(TextFieldTest, MalformedWindows1252XpPasteKeepsTheSelection)
{
    TextField field(10, false, TextField::Charset::Windows1252Xp);
    field.SetText("abcd");
    field.Select(1, 3);

    field.PasteUTF8("X\xf0\x9f");

    EXPECT_EQ(field.Value(), "abcd");
    EXPECT_EQ(field.Selected(), "bc");
}

TEST(TextFieldTest, EmptyWindows1252XpPasteErasesTheSelection)
{
    TextField field(10, false, TextField::Charset::Windows1252Xp);
    field.SetText("abcd");
    field.Select(1, 3);

    field.PasteUTF8("");

    EXPECT_EQ(field.Value(), "ad");
    EXPECT_EQ(field.Cursor(), 1u);
    EXPECT_FALSE(field.HasSelection());
}

TEST(TextFieldTest, Windows1252XpPasteStopsAtCarriageReturnAndKeepsLineFeed)
{
    TextField field(10, false, TextField::Charset::Windows1252Xp);
    field.PasteUTF8("A\nB\rC");
    EXPECT_EQ(field.Value(), std::string("A\nB", 3));

    field.Clear();
    field.PasteUTF8("X\rY");
    EXPECT_EQ(field.Value(), "X");
}

TEST(TextFieldTest, PasteUTF8StopsAtCarriageReturnAndReplacesTheSelection)
{
    TextField field(10, false, TextField::Charset::Windows1252Xp);
    field.SetText("abcd");
    field.Select(1, 3);

    field.PasteUTF8("X\rYZ");

    EXPECT_EQ(field.Value(), "aXd");
    EXPECT_EQ(field.Cursor(), 2u);
    EXPECT_FALSE(field.HasSelection());
}

TEST(TextFieldTest, PasteUTF8StopsAtEmbeddedNullAndReplacesTheSelection)
{
    TextField field(10, false, TextField::Charset::Windows1252Xp);
    field.SetText("abcd");
    field.Select(1, 3);

    field.PasteUTF8(std::string_view("X\0YZ", 4));

    EXPECT_EQ(field.Value(), "aXd");
    EXPECT_EQ(field.Cursor(), 2u);
    EXPECT_FALSE(field.HasSelection());
}
} // namespace
