#pragma once

#include "App/Config.h"
#include "Core/Expected.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

// The original asks GDI for WinMain's four fonts by name, with the face of text 0 of Text.bmd (and that of 18 for the
// fourth); FreeType needs a file (CL5.3). [Video] FontFile wins, and BoldFontFile if present; without FontFile, the
// face is looked up in the system font list, which only Windows has (Platform::Sdl::FindSystemFontFile). The face is
// a game text: it goes into no message, nor does the path the search returns, since the file name can be the face's.
namespace Client::App
{
// For FreeTypeRasterizer::Open. Without `bold`, the bold is built from the regular one.
struct FontFiles
{
    std::filesystem::path regular;
    std::optional<std::filesystem::path> bold;
};

// Finds the file of a face, regular or bold, among the system fonts. Empty where there is no list (Linux).
using SystemFontSearch = std::function<std::optional<std::filesystem::path>(std::string_view face, bool bold)>;

// The config's files; without FontFile, the ones `search` finds for `face` (the bold only if BoldFontFile is
// missing). An error, without naming the face, if there is no FontFile and the face is not found, is empty or there
// is no search: the message names the key.
[[nodiscard]] Core::Expected<FontFiles, std::string> ChooseFontFiles(const ClientConfig& config, std::string_view face,
                                                                     const SystemFontSearch& search);

// For the log, the error of FreeTypeRasterizer::Open with ChooseFontFiles' files. With [Video] FontFile they are the
// config's, and `error` goes out as is (it names the file the player gave). Without FontFile, the path came from the
// search and its name can be the face's: the message names the key and carries nothing of `error`.
[[nodiscard]] std::string FontOpenError(const ClientConfig& config, std::string_view error);
} // namespace Client::App
