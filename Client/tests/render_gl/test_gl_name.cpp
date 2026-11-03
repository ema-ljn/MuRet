#include "Render/Gl/GlName.h"

#include "Platform/GlLoader.h"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

// Client::Render::Gl::GlName, el RAII sobre un nombre de GL, sin GL: un borrador falso anota qué
// nombres borró. Viene del lab de la CL5.0 (labs/cl5.0-pipeline/gl/test_gl_name.cpp).
namespace
{
using Client::Render::Gl::GlName;

// Los nombres que borró RecordDelete, en orden. Es estado global porque un Deleter es un puntero a
// función, sin captura; el fixture lo vacía antes de cada test.
std::vector<GLuint> s_deleted;

void RecordDelete(const Client::Platform::GlFunctions& /*gl*/, GLuint name)
{
    s_deleted.push_back(name);
}

class GlNameTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        s_deleted.clear();
    }

    // Una tabla vacía: RecordDelete no la usa.
    const Client::Platform::GlFunctions m_gl{};
};

TEST_F(GlNameTest, DeletesItsNameOnce)
{
    {
        const GlName texture(m_gl, RecordDelete, 7);
        EXPECT_EQ(texture.Get(), 7u);
        EXPECT_TRUE(s_deleted.empty());
    }
    EXPECT_EQ(s_deleted, (std::vector<GLuint>{7}));
}

// Mover pasa el nombre: el de origen queda en 0 y no borra nada, y el nuevo dueño lo borra una vez.
TEST_F(GlNameTest, AMovedNameIsDeletedByItsNewOwner)
{
    {
        GlName first(m_gl, RecordDelete, 7);
        const GlName second(std::move(first));
        EXPECT_EQ(first.Get(), 0u); // NOLINT(bugprone-use-after-move): el test mira el de origen
        EXPECT_EQ(second.Get(), 7u);
    }
    EXPECT_EQ(s_deleted, (std::vector<GLuint>{7}));
}

// Asignar por movimiento borra primero el que tenía, y después el que recibió, cuando muere.
TEST_F(GlNameTest, MoveAssignmentDeletesTheOldName)
{
    {
        GlName kept(m_gl, RecordDelete, 8);
        GlName incoming(m_gl, RecordDelete, 7);
        kept = std::move(incoming);
        EXPECT_EQ(s_deleted, (std::vector<GLuint>{8}));
        EXPECT_EQ(kept.Get(), 7u);
    }
    EXPECT_EQ(s_deleted, (std::vector<GLuint>{8, 7}));
}

// Uno por defecto, o con el 0 que da glCreateShader cuando falla, no llama a GL.
TEST_F(GlNameTest, AnEmptyNameDeletesNothing)
{
    {
        const GlName none;
        const GlName failed(m_gl, RecordDelete, 0);
        EXPECT_EQ(none.Get(), 0u);
        EXPECT_EQ(failed.Get(), 0u);
    }
    EXPECT_TRUE(s_deleted.empty());
}
} // namespace
