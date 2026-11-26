#include "Render/Gl/GlName.h"

#include "Platform/Sdl/GlLoader.h"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace
{
using Client::Render::Gl::GlName;

// The names RecordDelete deleted, in order. Global because a Deleter is a captureless function pointer; the fixture
// clears it before each test.
std::vector<GLuint> s_deleted;

void RecordDelete(const Client::Platform::Sdl::GlFunctions& /*gl*/, GLuint name)
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

    // An empty table: RecordDelete does not use it.
    const Client::Platform::Sdl::GlFunctions m_gl{};
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

TEST_F(GlNameTest, AMovedNameIsDeletedByItsNewOwner)
{
    {
        GlName first(m_gl, RecordDelete, 7);
        const GlName second(std::move(first));
        EXPECT_EQ(first.Get(), 0u); // NOLINT(bugprone-use-after-move): the test checks the source
        EXPECT_EQ(second.Get(), 7u);
    }
    EXPECT_EQ(s_deleted, (std::vector<GLuint>{7}));
}

// The old name is deleted first, and the received one when it dies.
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

// Default, or holding the 0 glCreateShader returns on failure: no GL call.
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
