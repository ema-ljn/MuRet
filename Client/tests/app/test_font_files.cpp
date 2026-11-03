#include "App/FontFiles.h"

#include "App/Config.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::App::ChooseFontFiles: de dónde sale la fuente del texto (pregunta 8 del plan de la CL5).
// La config manda; sin [Video] FontFile, la búsqueda del sistema (la de Windows, en Platform) con
// la cara del texto 0 de Text.bmd; sin búsqueda o sin la cara, un error que nombra la clave y no la
// cara. La cara de estos tests es inventada, y la búsqueda, una falsa que anota lo que le piden.
namespace
{
using Client::App::ChooseFontFiles;
using Client::App::ClientConfig;
using Client::App::FontFiles;
using Client::App::SystemFontSearch;

constexpr std::string_view Face = "Cara de prueba";

// Lo que le pidieron a la búsqueda falsa: la cara y si era la negrita.
struct Request
{
    std::string face;
    bool bold = false;

    bool operator==(const Request&) const = default;
};

// Una búsqueda falsa: encuentra la normal y la negrita de `Face` si `found`, y anota cada pedido.
SystemFontSearch FakeSearch(std::vector<Request>& requests, bool found = true, bool hasBold = true)
{
    return [&requests, found, hasBold](std::string_view face, bool bold) -> std::optional<std::filesystem::path>
    {
        requests.push_back(Request{std::string(face), bold});
        if (!found || face != Face || (bold && !hasBold))
        {
            return std::nullopt;
        }
        return std::filesystem::path(bold ? "sistema/negrita.ttf" : "sistema/normal.ttf");
    };
}

ClientConfig WithFonts(std::optional<std::filesystem::path> regular, std::optional<std::filesystem::path> bold)
{
    ClientConfig config;
    config.fontFile = std::move(regular);
    config.boldFontFile = std::move(bold);
    return config;
}

// Con [Video] FontFile, la búsqueda no corre: van los dos archivos de la config.
TEST(FontFilesTest, TheConfigWins)
{
    std::vector<Request> requests;

    const auto files = ChooseFontFiles(WithFonts("letra.ttf", "negrita.ttf"), Face, FakeSearch(requests));

    ASSERT_TRUE(files.HasValue()) << files.Error();
    EXPECT_EQ(files.Value().regular, std::filesystem::path("letra.ttf"));
    EXPECT_EQ(files.Value().bold, std::filesystem::path("negrita.ttf"));
    EXPECT_TRUE(requests.empty());
}

// Sin BoldFontFile, la negrita la arma FreeTypeRasterizer (FT_Outline_EmboldenXY): no se busca
// otra.
TEST(FontFilesTest, WithoutABoldFileTheBoldIsSynthetic)
{
    std::vector<Request> requests;

    const auto files = ChooseFontFiles(WithFonts("letra.ttf", std::nullopt), Face, FakeSearch(requests));

    ASSERT_TRUE(files.HasValue()) << files.Error();
    EXPECT_EQ(files.Value().regular, std::filesystem::path("letra.ttf"));
    EXPECT_FALSE(files.Value().bold.has_value());
    EXPECT_TRUE(requests.empty());
}

// Sin la clave se busca la cara, y su negrita, como CreateFontA con peso 700 elige la negrita de la
// cara si el sistema la tiene.
TEST(FontFilesTest, WithoutTheKeyTheSystemListIsSearched)
{
    std::vector<Request> requests;

    const auto files = ChooseFontFiles(ClientConfig{}, Face, FakeSearch(requests));

    ASSERT_TRUE(files.HasValue()) << files.Error();
    EXPECT_EQ(files.Value().regular, std::filesystem::path("sistema/normal.ttf"));
    EXPECT_EQ(files.Value().bold, std::filesystem::path("sistema/negrita.ttf"));
    EXPECT_EQ(requests, (std::vector<Request>{{std::string(Face), false}, {std::string(Face), true}}));
}

// Una cara sin negrita en el sistema queda con la negrita armada.
TEST(FontFilesTest, AFaceWithoutBoldInTheSystemHasTheSyntheticOne)
{
    std::vector<Request> requests;

    const auto files = ChooseFontFiles(ClientConfig{}, Face, FakeSearch(requests, true, false));

    ASSERT_TRUE(files.HasValue()) << files.Error();
    EXPECT_EQ(files.Value().regular, std::filesystem::path("sistema/normal.ttf"));
    EXPECT_FALSE(files.Value().bold.has_value());
}

// BoldFontFile vale aunque falte FontFile: la normal sale del sistema y la negrita, de la config.
TEST(FontFilesTest, TheBoldOfTheConfigWinsOverTheSystem)
{
    std::vector<Request> requests;

    const auto files = ChooseFontFiles(WithFonts(std::nullopt, "negrita.ttf"), Face, FakeSearch(requests));

    ASSERT_TRUE(files.HasValue()) << files.Error();
    EXPECT_EQ(files.Value().regular, std::filesystem::path("sistema/normal.ttf"));
    EXPECT_EQ(files.Value().bold, std::filesystem::path("negrita.ttf"));
    EXPECT_EQ(requests, (std::vector<Request>{{std::string(Face), false}}));
}

// Sin búsqueda (Linux), sin la clave es un error que la nombra.
TEST(FontFilesTest, WithoutASearchTheErrorNamesTheKey)
{
    const auto files = ChooseFontFiles(ClientConfig{}, Face, SystemFontSearch{});

    ASSERT_FALSE(files.HasValue());
    EXPECT_NE(files.Error().find("[Video] FontFile"), std::string::npos) << files.Error();
    EXPECT_EQ(files.Error().find(Face), std::string::npos) << files.Error();
}

// Si la cara no está en el sistema, el error nombra la clave y no la cara: es un texto del juego.
TEST(FontFilesTest, AFaceThatIsNotInTheSystemIsAnErrorWithoutTheFace)
{
    std::vector<Request> requests;

    const auto files = ChooseFontFiles(ClientConfig{}, Face, FakeSearch(requests, false));

    ASSERT_FALSE(files.HasValue());
    EXPECT_NE(files.Error().find("[Video] FontFile"), std::string::npos) << files.Error();
    EXPECT_EQ(files.Error().find(Face), std::string::npos) << files.Error();
    EXPECT_EQ(requests.size(), 1u);
}

// Un texto 0 vacío no es una cara: no se busca, y es el mismo error.
TEST(FontFilesTest, AnEmptyFaceIsNotSearched)
{
    std::vector<Request> requests;

    const auto files = ChooseFontFiles(ClientConfig{}, "", FakeSearch(requests));

    ASSERT_FALSE(files.HasValue());
    EXPECT_NE(files.Error().find("[Video] FontFile"), std::string::npos) << files.Error();
    EXPECT_TRUE(requests.empty());
}
} // namespace

namespace
{
using Client::App::FontOpenError;

// Si Open falla con las rutas de la config, su error sale tal cual: nombra el archivo que dio el
// jugador.
TEST(FontFilesTest, AnOpenErrorWithTheConfigKeepsItsText)
{
    const ClientConfig config = WithFonts(std::filesystem::path("config/normal.ttf"), std::nullopt);

    EXPECT_EQ(FontOpenError(config, "no abre config/normal.ttf"), "no abre config/normal.ttf");
}

// Con una ruta de la búsqueda, el error de Open la nombraría, y su nombre puede ser el de la cara
// (la nota de la CL5.3): sale uno que nombra la clave, sin nada del de Open.
TEST(FontFilesTest, AnOpenErrorWithTheSearchNamesTheKey)
{
    const ClientConfig config = WithFonts(std::nullopt, std::filesystem::path("config/negrita.ttf"));

    const std::string error = FontOpenError(config, "no abre sistema/normal.ttf");

    EXPECT_NE(error.find("[Video] FontFile"), std::string::npos) << error;
    EXPECT_EQ(error.find("sistema/"), std::string::npos) << error;
    EXPECT_EQ(error.find("normal.ttf"), std::string::npos) << error;
}
} // namespace
