#include "support/WorldObjectVectors.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace Client::Tests
{
namespace
{
// Un renglón partido en palabras, de izquierda a derecha.
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

    // Los bits de un float, en hexadecimal.
    float Float()
    {
        return std::bit_cast<float>(static_cast<std::uint32_t>(std::stoul(Word(), nullptr, 16)));
    }

    // Que no sobren palabras en el renglón.
    void End()
    {
        std::string extra;
        if (m_stream >> extra)
        {
            throw std::runtime_error("sobra " + extra);
        }
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

WorldObjectVectorState StateOf(Words& words)
{
    WorldObjectVectorState state;
    state.scale = words.Float();
    state.position = words.Vec3();
    state.angles = words.Vec3();
    state.hiddenMesh = words.Integer();
    state.blendMesh = words.Integer();
    state.blendMeshLight = words.Float();
    state.uv.x = words.Float();
    state.uv.y = words.Float();
    state.speed = words.Float();
    state.vertexLight = words.Integer() != 0;
    state.light = words.Vec3();
    state.alpha = words.Float();
    state.targetAlpha = words.Float();
    state.frame = words.Float();
    state.action = static_cast<std::uint8_t>(words.Integer());
    return state;
}

WorldObjectVectorCall CallOf(Words& words)
{
    WorldObjectVectorCall call;
    call.name = words.Word();
    if (call.name == "RenderBody")
    {
        call.first = words.Integer();
        call.alpha = words.Float();
        call.blendMesh = words.Integer();
        call.blendMeshLight = words.Float();
        call.uv.x = words.Float();
        call.uv.y = words.Float();
        call.hiddenMesh = words.Integer();
    }
    else if (call.name == "RenderMesh")
    {
        call.first = words.Integer();
        call.flags = words.Integer();
        call.alpha = words.Float();
        call.blendMesh = words.Integer();
        call.blendMeshLight = words.Float();
        call.uv.x = words.Float();
        call.uv.y = words.Float();
    }
    else
    {
        throw std::runtime_error("una llamada que no es RenderBody ni RenderMesh");
    }
    words.Integer();
    call.color = words.Vec3();
    call.vertexLight = words.Integer() != 0;
    return call;
}

std::optional<std::string> ReadTheFile(const std::filesystem::path& path, WorldObjectVectors& out, std::string& text)
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
    if (text != "world55-objects 1")
    {
        return "no es un archivo de vectores de los objetos";
    }
    next();
    Words light(text);
    light.Word();
    out.terrainLight = light.Vec3();
    next();
    Words count(text);
    count.Word();
    const std::int32_t objects = count.Integer();
    out.objects.clear();
    for (std::int32_t k = 0; k < objects; ++k)
    {
        WorldObjectVector object;
        next();
        Words head(text);
        if (head.Word() != "object")
        {
            return "falta un objeto: " + text;
        }
        object.index = head.Integer();
        object.type = static_cast<std::int16_t>(head.Integer());
        object.hasModel = head.Integer() != 0;
        if (object.index != k)
        {
            return "el objeto " + std::to_string(k) + " viene con otro índice: " + text;
        }
        head.End();
        next();
        Words created(text);
        if (created.Word() != "created")
        {
            return "falta el estado al crearse: " + text;
        }
        object.created = StateOf(created);
        created.End();
        for (std::size_t k2 = 0; k2 < object.steps.size(); ++k2)
        {
            next();
            Words words(text);
            if (words.Word() != "step" || words.Integer() != static_cast<std::int32_t>(k2 + 1))
            {
                return "falta el paso " + std::to_string(k2 + 1) + ": " + text;
            }
            object.steps[k2] = StateOf(words);
            words.End();
        }
        next();
        Words draw(text);
        if (draw.Word() != "draw")
        {
            return "falta el dibujo: " + text;
        }
        const std::int32_t calls = draw.Integer();
        draw.End();
        for (std::int32_t c = 0; c < calls; ++c)
        {
            next();
            Words words(text);
            if (words.Word() != "call")
            {
                return "falta una llamada: " + text;
            }
            object.calls.push_back(CallOf(words));
            words.End();
        }
        out.objects.push_back(std::move(object));
    }
    next();
    if (text != "end")
    {
        return "sobra algo después de los objetos: " + text;
    }
    return std::nullopt;
}
} // namespace

std::filesystem::path World55VectorsDir()
{
    return MURET_CLIENT_WORLD55_VECTORS_DIR;
}

std::optional<std::string> ReadWorldObjectVectors(const std::filesystem::path& path, WorldObjectVectors& out)
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
