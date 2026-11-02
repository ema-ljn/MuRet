#include "Platform/GlLoader.h"

#include <gtest/gtest.h>

#include <cstdint>

// Client::Platform::IsValidProcAddress y HasRequiredGlVersion: lo de GlLoader que se prueba sin
// ventana ni contexto.
namespace
{
using Client::Platform::HasRequiredGlVersion;
using Client::Platform::IsValidProcAddress;

TEST(GlLoaderTest, RejectsTheInvalidAddresses)
{
    EXPECT_FALSE(IsValidProcAddress(0));
    EXPECT_FALSE(IsValidProcAddress(1));
    EXPECT_FALSE(IsValidProcAddress(2));
    EXPECT_FALSE(IsValidProcAddress(3));
    // El -1 de algunos drivers de Windows.
    EXPECT_FALSE(IsValidProcAddress(UINTPTR_MAX));
    EXPECT_TRUE(IsValidProcAddress(4));
}

TEST(GlLoaderTest, RequiresThreeThree)
{
    EXPECT_TRUE(HasRequiredGlVersion(3, 3));
    EXPECT_TRUE(HasRequiredGlVersion(3, 4));
    EXPECT_TRUE(HasRequiredGlVersion(4, 5));
    EXPECT_FALSE(HasRequiredGlVersion(3, 2));
    EXPECT_FALSE(HasRequiredGlVersion(2, 1));
}
} // namespace
