#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace
{
using GoldenDataFiles = Client::Tests::GoldenDataTest;

// The C3/C4 keys (Client/docs/research/03-red-y-cifrado.md, "C3/C4: cifrado por bloques con Enc1.dat y Dec2.dat").
// In the lab's Data\ each is 54 bytes.
TEST_F(GoldenDataFiles, KeyFilesForC3C4Exist)
{
    for (const char* name : {"Enc1.dat", "Dec2.dat"})
    {
        const auto path = DataFile(name);
        ASSERT_TRUE(path.has_value()) << name;
        EXPECT_EQ(std::filesystem::file_size(*path), 54u) << name;
    }
}

// main.exe asks for "Data\\Player\\Player.bmd"; in the lab the folder is player\.
TEST_F(GoldenDataFiles, FindsPlayerModelWhateverTheCase)
{
    const auto path = DataFile("Player\\Player.bmd");
    ASSERT_TRUE(path.has_value());
    EXPECT_GT(std::filesystem::file_size(*path), 0u);
}
} // namespace
