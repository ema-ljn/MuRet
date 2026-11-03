#include "support/GlTestContext.h"

#include "Platform/GlLoader.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <type_traits>

// Client::Tests::GlTestContext y la regla de MURET_CLIENT_REQUIRE_GL (la pregunta 9 del plan de la
// CL5), como los de HiddenGlWindow en el lab de la CL5.0.
namespace
{
using Client::Tests::GlTestContext;
using Client::Tests::IsGlRequired;

using GlTestContextTest = Client::Tests::GlTest;

// Los nombres de GL guardan un puntero a Gl(): si el contexto se pudiera mover o copiar, quedaría
// colgado. Por eso Create lo da en un unique_ptr.
static_assert(!std::is_move_constructible_v<GlTestContext>);
static_assert(!std::is_move_assignable_v<GlTestContext>);
static_assert(!std::is_copy_constructible_v<GlTestContext>);
static_assert(!std::is_copy_assignable_v<GlTestContext>);

// Solo "1" hace fallar a un test sin GL: sin la variable, vacía o con otro valor, se saltea.
TEST(GlRequirementTest, OnlyOneRequiresGl)
{
    EXPECT_TRUE(IsGlRequired(std::string("1")));
    EXPECT_FALSE(IsGlRequired(std::nullopt));
    EXPECT_FALSE(IsGlRequired(std::string()));
    EXPECT_FALSE(IsGlRequired(std::string("0")));
    EXPECT_FALSE(IsGlRequired(std::string("true")));
    EXPECT_FALSE(IsGlRequired(std::string(" 1")));
}

// El contexto es 3.3 o más nuevo (Mesa da 4.5), con el perfil core, y es el actual: se le puede
// preguntar sin error.
TEST_F(GlTestContextTest, OpensAThreeThreeCoreContext)
{
    GLint major = 0;
    GLint minor = 0;
    GLint profileMask = 0;
    Gl().getIntegerv(GL_MAJOR_VERSION, &major);
    Gl().getIntegerv(GL_MINOR_VERSION, &minor);
    Gl().getIntegerv(GL_CONTEXT_PROFILE_MASK, &profileMask);
    EXPECT_TRUE(Client::Platform::HasRequiredGlVersion(major, minor)) << major << "." << minor;
    EXPECT_TRUE(Client::Platform::IsCoreProfile(profileMask));
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}

// Cada función de la tabla quedó pedida: ninguna en nulo.
TEST_F(GlTestContextTest, LoadsEveryFunction)
{
    const Client::Platform::GlFunctions& gl = Gl();
#define MURET_EXPECT_LOADED(type, member, name) EXPECT_NE(gl.member, nullptr) << name;
    MURET_GL_FUNCTIONS(MURET_EXPECT_LOADED)
#undef MURET_EXPECT_LOADED
}
} // namespace
