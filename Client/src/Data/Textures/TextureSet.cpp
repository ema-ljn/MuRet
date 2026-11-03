#include "Data/Textures/TextureSet.h"

#include "Data/DataFile.h"
#include "Data/DataPath.h"
#include "Data/Textures/OzjTexture.h"
#include "Data/Textures/OztTexture.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <utility>

namespace Client::Data::Textures
{
namespace
{
using Core::Unexpected;

// Más que cualquier textura del laboratorio (un OZT de 512 × 512 son 1 MB y poco): un archivo más
// grande no es una textura, y no se lee entero a memoria.
constexpr std::uintmax_t MaxTextureFileBytes = 16u * 1024u * 1024u;

// La extensión que le pone cada cargador al nombre (DAT_006d6538 y DAT_006d6570), sin el punto.
constexpr std::string_view LoaderExtension(TextureLoader loader)
{
    return loader == TextureLoader::Ozj ? "OZJ" : "OZT";
}

TextureSetError MakeError(TextureSetErrorKind kind, std::string_view path)
{
    return TextureSetError{kind, std::string(path), std::nullopt, {}};
}

// Busca, lee y carga una entrada.
Core::Expected<LoadedTexture, TextureSetError> LoadEntry(const std::filesystem::path& dataRoot,
                                                         const TextureSetEntry& entry)
{
    const std::optional<TextureLoader> loader = TextureLoaderOf(entry.path);
    if (!loader.has_value())
    {
        return Unexpected(MakeError(TextureSetErrorKind::UnknownFormat, entry.path));
    }
    std::error_code error;
    const std::optional<std::filesystem::path> path =
        ResolveDataPath(dataRoot, TextureFileName(entry.path, *loader), error);
    if (!path.has_value())
    {
        TextureSetError missing = MakeError(TextureSetErrorKind::Missing, entry.path);
        missing.system = error;
        return Unexpected(std::move(missing));
    }
    const Core::Expected<std::vector<char>, std::error_code> file = ReadDataFile(*path, MaxTextureFileBytes);
    if (!file.HasValue())
    {
        TextureSetError unreadable = MakeError(TextureSetErrorKind::Unreadable, entry.path);
        unreadable.system = file.Error();
        return Unexpected(std::move(unreadable));
    }
    const std::span<const std::byte> bytes = std::as_bytes(std::span(file.Value()));
    Core::Expected<Texture, DataError> texture =
        *loader == TextureLoader::Ozj ? LoadOzjTexture(bytes) : LoadOztTexture(bytes);
    if (!texture.HasValue())
    {
        TextureSetError unreadable = MakeError(TextureSetErrorKind::Unreadable, entry.path);
        unreadable.data = texture.Error();
        return Unexpected(std::move(unreadable));
    }
    return LoadedTexture{entry.slot, entry.sampling, std::move(texture).Value()};
}
} // namespace

std::optional<TextureLoader> TextureLoaderOf(std::string_view originalPath)
{
    const std::size_t dot = originalPath.rfind('.');
    if (dot == std::string_view::npos)
    {
        return std::nullopt;
    }
    const std::string_view extension = originalPath.substr(dot + 1);
    if (EqualsIgnoringAsciiCase(extension, "jpg"))
    {
        return TextureLoader::Ozj;
    }
    if (EqualsIgnoringAsciiCase(extension, "tga"))
    {
        return TextureLoader::Ozt;
    }
    return std::nullopt;
}

std::string TextureFileName(std::string_view originalPath, TextureLoader loader)
{
    // Los dos cargadores copian hasta el primer punto, con él (0x63b8e4..0x63b8fd en el del OZJ).
    const std::size_t dot = originalPath.find('.');
    std::string name(originalPath.substr(0, dot));
    name += '.';
    name += LoaderExtension(loader);
    return name;
}

Core::Expected<std::vector<LoadedTexture>, TextureSetError> LoadTextureSet(const std::filesystem::path& dataRoot,
                                                                           std::span<const TextureSetEntry> entries)
{
    std::vector<LoadedTexture> loaded;
    loaded.reserve(entries.size());
    for (const TextureSetEntry& entry : entries)
    {
        Core::Expected<LoadedTexture, TextureSetError> texture = LoadEntry(dataRoot, entry);
        if (!texture.HasValue())
        {
            return Unexpected(texture.Error());
        }
        loaded.push_back(std::move(texture).Value());
    }
    return loaded;
}

std::string Describe(const TextureSetError& error)
{
    switch (error.kind)
    {
    case TextureSetErrorKind::UnknownFormat:
        return std::format("la textura {} no es .jpg ni .tga: ningún cargador del original la lee", error.path);
    case TextureSetErrorKind::Missing:
        if (error.system)
        {
            return std::format("no se pudo buscar la textura {}: {}", error.path, error.system.message());
        }
        return std::format("falta la textura {}", error.path);
    case TextureSetErrorKind::Unreadable:
        if (error.data.has_value())
        {
            return std::format("no se pudo leer la textura {}: {}", error.path, DescribeDataError(*error.data));
        }
        return std::format("no se pudo leer la textura {}: {}", error.path, error.system.message());
    }
    return std::format("la textura {} dio un error sin nombre", error.path);
}
} // namespace Client::Data::Textures
