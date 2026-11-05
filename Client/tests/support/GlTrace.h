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

// Una traza de GL de GLIntercept, la del laboratorio (pregunta 12 del plan de la CL6): una llamada
// por línea, como glVertex3fv([10061.592773,15235.984375,258.374969]), con una cabecera de líneas
// que empiezan con "=" o con un espacio. Los frames se cortan en cada wglSwapBuffers; lo que va
// después del último no es un frame. Se lee lo que los tests comparan con el original: los
// viewports, las llamadas de las matrices, los bloques de glBegin a glEnd y el estado de GL de la
// mezcla con que empieza cada uno (glEnable, glDisable, glBlendFunc y glDepthMask). Las demás llamadas
// se saltean, pero una línea que no tiene la forma de una llamada, o una
// de las que se leen con otros argumentos, es un error con su número.
namespace Client::Tests
{
// Un vértice con el estado de GL que tenía al llamar a glVertex: la última UV y el último color.
struct GlTraceVertex
{
    glm::vec2 texCoord{0.0f, 0.0f};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 position{};
};

// El estado de GL que decide cómo se mezcla un bloque, con los valores con que empieza GL: sin
// mezcla, ONE / ZERO, sin alpha test, sin culling, sin depth test y escribiendo la profundidad.
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

// De glBegin a glEnd, con la textura atada al empezar y el estado de la mezcla de ese momento.
struct GlTraceBlock
{
    std::string mode; // GL_TRIANGLES, GL_QUADS…
    std::uint32_t texture = 0;
    std::vector<GlTraceVertex> vertices;
    std::size_t line = 0; // la del glBegin, para los mensajes
    GlTraceState state;
};

struct GlTraceViewport
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// glMatrixMode, glLoadIdentity, glPushMatrix, glPopMatrix, glMultMatrixd, glRotatef, glTranslatef y
// glOrtho, en el orden de la traza. glMatrixMode lleva su modo en `argument`.
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

// El texto de una traza. El error dice la línea.
[[nodiscard]] Core::Expected<GlTrace, std::string> ParseGlTrace(std::istream& input);
// El archivo de una traza, ya descomprimido.
[[nodiscard]] Core::Expected<GlTrace, std::string> ReadGlTrace(const std::filesystem::path& path);
} // namespace Client::Tests
