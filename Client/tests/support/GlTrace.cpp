#include "support/GlTrace.h"

#include "Core/Expected.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <istream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Client::Tests
{
namespace
{
using Core::Unexpected;

// Una línea de la traza: el nombre de la llamada y lo que va entre sus paréntesis.
struct Call
{
    std::string_view name;
    std::string_view arguments;
};

std::string_view Trim(std::string_view text)
{
    const std::size_t first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos)
    {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

// "nombre(argumentos)" con lo que siga después del último paréntesis ("=true"), o std::nullopt.
std::optional<Call> SplitCall(std::string_view line)
{
    const std::size_t open = line.find('(');
    const std::size_t close = line.rfind(')');
    if (open == std::string_view::npos || open == 0 || close == std::string_view::npos || close < open)
    {
        return std::nullopt;
    }
    return Call{line.substr(0, open), line.substr(open + 1, close - open - 1)};
}

// Los argumentos separados por comas, sin los corchetes de los arreglos.
std::vector<std::string_view> SplitArguments(std::string_view arguments)
{
    std::vector<std::string_view> parts;
    if (Trim(arguments).empty())
    {
        return parts;
    }
    std::size_t start = 0;
    while (true)
    {
        const std::size_t comma = arguments.find(',', start);
        std::string_view part = Trim(arguments.substr(start, comma - start));
        if (!part.empty() && part.front() == '[')
        {
            part.remove_prefix(1);
        }
        if (!part.empty() && part.back() == ']')
        {
            part.remove_suffix(1);
        }
        parts.push_back(Trim(part));
        if (comma == std::string_view::npos)
        {
            return parts;
        }
        start = comma + 1;
    }
}

// Los números de una llamada, si son exactamente `count`.
template <typename T> std::optional<std::vector<T>> Numbers(std::string_view arguments, std::size_t count)
{
    const std::vector<std::string_view> parts = SplitArguments(arguments);
    if (parts.size() != count)
    {
        return std::nullopt;
    }
    std::vector<T> numbers;
    numbers.reserve(count);
    for (const std::string_view part : parts)
    {
        T value{};
        const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), value);
        if (error != std::errc{} || end != part.data() + part.size())
        {
            return std::nullopt;
        }
        numbers.push_back(value);
    }
    return numbers;
}

// glColor4ub va de 0 a 255.
constexpr float ByteColorScale = 255.0f;

// Cuántos números lleva cada llamada de las matrices; glMatrixMode, glLoadIdentity, glPushMatrix y
// glPopMatrix, ninguno (el modo de glMatrixMode va aparte).
constexpr std::array<std::pair<std::string_view, std::size_t>, 8> MatrixCalls = {{
    {"glMatrixMode", 0},
    {"glLoadIdentity", 0},
    {"glPushMatrix", 0},
    {"glPopMatrix", 0},
    {"glMultMatrixd", 16},
    {"glRotatef", 4},
    {"glTranslatef", 3},
    {"glOrtho", 6},
}};

// Lo que lleva el lector mientras recorre la traza: el estado de GL que importa y el frame de ahora.
// Cada llamada que se lee tiene su función, con el nombre y los argumentos; las demás no hacen nada.
class Reader
{
public:
    // Una línea; un texto con el error si no se entiende.
    std::optional<std::string> Line(std::size_t number, std::string_view line);
    GlTrace Finish()
    {
        return std::move(m_trace);
    }

private:
    using Handler = std::optional<std::string> (Reader::*)(std::string_view name, std::string_view arguments);

    static Handler HandlerOf(std::string_view name);
    std::optional<std::string> Swap(std::string_view name, std::string_view arguments);
    std::optional<std::string> Matrix(std::string_view name, std::string_view arguments);
    std::optional<std::string> Viewport(std::string_view name, std::string_view arguments);
    std::optional<std::string> BindTexture(std::string_view name, std::string_view arguments);
    std::optional<std::string> Begin(std::string_view name, std::string_view arguments);
    std::optional<std::string> End(std::string_view name, std::string_view arguments);
    std::optional<std::string> TexCoord(std::string_view name, std::string_view arguments);
    std::optional<std::string> Color(std::string_view name, std::string_view arguments);
    std::optional<std::string> ByteColor(std::string_view name, std::string_view arguments);
    std::optional<std::string> Vertex(std::string_view name, std::string_view arguments);

    GlTrace m_trace;
    GlTraceFrame m_frame;
    std::optional<GlTraceBlock> m_block;
    std::size_t m_line = 0;
    std::uint32_t m_texture = 0;
    glm::vec2 m_texCoord{0.0f, 0.0f};
    glm::vec4 m_color{1.0f, 1.0f, 1.0f, 1.0f};
};

// El texto del error de una llamada que no lleva `count` números.
std::string NotNumbers(std::string_view name, std::size_t count)
{
    return std::string(name) + " no lleva " + std::to_string(count) + " números";
}

Reader::Handler Reader::HandlerOf(std::string_view name)
{
    constexpr std::array<std::pair<std::string_view, Handler>, 11> Handlers = {{
        {"wglSwapBuffers", &Reader::Swap},
        {"glViewport", &Reader::Viewport},
        {"glBindTexture", &Reader::BindTexture},
        {"glBegin", &Reader::Begin},
        {"glEnd", &Reader::End},
        {"glTexCoord2f", &Reader::TexCoord},
        {"glColor3fv", &Reader::Color},
        {"glColor3f", &Reader::Color},
        {"glColor4f", &Reader::Color},
        {"glColor4ub", &Reader::ByteColor},
        {"glVertex3fv", &Reader::Vertex},
    }};
    for (const auto& [handled, handler] : Handlers)
    {
        if (name == handled)
        {
            return handler;
        }
    }
    if (name == "glVertex2f")
    {
        return &Reader::Vertex;
    }
    for (const auto& [matrix, count] : MatrixCalls)
    {
        if (name == matrix)
        {
            return &Reader::Matrix;
        }
    }
    return nullptr;
}

std::optional<std::string> Reader::Line(std::size_t number, std::string_view line)
{
    const std::optional<Call> call = SplitCall(Trim(line));
    if (!call || line.front() == ' ')
    {
        return "no es una llamada";
    }
    m_line = number;
    const Handler handler = HandlerOf(call->name);
    return handler == nullptr ? std::nullopt : (this->*handler)(call->name, call->arguments);
}

std::optional<std::string> Reader::Swap(std::string_view, std::string_view)
{
    if (m_block)
    {
        return "el frame se cierra con un glBegin abierto";
    }
    m_trace.frames.push_back(std::move(m_frame));
    m_frame = GlTraceFrame{};
    return std::nullopt;
}

std::optional<std::string> Reader::Matrix(std::string_view name, std::string_view arguments)
{
    GlTraceMatrixCall call;
    call.name = name;
    if (name == "glMatrixMode")
    {
        call.argument = Trim(arguments);
        if (call.argument.empty())
        {
            return "glMatrixMode sin modo";
        }
        m_frame.matrices.push_back(std::move(call));
        return std::nullopt;
    }
    std::size_t count = 0;
    for (const auto& [matrix, values] : MatrixCalls)
    {
        count = name == matrix ? values : count;
    }
    std::optional<std::vector<double>> values = Numbers<double>(arguments, count);
    if (!values)
    {
        return NotNumbers(name, count);
    }
    call.values = std::move(*values);
    m_frame.matrices.push_back(std::move(call));
    return std::nullopt;
}

std::optional<std::string> Reader::Viewport(std::string_view, std::string_view arguments)
{
    const std::optional<std::vector<int>> values = Numbers<int>(arguments, 4);
    if (!values)
    {
        return "glViewport no lleva cuatro enteros";
    }
    m_frame.viewports.push_back(GlTraceViewport{(*values)[0], (*values)[1], (*values)[2], (*values)[3]});
    return std::nullopt;
}

std::optional<std::string> Reader::BindTexture(std::string_view, std::string_view arguments)
{
    const std::vector<std::string_view> parts = SplitArguments(arguments);
    std::uint32_t texture = 0;
    if (parts.size() != 2)
    {
        return "glBindTexture no lleva un destino y un número";
    }
    const auto [end, error] = std::from_chars(parts[1].data(), parts[1].data() + parts[1].size(), texture);
    if (error != std::errc{} || end != parts[1].data() + parts[1].size())
    {
        return "glBindTexture no lleva un destino y un número";
    }
    m_texture = texture;
    return std::nullopt;
}

std::optional<std::string> Reader::Begin(std::string_view, std::string_view arguments)
{
    if (m_block)
    {
        return "un glBegin dentro de otro";
    }
    m_block = GlTraceBlock{std::string(Trim(arguments)), m_texture, {}, m_line};
    return std::nullopt;
}

std::optional<std::string> Reader::End(std::string_view, std::string_view)
{
    if (!m_block)
    {
        return "un glEnd sin glBegin";
    }
    m_frame.blocks.push_back(std::move(*m_block));
    m_block.reset();
    return std::nullopt;
}

std::optional<std::string> Reader::TexCoord(std::string_view name, std::string_view arguments)
{
    const std::optional<std::vector<float>> values = Numbers<float>(arguments, 2);
    if (!values)
    {
        return NotNumbers(name, 2);
    }
    m_texCoord = {(*values)[0], (*values)[1]};
    return std::nullopt;
}

// glColor3fv y glColor3f, con alfa 1, y glColor4f.
std::optional<std::string> Reader::Color(std::string_view name, std::string_view arguments)
{
    const std::size_t count = name == "glColor4f" ? 4 : 3;
    const std::optional<std::vector<float>> values = Numbers<float>(arguments, count);
    if (!values)
    {
        return NotNumbers(name, count);
    }
    m_color = {(*values)[0], (*values)[1], (*values)[2], count == 4 ? (*values)[3] : 1.0f};
    return std::nullopt;
}

std::optional<std::string> Reader::ByteColor(std::string_view name, std::string_view arguments)
{
    const std::optional<std::vector<int>> values = Numbers<int>(arguments, 4);
    if (!values)
    {
        return NotNumbers(name, 4);
    }
    for (int i = 0; i < 4; ++i)
    {
        m_color[i] = static_cast<float>((*values)[static_cast<std::size_t>(i)]) / ByteColorScale;
    }
    return std::nullopt;
}

// glVertex3fv y glVertex2f, con z en 0.
std::optional<std::string> Reader::Vertex(std::string_view name, std::string_view arguments)
{
    const std::size_t count = name == "glVertex3fv" ? 3 : 2;
    const std::optional<std::vector<float>> values = Numbers<float>(arguments, count);
    if (!values)
    {
        return NotNumbers(name, count);
    }
    if (!m_block)
    {
        return "un vértice fuera de glBegin";
    }
    const glm::vec3 position{(*values)[0], (*values)[1], count == 3 ? (*values)[2] : 0.0f};
    m_block->vertices.push_back(GlTraceVertex{m_texCoord, m_color, position});
    return std::nullopt;
}

// La cabecera de GLIntercept, antes de la primera llamada: líneas vacías, o que empiezan con "=" o con
// un espacio.
bool IsHeader(std::string_view line)
{
    return Trim(line).empty() || line.front() == '=' || line.front() == ' ';
}
} // namespace

Core::Expected<GlTrace, std::string> ParseGlTrace(std::istream& input)
{
    Reader reader;
    std::string line;
    std::size_t number = 0;
    bool header = true;
    while (std::getline(input, line))
    {
        ++number;
        if (header && IsHeader(line))
        {
            continue;
        }
        header = false;
        if (const std::optional<std::string> error = reader.Line(number, line))
        {
            return Unexpected("línea " + std::to_string(number) + ": " + *error + ": " + line);
        }
    }
    return reader.Finish();
}

Core::Expected<GlTrace, std::string> ReadGlTrace(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input)
    {
        return Unexpected("no se pudo abrir " + path.string());
    }
    return ParseGlTrace(input);
}
} // namespace Client::Tests
