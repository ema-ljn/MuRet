#include "Data/DataPath.h"

#include <algorithm>
#include <string>
#include <system_error>
#include <vector>

namespace
{

char ToLowerAscii(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool EqualsIgnoringAsciiCase(std::string_view a, std::string_view b)
{
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return ToLowerAscii(x) == ToLowerAscii(y); });
}

bool IsSeparator(char c)
{
    return c == '\\' || c == '/';
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

// El nombre tal como está en el disco, en bytes (UTF-8 también en Windows).
std::string FileNameBytes(const std::filesystem::path& path)
{
    const std::u8string name = path.filename().u8string();
    return std::string(name.begin(), name.end());
}

// Busca `name` dentro de `directory`: primero tal cual y, si no está, sin distinguir mayúsculas.
std::optional<std::filesystem::path> FindEntry(const std::filesystem::path& directory, std::string_view name)
{
    std::error_code error;
    const std::filesystem::path exact = directory / std::filesystem::path(std::string(name));
    if (std::filesystem::exists(exact, error))
    {
        return exact;
    }

    std::optional<std::filesystem::path> found;
    std::string foundName;
    std::filesystem::directory_iterator entries(directory, error);
    const std::filesystem::directory_iterator end;
    for (; !error && entries != end; entries.increment(error))
    {
        const std::string entryName = FileNameBytes(entries->path());
        if (EqualsIgnoringAsciiCase(entryName, name) && (!found || entryName < foundName))
        {
            found = entries->path();
            foundName = entryName;
        }
    }
    return found;
}

} // namespace

namespace Client::Data
{

std::optional<std::filesystem::path> ResolveDataPath(const std::filesystem::path& dataRoot,
                                                     std::string_view originalPath)
{
    // "\\x", "/x" y "C:\\x" no son rutas relativas a Data\.
    if (originalPath.empty() || IsSeparator(originalPath.front()) || originalPath.find(':') != std::string_view::npos)
    {
        return std::nullopt;
    }

    std::filesystem::path current = dataRoot;
    for (const std::string_view component : SplitComponents(originalPath))
    {
        if (component == ".")
        {
            continue;
        }
        if (component == "..")
        {
            return std::nullopt;
        }
        const std::optional<std::filesystem::path> next = FindEntry(current, component);
        if (!next)
        {
            return std::nullopt;
        }
        current = *next;
    }
    return current;
}

} // namespace Client::Data
