#include "Scenes/CharacterCloth.h"

#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/Cloth.h"
#include "Render/Pass3D.h"
#include "Render/SpriteVertex.h"
#include "support/ClothVectors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Client::Scenes::CharacterCloth: el bloque 0x50d58b..0x50db57 de 0x50ba30, frame por frame, contra el
// código real del main.exe emulado (tests/vectors/cloth): qué telas arma, la cuarta, la rotura y el
// rearmado, el estado de cada una al final de sus cinco pasos y cada triángulo que dibuja.
namespace
{
using Client::Render::ModelBatch;
using Client::Render::ModelBlend;
using Client::Render::Models::Cloth;
using Client::Render::Models::ClothBody;
using Client::Render::Models::ClothWorld;
using Client::Render::Models::CrtRandom;
using Client::Scenes::CharacterCloth;
using Client::Scenes::ClothWearer;
using Client::Tests::ClothVectorEvent;
using Client::Tests::ClothVectorFrame;
using Client::Tests::ClothVectors;
using Client::Tests::ClothVectorStep;

std::uint32_t Bits(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

void ExpectSameBits(const glm::vec3& ours, const glm::vec3& binary, const std::string& where)
{
    for (int k = 0; k < 3; ++k)
    {
        EXPECT_EQ(Bits(ours[k]), Bits(binary[k]))
            << where << " eje " << k << ": " << ours[k] << " contra " << binary[k];
    }
}

std::string NameOf(ModelBlend blend)
{
    switch (blend)
    {
    case ModelBlend::Opaque:
        return "opaque";
    case ModelBlend::Alpha:
        return "alpha";
    case ModelBlend::Additive:
        return "additive";
    default:
        return "?";
    }
}

// Un vértice del lote, con el estado de su tramo.
struct DrawnVertex
{
    std::uint16_t texture = 0;
    std::string blend;
    Client::Render::ModelVertex vertex;
};

std::vector<DrawnVertex> Flatten(const ModelBatch& batch)
{
    std::vector<DrawnVertex> out;
    for (const Client::Render::ModelDrawCommand& command : batch.Commands())
    {
        for (std::size_t v = 0; v < command.vertexCount; ++v)
        {
            out.push_back(DrawnVertex{.texture = command.state.texture ? command.state.texture->value
                                                                       : static_cast<std::uint16_t>(0),
                                      .blend = NameOf(command.state.blend),
                                      .vertex = batch.Vertices()[command.firstVertex + v]});
        }
    }
    return out;
}

void ExpectTheBlockCase(const std::string& name)
{
    ClothVectors vectors;
    const auto error = Client::Tests::ReadClothVectors(Client::Tests::ClothVectorsDir() / name, vectors);
    ASSERT_FALSE(error.has_value()) << *error;
    ClothWorld world{.random = CrtRandom(vectors.randomState)};
    CharacterCloth cloth;
    std::int64_t randoms = 0;
    for (std::size_t f = 0; f < vectors.frames.size(); ++f)
    {
        const ClothVectorFrame& frame = vectors.frames[f];
        const std::string where = name + " frame " + std::to_string(f);
        if (frame.windBase)
        {
            world.windBase = *frame.windBase;
        }
        world.logicSteps = frame.counter;
        world.openCounterAccesses = frame.openAccesses;
        for (int walk = 0; walk < frame.windWalks; ++walk)
        {
            Client::Render::Models::WalkTheWind(world);
        }
        EXPECT_EQ(Bits(world.windBase), Bits(frame.windBaseAfterTheWalks)) << where;
        const ClothBody body{
            .bones = frame.bones, .angles = frame.angles, .scale = frame.scale, .position = frame.origin};
        ClothWearer wearer;
        wearer.characterClass = frame.characterClass;
        wearer.armor = frame.armor;
        wearer.objectFlags = frame.objectFlags;
        if (frame.wings != 0xffff)
        {
            wearer.wings = frame.wings;
        }
        if (frame.monster != 0xffff)
        {
            wearer.monster = frame.monster;
        }
        ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        cloth.Frame(batch, wearer, body, world, frame.map, frame.worldTime, frame.wears,
                    frame.color.value_or(Client::Scenes::PlainClothColor));

        // Lo que dejó el binario: el último paso de cada tela, sus caras, los sprites y si se rompió.
        std::map<std::int32_t, ClothVectorStep> lastStep;
        std::vector<DrawnVertex> expected;
        std::vector<Client::Tests::ClothVectorFlare> flares;
        bool destroyed = false;
        for (const ClothVectorEvent& event : frame.events)
        {
            switch (event.kind)
            {
            case ClothVectorEvent::Kind::Step:
                lastStep[event.step.cloth] = event.step;
                break;
            case ClothVectorEvent::Kind::Flare:
                flares.push_back(event.flare);
                break;
            case ClothVectorEvent::Kind::Destroy:
                destroyed = true;
                break;
            case ClothVectorEvent::Kind::Face:
            {
                const auto& face = event.face;
                const ClothVectorStep& step = lastStep.at(face.cloth);
                const Client::Render::Color color{face.color.r, face.color.g, face.color.b, 1.0f};
                for (std::size_t q = 0; q + 3 < face.corners.size(); q += 4)
                {
                    for (const std::size_t c : {0u, 1u, 2u, 0u, 2u, 3u})
                    {
                        const auto& corner = face.corners[q + c];
                        expected.push_back(DrawnVertex{
                            .texture = face.texture,
                            .blend = face.blend,
                            .vertex = Client::Render::ModelVertex{
                                .position = step.particles[static_cast<std::size_t>(corner.particle)].position,
                                .uv = corner.uv,
                                .color = color}});
                    }
                }
                break;
            }
            case ClothVectorEvent::Kind::Broken:
                break;
            }
        }

        EXPECT_EQ(static_cast<std::int32_t>(cloth.Count()), frame.clothCount) << where << ": objeto + 0x18c";
        if (destroyed)
        {
            EXPECT_TRUE(cloth.Cloths().empty()) << where << ": la rotura las suelta";
        }
        else
        {
            for (const auto& [index, step] : lastStep)
            {
                ASSERT_LT(static_cast<std::size_t>(index), cloth.Cloths().size()) << where;
                const Cloth& ours = cloth.Cloths()[static_cast<std::size_t>(index)];
                ASSERT_EQ(ours.Particles().size(), step.particles.size()) << where << " tela " << index;
                EXPECT_EQ(Bits(ours.Wind()), Bits(step.wind)) << where << " tela " << index << ": el viento";
                for (std::size_t p = 0; p < step.particles.size(); ++p)
                {
                    const std::string particle =
                        where + " tela " + std::to_string(index) + " partícula " + std::to_string(p);
                    ExpectSameBits(ours.Particles()[p].position, step.particles[p].position, particle + " posición");
                    if (step.particles[p].velocity)
                    {
                        ExpectSameBits(ours.Particles()[p].velocity, *step.particles[p].velocity,
                                       particle + " velocidad");
                    }
                    if (step.particles[p].force)
                    {
                        ExpectSameBits(ours.Particles()[p].force, *step.particles[p].force, particle + " fuerza");
                    }
                }
            }
        }

        const std::vector<DrawnVertex> drawn = Flatten(batch);
        ASSERT_EQ(drawn.size(), expected.size()) << where << ": los vértices dibujados";
        for (std::size_t v = 0; v < drawn.size(); ++v)
        {
            const std::string vertex = where + " vértice " + std::to_string(v);
            EXPECT_EQ(drawn[v].texture, expected[v].texture) << vertex;
            EXPECT_EQ(drawn[v].blend, expected[v].blend) << vertex;
            ExpectSameBits(drawn[v].vertex.position, expected[v].vertex.position, vertex + " posición");
            EXPECT_EQ(Bits(drawn[v].vertex.uv.x), Bits(expected[v].vertex.uv.x)) << vertex;
            EXPECT_EQ(Bits(drawn[v].vertex.uv.y), Bits(expected[v].vertex.uv.y)) << vertex;
            EXPECT_EQ(Bits(drawn[v].vertex.color.red), Bits(expected[v].vertex.color.red)) << vertex;
            EXPECT_EQ(Bits(drawn[v].vertex.color.green), Bits(expected[v].vertex.color.green)) << vertex;
            EXPECT_EQ(Bits(drawn[v].vertex.color.blue), Bits(expected[v].vertex.color.blue)) << vertex;
        }

        ASSERT_EQ(cloth.Flares().size(), flares.size()) << where << ": los sprites del brillo";
        for (std::size_t s = 0; s < flares.size(); ++s)
        {
            ExpectSameBits(cloth.Flares()[s].position, flares[s].position, where + " sprite");
            EXPECT_EQ(Bits(cloth.Flares()[s].scale), Bits(flares[s].scale)) << where << " sprite " << s;
            EXPECT_EQ(Bits(cloth.Flares()[s].light), Bits(flares[s].light)) << where << " sprite " << s;
        }

        randoms += frame.randoms;
        CrtRandom after(vectors.randomState);
        for (std::int64_t r = 0; r < randoms; ++r)
        {
            static_cast<void>(after.Next());
        }
        EXPECT_EQ(world.random.State(), after.State()) << where << ": los rand() que sacó";
    }
}

// El Dark Lord del login (0x517270(2, 4, …)): las tres telas en el mapa 0x37, tres frames.
TEST(CharacterClothTest, TheLoginDarkLordMatchesTheBinary)
{
    ExpectTheBlockCase("a_login_dark_lord.txt");
}

// La cuarta tela con ArmorClass05: armada sin usar, vuelta a armar encima (con la esfera dos veces),
// suelta y vuelta a armar; la tercera sin la capa; y el color de un duelo, que la deja ir.
TEST(CharacterClothTest, TheFourthClothAndTheCapeMatchTheBinary)
{
    ExpectTheBlockCase("b_fourth_cloth_and_wings.txt");
}

// La clase 3: una tela con Robe01 y Robe02, fuera del 0x37 (el viento base paseado) y con otro acceso
// al contador abierto.
TEST(CharacterClothTest, TheClassThreeRobeMatchesTheBinary)
{
    ExpectTheBlockCase("c_class3_robe.txt");
}

// La rotura: los huesos saltan, un resorte pasa de 20 veces su largo, se sueltan todas y el frame
// siguiente se arman de nuevo desde la pose de ese momento.
TEST(CharacterClothTest, ABrokenClothIsBuiltAgainTheNextFrame)
{
    ExpectTheBlockCase("e_broken.txt");
}

// bVar23 para un jugador: la clase 3 o 4, salvo en los mapas 0x12 a 0x17.
TEST(CharacterClothTest, OnlyClassesThreeAndFourWearACloth)
{
    EXPECT_TRUE(Client::Scenes::WearsACloth(4, 0x37));
    EXPECT_TRUE(Client::Scenes::WearsACloth(3, 0x36));
    EXPECT_TRUE(Client::Scenes::WearsACloth(4 | 8, 0x37));
    EXPECT_FALSE(Client::Scenes::WearsACloth(0, 0x37));
    EXPECT_FALSE(Client::Scenes::WearsACloth(2, 0x37));
    EXPECT_FALSE(Client::Scenes::WearsACloth(4, 0x12));
    EXPECT_FALSE(Client::Scenes::WearsACloth(4, 0x17));
    EXPECT_TRUE(Client::Scenes::WearsACloth(4, 0x18));
}
} // namespace
