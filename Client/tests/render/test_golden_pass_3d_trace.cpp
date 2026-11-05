#include "App/WindowState.h"
#include "Render/Pass3D.h"
#include "Scenes/SceneCameras.h"
#include "World/GameCamera.h"

#include "support/GlTrace.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_int4.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

// Client::Render::MakePass3D contra los pases 3D de las trazas de GL del laboratorio
// (MURET_CLIENT_GL_TRACE_DIR; 06 § Cámara): cada glViewport, el glMultMatrixd de su perspectiva y
// los glRotatef y el glTranslatef de su vista. En la selección, los dos pases con la cámara de la
// escena 4 (SelectionCamera): la franja de arriba, 0x620db0(0, 0, 640, 48), y el mundo,
// 0x620db0(0, 25, 640, 430). En Lorencia la cámara se mueve: el pase del mundo,
// 0x620db0(0, 0, 640, 432), con la cámara calculada desde el héroe de la traza, da su perspectiva y su
// vista. Antes de esos, cada frame de la selección tiene una perspectiva de 45° que no dibuja nada y
// la del 2D, que no son de una cámara de MuRet.
namespace
{
using Client::App::PixelSize;
using Client::Render::Camera3D;
using Client::Render::MakePass3D;
using Client::Render::Pass3D;
using Client::Render::ViewportRect;
using Client::Scenes::SelectionCamera;
using Client::Scenes::WorldViewport;
using Client::Tests::GlTrace;
using Client::Tests::GlTraceFrame;
using Client::Tests::GlTraceMatrixCall;
using Client::Tests::GlTraceViewport;
using Client::Tests::ReadGlTrace;

constexpr std::string_view SelectionTrace = "character-selection-Frame_003207.txt";
constexpr std::string_view LorenciaTrace = "lorencia-Frame_016215.txt";

// La ventana de las dos trazas.
constexpr PixelSize TraceWindow{1024, 768};

// La franja de arriba y el mundo de Lorencia (06 § Cámara).
constexpr ViewportRect TopStrip{0, 0, 640, 48};
constexpr ViewportRect LorenciaWorld{0, 0, 640, 432};

// La traza escribe seis decimales: lo que la perspectiva puede diferir, relativo a su valor, y lo
// que puede diferir la vista, que lleva coordenadas de 15.000.
constexpr double ProjectionTolerance = 2e-6;
constexpr float ViewTolerance = 0.02f;

// Un pase de la traza: su viewport, la perspectiva (por columnas, como glMultMatrixd) y los
// glRotatef y glTranslatef que siguen, en orden: los cuatro primeros son los de la cámara, y en
// Lorencia siguen los de lo que dibuja el pase.
struct TracedPass
{
    GlTraceViewport viewport;
    std::vector<double> projection;
    std::vector<GlTraceMatrixCall> view;
};

// Cada glViewport de un frame lleva justo un glMultMatrixd detrás: el pase n es el viewport n con el
// glMultMatrixd n y lo que sigue hasta el próximo.
std::vector<TracedPass> PassesOf(const GlTraceFrame& frame)
{
    std::vector<TracedPass> passes;
    for (const GlTraceMatrixCall& call : frame.matrices)
    {
        if (call.name == "glMultMatrixd")
        {
            TracedPass pass;
            if (passes.size() < frame.viewports.size())
            {
                pass.viewport = frame.viewports[passes.size()];
            }
            pass.projection = call.values;
            passes.push_back(std::move(pass));
        }
        else if (!passes.empty() && (call.name == "glRotatef" || call.name == "glTranslatef"))
        {
            passes.back().view.push_back(call);
        }
    }
    return passes;
}

const TracedPass* FindPass(const std::vector<TracedPass>& passes, const glm::ivec4& viewport)
{
    for (const TracedPass& pass : passes)
    {
        if (glm::ivec4(pass.viewport.x, pass.viewport.y, pass.viewport.width, pass.viewport.height) == viewport)
        {
            return &pass;
        }
    }
    return nullptr;
}

// La matriz de glRotatef(ángulo, eje) de la especificación de GL, por columnas.
glm::mat4 GlRotation(double degrees, const glm::vec3& axis)
{
    const double radians = degrees * std::numbers::pi / 180.0;
    const auto c = static_cast<float>(std::cos(radians));
    const auto s = static_cast<float>(std::sin(radians));
    const glm::vec3 a = axis;
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(a.x * a.x * (1 - c) + c, a.y * a.x * (1 - c) + a.z * s, a.x * a.z * (1 - c) - a.y * s, 0);
    m[1] = glm::vec4(a.x * a.y * (1 - c) - a.z * s, a.y * a.y * (1 - c) + c, a.y * a.z * (1 - c) + a.x * s, 0);
    m[2] = glm::vec4(a.x * a.z * (1 - c) + a.y * s, a.y * a.z * (1 - c) - a.x * s, a.z * a.z * (1 - c) + c, 0);
    return m;
}

// Las llamadas de la cámara: tres glRotatef y un glTranslatef.
constexpr std::size_t CameraCalls = 4;

// La vista de la cámara de la traza: sus tres glRotatef y su glTranslatef multiplicados en orden,
// como la pila de GL.
glm::mat4 TracedView(const TracedPass& pass)
{
    glm::mat4 view(1.0f);
    for (std::size_t k = 0; k < CameraCalls && k < pass.view.size(); ++k)
    {
        const GlTraceMatrixCall& call = pass.view[k];
        if (call.name == "glRotatef" && call.values.size() == 4)
        {
            const glm::vec3 axis(static_cast<float>(call.values[1]), static_cast<float>(call.values[2]),
                                 static_cast<float>(call.values[3]));
            view = view * GlRotation(call.values[0], axis);
        }
        else if (call.name == "glTranslatef" && call.values.size() == 3)
        {
            glm::mat4 translation(1.0f);
            translation[3] = glm::vec4(static_cast<float>(call.values[0]), static_cast<float>(call.values[1]),
                                       static_cast<float>(call.values[2]), 1.0f);
            view = view * translation;
        }
    }
    return view;
}

void ExpectTheProjection(const Pass3D& pass, const TracedPass& traced, std::size_t frame)
{
    ASSERT_EQ(traced.projection.size(), 16u) << "frame " << frame;
    const float* mine = glm::value_ptr(pass.projection);
    for (std::size_t k = 0; k < 16; ++k)
    {
        const double expected = traced.projection[k];
        EXPECT_NEAR(static_cast<double>(mine[k]), expected, ProjectionTolerance * std::max(1.0, std::abs(expected)))
            << "frame " << frame << ", la " << k << " de la perspectiva";
    }
}

void ExpectTheView(const Pass3D& pass, const TracedPass& traced, std::size_t frame)
{
    ASSERT_GE(traced.view.size(), CameraCalls) << "frame " << frame << ": tres glRotatef y un glTranslatef";
    const glm::mat4 expected = TracedView(traced);
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            EXPECT_NEAR(pass.view[column][row], expected[column][row], ViewTolerance)
                << "frame " << frame << ", la vista en [" << column << "][" << row << "]";
        }
    }
}

