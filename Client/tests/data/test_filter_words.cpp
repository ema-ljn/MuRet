#include "Data/Text/FilterWords.h"

#include "Core/Xor3.h"
#include "Data/DataError.h"
#include "Data/Tables/TableChecksum.h"

#include "support/ExpectedError.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Client::Data::Text::LoadFilterWords: FilterName.bmd y Filter.bmd (0x549fa0 y 0x549c60), con
// palabras sintéticas.
namespace
{
using Client::Data::DataError;
using Client::Data::Text::FilterChecksumKey;
using Client::Data::Text::FilterNameChecksumKey;
using Client::Data::Text::FilterRecordSize;
using Client::Data::Text::FilterTableSize;
using Client::Data::Text::LoadFilterWords;
using Client::Tests::ErrorOf;

std::vector<std::byte> FilterFile(const std::vector<std::string>& words,
                                  std::uint32_t checksumKey = FilterNameChecksumKey)
{
    return Client::Tests::FilterFileBytes(words, checksumKey);
}

// La suma es la de las tablas (TableChecksum, con la paridad de Item.bmd) con la clave << 9 de
// semilla: la del laboratorio da (GoldenFilterWords). La de un archivo no es la del otro.
TEST(FilterWordsTest, EachFileHasItsChecksumKey)
{
    EXPECT_EQ(FilterNameChecksumKey << 9, 0x578200u);
    EXPECT_EQ(FilterChecksumKey << 9, 0x7cfa00u);
    EXPECT_TRUE(LoadFilterWords(FilterFile({"Synth"}, FilterChecksumKey), FilterChecksumKey).HasValue());
    EXPECT_FALSE(LoadFilterWords(FilterFile({"Synth"}, FilterChecksumKey), FilterNameChecksumKey).HasValue());
}

// Cada registro de 20 lleva el XOR3 desde su primer byte (0x5496c0 por registro): como 20 no es
// múltiplo de 3, no es lo mismo que de punta a punta.
TEST(FilterWordsTest, EachRecordIsDecodedFromItsStart)
{
    const auto words = LoadFilterWords(FilterFile({"Synth", "abcdefghijklmnopqrs"}), FilterNameChecksumKey);

    ASSERT_TRUE(words.HasValue());
    EXPECT_EQ(words.Value(), (std::vector<std::string>{"Synth", "abcdefghijklmnopqrs"}));
}

// La lectura para en el primer registro vacío: lo que viene después no cuenta.
TEST(FilterWordsTest, TheWordsEndAtTheFirstEmptyRecord)
{
    std::vector<std::byte> file = FilterFile({"one", "two"});
    // Un tercero después del vacío, con la suma rehecha.
    std::span<std::byte> fourth(file.data() + 3 * FilterRecordSize, FilterRecordSize);
    std::ranges::fill(fourth, std::byte{0});
    fourth[0] = std::byte{'x'};
    Client::Core::Xor3(fourth);
    const std::uint32_t sum =
        Client::Data::Tables::TableChecksum(std::span(file).first(FilterTableSize), FilterNameChecksumKey,
                                            Client::Data::Tables::ChecksumParity::XorOddDwords);
    for (std::size_t k = 0; k < 4; ++k)
    {
        file[FilterTableSize + k] = static_cast<std::byte>((sum >> (8 * k)) & 0xff);
    }

    const auto words = LoadFilterWords(file, FilterNameChecksumKey);

    ASSERT_TRUE(words.HasValue());
    EXPECT_EQ(words.Value(), (std::vector<std::string>{"one", "two"}));
    EXPECT_TRUE(LoadFilterWords(FilterFile({}), FilterNameChecksumKey).Value().empty());
}

// Un registro lleno, sin cero, es una palabra de 20 bytes.
TEST(FilterWordsTest, AFullRecordIsAWordOfTwentyBytes)
{
    const auto words = LoadFilterWords(FilterFile({std::string(20, 'w'), "x"}), FilterNameChecksumKey);

    ASSERT_TRUE(words.HasValue());
    EXPECT_EQ(words.Value(), (std::vector<std::string>{std::string(20, 'w'), "x"}));
}

// Con los 1.000 llenos, 0x549fa0 sale por el tope (0x54a139) sin guardar la cuenta, que queda en 0:
// ninguna palabra.
TEST(FilterWordsTest, ATableWithoutAnEmptyRecordHasNoWords)
{
    const std::vector<std::string> all(1000, std::string(20, 'w'));

    const auto words = LoadFilterWords(FilterFile(all), FilterNameChecksumKey);

    ASSERT_TRUE(words.HasValue());
    EXPECT_TRUE(words.Value().empty());
}

// Con otra suma, ChecksumMismatch (el original avisa "File corrupted" y cierra); corto,
// FileTooShort; con la clave del otro archivo, tampoco da.
TEST(FilterWordsTest, AWrongSumOrAShortFileIsAnError)
{
    std::vector<std::byte> file = FilterFile({"Synth"});
    file[3] ^= std::byte{0x01};
    EXPECT_EQ(ErrorOf(LoadFilterWords(file, FilterNameChecksumKey)), DataError::ChecksumMismatch);
    EXPECT_EQ(ErrorOf(LoadFilterWords(FilterFile({"Synth"}), FilterChecksumKey)), DataError::ChecksumMismatch);
    const std::vector<std::byte> shortFile(FilterTableSize + 3, std::byte{0});
    EXPECT_EQ(ErrorOf(LoadFilterWords(shortFile, FilterNameChecksumKey)), DataError::FileTooShort);
}
} // namespace
