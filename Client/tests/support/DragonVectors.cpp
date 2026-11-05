#include "support/DragonVectors.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace Client::Tests
{
namespace
{
class Words
{
public:
    explicit Words(const std::string& text) : m_stream(text) {}

    std::string Word()
    {
        std::string word;
        if (!(m_stream >> word))
        {
            throw std::runtime_error("faltan palabras");
        }
        return word;
    }

    std::int32_t Integer()
    {
        return static_cast<std::int32_t>(std::stol(Word()));
    }

    float Float()
    {
        return std::bit_cast<float>(static_cast<std::uint32_t>(std::stoul(Word(), nullptr, 16)));
    }

    glm::vec3 Vec3()
    {
        const float x = Float();
        const float y = Float();
        const float z = Float();
        return {x, y, z};
    }

private:
    std::istringstream m_stream;
};

DragonVectorState StateOf(Words& words)
{
    DragonVectorState state;
    state.alive = words.Integer() != 0;
    state.type = static_cast<std::int16_t>(words.Integer());
    state.scale = words.Float();
    state.position = words.Vec3();
    state.angles = words.Vec3();
    state.life = words.Integer();
    state.velocity = words.Vec3();
    state.speed = words.Float();
    state.turnPending = words.Float();
    state.turnDirection = words.Float();
    state.flagEc = words.Float();
    state.state = static_cast<std::uint8_t>(words.Integer());
    state.reference = words.Float();
    state.frame = words.Float();
    state.previousFrame = words.Float();
    state.action = static_cast<std::uint8_t>(words.Integer());
    state.light = words.Vec3();
    state.alpha = words.Float();
    state.hiddenMesh = words.Integer();
    state.blendMesh = words.Integer();
    state.blendMeshLight = words.Float();
    state.vertexLight = words.Integer() != 0;
    state.start = words.Vec3();
    return state;
}

std::optional<std::string> ReadTheFile(const std::filesystem::path& path, DragonVectors& out, std::string& text)
{
    std::ifstream file(path);
    if (!file)
    {
        return "no se abre " + path.string();
    }
    const auto next = [&file, &text]()
    {
        if (!std::getline(file, text))
        {
            throw std::runtime_error("el archivo se corta");
        }
    };
    next();
    if (text != "dragon-steps 1")
    {
        return "no es un archivo de vectores del dragón";
    }
    next();
    Words created(text);
    created.Word();
    out.created = StateOf(created);
    next();
    Words count(text);
    count.Word();
    const std::int32_t steps = count.Integer();
    out.steps.clear();
    for (std::int32_t k = 0; k < steps; ++k)
    {
        next();
        Words words(text);
        if (words.Word() != "step" || words.Integer() != k + 1)
        {
            return "falta el paso " + std::to_string(k + 1) + ": " + text;
        }
        out.steps.push_back(StateOf(words));
    }
    next();
    Words random(text);
    random.Word();
    out.randomCalls = random.Integer();
    next();
    if (text != "end")
    {
        return "sobra algo después de los pasos: " + text;
    }
    return std::nullopt;
}
} // namespace

std::optional<std::string> ReadDragonVectors(const std::filesystem::path& path, DragonVectors& out)
{
    std::string text;
    try
    {
        return ReadTheFile(path, out, text);
    }
    catch (const std::exception& error)
    {
        return std::string("renglón ilegible (") + error.what() + "): " + text;
    }
}
} // namespace Client::Tests
