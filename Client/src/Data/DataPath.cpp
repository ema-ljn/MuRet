#include "Data/DataPath.h"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace
{
// Solo A-Z. std::tolower depende del locale de C y es comportamiento indefinido con un char
// negativo (cualquier byte >= 0x80 de un nombre en UTF-8).
char ToLowerAscii(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// operator== de string_view compara bytes exactos, y la biblioteca estándar no trae una
// comparación sin mayúsculas. ranges::equal sí deja proyectar: pasa cada char de los dos lados
// por ToLowerAscii antes del ==, y si los tamaños difieren devuelve false sin comparar nada.
bool EqualsIgnoringAsciiCase(std::string_view a, std::string_view b)
{
    return std::ranges::equal(a, b, std::ranges::equal_to{}, ToLowerAscii, ToLowerAscii);
}

bool IsSeparator(char c)
{
    return c == '\\' || c == '/';
}

// Como una cadena de C: hasta el primer '\0', si hay.
std::string_view UpToFirstNul(std::string_view text)
{
    return text.substr(0, text.find('\0'));
}

// "\\x", "/x" y "C:\\x" no son rutas relativas a Data\, y "x\\" nombra una carpeta, no un archivo.
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

// Los nombres del original son bytes; en std::filesystem van como UTF-8. Sin el u8string, en
// Windows un std::string se interpretaría con la página de códigos del sistema.
std::filesystem::path PathFromBytes(std::string_view bytes)
{
    return std::filesystem::path(std::u8string(bytes.begin(), bytes.end()));
}

// El nombre tal como está en el disco, en bytes (UTF-8 también en Windows).
std::string FileNameBytes(const std::filesystem::path& path)
{
    const std::u8string name = path.filename().u8string();
    return std::string(name.begin(), name.end());
}

// Recorre `directory` buscando `name` sin distinguir mayúsculas; si hay varias, la menor en bytes.
// Un symlink roto aparece al recorrer pero no existe: no cuenta.
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
        if (!EqualsIgnoringAsciiCase(entryName, name) || (found && entryName >= foundName))
        {
            continue;
        }
        // La función libre, no directory_entry::exists: esa deja `error` puesto cuando el
        // destino del symlink no existe, y eso no es un error de E/S.
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

// Busca `name` dentro de `directory`: primero tal cual y, si no está, sin distinguir mayúsculas.
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
    // "." o ".\." nombran la carpeta Data\ misma, no algo adentro.
    if (!resolvedAny)
    {
        return std::nullopt;
    }
    return current;
}
} // namespace Client::Data
