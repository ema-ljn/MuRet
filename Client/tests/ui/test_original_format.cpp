#include "UI/OriginalFormat.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

// Client::UI::FormatOriginal (CL9 hito 11): el sprintf de la CRT (0x69fd9b) y wsprintfA ([0x6b851c]) con los
// argumentos que pasa el original, para los textos de Text.bmd que usan de formato los paquetes de los eventos.
namespace
{
using Client::UI::FormatArgument;
using Client::UI::FormatFunction;
using Client::UI::FormatOriginal;
using Client::UI::TextArgument;

std::string Formatted(FormatFunction function, std::string_view format, const std::vector<FormatArgument>& arguments)
{
    const auto formatted = FormatOriginal(function, format, arguments, "test");
    EXPECT_TRUE(formatted.HasValue()) << formatted.Error();
    return formatted.HasValue() ? formatted.Value() : std::string{};
}

// El texto 828 de Chaos Castle con 1147: el nombre por puntero y los segundos por valor.
TEST(OriginalFormatTest, ATextGoesByPointerAndANumberByValue)
{
    const std::vector<FormatArgument> arguments{TextArgument{.index = 1147, .text = "Chaos Castle"}, 18};
    EXPECT_EQ(Formatted(FormatFunction::Wsprintf, "%s abrira (en %d segundos)", arguments),
              "Chaos Castle abrira (en 18 segundos)");
    EXPECT_EQ(Formatted(FormatFunction::Sprintf, "%s abrira (en %d segundos)", arguments),
              "Chaos Castle abrira (en 18 segundos)");
}

// 0x69fd9b: "% d" pone un espacio delante de un número sin signo y "% s" no cambia nada.
TEST(OriginalFormatTest, SprintfTakesTheSpaceFlag)
{
    const std::vector<FormatArgument> arguments{TextArgument{.index = 1146, .text = "Blood Castle"}, 10};
    EXPECT_EQ(Formatted(FormatFunction::Sprintf, "% s cierra en% d", arguments), "Blood Castle cierra en 10");
    EXPECT_EQ(Formatted(FormatFunction::Sprintf, "% d", {-3}), "-3");
}

// P-C290 (inferido): wsprintfA no tiene la bandera ' ': copia el espacio, no toma argumento y la letra sale como
// texto. "% s ... (en% d segundos)" de los textos 821..825 queda sin el nombre ni los segundos.
TEST(OriginalFormatTest, WsprintfCopiesTheSpaceAndTakesNoArgument)
{
    const std::vector<FormatArgument> arguments{TextArgument{.index = 1146, .text = "Blood Castle"}, 30};
    EXPECT_EQ(Formatted(FormatFunction::Wsprintf, "% s invasion (en% d segundos) ", arguments),
              " s invasion (en d segundos) ");
}

// Q-C1321: un "%d" que toma un texto imprime su dirección, 0x76bd1dc + índice · 300 (853 con 1146 en el 9A).
TEST(OriginalFormatTest, ANumberConversionOfATextPrintsItsAddress)
{
    const std::vector<FormatArgument> arguments{TextArgument{.index = 1146, .text = "Blood Castle"}, 10};
    const std::uint32_t address = Client::UI::TextTableAddress + 1146u * Client::UI::TextRecordBytes;
    EXPECT_EQ(Formatted(FormatFunction::Sprintf, "%d y %d", arguments), std::to_string(address) + " y 10");
    EXPECT_EQ(Formatted(FormatFunction::Sprintf, "%X", arguments), "77110D4");
    EXPECT_EQ(address, 0x077110d4u);
}

// El ancho, la precisión, el cero, la izquierda, el '#' y el %%.
TEST(OriginalFormatTest, KeepsWidthPrecisionAndFlags)
{
    EXPECT_EQ(Formatted(FormatFunction::Sprintf, "[%5d][%-4d][%03d][%.3d][%#x][%%]", {42, 7, 5, 9, 255}),
              "[   42][7   ][005][009][0xff][%]");
    EXPECT_EQ(Formatted(FormatFunction::Wsprintf, "[%.2s][%c]", {TextArgument{.index = 0, .text = "abc"}, 0x41}),
              "[ab][A]");
}

// Lo que el original haría con la pila o con un puntero que no es corta con error.
TEST(OriginalFormatTest, StopsWhereTheOriginalReadsGarbage)
{
    const auto missing = FormatOriginal(FormatFunction::Sprintf, "%d %d", std::vector<FormatArgument>{1}, "aquí");
    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("aquí"), std::string::npos) << missing.Error();
    const auto pointer = FormatOriginal(FormatFunction::Sprintf, "%s", std::vector<FormatArgument>{1}, "aquí");
    ASSERT_FALSE(pointer.HasValue());
    const auto unknown = FormatOriginal(FormatFunction::Sprintf, "%f", std::vector<FormatArgument>{1}, "aquí");
    ASSERT_FALSE(unknown.HasValue());
    EXPECT_EQ(Formatted(FormatFunction::Wsprintf, "%f", {}), "f");
}
} // namespace
