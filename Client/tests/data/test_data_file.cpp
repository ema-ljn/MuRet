#include "Data/DataFile.h"

#include "Data/DataError.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Client::Data::ReadDataFile, ReadDataFilePrefix y DescribeDataError: archivos enteros con tope,
// prefijos sin limitar la cola y errores en palabras. Los usan texturas, Text.bmd y Gate.bmd.
namespace
{
using Client::Data::DataError;
using Client::Data::DescribeDataError;
using Client::Data::ReadDataFile;
using Client::Data::ReadDataFilePrefix;
using Client::Tests::TemporaryFolder;

void WriteBytes(const std::filesystem::path& path, std::string_view bytes)
{
    std::ofstream file(path, std::ios::binary);
    file << bytes;
    ASSERT_TRUE(file.good());
}

TEST(DataFileTest, ReadsEveryByte)
{
    const TemporaryFolder folder("data-file");
    const std::filesystem::path path = folder.Path() / "uno.bin";
    WriteBytes(path, std::string_view("\x01\x00\x7f\xff", 4));

    const auto read = ReadDataFile(path, 16);

    ASSERT_TRUE(read.HasValue()) << read.Error().message();
    EXPECT_EQ(read.Value(), (std::vector<char>{'\x01', '\x00', '\x7f', '\xff'}));
}

// Un archivo más grande que el tope no es del formato que se pide, y no se lee entero a memoria.
TEST(DataFileTest, AFileOverTheCapIsNotRead)
{
    const TemporaryFolder folder("data-file");
    const std::filesystem::path path = folder.Path() / "grande.bin";
    WriteBytes(path, std::string(17, 'x'));

    const auto read = ReadDataFile(path, 16);

    ASSERT_FALSE(read.HasValue());
    EXPECT_EQ(read.Error(), std::make_error_code(std::errc::file_too_large));
    EXPECT_TRUE(ReadDataFile(path, 17).HasValue());
}

TEST(DataFileTest, AMissingFileIsTheErrorOfTheSystem)
{
    const TemporaryFolder folder("data-file");

    const auto read = ReadDataFile(folder.Path() / "no-esta.bin", 16);

    ASSERT_FALSE(read.HasValue());
    EXPECT_TRUE(static_cast<bool>(read.Error()));
}

TEST(DataFileTest, DescribesEachDataError)
{
    constexpr std::array<DataError, 5> errors = {DataError::FileTooShort, DataError::ChecksumMismatch,
                                                 DataError::WrongWorld, DataError::UnexpectedSize,
                                                 DataError::InvalidData};
    std::set<std::string_view> texts;
    for (const DataError error : errors)
    {
        const std::string_view text = DescribeDataError(error);
        EXPECT_FALSE(text.empty());
        EXPECT_TRUE(texts.insert(text).second) << text;
    }
}

TEST(DataFileTest, ReadsOnlyTheRequestedPrefix)
{
    const TemporaryFolder folder("data-file");
    const std::filesystem::path path = folder.Path() / "cola.bin";
    constexpr std::size_t TailBytes = 128 * 1024;
    std::string file("\x01\x00\x7f\xff", 4);
    file.append(TailBytes, 'x');
    WriteBytes(path, file);

    const auto read = ReadDataFilePrefix(path, 4);

    ASSERT_TRUE(read.HasValue()) << read.Error().message();
    EXPECT_EQ(read.Value(), (std::vector<char>{'\x01', '\x00', '\x7f', '\xff'}));
}

TEST(DataFileTest, AShortPrefixReadReturnsOnlyTheAvailableBytes)
{
    const TemporaryFolder folder("data-file");
    const std::filesystem::path path = folder.Path() / "corto.bin";
    WriteBytes(path, "abc");

    const auto read = ReadDataFilePrefix(path, 8);

    ASSERT_TRUE(read.HasValue()) << read.Error().message();
    EXPECT_EQ(read.Value(), (std::vector<char>{'a', 'b', 'c'}));
}

TEST(DataFileTest, AnUnreadablePrefixIsAnError)
{
    const TemporaryFolder folder("data-file");

    const auto missing = ReadDataFilePrefix(folder.Path() / "no-esta.bin", 4);
    const auto directory = ReadDataFilePrefix(folder.Path(), 4);

    ASSERT_FALSE(missing.HasValue());
    EXPECT_TRUE(static_cast<bool>(missing.Error()));
    ASSERT_FALSE(directory.HasValue());
    EXPECT_EQ(directory.Error(), std::make_error_code(std::errc::is_a_directory));
}

#ifndef _WIN32
TEST(DataFileTest, APrefixFromADeviceSymlinkIsRejected)
{
    const TemporaryFolder folder("data-file");
    const std::filesystem::path path = folder.Path() / "dispositivo.bin";
    std::error_code error;
    std::filesystem::create_symlink("/dev/zero", path, error);
    ASSERT_FALSE(error) << error.message();

    const auto read = ReadDataFilePrefix(path, 4);

    ASSERT_FALSE(read.HasValue());
    EXPECT_EQ(read.Error(), std::make_error_code(std::errc::not_supported));
}

TEST(DataFileTest, APrefixFromARegularFileSymlinkIsRead)
{
    const TemporaryFolder folder("data-file");
    const std::filesystem::path target = folder.Path() / "regular.bin";
    const std::filesystem::path path = folder.Path() / "enlace.bin";
    WriteBytes(target, "abcdef");
    std::error_code error;
    std::filesystem::create_symlink(target, path, error);
    ASSERT_FALSE(error) << error.message();

    const auto read = ReadDataFilePrefix(path, 3);

    ASSERT_TRUE(read.HasValue()) << read.Error().message();
    EXPECT_EQ(read.Value(), (std::vector<char>{'a', 'b', 'c'}));
}
#endif

TEST(DataFileTest, AnUnrepresentablePrefixLengthIsAnErrorWithoutAllocating)
{
    const TemporaryFolder folder("data-file");
    const std::filesystem::path path = folder.Path() / "vacio.bin";
    WriteBytes(path, {});
    constexpr std::size_t UnrepresentableLength = std::numeric_limits<std::size_t>::max();

    const auto read = ReadDataFilePrefix(path, UnrepresentableLength);

    ASSERT_FALSE(read.HasValue());
    EXPECT_EQ(read.Error(), std::make_error_code(std::errc::value_too_large));
}

TEST(DataFileTest, APrefixBeyondTheContainerLimitIsAnErrorWithoutAllocating)
{
    const TemporaryFolder folder("data-file");
    const std::filesystem::path path = folder.Path() / "vacio.bin";
    WriteBytes(path, {});
    const std::size_t containerLimit = std::vector<char>{}.max_size();
    ASSERT_LT(containerLimit, std::numeric_limits<std::size_t>::max());

    // No aísla la guarda de max_size(): streamsize puede rechazar la misma cantidad antes.
    const auto read = ReadDataFilePrefix(path, containerLimit + 1);

    ASSERT_FALSE(read.HasValue());
    EXPECT_EQ(read.Error(), std::make_error_code(std::errc::value_too_large));
}

TEST(DataFileTest, AMissingPrefixFileIsRejectedBeforeAllocating)
{
    const TemporaryFolder folder("data-file");

    const auto read = ReadDataFilePrefix(folder.Path() / "no-esta.bin", std::vector<char>{}.max_size());

    ASSERT_FALSE(read.HasValue());
    EXPECT_TRUE(static_cast<bool>(read.Error()));
}
} // namespace
