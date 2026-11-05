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
} // namespace
