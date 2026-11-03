#include "Core/Environment.h"

#include <gtest/gtest.h>

// Client::Core::ReadEnvironmentVariable: lee el entorno de verdad. PATH está siempre, y un nombre
// inventado no. No se imprime lo que vale PATH.
namespace
{
using Client::Core::ReadEnvironmentVariable;

TEST(EnvironmentTest, ReadsTheProcess)
{
    EXPECT_TRUE(ReadEnvironmentVariable("PATH").has_value());
    EXPECT_FALSE(ReadEnvironmentVariable("MURET_CLIENT_NO_EXISTE_ESTA_VARIABLE").has_value());
}
} // namespace
