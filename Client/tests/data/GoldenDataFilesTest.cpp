// Tests golden de ejemplo: leen el Data\ real de MURET_CLIENT_GOLDEN_DIR y se saltean si no está
// (Client/tests/support/GoldenDir.h).

#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace
{

using GoldenDataFiles = Client::Tests::GoldenDataTest;

// Las claves del cifrado C3/C4 (Client/docs/research/03-red-y-cifrado.md, "C3/C4: cifrado por
// bloques con Enc1.dat y Dec2.dat"). En el Data\ del laboratorio miden 54 bytes cada una.
TEST_F(GoldenDataFiles, KeyFilesForC3C4Exist)
{
    for (const char* name : {"Enc1.dat", "Dec2.dat"})
    {
        const auto path = DataFile(name);
        ASSERT_TRUE(path.has_value()) << name;
        EXPECT_EQ(std::filesystem::file_size(*path), 54u) << name;
    }
}

// main.exe pide "Data\\Player\\Player.bmd"; en el laboratorio la carpeta es player\.
TEST_F(GoldenDataFiles, FindsPlayerModelWhateverTheCase)
{
    const auto path = DataFile("Player\\Player.bmd");
    ASSERT_TRUE(path.has_value());
    EXPECT_GT(std::filesystem::file_size(*path), 0u);
}

} // namespace
