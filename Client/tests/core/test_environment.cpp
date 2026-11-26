#include "Core/Environment.h"

#include <gtest/gtest.h>

// Reads the real environment: PATH is always there and an invented name is not. PATH's value is not printed.
namespace
{
using Client::Core::ReadEnvironmentVariable;

TEST(EnvironmentTest, ReadsTheProcess)
{
    EXPECT_TRUE(ReadEnvironmentVariable("PATH").has_value());
    EXPECT_FALSE(ReadEnvironmentVariable("MU_CLIENT_NO_EXISTE_ESTA_VARIABLE").has_value());
}
} // namespace
