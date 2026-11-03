#include "support/GlTrace.h"

#include "Core/Expected.h"

#include <glm/ext/vector_float2.hpp>
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

// Lo que lleva el lector mientras recorre la traza: el estado de GL que importa y el frame de ahora.
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
    std::optional<std::string> Matrix(std::string_view name, std::string_view arguments);
    std::optional<std::string> Vertex(const std::vector<float>& position);

    GlTrace m_trace;
    GlTraceFrame m_frame;
    std::optional<GlTraceBlock> m_block;
    std::uint32_t m_texture = 0;
    glm::vec2 m_texCoord{0.0f, 0.0f};
    glm::vec4 m_color{1.0f, 1.0f, 1.0f, 1.0f};
};

constexpr std::array<std::string_view, 8> MatrixCalls = {"glMatrixMode", "glLoadIdentity", "glPushMatrix",
                                                         "glPopMatrix",  "glMultMatrixd",  "glRotatef",
                                                         "glTranslatef", "glOrtho"};

// Cuántos números lleva cada llamada de las matrices; glMatrixMode, ninguno (su modo va aparte).
std::size_t MatrixValues(std::string_view name)
{
    if (name == "glMultMatrixd")
    {
        return 16;
    }
    if (name == "glRotatef")
    {
        return 4;
    }
    if (name == "glTranslatef")
    {
        return 3;
    }
    if (name == "glOrtho")
    {
        return 6;
    }
    return 0;
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
    }
    else
    {
        const std::size_t count = MatrixValues(name);
        std::optional<std::vector<double>> values = Numbers<double>(arguments, count);
        if (!values)
        {
            return std::string(name) + " no lleva " + std::to_string(count) + " números";
        }
        call.values = std::move(*values);
    }
    m_frame.matrices.push_back(std::move(call));
    return std::nullopt;
}

std::optional<std::string> Reader::Vertex(const std::vector<float>& position)
{
    if (!m_block)
    {
        return "un vértice fuera de glBegin";
    }
    m_block->vertices.push_back(
        GlTraceVertex{m_texCoord, m_color, {position[0], position[1], position.size() > 2 ? position[2] : 0.0f}});
    return std::nullopt;
}

std::optional<std::string> Reader::Line(std::size_t number, std::string_view line)
{
    const std::optional<Call> call = SplitCall(Trim(line));
    if (!call)
    {
        return "no es una llamada";
    }
    const std::string_view name = call->name;
    const std::string_view arguments = call->arguments;

    if (name == "wglSwapBuffers")
    {
        if (m_block)
        {
            return "el frame se cierra con un glBegin abierto";
        }
        m_trace.frames.push_back(std::move(m_frame));
        m_frame = GlTraceFrame{};
        return std::nullopt;
    }
    for (const std::string_view matrix : MatrixCalls)
    {
        if (name == matrix)
        {
            return Matrix(name, arguments);
        }
    }
    if (name == "glViewport")
    {
        const std::optional<std::vector<int>> values = Numbers<int>(arguments, 4);
        if (!values)
        {
            return "glViewport no lleva cuatro enteros";
        }
        m_frame.viewports.push_back(GlTraceViewport{(*values)[0], (*values)[1], (*values)[2], (*values)[3]});
        return std::nullopt;
    }
    if (name == "glBindTexture")
    {
        const std::vector<std::string_view> parts = SplitArguments(arguments);
        std::uint32_t texture = 0;
        if (parts.size() != 2 ||
            std::from_chars(parts[1].data(), parts[1].data() + parts[1].size(), texture).ec != std::errc{})
        {
            return "glBindTexture no lleva un destino y un número";
        }
        m_texture = texture;
        return std::nullopt;
    }
    if (name == "glBegin")
    {
        if (m_block)
        {
            return "un glBegin dentro de otro";
        }
        m_block = GlTraceBlock{std::string(Trim(arguments)), m_texture, {}, number};
        return std::nullopt;
    }
    if (name == "glEnd")
    {
        if (!m_block)
        {
            return "un glEnd sin glBegin";
        }
        m_frame.blocks.push_back(std::move(*m_block));
        m_block.reset();
        return std::nullopt;
    }
    if (name == "glTexCoord2f")
    {
        const std::optional<std::vector<float>> values = Numbers<float>(arguments, 2);
        if (!values)
        {
            return "glTexCoord2f no lleva dos números";
        }
        m_texCoord = {(*values)[0], (*values)[1]};
        return std::nullopt;
    }
    if (name == "glColor3fv" || name == "glColor3f")
    {
        const std::optional<std::vector<float>> values = Numbers<float>(arguments, 3);
        if (!values)
        {
            return std::string(name) + " no lleva tres números";
        }
        m_color = {(*values)[0], (*values)[1], (*values)[2], 1.0f};
        return std::nullopt;
    }
    if (name == "glColor4f")
    {
        const std::optional<std::vector<float>> values = Numbers<float>(arguments, 4);
        if (!values)
        {
            return "glColor4f no lleva cuatro números";
        }
        m_color = {(*values)[0], (*values)[1], (*values)[2], (*values)[3]};
        return std::nullopt;
    }
    if (name == "glColor4ub")
    {
        const std::optional<std::vector<int>> values = Numbers<int>(arguments, 4);
        if (!values)
        {
            return "glColor4ub no lleva cuatro enteros";
        }
        glm::vec4 color{};
        for (int i = 0; i < 4; ++i)
        {
            color[i] = static_cast<float>((*values)[static_cast<std::size_t>(i)]) / 255.0f;
        }
        m_color = color;
        return std::nullopt;
    }
    if (name == "glVertex3fv" || name == "glVertex2f")
    {
        const std::size_t count = name == "glVertex3fv" ? 3 : 2;
        const std::optional<std::vector<float>> values = Numbers<float>(arguments, count);
        if (!values)
        {
            return std::string(name) + " no lleva " + std::to_string(count) + " números";
        }
        return Vertex(*values);
    }
    return std::nullopt;
}

// La cabecera de GLIntercept: líneas vacías, o que empiezan con "=" o con un espacio.
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
    while (std::getline(input, line))
    {
        ++number;
        if (IsHeader(line))
        {
            continue;
        }
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