class GoldenPass3DTrace : public Client::Tests::GoldenAndGlTraceTest
{
protected:
    std::optional<GlTrace> LoadTrace(std::string_view name) const
    {
        const auto path = TraceFile(name);
        if (!path)
        {
            return std::nullopt;
        }
        auto trace = ReadGlTrace(*path);
        if (!trace.HasValue())
        {
            ADD_FAILURE() << name << ": " << trace.Error();
            return std::nullopt;
        }
        return std::move(trace).Value();
    }
};

// En cada frame de la selección, la franja y el mundo con SelectionCamera dan el viewport, la
// perspectiva y la vista de la traza.
TEST_F(GoldenPass3DTrace, TheSelectionCameraPassesMatch)
{
    const std::optional<GlTrace> trace = LoadTrace(SelectionTrace);
    ASSERT_TRUE(trace.has_value());
    ASSERT_FALSE(trace->frames.empty());

    for (std::size_t f = 0; f < trace->frames.size(); ++f)
    {
        const std::vector<TracedPass> passes = PassesOf(trace->frames[f]);
        for (const ViewportRect& rect : {TopStrip, WorldViewport})
        {
            const Pass3D pass = MakePass3D(TraceWindow, rect, SelectionCamera());
            const TracedPass* traced = FindPass(passes, pass.viewport);
            ASSERT_NE(traced, nullptr) << "frame " << f << ": ningún glViewport es el del pase de (" << rect.x << ", "
                                       << rect.y << ", " << rect.width << ", " << rect.height << ")";
            ExpectTheProjection(pass, *traced, f);
            ExpectTheView(pass, *traced, f);
        }
    }
}

// CL7.3 A: la cámara se calcula desde el héroe, sin tomar de la propia traza lo que se verifica.
TEST_F(GoldenPass3DTrace, TheLorenciaWorldPassMatches)
{
    const std::optional<GlTrace> trace = LoadTrace(LorenciaTrace);
    ASSERT_TRUE(trace.has_value());
    ASSERT_FALSE(trace->frames.empty());

    Client::World::GameCamera state;
    const Client::World::GameCameraInput input{.hero = {14350.0f, 12050.0f, 165.0f}, .cellAttribute = 0x01};
    for (std::size_t f = 0; f < trace->frames.size(); ++f)
    {
        const std::vector<TracedPass> passes = PassesOf(trace->frames[f]);
        const Camera3D camera = Client::World::StepGameCamera(state, input);
        const glm::ivec4 viewport = MakePass3D(TraceWindow, LorenciaWorld, camera).viewport;
        const TracedPass* traced = FindPass(passes, viewport);
        ASSERT_NE(traced, nullptr) << "frame " << f << ": ningún glViewport es el del mundo";
        ASSERT_GE(traced->view.size(), CameraCalls) << "frame " << f;
        const Pass3D pass = MakePass3D(TraceWindow, LorenciaWorld, camera);

        ExpectTheProjection(pass, *traced, f);
        ExpectTheView(pass, *traced, f);
    }
}
} // namespace
