#include "Platform/Sdl/SystemFonts.h"

#include "Core/Ascii.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#ifdef _WIN32
// No min and max macros from <windows.h>, which clobber std::min, and nothing the registry does not need.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <vector>
#endif

namespace Client::Platform::Sdl
{
namespace
{
using namespace std::string_view_literals;

// The list's types that have outlines. .fon entries say something else, or nothing.
constexpr std::array OutlineSuffixes{" (TrueType)"sv, " (OpenType)"sv};
constexpr std::string_view FaceSeparator = " & ";
constexpr std::string_view BoldSuffix = " Bold";

bool EndsWithIgnoringAsciiCase(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() &&
           Core::EqualsIgnoringAsciiCase(text.substr(text.size() - suffix.size()), suffix);
}
} // namespace

bool FontEntryNamesFace(std::string_view entryName, std::string_view face, bool bold)
{
    if (face.empty())
    {
        return false;
    }
    std::string_view names;
    bool outline = false;
    for (const std::string_view suffix : OutlineSuffixes)
    {
        if (EndsWithIgnoringAsciiCase(entryName, suffix))
        {
            names = entryName.substr(0, entryName.size() - suffix.size());
            outline = true;
            break;
        }
    }
    if (!outline)
    {
        return false;
    }
    std::string wanted(face);
    if (bold)
    {
        wanted += BoldSuffix;
    }
    while (true)
    {
        const std::size_t separator = names.find(FaceSeparator);
        if (Core::EqualsIgnoringAsciiCase(names.substr(0, separator), wanted))
        {
            return true;
        }
        if (separator == std::string_view::npos)
        {
            return false;
        }
        names.remove_prefix(separator + FaceSeparator.size());
    }
}

#ifdef _WIN32
namespace
{
constexpr const char* FontsKey = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts";

class RegistryKey
{
public:
    explicit RegistryKey(HKEY root)
    {
        if (RegOpenKeyExA(root, FontsKey, 0, KEY_QUERY_VALUE, &m_key) != ERROR_SUCCESS)
        {
            m_key = nullptr;
        }
    }
    ~RegistryKey()
    {
        if (m_key != nullptr)
        {
            RegCloseKey(m_key);
        }
    }
    RegistryKey(const RegistryKey&) = delete;
    RegistryKey& operator=(const RegistryKey&) = delete;
    RegistryKey(RegistryKey&&) = delete;
    RegistryKey& operator=(RegistryKey&&) = delete;

    [[nodiscard]] HKEY Get() const
    {
        return m_key;
    }

private:
    HKEY m_key = nullptr;
};

// Windows' Fonts folder, where the files the HKLM list names without a path are.
std::filesystem::path FontsFolder()
{
    std::array<char, MAX_PATH + 1> windows{};
    const UINT length = GetWindowsDirectoryA(windows.data(), static_cast<UINT>(windows.size()));
    if (length == 0 || length >= windows.size())
    {
        return std::filesystem::path("C:\\Windows\\Fonts");
    }
    return std::filesystem::path(std::string(windows.data(), length)) / "Fonts";
}

// With the registry's A functions, as CreateFontA receives the face: names and paths in the system code page, which
// std::filesystem::path uses with MSVC for a std::string.
std::optional<std::filesystem::path> SearchList(HKEY root, std::string_view face, bool bold)
{
    const RegistryKey key(root);
    if (key.Get() == nullptr)
    {
        return std::nullopt;
    }
    DWORD count = 0;
    DWORD maxNameLength = 0;
    DWORD maxDataBytes = 0;
    if (RegQueryInfoKeyA(key.Get(), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &count, &maxNameLength,
                         &maxDataBytes, nullptr, nullptr) != ERROR_SUCCESS)
    {
        return std::nullopt;
    }
    std::vector<char> name(static_cast<std::size_t>(maxNameLength) + 1);
    std::vector<BYTE> data(static_cast<std::size_t>(maxDataBytes) + 1);
    for (DWORD index = 0; index < count; ++index)
    {
        auto nameLength = static_cast<DWORD>(name.size());
        auto dataBytes = static_cast<DWORD>(data.size() - 1);
        DWORD type = 0;
        if (RegEnumValueA(key.Get(), index, name.data(), &nameLength, nullptr, &type, data.data(), &dataBytes) !=
                ERROR_SUCCESS ||
            (type != REG_SZ && type != REG_EXPAND_SZ))
        {
            continue;
        }
        if (!FontEntryNamesFace(std::string_view(name.data(), nameLength), face, bold))
        {
            continue;
        }
        // REG_SZ may or may not carry its trailing 0.
        std::string file(reinterpret_cast<const char*>(data.data()), dataBytes);
        while (!file.empty() && file.back() == '\0')
        {
            file.pop_back();
        }
        if (file.empty())
        {
            continue;
        }
        std::filesystem::path path(file);
        if (path.is_relative())
        {
            path = FontsFolder() / path;
        }
        return path;
    }
    return std::nullopt;
}
} // namespace

bool HasSystemFontList()
{
    return true;
}

std::optional<std::filesystem::path> FindSystemFontFile(std::string_view face, bool bold)
{
    if (face.empty())
    {
        return std::nullopt;
    }
    if (std::optional<std::filesystem::path> found = SearchList(HKEY_LOCAL_MACHINE, face, bold))
    {
        return found;
    }
    return SearchList(HKEY_CURRENT_USER, face, bold);
}
#else
bool HasSystemFontList()
{
    return false;
}

std::optional<std::filesystem::path> FindSystemFontFile(std::string_view /*face*/, bool /*bold*/)
{
    return std::nullopt;
}
#endif
} // namespace Client::Platform::Sdl
