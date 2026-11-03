#include "Platform/GlLoader.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <string_view>

// Client::Platform::IsValidProcAddress, HasRequiredGlVersion y la lista de GlFunctionList.h: lo de
// GlLoader que se prueba sin ventana ni contexto.
namespace
{
using Client::Platform::GlFunctionNames;
using Client::Platform::HasRequiredGlVersion;
using Client::Platform::IsCoreProfile;
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

TEST(GlLoaderTest, RequiresTheCoreProfile)
{
    EXPECT_TRUE(IsCoreProfile(GL_CONTEXT_CORE_PROFILE_BIT));
    EXPECT_FALSE(IsCoreProfile(GL_CONTEXT_COMPATIBILITY_PROFILE_BIT));
    // Antes de la 3.2 la consulta no existe y el entero queda en 0.
    EXPECT_FALSE(IsCoreProfile(0));
}

// La X-macro no repite una función (dos líneas con el mismo nombre de GL pedirían dos veces lo
// mismo, en dos miembros), y cada nombre es el de una función de GL, como lo espera
// SDL_GL_GetProcAddress: empieza con "gl". Los miembros repetidos no compilan.
TEST(GlLoaderTest, EveryFunctionHasItsName)
{
    const std::set<std::string_view> unique(GlFunctionNames.begin(), GlFunctionNames.end());
    EXPECT_EQ(unique.size(), GlFunctionNames.size());
    for (const std::string_view name : GlFunctionNames)
    {
        EXPECT_TRUE(name.starts_with("gl")) << name;
        EXPECT_GT(name.size(), 2u) << name;
    }
}

// Las dos con que se pregunta qué contexto dio el driver van primero: LoadGlFunctions mira la
// versión y el perfil antes de pedir el resto, así que un driver viejo dice su versión y no la
// primera función de la 3.3 que no tiene.
TEST(GlLoaderTest, TheContextIsAskedFirst)
{
    ASSERT_GE(GlFunctionNames.size(), 2u);
    EXPECT_EQ(GlFunctionNames[0], "glGetString");
    EXPECT_EQ(GlFunctionNames[1], "glGetIntegerv");
}
} // namespace
