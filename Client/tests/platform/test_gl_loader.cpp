#include "Platform/Sdl/GlLoader.h"

#include "Platform/Sdl/GlFunctionList.h"

#include <gtest/gtest.h>

#include <SDL3/SDL_stdinc.h>

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using Client::Platform::Sdl::GlFunctionNames;
using Client::Platform::Sdl::HasRequiredGlVersion;
using Client::Platform::Sdl::IsCoreProfile;
using Client::Platform::Sdl::IsValidProcAddress;
using Client::Platform::Sdl::LoadGlFunctions;

TEST(GlLoaderTest, RejectsTheInvalidAddresses)
{
    EXPECT_FALSE(IsValidProcAddress(0));
    EXPECT_FALSE(IsValidProcAddress(1));
    EXPECT_FALSE(IsValidProcAddress(2));
    EXPECT_FALSE(IsValidProcAddress(3));
    // The -1 of some Windows drivers.
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
    // Before 3.2 the query does not exist and the integer stays 0.
    EXPECT_FALSE(IsCoreProfile(0));
}

// Two lines with the same GL name would request the same thing twice, in two members, and each name is a GL function
// as SDL_GL_GetProcAddress expects it: it starts with "gl". Repeated members do not compile.
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

// LoadGlFunctions checks the version and the profile before requesting the rest, so an old driver reports its version
// and not the first 3.3 function it lacks.
TEST(GlLoaderTest, TheContextIsAskedFirst)
{
    ASSERT_GE(GlFunctionNames.size(), 2u);
    EXPECT_EQ(GlFunctionNames[0], "glGetString");
    EXPECT_EQ(GlFunctionNames[1], "glGetIntegerv");
}

// The version and profile it reports, the function it lacks and the names requested, in order. Global state because the
// lookup is a function pointer with no capture; the fixture resets it to a full 3.3 core before each test.
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

// Before 3.0, GL_MAJOR_VERSION does not exist: a real driver leaves the integer as it was and sets GL_INVALID_ENUM.
// The fake does the same with a 0.0.
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

// What it gives for the rest: LoadGlFunctions requests them and does not call them.
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

// A 2.1 driver, without the 3.3 functions: LoadGlFunctions checks the context with the first two and requests no more,
// so the error is not the first missing 3.3 function.
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

// A 3.3 without the core profile (the compatibility one) is not enough, and the rest is not requested either.
TEST_F(GlLoaderDriverTest, ACompatibilityContextIsRefused)
{
    s_driver.profileMask = GL_CONTEXT_COMPATIBILITY_PROFILE_BIT;

    const auto gl = LoadGlFunctions(FakeLookup);

    ASSERT_FALSE(gl.HasValue());
    EXPECT_EQ(gl.Error(), "se pidió OpenGL 3.3 core y el driver dio un contexto sin el perfil core (2.1 falso)");
    EXPECT_EQ(s_driver.requested.size(), 2u);
}

// With a 3.3 core, the first missing one stops it, named, and the later ones are not requested.
TEST_F(GlLoaderDriverTest, TheFirstMissingFunctionIsNamed)
{
    s_driver.missing = "glTexImage2D";

    const auto gl = LoadGlFunctions(FakeLookup);

    ASSERT_FALSE(gl.HasValue());
    EXPECT_EQ(gl.Error(), "el driver no tiene glTexImage2D");
    EXPECT_EQ(s_driver.requested.back(), "glTexImage2D");
}

TEST_F(GlLoaderDriverTest, AFullDriverGivesEveryFunction)
{
    const auto gl = LoadGlFunctions(FakeLookup);

    ASSERT_TRUE(gl.HasValue()) << gl.Error();
    EXPECT_EQ(s_driver.requested, (std::vector<std::string>(GlFunctionNames.begin(), GlFunctionNames.end())));
#define MU_EXPECT_LOADED(type, member, name) EXPECT_NE(gl.Value().member, nullptr) << name;
    MU_GL_FUNCTIONS(MU_EXPECT_LOADED)
#undef MU_EXPECT_LOADED
}
} // namespace
