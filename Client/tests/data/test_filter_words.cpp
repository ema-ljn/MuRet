#include "Data/Text/FilterWords.h"

#include "Core/Xor3.h"
#include "Data/DataError.h"

#include "support/ExpectedError.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Client::Data::Text::LoadFilterWords: FilterName.bmd y Filter.bmd (0x549fa0 y 0x549c60), con
// palabras sintéticas.
namespace
{
using Client::Data::DataError;
using Client::Data::Text::FilterChecksum;
using Client::Data::Text::FilterKey;
using Client::Data::Text::FilterNameKey;
using Client::Data::Text::FilterRecordSize;
using Client::Data::Text::FilterTableSize;
using Client::Data::Text::LoadFilterWords;
using Client::Tests::ErrorOf;

std::vector<std::byte> FilterFile(const std::vector<std::string>& words,
                                  Client::Data::Text::FilterChecksumKey key = FilterNameKey)
{
    return Client::Tests::FilterFileBytes(words, key);
}

// La suma de 0x549fa0 sobre 20.000 bytes en cero: palabra 0 sumada, la 1 con XOR, … y cada 16
// bytes, s ^ ((s + 0x2bc1) >> ((k & 7) + 1)). Sobre ceros, solo cuenta ese último paso.
TEST(FilterWordsTest, TheChecksumMixesAddXorAndAShift)
{
    std::vector<std::byte> zeros(FilterTableSize, std::byte{0});
    std::uint32_t expected = FilterNameKey.seed;
    for (std::uint32_t offset = 0; offset < FilterTableSize; offset += 4)
    {
        if ((offset & 0xf) == 0)
        {
            expected ^= (expected + FilterNameKey.step) >> (((offset >> 2) & 7) + 1);
        }
    }
    EXPECT_EQ(FilterChecksum(std::span(zeros).first<FilterTableSize>(), FilterNameKey), expected);

    // Una palabra: la 0 se suma y la 1 va con XOR.
    std::vector<std::byte> one(FilterTableSize, std::byte{0});
    one[0] = std::byte{1};
    one[4] = std::byte{2};
    std::uint32_t mixed = FilterNameKey.seed + 1;
    mixed ^= (mixed + FilterNameKey.step) >> 1;
    mixed ^= 2;
    for (std::uint32_t offset = 16; offset < FilterTableSize; offset += 16)
    {
        mixed ^= (mixed + FilterNameKey.step) >> (((offset >> 2) & 7) + 1);
    }
    EXPECT_EQ(FilterChecksum(std::span(one).first<FilterTableSize>(), FilterNameKey), mixed);
    EXPECT_NE(FilterChecksum(std::span(one).first<FilterTableSize>(), FilterKey), mixed);
}

// Cada registro de 20 lleva el XOR3 desde su primer byte (0x5496c0 por registro): como 20 no es
// múltiplo de 3, no es lo mismo que de punta a punta.
TEST(FilterWordsTest, EachRecordIsDecodedFromItsStart)
{
    const auto words = LoadFilterWords(FilterFile({"Synth", "abcdefghijklmnopqrs"}), FilterNameKey);

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
    const std::uint32_t sum = FilterChecksum(std::span(file).first<FilterTableSize>(), FilterNameKey);
    for (std::size_t k = 0; k < 4; ++k)
    {
        file[FilterTableSize + k] = static_cast<std::byte>((sum >> (8 * k)) & 0xff);
    }

    const auto words = LoadFilterWords(file, FilterNameKey);

    ASSERT_TRUE(words.HasValue());
    EXPECT_EQ(words.Value(), (std::vector<std::string>{"one", "two"}));
    EXPECT_TRUE(LoadFilterWords(FilterFile({}), FilterNameKey).Value().empty());
}

// Un registro lleno, sin cero, es una palabra de 20 bytes; y los 1.000 llenos son 1.000 palabras.
TEST(FilterWordsTest, AFullRecordIsAWordOfTwentyBytes)
{
    const std::vector<std::string> all(1000, std::string(20, 'w'));

    const auto words = LoadFilterWords(FilterFile(all), FilterNameKey);

    ASSERT_TRUE(words.HasValue());
    ASSERT_EQ(words.Value().size(), 1000u);
    EXPECT_EQ(words.Value()[999], std::string(20, 'w'));
}

// Con otra suma, ChecksumMismatch (el original avisa "File corrupted" y cierra); corto,
// FileTooShort; con la clave del otro archivo, tampoco da.
TEST(FilterWordsTest, AWrongSumOrAShortFileIsAnError)
{
    std::vector<std::byte> file = FilterFile({"Synth"});
    file[3] ^= std::byte{0x01};
    EXPECT_EQ(ErrorOf(LoadFilterWords(file, FilterNameKey)), DataError::ChecksumMismatch);
    EXPECT_EQ(ErrorOf(LoadFilterWords(FilterFile({"Synth"}), FilterKey)), DataError::ChecksumMismatch);
    const std::vector<std::byte> shortFile(FilterTableSize + 3, std::byte{0});
    EXPECT_EQ(ErrorOf(LoadFilterWords(shortFile, FilterNameKey)), DataError::FileTooShort);
}
} // namespace
