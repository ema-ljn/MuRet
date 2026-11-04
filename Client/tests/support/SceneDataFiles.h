#pragma once

#include "Core/Xor3.h"
#include "Data/Text/FilterWords.h"
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
#include <ios>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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

// Los bytes de un Text.bmd con `texts` en sus índices y el resto vacío, cifrados con XOR3.
inline std::vector<std::byte> TextTableBytes(const std::map<std::size_t, std::string>& texts)
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
    return table;
}

// Un Text.bmd con `texts` en sus índices y el resto vacío, cifrado con XOR3, en `relative`. Con
// `size`, el archivo mide eso (uno más corto que la tabla no carga).
inline void WriteTextTable(const std::filesystem::path& root, const std::map<std::size_t, std::string>& texts,
                           const std::filesystem::path& relative = "Local/Text.bmd",
                           std::size_t size = Data::Text::TextTableSize)
{
    std::vector<std::byte> table = TextTableBytes(texts);
    table.resize(size);
    WriteSceneFile(root, relative, table);
}

// FilterName.bmd o Filter.bmd con `words` (sintéticas): cada registro de 20, también los vacíos, con
// el XOR3 desde su principio, y la suma de `key` al final, en little-endian.
inline std::vector<std::byte> FilterFileBytes(const std::vector<std::string>& words, Data::Text::FilterChecksumKey key)
{
    EXPECT_LE(words.size(), Data::Text::FilterTableSize / Data::Text::FilterRecordSize);
    std::vector<std::byte> file(Data::Text::FilterTableSize + 4, std::byte{0});
    for (std::size_t first = 0; first < Data::Text::FilterTableSize; first += Data::Text::FilterRecordSize)
    {
        const std::span<std::byte> record(file.data() + first, Data::Text::FilterRecordSize);
        if (const std::size_t index = first / Data::Text::FilterRecordSize; index < words.size())
        {
            EXPECT_LE(words[index].size(), Data::Text::FilterRecordSize);
            std::ranges::transform(words[index].substr(0, Data::Text::FilterRecordSize), record.begin(),
                                   [](char letter) { return static_cast<std::byte>(letter); });
        }
        Core::Xor3(record);
    }
    const std::uint32_t sum = Data::Text::FilterChecksum(std::span(file).first<Data::Text::FilterTableSize>(), key);
    for (std::size_t k = 0; k < 4; ++k)
    {
        file[Data::Text::FilterTableSize + k] = static_cast<std::byte>((sum >> (8 * k)) & 0xffU);
    }
    return file;
}

inline void WriteFilterFile(const std::filesystem::path& root, const std::vector<std::string>& words,
                            Data::Text::FilterChecksumKey key, const std::filesystem::path& relative)
{
    WriteSceneFile(root, relative, FilterFileBytes(words, key));
}

// La tabla de textos de `texts`, en memoria, para los tests de la UI (CL5.5): textos sintéticos,
// nunca los del laboratorio.
inline Data::Text::TextTable MakeTextTable(const std::map<std::size_t, std::string>& texts)
{
    auto table = Data::Text::LoadTextTable(TextTableBytes(texts));
    EXPECT_TRUE(table.HasValue());
    return std::move(table).Value();
}
} // namespace Client::Tests
