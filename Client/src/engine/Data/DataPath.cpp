#include "Data/DataPath.h"

#include "Core/Ascii.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
bool IsSeparator(char c)
{
    return c == '\\' || c == '/';
}

std::string_view UpToFirstNul(std::string_view text)
{
    return text.substr(0, text.find('\0'));
}

// "\\x", "/x" and "C:\\x" are not relative to Data\, and "x\\" names a folder, not a file.
bool IsDataRelative(std::string_view path)
{
    return !path.empty() && !IsSeparator(path.front()) && !IsSeparator(path.back()) &&
           path.find(':') == std::string_view::npos;
}

std::vector<std::string_view> SplitComponents(std::string_view path)
{
    std::vector<std::string_view> components;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= path.size(); ++i)
    {
        if (i == path.size() || IsSeparator(path[i]))
        {
            if (i > start)
            {
                components.push_back(path.substr(start, i - start));
            }
            start = i + 1;
        }
    }
    return components;
}

// The original's names are bytes; std::filesystem takes them as UTF-8. Without the u8string, Windows would read a
// std::string with the system code page.
std::filesystem::path PathFromBytes(std::string_view bytes)
{
    return std::filesystem::path(std::u8string(bytes.begin(), bytes.end()));
}

// The on-disk name as bytes (UTF-8 on Windows too).
std::string FileNameBytes(const std::filesystem::path& path)
{
    const std::u8string name = path.filename().u8string();
    return std::string(name.begin(), name.end());
}

// With several matches, the lowest in bytes. A broken symlink shows up in the walk but does not exist, so it does not
// count.
std::optional<std::filesystem::path> FindIgnoringCase(const std::filesystem::path& directory, std::string_view name,
                                                      std::error_code& error)
{
    std::optional<std::filesystem::path> found;
    std::string foundName;
    std::filesystem::directory_iterator entries(directory, error);
    const std::filesystem::directory_iterator end;
    for (; !error && entries != end; entries.increment(error))
    {
        const std::string entryName = FileNameBytes(entries->path());
        if (!Client::Core::EqualsIgnoringAsciiCase(entryName, name) || (found && entryName >= foundName))
        {
            continue;
        }
        // The free function, not directory_entry::exists: that one leaves `error` set when the symlink's target does
        // not exist, which is not an I/O error.
        const bool entryExists = std::filesystem::exists(entries->path(), error);
        if (error)
        {
            return std::nullopt;
        }
        if (entryExists)
        {
            found = entries->path();
            foundName = entryName;
        }
    }
    if (error)
    {
        return std::nullopt;
    }
    return found;
}

std::optional<std::filesystem::path> FindEntry(const std::filesystem::path& directory, std::string_view name,
                                               std::error_code& error)
{
    const std::filesystem::path exact = directory / PathFromBytes(name);
    if (std::filesystem::exists(exact, error))
    {
        return exact;
    }
    if (error)
    {
        return std::nullopt;
    }
    return FindIgnoringCase(directory, name, error);
}
} // namespace

namespace Client::Data
{
std::optional<std::filesystem::path> ResolveDataPath(const std::filesystem::path& dataRoot,
                                                     std::string_view originalPath, std::error_code& error)
{
    error.clear();
    if (dataRoot.empty())
    {
        error = std::make_error_code(std::errc::invalid_argument);
        return std::nullopt;
    }
    const std::string_view path = UpToFirstNul(originalPath);
    if (!IsDataRelative(path))
    {
        return std::nullopt;
    }

    std::filesystem::path current = dataRoot;
    bool resolvedAny = false;
    for (const std::string_view component : SplitComponents(path))
    {
        if (component == ".")
        {
            continue;
        }
        if (component == "..")
        {
            return std::nullopt;
        }
        std::optional<std::filesystem::path> next = FindEntry(current, component, error);
        if (!next)
        {
            return std::nullopt;
        }
        current = std::move(*next);
        resolvedAny = true;
    }
    // "." or ".\." name the Data\ folder itself, not something inside it.
    if (!resolvedAny)
    {
        return std::nullopt;
    }
    return current;
}
} // namespace Client::Data
