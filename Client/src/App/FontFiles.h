#pragma once

#include "App/Config.h"
#include "Core/Expected.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

// De dónde sale la fuente del texto (CL5.3, pregunta 8 del plan de la CL5). El original pide las
// cuatro de WinMain por nombre a GDI, con la cara del texto 0 de Text.bmd (y la del 18 para la
// cuarta); FreeType necesita un archivo. Manda [Video] FontFile, y BoldFontFile si está; sin
// FontFile, se busca la cara en la lista de fuentes del sistema, que solo Windows tiene
// (Platform::FindSystemFontFile). La cara es un texto del juego: no va a ningún mensaje.
namespace Client::App
{
// Los archivos para FreeTypeRasterizer::Open. Sin `bold`, la negrita se arma desde la normal.
struct FontFiles
{
    std::filesystem::path regular;
    std::optional<std::filesystem::path> bold;
};

// Busca el archivo de una cara, la normal o la negrita, en las fuentes del sistema. Vacía donde no
// hay lista (Linux).
using SystemFontSearch = std::function<std::optional<std::filesystem::path>(std::string_view face, bool bold)>;

// Los archivos de la config; sin FontFile, los que `search` encuentra para `face` (la negrita,
// solo si BoldFontFile falta). Error, sin nombrar la cara, si no hay FontFile y la cara no se
// encuentra, está vacía o no hay búsqueda: el mensaje nombra la clave.
[[nodiscard]] Core::Expected<FontFiles, std::string> ChooseFontFiles(const ClientConfig& config, std::string_view face,
                                                                     const SystemFontSearch& search);
} // namespace Client::App
