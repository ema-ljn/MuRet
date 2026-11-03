#include "LabGlTest.h"

#include "HiddenGlWindow.h"

#include "Platform/GlLoader.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <type_traits>

// Labs::HiddenGlWindow y la regla de MURET_CLIENT_REQUIRE_GL (la pregunta 9 del plan de la CL5).
namespace
{
using Labs::IsGlRequired;

using HiddenGlWindowTest = Labs::LabGlTest;

// GlName, LabFramebuffer y LabQuadRenderer guardan un puntero a Gl(): si la ventana se pudiera
// mover o copiar, quedaría colgado. Por eso Create la da en un unique_ptr (revisión de la CL5.0).
static_assert(!std::is_move_constructible_v<Labs::HiddenGlWindow>);
static_assert(!std::is_move_assignable_v<Labs::HiddenGlWindow>);
static_assert(!std::is_copy_constructible_v<Labs::HiddenGlWindow>);
static_assert(!std::is_copy_assignable_v<Labs::HiddenGlWindow>);

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

// La ventana da un contexto 3.3 o más nuevo (Mesa da 4.5), con el perfil core, y es el actual: se
// le puede preguntar sin error.
TEST_F(HiddenGlWindowTest, OpensAThreeThreeCoreContext)
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
} // namespace
