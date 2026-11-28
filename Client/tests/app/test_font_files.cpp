#include "App/FontFiles.h"

#include "App/Config.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::App::ChooseFontFiles (question 8 of the CL5 plan). The config wins; without [Video] FontFile, the system
// search (Windows', in Platform) with the face of text 0 of Text.bmd; without a search or the face, an error that
// names the key and not the face. The face in these tests is made up, and the search is a fake that records what it
// is asked.
namespace
{
using Client::App::ChooseFontFiles;
using Client::App::ClientConfig;
using Client::App::FontFiles;
using Client::App::SystemFontSearch;

constexpr std::string_view Face = "Cara de prueba";

struct Request
{
    std::string face;
    bool bold = false;

    bool operator==(const Request&) const = default;
};

// Finds the regular and the bold of `Face` if `found`, and records each request.
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

// With [Video] FontFile, the search does not run: the config's two files are used.
TEST(FontFilesTest, TheConfigWins)
{
    std::vector<Request> requests;

    const auto files = ChooseFontFiles(WithFonts("letra.ttf", "negrita.ttf"), Face, FakeSearch(requests));

    ASSERT_TRUE(files.HasValue()) << files.Error();
    EXPECT_EQ(files.Value().regular, std::filesystem::path("letra.ttf"));
    EXPECT_EQ(files.Value().bold, std::filesystem::path("negrita.ttf"));
    EXPECT_TRUE(requests.empty());
}

// Without BoldFontFile, FreeTypeRasterizer builds the bold (FT_Outline_EmboldenXY): no other one is searched.
TEST(FontFilesTest, WithoutABoldFileTheBoldIsSynthetic)
{
    std::vector<Request> requests;

    const auto files = ChooseFontFiles(WithFonts("letra.ttf", std::nullopt), Face, FakeSearch(requests));

    ASSERT_TRUE(files.HasValue()) << files.Error();
    EXPECT_EQ(files.Value().regular, std::filesystem::path("letra.ttf"));
    EXPECT_FALSE(files.Value().bold.has_value());
    EXPECT_TRUE(requests.empty());
}

// Without the key the face is searched, and its bold, as CreateFontA with weight 700 picks the face's bold if the
// system has it.
TEST(FontFilesTest, WithoutTheKeyTheSystemListIsSearched)
{
    std::vector<Request> requests;

    const auto files = ChooseFontFiles(ClientConfig{}, Face, FakeSearch(requests));

    ASSERT_TRUE(files.HasValue()) << files.Error();
    EXPECT_EQ(files.Value().regular, std::filesystem::path("sistema/normal.ttf"));
    EXPECT_EQ(files.Value().bold, std::filesystem::path("sistema/negrita.ttf"));
    EXPECT_EQ(requests, (std::vector<Request>{{std::string(Face), false}, {std::string(Face), true}}));
}

TEST(FontFilesTest, AFaceWithoutBoldInTheSystemHasTheSyntheticOne)
{
    std::vector<Request> requests;

    const auto files = ChooseFontFiles(ClientConfig{}, Face, FakeSearch(requests, true, false));

    ASSERT_TRUE(files.HasValue()) << files.Error();
    EXPECT_EQ(files.Value().regular, std::filesystem::path("sistema/normal.ttf"));
    EXPECT_FALSE(files.Value().bold.has_value());
}

// BoldFontFile counts even without FontFile: the regular comes from the system and the bold from the config.
TEST(FontFilesTest, TheBoldOfTheConfigWinsOverTheSystem)
{
    std::vector<Request> requests;

    const auto files = ChooseFontFiles(WithFonts(std::nullopt, "negrita.ttf"), Face, FakeSearch(requests));

    ASSERT_TRUE(files.HasValue()) << files.Error();
    EXPECT_EQ(files.Value().regular, std::filesystem::path("sistema/normal.ttf"));
    EXPECT_EQ(files.Value().bold, std::filesystem::path("negrita.ttf"));
    EXPECT_EQ(requests, (std::vector<Request>{{std::string(Face), false}}));
}

// Without a search (Linux), a missing key is an error that names it.
TEST(FontFilesTest, WithoutASearchTheErrorNamesTheKey)
{
    const auto files = ChooseFontFiles(ClientConfig{}, Face, SystemFontSearch{});

    ASSERT_FALSE(files.HasValue());
    EXPECT_NE(files.Error().find("[Video] FontFile"), std::string::npos) << files.Error();
    EXPECT_EQ(files.Error().find(Face), std::string::npos) << files.Error();
}

// The error names the key and not the face: the face is a game text.
TEST(FontFilesTest, AFaceThatIsNotInTheSystemIsAnErrorWithoutTheFace)
{
    std::vector<Request> requests;

    const auto files = ChooseFontFiles(ClientConfig{}, Face, FakeSearch(requests, false));

    ASSERT_FALSE(files.HasValue());
    EXPECT_NE(files.Error().find("[Video] FontFile"), std::string::npos) << files.Error();
    EXPECT_EQ(files.Error().find(Face), std::string::npos) << files.Error();
    EXPECT_EQ(requests.size(), 1u);
}

// An empty text 0 is not a face: it is not searched, and it is the same error.
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

// With the config's paths, Open's error goes out as is: it names the file the player gave.
TEST(FontFilesTest, AnOpenErrorWithTheConfigKeepsItsText)
{
    const ClientConfig config = WithFonts(std::filesystem::path("config/normal.ttf"), std::nullopt);

    EXPECT_EQ(FontOpenError(config, "no abre config/normal.ttf"), "no abre config/normal.ttf");
}

// With a path from the search, Open's error would name it, and its name can be the face's (the CL5.3 note): the
// message names the key, with nothing of Open's.
TEST(FontFilesTest, AnOpenErrorWithTheSearchNamesTheKey)
{
    const ClientConfig config = WithFonts(std::nullopt, std::filesystem::path("config/negrita.ttf"));

    const std::string error = FontOpenError(config, "no abre sistema/normal.ttf");

    EXPECT_NE(error.find("[Video] FontFile"), std::string::npos) << error;
    EXPECT_EQ(error.find("sistema/"), std::string::npos) << error;
    EXPECT_EQ(error.find("normal.ttf"), std::string::npos) << error;
}
} // namespace
