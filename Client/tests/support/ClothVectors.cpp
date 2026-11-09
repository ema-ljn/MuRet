#include "support/ClothVectors.h"

#include "Render/Models/HalfLifeMath.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <exception>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace Client::Tests
{
namespace
{
// Un renglón partido en palabras, con lo que se va leyendo.
class Line
{
public:
    explicit Line(const std::string& text) : m_stream(text) {}

    std::string Word()
    {
        std::string word;
        m_stream >> word;
        return word;
    }

    std::int64_t Integer()
    {
        return std::stoll(Word(), nullptr, 0);
    }

    float Bits()
    {
        return std::bit_cast<float>(static_cast<std::uint32_t>(std::stoul(Word(), nullptr, 16)));
    }

    std::optional<float> OptionalBits()
    {
        const std::string word = Word();
        if (word == "-")
        {
            return std::nullopt;
        }
        return std::bit_cast<float>(static_cast<std::uint32_t>(std::stoul(word, nullptr, 16)));
    }

    glm::vec3 Vector()
    {
        const float x = Bits();
        const float y = Bits();
        const float z = Bits();
        return {x, y, z};
    }

private:
    std::istringstream m_stream;
};

template <typename T> T As(std::int64_t value)
{
    return static_cast<T>(value);
}
} // namespace

std::filesystem::path ClothVectorsDir()
{
    return MURET_CLIENT_CLOTH_VECTORS_DIR;
}

namespace
{
std::optional<std::string> ReadTheFile(const std::filesystem::path& path, ClothVectors& out, std::string& text);
} // namespace

std::optional<std::string> ReadClothVectors(const std::filesystem::path& path, ClothVectors& out)
{
    // Un número que no se entiende o un renglón cortado: stoll y stoul tiran; el error lleva el renglón.
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

namespace
{
std::optional<std::string> ReadTheFile(const std::filesystem::path& path, ClothVectors& out, std::string& text)
{
    std::ifstream file(path);
    if (!file)
    {
        return "no se abre " + path.string();
    }
    const auto next = [&file, &text]() -> bool { return static_cast<bool>(std::getline(file, text)); };
    if (!next() || text != "cloth-vectors 1")
    {
        return "no es un archivo de vectores de la tela";
    }
    next();
    Line randomLine(text);
    randomLine.Word();
    out.randomState = As<std::uint32_t>(randomLine.Integer() & 0xffffffff);
    next();
    Line directLine(text);
    directLine.Word();
    const auto directCount = directLine.Integer();
    for (std::int64_t c = 0; c < directCount; ++c)
    {
        next();
        Line cloth(text);
        cloth.Word();
        ClothVectorShape shape;
        shape.bone = As<std::int32_t>(cloth.Integer());
        shape.anchor = cloth.Vector();
        shape.columns = As<std::int32_t>(cloth.Integer());
        shape.rows = As<std::int32_t>(cloth.Integer());
        shape.width = cloth.Bits();
        shape.height = cloth.Bits();
        shape.front = As<std::uint16_t>(cloth.Integer());
        shape.back = As<std::uint16_t>(cloth.Integer());
        shape.flags = As<std::uint32_t>(cloth.Integer());
        shape.windMinimum = As<std::uint8_t>(cloth.Integer());
        shape.windRange = As<std::uint8_t>(cloth.Integer());
        next();
        Line spheres(text);
        spheres.Word();
        const auto sphereCount = spheres.Integer();
        for (std::int64_t s = 0; s < sphereCount; ++s)
        {
            next();
            Line sphere(text);
            ClothVectorShape::Sphere entry;
            entry.local = sphere.Vector();
            entry.radius = sphere.Bits();
            entry.bone = As<std::int32_t>(sphere.Integer());
            shape.spheres.push_back(entry);
        }
        out.direct.push_back(shape);
    }
    next();
    Line framesLine(text);
    framesLine.Word();
    const auto frameCount = framesLine.Integer();
    for (std::int64_t f = 0; f < frameCount; ++f)
    {
        ClothVectorFrame frame;
        next();
        if (Line(text).Word() != "frame")
        {
            return "se esperaba un frame: " + text;
        }
        next();
        Line character(text);
        character.Word();
        frame.characterClass = As<std::uint8_t>(character.Integer());
        frame.armor = As<std::uint16_t>(character.Integer());
        frame.wings = As<std::uint16_t>(character.Integer());
        frame.monster = As<std::uint16_t>(character.Integer());
        frame.objectFlags = As<std::uint32_t>(character.Integer());
        frame.type = As<std::uint16_t>(character.Integer());
        next();
        Line object(text);
        object.Word();
        frame.angles = object.Vector();
        frame.scale = object.Bits();
        frame.origin = object.Vector();
        next();
        Line bones(text);
        bones.Word();
        const auto boneCount = bones.Integer();
        for (std::int64_t b = 0; b < boneCount; ++b)
        {
            next();
            Line row(text);
            Render::Models::BoneTransform m(0.0f);
            // Los 12 floats en el orden de Half-Life: HL[0][0..3], HL[1][0..3], HL[2][0..3].
            for (int r = 0; r < 3; ++r)
            {
                for (int c = 0; c < 4; ++c)
                {
                    m[c][r] = row.Bits();
                }
            }
            frame.bones.push_back(m);
        }
        next();
        Line globals(text);
        globals.Word();
        frame.map = As<std::uint8_t>(globals.Integer());
        frame.worldTime = globals.Bits();
        frame.windBase = globals.OptionalBits();
        frame.counter = As<std::int32_t>(globals.Integer());
        frame.openAccesses = As<std::uint8_t>(globals.Integer());
        frame.windWalks = As<std::int32_t>(globals.Integer());
        const std::string first = globals.Word();
        if (first != "none")
        {
            const float red = std::bit_cast<float>(static_cast<std::uint32_t>(std::stoul(first, nullptr, 16)));
            const float green = globals.Bits();
            const float blue = globals.Bits();
            frame.color = glm::vec3{red, green, blue};
        }
        frame.wears = globals.Integer() != 0;
        frame.dt = globals.OptionalBits();
        frame.windZ = globals.OptionalBits();
        next();
        Line wind(text);
        wind.Word();
        frame.windBaseAfterTheWalks = wind.Bits();
        next();
        if (Line(text).Word() != "events")
        {
            return "se esperaban los eventos: " + text;
        }
        while (next())
        {
            Line event(text);
            const std::string kind = event.Word();
            if (kind == "end")
            {
                frame.clothCount = As<std::int32_t>(event.Integer());
                frame.randoms = As<std::int32_t>(event.Integer());
                break;
            }
            ClothVectorEvent entry;
            if (kind == "step")
            {
                entry.kind = ClothVectorEvent::Kind::Step;
                ClothVectorStep& step = entry.step;
                step.cloth = As<std::int32_t>(event.Integer());
                step.iteration = As<std::int32_t>(event.Integer());
                step.ok = event.Integer() != 0;
                step.wind = event.Bits();
                const auto particles = event.Integer();
                step.full = event.Word() == "full";
                for (std::int64_t p = 0; p < particles; ++p)
                {
                    next();
                    Line particle(text);
                    ClothVectorParticle state;
                    state.position = particle.Vector();
                    if (step.full)
                    {
                        state.velocity = particle.Vector();
                        state.force = particle.Vector();
                    }
                    state.anchored = particle.Integer() != 0;
                    step.particles.push_back(state);
                }
            }
            else if (kind == "face")
            {
                entry.kind = ClothVectorEvent::Kind::Face;
                ClothVectorFace& face = entry.face;
                face.cloth = As<std::int32_t>(event.Integer());
                face.blend = event.Word();
                face.texture = As<std::uint16_t>(event.Integer());
                face.color = event.Vector();
                const auto corners = event.Integer();
                for (std::int64_t c = 0; c < corners; ++c)
                {
                    next();
                    Line corner(text);
                    ClothVectorFace::Corner entryCorner;
                    entryCorner.particle = As<std::int32_t>(corner.Integer());
                    const float u = corner.Bits();
                    const float v = corner.Bits();
                    entryCorner.uv = {u, v};
                    face.corners.push_back(entryCorner);
                }
            }
            else if (kind == "flare")
            {
                entry.kind = ClothVectorEvent::Kind::Flare;
                entry.flare.position = event.Vector();
                entry.flare.scale = event.Bits();
                entry.flare.light = event.Bits();
            }
            else if (kind == "destroy")
            {
                entry.kind = ClothVectorEvent::Kind::Destroy;
            }
            else if (kind == "broken")
            {
                entry.kind = ClothVectorEvent::Kind::Broken;
                entry.broken = As<std::int32_t>(event.Integer());
            }
            else
            {
                return "evento desconocido: " + text;
            }
            frame.events.push_back(entry);
        }
        out.frames.push_back(frame);
    }
    return std::nullopt;
}
} // namespace
} // namespace Client::Tests
