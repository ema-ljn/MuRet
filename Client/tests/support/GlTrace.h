#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <string>
#include <vector>

// A GLIntercept GL trace, the lab's (question 12 of CL6's plan): one call per line, like
// glVertex3fv([10061.592773,15235.984375,258.374969]), with a header of lines that start with "=" or a space. Frames
// end at each wglSwapBuffers; what follows the last one is not a frame. It reads what the tests compare with the
// original: the viewports, the matrix calls, the glBegin to glEnd blocks and the blending GL state each starts with
// (glEnable, glDisable, glBlendFunc and glDepthMask). Other calls are skipped, but a line that does not have a call's
// form, or one of the calls it reads with other arguments, is an error with its number.
namespace Client::Tests
{
// A vertex with the GL state it had when glVertex was called: the last UV and the last color.
struct GlTraceVertex
{
    glm::vec2 texCoord{0.0f, 0.0f};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 position{};
};

// The GL state that decides how a block blends, with GL's initial values: no blending, ONE / ZERO, no alpha test, no
// culling, no depth test and writing depth.
struct GlTraceState
{
    bool blend = false;
    std::string blendSource = "GL_ONE";
    std::string blendDestination = "GL_ZERO";
    bool alphaTest = false;
    bool cullFace = false;
    bool depthTest = false;
    bool depthMask = true;

    friend bool operator==(const GlTraceState&, const GlTraceState&) = default;
};

// From glBegin to glEnd, with the texture bound at the start and the blending state of that moment.
struct GlTraceBlock
{
    std::string mode; // GL_TRIANGLES, GL_QUADS…
    std::uint32_t texture = 0;
    std::vector<GlTraceVertex> vertices;
    std::size_t line = 0; // the glBegin's, for the messages
    GlTraceState state;
};

struct GlTraceViewport
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// glMatrixMode, glLoadIdentity, glPushMatrix, glPopMatrix, glMultMatrixd, glRotatef, glTranslatef and glOrtho, in the
// trace's order. glMatrixMode carries its mode in `argument`.
struct GlTraceMatrixCall
{
    std::string name;
    std::string argument;
    std::vector<double> values;
};

struct GlTraceFrame
{
    std::vector<GlTraceViewport> viewports;
    std::vector<GlTraceMatrixCall> matrices;
    std::vector<GlTraceBlock> blocks;
};

struct GlTrace
{
    std::vector<GlTraceFrame> frames;
};

// A trace's text. The error names the line.
[[nodiscard]] Core::Expected<GlTrace, std::string> ParseGlTrace(std::istream& input);
// A trace's file, already decompressed.
[[nodiscard]] Core::Expected<GlTrace, std::string> ReadGlTrace(const std::filesystem::path& path);
} // namespace Client::Tests
