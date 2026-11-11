#include "Scenes/CharacterCloth.h"

#include "Core/IClock.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Pass3D.h"
#include "Render/SpriteVertex.h"
#include "support/ClothVectors.h"

#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <chrono>
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

// El monstruo 0x37: su tela de Robe03, y el objeto con 0x1000 (el dt en 0) y con 0x4000 (ni paso ni
// dibujo).
TEST(CharacterClothTest, TheMonsterClothMatchesTheBinary)
{
    ExpectTheBlockCase("d_monster_0x37.txt");
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

namespace
{
// El Dark Lord del primer frame del login (el caso A), para los tests del ritmo.
struct LoginLord
{
    ClothVectors vectors;
    ClothBody body;
    ClothWearer wearer;
    std::uint8_t map = 0;
};

LoginLord ReadTheLoginLord()
{
    LoginLord lord;
    const auto error =
        Client::Tests::ReadClothVectors(Client::Tests::ClothVectorsDir() / "a_login_dark_lord.txt", lord.vectors);
    EXPECT_FALSE(error.has_value()) << *error;
    const ClothVectorFrame& frame = lord.vectors.frames.at(0);
    lord.body = ClothBody{.bones = frame.bones, .angles = frame.angles, .scale = frame.scale, .position = frame.origin};
    lord.wearer.characterClass = frame.characterClass;
    lord.wearer.armor = frame.armor;
    lord.wearer.wings = frame.wings;
    lord.map = frame.map;
    return lord;
}

void ExpectTheSameCloths(const CharacterCloth& ours, const CharacterCloth& reference)
{
    ASSERT_EQ(ours.Cloths().size(), reference.Cloths().size());
    for (std::size_t c = 0; c < ours.Cloths().size(); ++c)
    {
        const auto a = ours.Cloths()[c].Particles();
        const auto b = reference.Cloths()[c].Particles();
        ASSERT_EQ(a.size(), b.size());
        for (std::size_t p = 0; p < a.size(); ++p)
        {
            ExpectSameBits(a[p].position, b[p].position,
                           "tela " + std::to_string(c) + " partícula " + std::to_string(p));
        }
    }
}

// Q-C124 corregido como Q-C20: la tela corre un frame del original (5 pasos) cada 46,875 ms de reloj, a
// cualquier FPS. Un segundo a 60 o a 144 FPS es lo mismo que 1 + ⌊t / 46,875 ms⌋ frames del original,
// con t el reloj del último: el primero arma la tela.
TEST(CharacterClothTest, TheClothRunsAtTheFrameRateOfTheOriginal)
{
    for (const int fps : {60, 144})
    {
        const LoginLord lord = ReadTheLoginLord();
        ClothWorld world{.random = CrtRandom(7)};
        ClothWorld referenceWorld{.random = CrtRandom(7)};
        CharacterCloth cloth;
        CharacterCloth reference;
        Client::Core::Duration clock{};
        for (int frame = 0; frame < fps; ++frame)
        {
            ModelBatch batch;
            batch.Begin(Client::Render::Pass3D{});
            clock = std::chrono::duration_cast<Client::Core::Duration>(
                std::chrono::duration<double>(static_cast<double>(frame) / static_cast<double>(fps)));
            cloth.Advance(batch, lord.wearer, lord.body, world, lord.map, clock, true);
            EXPECT_FALSE(batch.Vertices().empty()) << fps << " FPS, frame " << frame << ": cada frame dibuja";
        }
        const auto originals = 1 + clock / Client::Render::Models::OriginalFrameTime;
        EXPECT_LT(originals, fps) << "la tela no corre una vez por frame de MuRet";
        for (std::int64_t original = 0; original < originals; ++original)
        {
            ModelBatch batch;
            batch.Begin(Client::Render::Pass3D{});
            reference.Frame(batch, lord.wearer, lord.body, referenceWorld, lord.map, 0.0f, true);
        }
        EXPECT_EQ(world.random.State(), referenceWorld.random.State()) << fps << " FPS";
        ExpectTheSameCloths(cloth, reference);
    }
}

// En el modo fiel cada frame dura lo de uno del original: un frame del bloque por frame dibujado.
TEST(CharacterClothTest, AtTheOriginalFrameRateEachFrameRunsTheBlockOnce)
{
    const LoginLord lord = ReadTheLoginLord();
    ClothWorld world{.random = CrtRandom(3)};
    ClothWorld referenceWorld{.random = CrtRandom(3)};
    CharacterCloth cloth;
    CharacterCloth reference;
    for (int frame = 0; frame < 30; ++frame)
    {
        ModelBatch ours;
        ours.Begin(Client::Render::Pass3D{});
        cloth.Advance(ours, lord.wearer, lord.body, world, lord.map, Client::Render::Models::OriginalFrameTime * frame,
                      true);
        ModelBatch theirs;
        theirs.Begin(Client::Render::Pass3D{});
        reference.Frame(theirs, lord.wearer, lord.body, referenceWorld, lord.map, 0.0f, true);
        ASSERT_EQ(world.random.State(), referenceWorld.random.State()) << "frame " << frame;
    }
    ExpectTheSameCloths(cloth, reference);
}

// Y aunque el frame fiel no dure justo 46,875 ms: con OncePerDraw, el primero tarde y los demás antes, cada
// dibujo corre el bloque una vez (por el reloj correría 0 y después 2).
TEST(CharacterClothTest, InTheFaithfulModeEachDrawRunsTheBlockOnceWhateverItsClock)
{
    using namespace std::chrono_literals;
    const LoginLord lord = ReadTheLoginLord();
    ClothWorld world{.random = CrtRandom(3)};
    ClothWorld referenceWorld{.random = CrtRandom(3)};
    CharacterCloth cloth;
    CharacterCloth reference;
    for (int frame = 0; frame < 30; ++frame)
    {
        const Client::Core::Duration late = frame == 0 ? 300us : 100us;
        ModelBatch ours;
        ours.Begin(Client::Render::Pass3D{});
        cloth.Advance(ours, lord.wearer, lord.body, world, lord.map,
                      Client::Render::Models::OriginalFrameTime * frame + late, true, Client::Scenes::PlainClothColor,
                      Client::Render::Models::OriginalFramePace::OncePerDraw);
        ModelBatch theirs;
        theirs.Begin(Client::Render::Pass3D{});
        reference.Frame(theirs, lord.wearer, lord.body, referenceWorld, lord.map, 0.0f, true);
        ASSERT_EQ(world.random.State(), referenceWorld.random.State()) << "frame " << frame;
        EXPECT_EQ(ours.Vertices().size(), theirs.Vertices().size()) << "frame " << frame;
    }
    ExpectTheSameCloths(cloth, reference);
}

// Después de un frame retenido (Q-C85, P-C27 f), RestartTheClock: el próximo Advance corre el bloque una vez,
// como el original en el primer frame que dibuja después del connect, aunque pasaron 5 s (por el reloj serían
// 64). Las telas siguen desde donde estaban.
TEST(CharacterClothTest, AfterARestartTheNextAdvanceRunsTheBlockOnce)
{
    using namespace std::chrono_literals;
    using Client::Render::Models::OriginalFrameTime;
    const LoginLord lord = ReadTheLoginLord();
    ClothWorld world{.random = CrtRandom(3)};
    ClothWorld referenceWorld{.random = CrtRandom(3)};
    CharacterCloth cloth;
    CharacterCloth reference;
    constexpr int FramesBefore = 3;
    const auto runOnce = [&](Client::Core::Duration clock)
    {
        ModelBatch ours;
        ours.Begin(Client::Render::Pass3D{});
        cloth.Advance(ours, lord.wearer, lord.body, world, lord.map, clock, true);
        ModelBatch theirs;
        theirs.Begin(Client::Render::Pass3D{});
        reference.Frame(theirs, lord.wearer, lord.body, referenceWorld, lord.map, 0.0f, true);
    };
    for (int frame = 0; frame < FramesBefore; ++frame)
    {
        runOnce(OriginalFrameTime * frame);
    }
    ASSERT_EQ(world.random.State(), referenceWorld.random.State());

    cloth.RestartTheClock();
    runOnce((OriginalFrameTime * (FramesBefore - 1)) + 5s);

    EXPECT_EQ(world.random.State(), referenceWorld.random.State()) << "un frame del bloque, no 64";
    ExpectTheSameCloths(cloth, reference);
}

// Un personaje que pasa de la clase 3 a la 4 con la misma tela: el arreglo de una no alcanza para las
// cuatro del Dark Lord, y la tela se vuelve a armar en vez de leer fuera de él.
TEST(CharacterClothTest, AClassChangeBuildsTheClothsAgain)
{
    const LoginLord lord = ReadTheLoginLord();
    ClothWorld world{.random = CrtRandom(5)};
    CharacterCloth cloth;
    ClothWearer gladiator = lord.wearer;
    gladiator.characterClass = 3;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    cloth.Frame(batch, gladiator, lord.body, world, lord.map, 0.0f, true);
    ASSERT_EQ(cloth.Cloths().size(), 1u);

    ClothWearer lord4 = lord.wearer;
    lord4.armor = 0x221e;
    cloth.Frame(batch, lord4, lord.body, world, lord.map, 0.0f, true);
    EXPECT_EQ(cloth.Cloths().size(), 4u);
    EXPECT_EQ(cloth.Count(), 3u);
}

// #307: entre dos frames del original, la tela se dibuja llevada con el hueso del que cuelga. Un dibujo
// intermedio cae a los 20 ms, antes del segundo frame del original.
constexpr Client::Core::Duration BetweenFrames = std::chrono::milliseconds{20};
// Lo que se aleja un punto llevado del que daría la cuenta exacta: la tela está a unas 10.000 unidades
// del origen del mundo, donde un float tiene un ulp de 0,001.
constexpr float CarryTolerance = 0.01f;

std::vector<glm::vec3> PositionsOf(const ModelBatch& batch)
{
    std::vector<glm::vec3> positions;
    for (const Client::Render::ModelVertex& vertex : batch.Vertices())
    {
        positions.push_back(vertex.position);
    }
    return positions;
}

// El primer frame del original, que arma la tela y la dibuja con el cuerpo de `lord`.
std::vector<glm::vec3> TheFirstFrame(CharacterCloth& cloth, const LoginLord& lord)
{
    ClothWorld world{.random = CrtRandom(11)};
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    cloth.Advance(batch, lord.wearer, lord.body, world, lord.map, Client::Core::Duration{}, true);
    return PositionsOf(batch);
}

// Un dibujo entre frames del original con el cuerpo `body`, por DrawCurrent (el juego) o por Advance sin
// frames del original que correr (el login y la selección).
std::vector<glm::vec3> ADrawBetweenFrames(CharacterCloth& cloth, const LoginLord& lord, const ClothBody& body,
                                          bool byAdvance)
{
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    if (byAdvance)
    {
        ClothWorld world{.random = CrtRandom(11)};
        cloth.Advance(batch, lord.wearer, body, world, lord.map, BetweenFrames, true);
        EXPECT_EQ(world.random.State(), CrtRandom(11).State()) << "entre frames del original no se simula";
    }
    else
    {
        cloth.DrawCurrent(batch, lord.wearer, body, BetweenFrames, true);
    }
    return PositionsOf(batch);
}

void ExpectNear(const glm::vec3& ours, const glm::vec3& expected, std::size_t vertex)
{
    for (int k = 0; k < 3; ++k)
    {
        EXPECT_NEAR(ours[k], expected[k], CarryTolerance) << "vértice " << vertex << " eje " << k;
    }
}

// Con el cuerpo quieto (los mismos huesos), el dibujo intermedio es el del último frame del original, bit a
// bit. Los frames del original, y todos los del modo fiel, simulan y no pasan por acá.
TEST(CharacterClothTest, BetweenOriginalFramesAStillBodyDrawsTheClothAsItWas)
{
    for (const bool byAdvance : {false, true})
    {
        const LoginLord lord = ReadTheLoginLord();
        CharacterCloth cloth;
        const std::vector<glm::vec3> first = TheFirstFrame(cloth, lord);
        const std::vector<glm::vec3> between = ADrawBetweenFrames(cloth, lord, lord.body, byAdvance);

        ASSERT_FALSE(first.empty());
        ASSERT_EQ(between.size(), first.size());
        for (std::size_t vertex = 0; vertex < first.size(); ++vertex)
        {
            ExpectSameBits(between[vertex], first[vertex], "vértice " + std::to_string(vertex));
        }
    }
}

// El personaje que dio un paso entre dos frames del original: la tela va con él, corrida lo mismo que el
// cuerpo, y no se queda donde estaba hasta el frame siguiente (el rebote de #307).
TEST(CharacterClothTest, BetweenOriginalFramesTheClothWalksWithTheBody)
{
    for (const bool byAdvance : {false, true})
    {
        const LoginLord lord = ReadTheLoginLord();
        CharacterCloth cloth;
        const std::vector<glm::vec3> first = TheFirstFrame(cloth, lord);
        constexpr glm::vec3 Step{60.0f, -25.0f, 3.0f};
        ClothBody walked = lord.body;
        walked.position += Step;
        const std::vector<glm::vec3> between = ADrawBetweenFrames(cloth, lord, walked, byAdvance);

        ASSERT_FALSE(first.empty());
        ASSERT_EQ(between.size(), first.size());
        for (std::size_t vertex = 0; vertex < first.size(); ++vertex)
        {
            ExpectNear(between[vertex], first[vertex] + Step, vertex);
        }
    }
}

// El personaje que giró 90° entre dos frames del original: los huesos traen el giro, y la tela gira con
// ellos alrededor de la posición del personaje.
TEST(CharacterClothTest, BetweenOriginalFramesTheClothTurnsWithTheBody)
{
    const LoginLord lord = ReadTheLoginLord();
    CharacterCloth cloth;
    const std::vector<glm::vec3> first = TheFirstFrame(cloth, lord);
    const glm::mat3 quarterTurn{{0.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    std::vector<Client::Render::Models::BoneTransform> turnedBones;
    for (const Client::Render::Models::BoneTransform& bone : lord.body.bones)
    {
        turnedBones.emplace_back(quarterTurn * bone[0], quarterTurn * bone[1], quarterTurn * bone[2],
                                 quarterTurn * bone[3]);
    }
    ClothBody turned = lord.body;
    turned.bones = turnedBones;
    turned.angles.z += 90.0f;
    const std::vector<glm::vec3> between = ADrawBetweenFrames(cloth, lord, turned, false);

    ASSERT_FALSE(first.empty());
    ASSERT_EQ(between.size(), first.size());
    for (std::size_t vertex = 0; vertex < first.size(); ++vertex)
    {
        const glm::vec3 expected = lord.body.position + quarterTurn * (first[vertex] - lord.body.position);
        ExpectNear(between[vertex], expected, vertex);
    }
}
} // namespace
