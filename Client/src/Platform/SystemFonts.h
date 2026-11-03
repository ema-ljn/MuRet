#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

// La lista de fuentes del sistema (CL5.3, pregunta 8 del plan de la CL5), para App::ChooseFontFiles
// sin [Video] FontFile. El original pide la cara por nombre a CreateFontA; MuRet busca su archivo
// en la lista que lee GDI: HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts, y la del
// usuario en HKCU (las fuentes instaladas sin ser administrador, desde Windows 10 1809). Solo en
// Windows: en otro sistema no hay lista. La cara es un texto del juego, y no va a ningún mensaje.
namespace Client::Platform
{
// true si el valor `entryName` de la lista es de la cara `face`, la normal o con `bold` la
// negrita: "<cara> (TrueType)" o "<cara> Bold (TrueType)", también "(OpenType)", y en una
// colección cualquiera de las caras separadas por " & ". Sin mirar mayúsculas (ASCII), como el
// nombre de la cara en GDI. Una fuente de mapa de bits (sin esos tipos) o una cara vacía no
// cuentan nunca.
[[nodiscard]] bool FontEntryNamesFace(std::string_view entryName, std::string_view face, bool bold);

// El archivo de la cara en la lista del sistema: el del primer valor que la nombra, en HKLM y
// después en HKCU, con la carpeta Fonts de Windows delante si el valor no trae la ruta entera.
// std::nullopt si no está, si la cara está vacía o fuera de Windows.
[[nodiscard]] std::optional<std::filesystem::path> FindSystemFontFile(std::string_view face, bool bold);
} // namespace Client::Platform
