#pragma once

#include "PlatformApi.h"

#include <filesystem>
#include <optional>
#include <string_view>

// For App::ChooseFontFiles without [Video] FontFile. The original asks CreateFontA for the face by name; MuRet looks
// for its file in the list GDI reads: HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts, and the user's in HKCU
// (fonts installed without admin rights, since Windows 10 1809). Windows only: other systems have no list. The face is
// game text and goes into no message.
namespace Client::Platform::Sdl
{
// "<face> (TrueType)" or "<face> Bold (TrueType)", also "(OpenType)", and in a collection any of the faces separated
// by " & ". ASCII case-insensitive, like GDI's face name. A bitmap font (without those types) or an empty face never
// count.
[[nodiscard]] PLATFORM_API bool FontEntryNamesFace(std::string_view entryName, std::string_view face, bool bold);

// Windows only. Without it, main.cpp passes App::ChooseFontFiles no search, and its error says the client does not
// look for the face, rather than that it did not find it.
[[nodiscard]] PLATFORM_API bool HasSystemFontList();

// The first value naming it, in HKLM and then HKCU, with Windows' Fonts folder in front if the value has no full path.
// std::nullopt if missing, if the face is empty or outside Windows.
[[nodiscard]] PLATFORM_API std::optional<std::filesystem::path> FindSystemFontFile(std::string_view face, bool bold);
} // namespace Client::Platform::Sdl
