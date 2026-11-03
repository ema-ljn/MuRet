#include "App/FontFiles.h"

#include <utility>

namespace Client::App
{
Core::Expected<FontFiles, std::string> ChooseFontFiles(const ClientConfig& config, std::string_view face,
                                                       const SystemFontSearch& search)
{
    if (config.fontFile.has_value())
    {
        return FontFiles{*config.fontFile, config.boldFontFile};
    }
    if (!search)
    {
        return Core::Unexpected(std::string("falta [Video] FontFile en muret-client.ini: FreeType necesita la ruta de "
                                            "una fuente TrueType, y en este sistema el cliente no la busca"));
    }
    std::optional<std::filesystem::path> regular;
    if (!face.empty())
    {
        regular = search(face, false);
    }
    if (!regular.has_value())
    {
        return Core::Unexpected(
            std::string("falta [Video] FontFile en muret-client.ini: la cara que pide el texto 0 de "
                        "Text.bmd no está en las fuentes del sistema"));
    }
    std::optional<std::filesystem::path> bold = config.boldFontFile;
    if (!bold.has_value())
    {
        bold = search(face, true);
    }
    return FontFiles{std::move(*regular), std::move(bold)};
}

std::string FontOpenError(const ClientConfig& config, std::string_view error)
{
    if (config.fontFile.has_value())
    {
        return std::string(error);
    }
    return "la fuente del sistema para la cara del texto 0 de Text.bmd no se pudo abrir con FreeType: "
           "dé [Video] FontFile en muret-client.ini";
}
} // namespace Client::App
