#include "support/GlTrace.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>

#include <sstream>
#include <string>
#include <vector>

namespace
{
using Client::Tests::GlTrace;
using Client::Tests::ParseGlTrace;

// The header GLIntercept writes before the first call.
const std::string Header = "===============================================================================\n"
                           " GLIntercept version 1.34 Log generated on: Sat Oct 31 01:16:20 2026\n"
                           " \n"
                           "===============================================================================\n"
                           "\n";

GlTrace Parse(const std::string& text)
{
    std::istringstream input(Header + text);
    auto trace = ParseGlTrace(input);
    EXPECT_TRUE(trace.HasValue()) << (trace.HasValue() ? "" : trace.Error());
    return trace.HasValue() ? trace.Value() : GlTrace{};
}

TEST(GlTraceTest, SplitsTheFramesAtTheSwap)
{
    const GlTrace trace = Parse("glBegin(GL_TRIANGLES)\n"
                                "glVertex3fv([1.000000,2.000000,3.000000])\n"
                                "glEnd()\n"
                                "wglSwapBuffers(2C010564)=true \n"
                                "glBegin(GL_QUADS)\n"
                                "glEnd()\n"
                                "glBegin(GL_TRIANGLES)\n"
                                "glEnd()\n"
                                "wglSwapBuffers(2C010564)=true \n"
                                "glBegin(GL_TRIANGLES)\n"
                                "glEnd()\n");

    ASSERT_EQ(trace.frames.size(), 2u);
    ASSERT_EQ(trace.frames[0].blocks.size(), 1u);
    EXPECT_EQ(trace.frames[0].blocks[0].vertices.size(), 1u);
    ASSERT_EQ(trace.frames[1].blocks.size(), 2u);
    EXPECT_EQ(trace.frames[1].blocks[0].mode, "GL_QUADS");
}

TEST(GlTraceTest, ABlockKeepsItsTexture)
{
    const GlTrace trace = Parse("glBindTexture(GL_TEXTURE_2D,1094)\n"
                                "glColor4f(0.500000,0.250000,1.000000,0.750000)\n"
                                "glBegin(GL_TRIANGLES)\n"
                                "glTexCoord2f(0.000500,0.666500)\n"
                                "glColor3fv([0.426867,0.412833,0.515826])\n"
                                "glVertex3fv([10061.592773,15235.984375,258.374969])\n"
                                "glVertex3fv([1.000000,2.000000,3.000000])\n"
                                "glEnd()\n"
                                "glBegin(GL_QUADS)\n"
                                "glColor4ub(255,0,51,102)\n"
                                "glVertex2f(10.000000,20.000000)\n"
                                "glEnd()\n"
                                "wglSwapBuffers(2C010564)=true \n");

    ASSERT_EQ(trace.frames.size(), 1u);
    const auto& blocks = trace.frames[0].blocks;
    ASSERT_EQ(blocks.size(), 2u);
    EXPECT_EQ(blocks[0].texture, 1094u);
    EXPECT_EQ(blocks[0].mode, "GL_TRIANGLES");
    ASSERT_EQ(blocks[0].vertices.size(), 2u);
    EXPECT_EQ(blocks[0].vertices[0].texCoord, glm::vec2(0.0005f, 0.6665f));
    EXPECT_EQ(blocks[0].vertices[0].color, glm::vec4(0.426867f, 0.412833f, 0.515826f, 1.0f));
    EXPECT_EQ(blocks[0].vertices[0].position, glm::vec3(10061.592773f, 15235.984375f, 258.374969f));
    EXPECT_EQ(blocks[0].vertices[1].texCoord, glm::vec2(0.0005f, 0.6665f));
    EXPECT_EQ(blocks[1].texture, 1094u);
    ASSERT_EQ(blocks[1].vertices.size(), 1u);
    EXPECT_EQ(blocks[1].vertices[0].color, glm::vec4(1.0f, 0.0f, 0.2f, 0.4f));
    EXPECT_EQ(blocks[1].vertices[0].position, glm::vec3(10.0f, 20.0f, 0.0f));
}

// Each block gets GL's blending state at glBegin: GL's initial one (no blending, ONE / ZERO, no alpha test, no
// culling, no depth test and writing depth) until a call changes it. glEnable of something else does not count.
TEST(GlTraceTest, ABlockKeepsTheStateOfGl)
{
    const GlTrace trace = Parse("glBegin(GL_TRIANGLE_FAN)\n"
                                "glEnd()\n"
                                "glEnable(GL_BLEND)\n"
                                "glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA)\n"
                                "glEnable(GL_CULL_FACE)\n"
                                "glDisable(GL_CULL_FACE)\n"
                                "glEnable(GL_ALPHA_TEST)\n"
                                "glEnable(GL_DEPTH_TEST)\n"
                                "glDepthMask(false)\n"
                                "glEnable(GL_TEXTURE_2D)\n"
                                "glBegin(GL_TRIANGLE_FAN)\n"
                                "glEnd()\n"
                                "glDisable(GL_BLEND)\n"
                                "glDepthMask(true)\n"
                                "glBegin(GL_TRIANGLES)\n"
                                "glEnd()\n"
                                "wglSwapBuffers(2C010564)=true \n");

    ASSERT_EQ(trace.frames.size(), 1u);
    const auto& blocks = trace.frames[0].blocks;
    ASSERT_EQ(blocks.size(), 3u);
    EXPECT_EQ(blocks[0].state, Client::Tests::GlTraceState{});
    EXPECT_FALSE(blocks[0].state.blend);
    EXPECT_EQ(blocks[0].state.blendSource, "GL_ONE");
    EXPECT_EQ(blocks[0].state.blendDestination, "GL_ZERO");
    EXPECT_TRUE(blocks[0].state.depthMask);

    EXPECT_TRUE(blocks[1].state.blend);
    EXPECT_EQ(blocks[1].state.blendSource, "GL_SRC_ALPHA");
    EXPECT_EQ(blocks[1].state.blendDestination, "GL_ONE_MINUS_SRC_ALPHA");
    EXPECT_FALSE(blocks[1].state.cullFace);
    EXPECT_TRUE(blocks[1].state.alphaTest);
    EXPECT_TRUE(blocks[1].state.depthTest);
    EXPECT_FALSE(blocks[1].state.depthMask);

    EXPECT_FALSE(blocks[2].state.blend);
    EXPECT_EQ(blocks[2].state.blendSource, "GL_SRC_ALPHA");
    EXPECT_TRUE(blocks[2].state.alphaTest);
    EXPECT_TRUE(blocks[2].state.depthMask);
}

TEST(GlTraceTest, AMalformedStateCallIsAnError)
{
    for (const std::string line : {"glBlendFunc(GL_ONE)", "glDepthMask(maybe)", "glEnable()"})
    {
        std::istringstream input(Header + line + "\n");
        const auto trace = ParseGlTrace(input);
        ASSERT_FALSE(trace.HasValue()) << line;
        EXPECT_NE(trace.Error().find("línea 6"), std::string::npos) << trace.Error();
    }
}

TEST(GlTraceTest, ReadsTheMatricesInOrder)
{
    const GlTrace trace =
        Parse("glMatrixMode(GL_PROJECTION)\n"
              "glPushMatrix()\n"
              "glLoadIdentity()\n"
              "glViewport(0,25,1024,688)\n"
              "glMultMatrixd([1.810660,0.000000,0.000000,0.000000,0.000000,2.414214,0.000000,0.000000,"
              "0.000000,0.000000,-1.002483,-1.000000,0.000000,0.000000,-40.049665,0.000000])\n"
              "glRotatef(-86.999985,1.000000,0.000000,0.000000)\n"
              "glTranslatef(-335.880066,753.116699,-250.000031)\n"
              "glOrtho(0.000000,640.000000,0.000000,480.000000,-1.000000,1.000000)\n"
              "glPopMatrix()\n"
              "wglSwapBuffers(2C010564)=true \n");

    ASSERT_EQ(trace.frames.size(), 1u);
    const auto& frame = trace.frames[0];
    ASSERT_EQ(frame.viewports.size(), 1u);
    EXPECT_EQ(frame.viewports[0].y, 25);
    EXPECT_EQ(frame.viewports[0].height, 688);
    std::vector<std::string> names;
    for (const auto& call : frame.matrices)
    {
        names.push_back(call.name);
    }
    EXPECT_EQ(names, (std::vector<std::string>{"glMatrixMode", "glPushMatrix", "glLoadIdentity", "glMultMatrixd",
                                               "glRotatef", "glTranslatef", "glOrtho", "glPopMatrix"}));
    EXPECT_EQ(frame.matrices[0].argument, "GL_PROJECTION");
    ASSERT_EQ(frame.matrices[3].values.size(), 16u);
    EXPECT_DOUBLE_EQ(frame.matrices[3].values[14], -40.049665);
    EXPECT_EQ(frame.matrices[4].values, (std::vector<double>{-86.999985, 1.0, 0.0, 0.0}));
    EXPECT_EQ(frame.matrices[6].values.size(), 6u);
}

// The number counts the header. After the first call there is no more header: a line that starts with a space is
// also an error, and a frame does not close with an open glBegin.
TEST(GlTraceTest, AMalformedLineSaysItsNumber)
{
    for (const std::string& bad :
         {std::string("glBegin(GL_TRIANGLES)\nglVertex3fv([1.000000,2.000000])\n"),
          std::string("glBegin(GL_TRIANGLES)\nesto no es una llamada\n"),
          std::string("glEnable(GL_BLEND)\nglVertex3fv([1.000000,2.000000,3.000000])\n"),
          std::string("glEnable(GL_BLEND)\nglEnd()\n"), std::string("glBegin(GL_TRIANGLES)\nglBegin(GL_TRIANGLES)\n"),
          std::string("glBegin(GL_TRIANGLES)\nglTexCoord2f(0.5,x)\n"),
          std::string("glBegin(GL_TRIANGLES)\n glVertex3fv([1.000000,2.000000,3.000000])\n"),
          std::string("glEnable(GL_BLEND)\nglBindTexture(GL_TEXTURE_2D,12abc)\n"),
          std::string("glBegin(GL_TRIANGLES)\nwglSwapBuffers(2C010564)=true \n")})
    {
        std::istringstream input(Header + bad);

        const auto trace = ParseGlTrace(input);

        ASSERT_FALSE(trace.HasValue()) << bad;
        EXPECT_NE(trace.Error().find("línea 7"), std::string::npos) << trace.Error();
    }
}
} // namespace
