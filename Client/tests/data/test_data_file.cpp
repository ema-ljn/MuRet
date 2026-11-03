#include "Data/DataFile.h"

#include "Data/DataError.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Client::Data::ReadDataFile y DescribeDataError: leer un archivo de Data\ entero, con un tope, y
// el error de un cargador en palabras para el log. Los usan los juegos de texturas (CL5.1) y
// Text.bmd (CL5.4).
namespace
{
using Client::Data::DataError;
using Client::Data::DescribeDataError;
using Client::Data::ReadDataFile;
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
} // namespace
