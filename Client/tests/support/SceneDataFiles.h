#pragma once

#include "Core/Xor3.h"
#include "Data/Text/TextTable.h"
#include "Data/Textures/TextureSet.h"
#include "support/JpegVectors.h"
#include "support/TextureFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Un Data\ sintético para los tests de las escenas (CL5.4), en una carpeta temporal: los archivos
// de un juego de texturas con el nombre que abre el cargador (TextureFileName), y un Text.bmd con
// textos inventados, cifrado con XOR3 como el de verdad. Nada de esto sale del Data\ del original.
namespace Client::Tests
{
// El OZT de los tests de las escenas: 4 × 4, sin nada que mirar.
inline constexpr std::int16_t SceneOztSide = 4;

// Escribe `bytes` en `root / relative`, con sus carpetas.
inline void WriteSceneFile(const std::filesystem::path& root, const std::filesystem::path& relative,
                           std::span<const std::byte> bytes)
{
    const std::filesystem::path path = root / relative;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    for (const std::byte value : bytes)
    {
        file.put(static_cast<char>(value));
    }
    ASSERT_TRUE(file.good()) << "no se pudo escribir " << relative.generic_string();
}

// La ruta de disco del archivo que abre el cargador para `entry`, con '/' en vez de '\'.
inline std::filesystem::path SceneTextureFile(const Data::Textures::TextureSetEntry& entry)
{
    const auto loader = Data::Textures::TextureLoaderOf(entry.path);
    std::string name = Data::Textures::TextureFileName(entry.path, loader.value_or(Data::Textures::TextureLoader::Ozj));
    std::replace(name.begin(), name.end(), '\\', '/');
    return name;
}

// Los archivos de todas las entradas de `set` salvo la de `skipped`: un OZJ con el JPEG
// baseline-420 de vectors/jpeg, o un OZT de 4 × 4.
inline void WriteTextureSet(const std::filesystem::path& root, std::span<const Data::Textures::TextureSetEntry> set,
                            std::optional<std::uint16_t> skipped = std::nullopt)
{
    const std::vector<std::byte> ozj = OzjOf(ReadJpegVector("baseline-420.jpg"));
    const std::vector<std::byte> ozt = PlainOzt(SceneOztSide, SceneOztSide);
    for (const Data::Textures::TextureSetEntry& entry : set)
    {
        if (skipped == entry.slot)
        {
            continue;
        }
        const bool isOzt = Data::Textures::TextureLoaderOf(entry.path) == Data::Textures::TextureLoader::Ozt;
        WriteSceneFile(root, SceneTextureFile(entry), isOzt ? ozt : ozj);
    }
}

// Un Text.bmd con `texts` en sus índices y el resto vacío, cifrado con XOR3, en `relative`. Con
// `size`, el archivo mide eso (uno más corto que la tabla no carga).
inline void WriteTextTable(const std::filesystem::path& root, const std::map<std::size_t, std::string>& texts,
                           const std::filesystem::path& relative = "Local/Text.bmd",
                           std::size_t size = Data::Text::TextTableSize)
{
    std::vector<std::byte> table(Data::Text::TextTableSize);
    for (const auto& [index, text] : texts)
    {
        for (std::size_t i = 0; i < text.size() && i + 1 < Data::Text::TextRecordSize; ++i)
        {
            table[index * Data::Text::TextRecordSize + i] = static_cast<std::byte>(text[i]);
        }
    }
    Core::Xor3(table);
    table.resize(size);
    WriteSceneFile(root, relative, table);
}
} // namespace Client::Tests
