#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "EngineApi.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// The textures a scene uploads on entry, each with its slot in the original's table and its sampling
// (Client/docs/research/05-formatos-de-datos.md, "Texturas"; 06, the loading and the login). A set is data (a table
// per scene or world), never a list in the code that uses it.
namespace Client::Data::Textures
{
// 0x63b870, the OZJ's, or 0x63bd00, the OZT's. The caller picks by what it asks for: "<name>.jpg" the first and
// "<name>.tga" the second.
enum class TextureLoader : std::uint8_t
{
    Ozj,
    Ozt,
};

// As the original asks for it ("Logo\\Login_Back01.jpg"), its slot and its sampling (the loader's third and fourth
// arguments).
struct TextureSetEntry
{
    std::string_view path;
    std::uint16_t slot = 0;
    Sampling sampling{};
};

enum class TextureSetErrorKind : std::uint8_t
{
    UnknownFormat, // not .jpg or .tga: an error in the table, not on disk
    Missing,       // the original builds "File not exist", MessageBox and WM_DESTROY (Q-C23)
    Unreadable,    // present but unreadable; the original skips the upload silently (Q-C14, Q-C29)
};

struct TextureSetError
{
    TextureSetErrorKind kind = TextureSetErrorKind::Missing;
    // As the original asks for it: the entry's.
    std::string path;
    // With Unreadable, the loader's error.
    std::optional<DataError> data;
    // With Missing or Unreadable, what the system said while finding or reading it, if anything: a folder without
    // permission, an I/O failure.
    std::error_code system;
};

struct LoadedTexture
{
    std::uint16_t slot = 0;
    Sampling sampling{};
    Texture texture;
};

// By extension, ignoring case: ".jpg" the OZJ and ".tga" the OZT; any other, or none, std::nullopt. The scene tables'
// rule, where each call of the original names its loader and the extension matches. A model's textures (0x60d5f0)
// follow another: the letter after the first dot, 't' or 'T' the OZT and any other the OZJ.
[[nodiscard]] ENGINE_API std::optional<TextureLoader> TextureLoaderOf(std::string_view originalPath);

// The file the loader opens, as 0x63b870 and 0x63bd00 do with Data\: the name up to and including the first dot, and
// the loader's extension. "Monster\\bossmap1_R.jpg.jpg" opens "Monster\\bossmap1_R.OZJ" (09 § El Data\). Without a
// dot the original never writes the byte before the extension and it keeps whatever was on the stack (Q-C74); here
// it is a dot.
[[nodiscard]] ENGINE_API std::string TextureFileName(std::string_view originalPath, TextureLoader loader);

// In order, each file found with ResolveDataPath (ignoring case) and read with its extension's loader. The first that
// is not .jpg or .tga, is missing or is unreadable stops it.
[[nodiscard]] ENGINE_API Core::Expected<std::vector<LoadedTexture>, TextureSetError>
LoadTextureSet(const std::filesystem::path& dataRoot, std::span<const TextureSetEntry> entries);

// For the log: what happened and with which path, without bytes of the file.
[[nodiscard]] ENGINE_API std::string Describe(const TextureSetError& error);
} // namespace Client::Data::Textures
