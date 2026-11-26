#include "Data/Textures/TextureSet.h"

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Textures/OzjTexture.h"
#include "Data/Textures/OztTexture.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "support/JpegVectors.h"
#include "support/TemporaryFolder.h"
#include "support/TextureFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The name with the loader's extension (0x63b870 and 0x63bd00), found ignoring case. Synthetic files in a temporary
// folder; the golden test of scenes/ covers the lab's Data\.
namespace
{
using Client::Data::DataError;
using Client::Data::Textures::Filter;
using Client::Data::Textures::LoadedTexture;
using Client::Data::Textures::LoadOzjTexture;
using Client::Data::Textures::LoadOztTexture;
using Client::Data::Textures::LoadTextureSet;
using Client::Data::Textures::PixelFormat;
using Client::Data::Textures::Sampling;
using Client::Data::Textures::Texture;
using Client::Data::Textures::TextureFileName;
using Client::Data::Textures::TextureLoader;
using Client::Data::Textures::TextureLoaderOf;
using Client::Data::Textures::TextureSetEntry;
using Client::Data::Textures::TextureSetError;
using Client::Data::Textures::TextureSetErrorKind;
using Client::Data::Textures::Wrap;
using Client::Tests::OzjOf;
using Client::Tests::PlainOzt;
using Client::Tests::ReadJpegVector;
using Client::Tests::TemporaryFolder;

namespace fs = std::filesystem;

void WriteFile(const fs::path& root, const fs::path& relative, std::span<const std::byte> bytes)
{
    const fs::path path = root / relative;
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    for (const std::byte value : bytes)
    {
        file.put(static_cast<char>(value));
    }
    ASSERT_TRUE(file.good()) << "no se pudo escribir " << relative.generic_string();
}

// To compare with what the set brings.
Texture Loaded(Client::Core::Expected<Texture, DataError> texture)
{
    EXPECT_TRUE(texture.HasValue());
    return texture.HasValue() ? std::move(texture).Value() : Texture{};
}

// Or the test's failure if it loaded.
TextureSetError ErrorOf(const Client::Core::Expected<std::vector<LoadedTexture>, TextureSetError>& loaded)
{
    EXPECT_FALSE(loaded.HasValue());
    return loaded.HasValue() ? TextureSetError{} : loaded.Error();
}

// The original cuts the name at the first dot and adds its loader's extension: OZJ for what it asks as .jpg, OZT for
// .tga (09 § El Data\, with Monster\bossmap1_R.jpg.jpg).
TEST(TextureSetTest, ChangesTheExtensionLikeTheOriginal)
{
    EXPECT_EQ(TextureLoaderOf("Logo\\Login_Back01.jpg"), TextureLoader::Ozj);
    EXPECT_EQ(TextureLoaderOf("Interface\\cha_bt.TGA"), TextureLoader::Ozt);
    EXPECT_EQ(TextureLoaderOf("Interface\\deco.Tga"), TextureLoader::Ozt);
    EXPECT_EQ(TextureLoaderOf("Monster\\bossmap1_R.jpg.jpg"), TextureLoader::Ozj);
    // No texture loader asks for another: a .bmp is a BMP (OZB), and without an extension there is none.
    EXPECT_EQ(TextureLoaderOf("Interface\\deco.bmp"), std::nullopt);
    EXPECT_EQ(TextureLoaderOf("Interface\\deco"), std::nullopt);
    EXPECT_EQ(TextureLoaderOf("Interface\\deco.jpgx"), std::nullopt);

    EXPECT_EQ(TextureFileName("Logo\\Login_Back01.jpg", TextureLoader::Ozj), "Logo\\Login_Back01.OZJ");
    EXPECT_EQ(TextureFileName("Interface\\cha_bt.TGA", TextureLoader::Ozt), "Interface\\cha_bt.OZT");
    EXPECT_EQ(TextureFileName("Monster\\bossmap1_R.jpg.jpg", TextureLoader::Ozj), "Monster\\bossmap1_R.OZJ");
}

// Without a dot the original leaves the byte before the extension unwritten and the name carries whatever was on the
// stack (Q-C74): MuRet puts the dot, as if there were one.
TEST(TextureSetTest, ANameWithoutADotGetsOne)
{
    EXPECT_EQ(TextureFileName("Interface\\deco", TextureLoader::Ozj), "Interface\\deco.OZJ");
    EXPECT_EQ(TextureFileName("Interface\\deco", TextureLoader::Ozt), "Interface\\deco.OZT");
}

TEST(TextureSetTest, FindsTheFileInAnyCase)
{
    const TemporaryFolder data("textureset-case");
    WriteFile(data.Path(), "interface/cha_bt.ozt", PlainOzt(2, 2));
    const std::array entries{TextureSetEntry{"Interface\\cha_bt.tga", 0x0c, {}}};

    const auto loaded = LoadTextureSet(data.Path(), entries);

    ASSERT_TRUE(loaded.HasValue()) << Describe(loaded.Error());
    EXPECT_EQ(loaded.Value().size(), 1u);
}

// Like "File not exist" in the original (Q-C23), with the requested path.
TEST(TextureSetTest, AMissingFileSaysItsPath)
{
    const TemporaryFolder data("textureset-missing");
    WriteFile(data.Path(), "Interface/cha_bt.OZT", PlainOzt(2, 2));
    const std::array entries{TextureSetEntry{"Interface\\cha_bt.tga", 0x0c, {}},
                             TextureSetEntry{"Logo\\Login_Back01.jpg", 0x15, {}}};

    const TextureSetError error = ErrorOf(LoadTextureSet(data.Path(), entries));

    EXPECT_EQ(error.kind, TextureSetErrorKind::Missing);
    EXPECT_EQ(error.path, "Logo\\Login_Back01.jpg");
    EXPECT_FALSE(error.system);
    EXPECT_NE(Describe(error).find("Logo\\Login_Back01.jpg"), std::string::npos) << Describe(error);
}

// The original skips the upload silently (Q-C29, the 24-bit OZT).
TEST(TextureSetTest, AnUnreadableFileSaysWhy)
{
    const TemporaryFolder data("textureset-unreadable");
    WriteFile(data.Path(), "Monster/wolfa02.OZT", PlainOzt(2, 2, 24));
    const std::array entries{TextureSetEntry{"Monster\\wolfa02.tga", 0x30, {}}};

    const TextureSetError error = ErrorOf(LoadTextureSet(data.Path(), entries));

    EXPECT_EQ(error.kind, TextureSetErrorKind::Unreadable);
    EXPECT_EQ(error.path, "Monster\\wolfa02.tga");
    EXPECT_EQ(error.data, DataError::InvalidData);
    EXPECT_NE(Describe(error).find("Monster\\wolfa02.tga"), std::string::npos) << Describe(error);
}

// An error in the table, not on disk: it is not looked up.
TEST(TextureSetTest, AnUnknownExtensionIsRefused)
{
    const TemporaryFolder data("textureset-unknown");
    WriteFile(data.Path(), "Interface/deco.bmp", PlainOzt(2, 2));
    const std::array entries{TextureSetEntry{"Interface\\deco.bmp", 0x12, {}}};

    const TextureSetError error = ErrorOf(LoadTextureSet(data.Path(), entries));

    EXPECT_EQ(error.kind, TextureSetErrorKind::UnknownFormat);
    EXPECT_EQ(error.path, "Interface\\deco.bmp");
}

// The OZJ in RGB and the OZT in RGBA.
TEST(TextureSetTest, LoadsInOrder)
{
    const TemporaryFolder data("textureset-order");
    const std::vector<std::byte> ozt = PlainOzt(3, 2);
    const std::vector<std::byte> ozj = OzjOf(ReadJpegVector("baseline-420.jpg"));
    WriteFile(data.Path(), "Interface/login_me.OZT", ozt);
    WriteFile(data.Path(), "Object9/sand01.OZJ", ozj);
    const Sampling sand{Filter::Linear, Wrap::Repeat};
    const std::array entries{TextureSetEntry{"Object9\\sand01.jpg", 0x5c2, sand},
                             TextureSetEntry{"Interface\\login_me.tga", 0x14, {}}};

    const auto loaded = LoadTextureSet(data.Path(), entries);

    ASSERT_TRUE(loaded.HasValue()) << Describe(loaded.Error());
    const std::vector<LoadedTexture>& textures = loaded.Value();
    ASSERT_EQ(textures.size(), 2u);
    const Texture expectedSand = Loaded(LoadOzjTexture(ozj));
    EXPECT_EQ(textures[0].slot, 0x5c2);
    EXPECT_EQ(textures[0].sampling.filter, Filter::Linear);
    EXPECT_EQ(textures[0].sampling.wrap, Wrap::Repeat);
    EXPECT_EQ(textures[0].texture.format, PixelFormat::Rgb);
    EXPECT_EQ(textures[0].texture.width, expectedSand.width);
    EXPECT_EQ(textures[0].texture.height, expectedSand.height);
    EXPECT_TRUE(textures[0].texture.pixels == expectedSand.pixels);
    const Texture expectedMe = Loaded(LoadOztTexture(ozt));
    EXPECT_EQ(textures[1].slot, 0x14);
    EXPECT_EQ(textures[1].sampling.filter, Filter::Nearest);
    EXPECT_EQ(textures[1].sampling.wrap, Wrap::Clamp);
    EXPECT_EQ(textures[1].texture.format, PixelFormat::Rgba);
    EXPECT_EQ(textures[1].texture.width, 3);
    EXPECT_EQ(textures[1].texture.height, 2);
    EXPECT_TRUE(textures[1].texture.pixels == expectedMe.pixels);
}

TEST(TextureSetTest, AnEmptySetLoadsNothing)
{
    const TemporaryFolder data("textureset-empty");

    const auto loaded = LoadTextureSet(data.Path(), std::span<const TextureSetEntry>{});

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_TRUE(loaded.Value().empty());
}
} // namespace
