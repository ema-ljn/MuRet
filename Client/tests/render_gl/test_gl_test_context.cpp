#include "support/GlTestContext.h"

#include "Platform/Sdl/GlFunctionList.h"
#include "Platform/Sdl/GlLoader.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <type_traits>

namespace
{
using Client::Tests::GlTestContext;
using Client::Tests::IsGlRequired;

using GlTestContextTest = Client::Tests::GlTest;

// GL names keep a pointer to Gl(): if the context could be moved or copied, it would dangle. So Create returns it in a
// unique_ptr.
static_assert(!std::is_move_constructible_v<GlTestContext>);
static_assert(!std::is_move_assignable_v<GlTestContext>);
static_assert(!std::is_copy_constructible_v<GlTestContext>);
static_assert(!std::is_copy_assignable_v<GlTestContext>);

// Only "1" fails a test without GL: unset, empty or any other value skips it.
TEST(GlRequirementTest, OnlyOneRequiresGl)
{
    EXPECT_TRUE(IsGlRequired(std::string("1")));
    EXPECT_FALSE(IsGlRequired(std::nullopt));
    EXPECT_FALSE(IsGlRequired(std::string()));
    EXPECT_FALSE(IsGlRequired(std::string("0")));
    EXPECT_FALSE(IsGlRequired(std::string("true")));
    EXPECT_FALSE(IsGlRequired(std::string(" 1")));
}

// The context is 3.3 or newer (Mesa gives 4.5), core profile, and current: it can be queried without error.
TEST_F(GlTestContextTest, OpensAThreeThreeCoreContext)
{
    GLint major = 0;
    GLint minor = 0;
    GLint profileMask = 0;
    Gl().getIntegerv(GL_MAJOR_VERSION, &major);
    Gl().getIntegerv(GL_MINOR_VERSION, &minor);
    Gl().getIntegerv(GL_CONTEXT_PROFILE_MASK, &profileMask);
    EXPECT_TRUE(Client::Platform::Sdl::HasRequiredGlVersion(major, minor)) << major << "." << minor;
    EXPECT_TRUE(Client::Platform::Sdl::IsCoreProfile(profileMask));
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST_F(GlTestContextTest, LoadsEveryFunction)
{
    const Client::Platform::Sdl::GlFunctions& gl = Gl();
#define MU_EXPECT_LOADED(type, member, name) EXPECT_NE(gl.member, nullptr) << name;
    MU_GL_FUNCTIONS(MU_EXPECT_LOADED)
#undef MU_EXPECT_LOADED
}
} // namespace
