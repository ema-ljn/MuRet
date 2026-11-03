#include "Platform/GlLoader.h"

#include "Platform/GlFunctionList.h"

#include <gtest/gtest.h>

#include <SDL3/SDL_stdinc.h>

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// Client::Platform::IsValidProcAddress, HasRequiredGlVersion, la lista de GlFunctionList.h y el
// orden de LoadGlFunctions con un driver falso: lo de GlLoader que se prueba sin ventana ni
// contexto.
namespace
{
using Client::Platform::GlFunctionNames;
using Client::Platform::HasRequiredGlVersion;
using Client::Platform::IsCoreProfile;
using Client::Platform::IsValidProcAddress;
using Client::Platform::LoadGlFunctions;

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

// Un driver falso para LoadGlFunctions, sin ventana: qué versión y qué perfil dice, qué función
// no tiene y qué nombres le pidieron, en orden. Es estado global porque el pedido es un puntero a
// función, sin captura; el fixture lo deja en un 3.3 core completo antes de cada test.
struct FakeDriver
{
    GLint major = 3;
    GLint minor = 3;
    GLint profileMask = GL_CONTEXT_CORE_PROFILE_BIT;
    std::string_view missing;
    std::vector<std::string> requested;
};

FakeDriver s_driver;

const GLubyte* GLAPIENTRY FakeGetString(GLenum /*name*/)
{
    return reinterpret_cast<const GLubyte*>("2.1 falso");
}

// Antes de la 3.0, GL_MAJOR_VERSION no existe: un driver de verdad deja el entero como estaba y
// marca GL_INVALID_ENUM. El falso hace lo mismo con un 0.0.
void GLAPIENTRY FakeGetIntegerv(GLenum name, GLint* value)
{
    if (name == GL_MAJOR_VERSION && s_driver.major >= 3)
    {
        *value = s_driver.major;
    }
    if (name == GL_MINOR_VERSION && s_driver.major >= 3)
    {
        *value = s_driver.minor;
    }
    if (name == GL_CONTEXT_PROFILE_MASK)
    {
        *value = s_driver.profileMask;
    }
}

// Lo que da por las demás: LoadGlFunctions las pide y no las llama.
void GLAPIENTRY FakeAnyFunction() {}

SDL_FunctionPointer FakeLookup(const char* name)
{
    s_driver.requested.emplace_back(name);
    const std::string_view asked(name);
    if (asked == s_driver.missing)
    {
        return nullptr;
    }
    if (asked == "glGetString")
    {
        return reinterpret_cast<SDL_FunctionPointer>(&FakeGetString);
    }
    if (asked == "glGetIntegerv")
    {
        return reinterpret_cast<SDL_FunctionPointer>(&FakeGetIntegerv);
    }
    return reinterpret_cast<SDL_FunctionPointer>(&FakeAnyFunction);
}

class GlLoaderDriverTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        s_driver = FakeDriver{};
    }
};

// Un driver viejo (un 2.1, sin las de la 3.3) dice su versión: LoadGlFunctions mira el contexto con
// las dos primeras y no pide ninguna más, así que el error no es la primera de la 3.3 que falta.
TEST_F(GlLoaderDriverTest, AnOldDriverSaysItsVersion)
{
    s_driver.major = 2;
    s_driver.minor = 1;
    s_driver.missing = "glViewport";

    const auto gl = LoadGlFunctions(FakeLookup);

    ASSERT_FALSE(gl.HasValue());
    EXPECT_EQ(gl.Error(), "se pidió OpenGL 3.3 y el driver dio 0.0 (2.1 falso)");
    EXPECT_EQ(s_driver.requested, (std::vector<std::string>{"glGetString", "glGetIntegerv"}));
}

// Un 3.3 sin el perfil core (el de compatibilidad) no alcanza, y tampoco pide el resto.
TEST_F(GlLoaderDriverTest, ACompatibilityContextIsRefused)
{
    s_driver.profileMask = GL_CONTEXT_COMPATIBILITY_PROFILE_BIT;

    const auto gl = LoadGlFunctions(FakeLookup);

    ASSERT_FALSE(gl.HasValue());
    EXPECT_EQ(gl.Error(), "se pidió OpenGL 3.3 core y el driver dio un contexto sin el perfil core (2.1 falso)");
    EXPECT_EQ(s_driver.requested.size(), 2u);
}

// Con un 3.3 core, la primera que falta corta, con su nombre, y las de después no se piden.
TEST_F(GlLoaderDriverTest, TheFirstMissingFunctionIsNamed)
{
    s_driver.missing = "glTexImage2D";

    const auto gl = LoadGlFunctions(FakeLookup);

    ASSERT_FALSE(gl.HasValue());
    EXPECT_EQ(gl.Error(), "el driver no tiene glTexImage2D");
    EXPECT_EQ(s_driver.requested.back(), "glTexImage2D");
}

// Con todo, pide cada una de la lista una vez, en su orden, y ningún puntero queda en nulo.
TEST_F(GlLoaderDriverTest, AFullDriverGivesEveryFunction)
{
    const auto gl = LoadGlFunctions(FakeLookup);

    ASSERT_TRUE(gl.HasValue()) << gl.Error();
    EXPECT_EQ(s_driver.requested, (std::vector<std::string>(GlFunctionNames.begin(), GlFunctionNames.end())));
#define MURET_EXPECT_LOADED(type, member, name) EXPECT_NE(gl.Value().member, nullptr) << name;
    MURET_GL_FUNCTIONS(MURET_EXPECT_LOADED)
#undef MURET_EXPECT_LOADED
}
} // namespace
