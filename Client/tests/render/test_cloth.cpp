#include "Render/Models/Cloth.h"

#include "Render/TextureSlot.h"
#include "support/ClothVectors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

// Client::Render::Models::Cloth: la tela de la vtable 0x6b8cd8 contra el código real del main.exe,
// emulado (tests/vectors/cloth): cada paso de cada tela, partícula por partícula y bit a bit, las
// caras, los sprites del brillo y los rand() que saca.
namespace
{
using Client::Render::TextureSlot;
using Client::Render::Models::Cloth;
using Client::Render::Models::ClothBlend;
using Client::Render::Models::ClothBody;
using Client::Render::Models::ClothCorner;
using Client::Render::Models::ClothFlare;
using Client::Render::Models::ClothParticle;
using Client::Render::Models::ClothShape;
using Client::Render::Models::ClothWorld;
using Client::Render::Models::CrtRandom;
using Client::Render::Models::FaceBlendOf;
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

ClothVectors Read(const std::string& name)
{
    ClothVectors vectors;
    const auto error = Client::Tests::ReadClothVectors(Client::Tests::ClothVectorsDir() / name, vectors);
    EXPECT_FALSE(error.has_value()) << *error;
    return vectors;
}

ClothBody BodyOf(const ClothVectorFrame& frame)
{
    return ClothBody{.bones = frame.bones, .angles = frame.angles, .scale = frame.scale, .position = frame.origin};
}

// El frame del mundo antes de las telas: los globales de la entrada y el paseo del viento.
void PrepareTheWorld(ClothWorld& world, const ClothVectorFrame& frame)
{
    if (frame.windBase)
    {
        world.windBase = *frame.windBase;
    }
    if (frame.windZ)
    {
        world.wind.z = *frame.windZ;
    }
    world.logicSteps = frame.counter;
    world.openCounterAccesses = frame.openAccesses;
    for (int walk = 0; walk < frame.windWalks; ++walk)
    {
        Client::Render::Models::WalkTheWind(world);
    }
    EXPECT_EQ(Bits(world.windBase), Bits(frame.windBaseAfterTheWalks)) << "el viento base después del paseo";
}

void ExpectTheStep(const Cloth& cloth, const ClothVectorStep& step, const std::string& where)
{
    ASSERT_EQ(cloth.Particles().size(), step.particles.size()) << where;
    EXPECT_EQ(Bits(cloth.Wind()), Bits(step.wind)) << where << ": el viento";
    for (std::size_t p = 0; p < step.particles.size(); ++p)
    {
        const ClothParticle& ours = cloth.Particles()[p];
        const std::string particle = where + " partícula " + std::to_string(p);
        ExpectSameBits(ours.position, step.particles[p].position, particle + " posición");
        if (step.particles[p].velocity)
        {
            ExpectSameBits(ours.velocity, *step.particles[p].velocity, particle + " velocidad");
        }
        if (step.particles[p].force)
        {
            ExpectSameBits(ours.force, *step.particles[p].force, particle + " fuerza");
        }
        EXPECT_EQ(ours.anchored, step.particles[p].anchored) << particle;
    }
}

std::string NameOf(ClothBlend blend)
{
    switch (blend)
    {
    case ClothBlend::Opaque:
        return "opaque";
    case ClothBlend::Alpha:
        return "alpha";
    case ClothBlend::Additive:
        return "additive";
    case ClothBlend::Previous:
        return "previous";
    }
    return "?";
}

// Lo que dibuja una tela, en el orden de 0x455930: los sprites del brillo y las dos caras.
void ExpectTheDrawing(const Cloth& cloth, const std::vector<ClothVectorEvent>& events, std::size_t& next,
                      const ClothVectorFrame& frame, std::string& previousBlend, const std::string& where)
{
    const bool glows = (cloth.Flags() & Client::Render::Models::ClothGlows) != 0;
    const float glow = Cloth::GlowAt(frame.worldTime);
    if (glows)
    {
        const std::vector<ClothFlare> flares = cloth.Flares(glow);
        for (std::size_t f = 0; f < flares.size(); ++f)
        {
            ASSERT_LT(next, events.size()) << where;
            ASSERT_EQ(events[next].kind, ClothVectorEvent::Kind::Flare) << where << " sprite " << f;
            ExpectSameBits(flares[f].position, events[next].flare.position, where + " sprite");
            EXPECT_EQ(Bits(flares[f].scale), Bits(events[next].flare.scale)) << where << " sprite " << f;
            EXPECT_EQ(Bits(flares[f].light), Bits(events[next].flare.light)) << where << " sprite " << f;
            ++next;
        }
    }
    const glm::vec3 color = glows ? glm::vec3(glow) : frame.color.value_or(glm::vec3(1.0f));
    for (const bool front : {true, false})
    {
        if (!front && !cloth.DrawsTheBack())
        {
            continue;
        }
        ASSERT_LT(next, events.size()) << where;
        ASSERT_EQ(events[next].kind, ClothVectorEvent::Kind::Face) << where << (front ? " adelante" : " atrás");
        const Client::Tests::ClothVectorFace& face = events[next].face;
        const ClothBlend blend = FaceBlendOf(cloth.Flags());
        const std::string name = blend == ClothBlend::Previous ? previousBlend : NameOf(blend);
        EXPECT_EQ(name, face.blend) << where;
        previousBlend = face.blend;
        // 0x620780 ata un slot negativo como el nombre de GL −slot; el vector guarda el nombre atado.
        const auto slot = static_cast<std::int16_t>((front ? cloth.Front() : cloth.Back()).value);
        EXPECT_EQ(slot < 0 ? -slot : slot, face.texture) << where;
        ExpectSameBits(color, face.color, where + " color");
        const std::vector<ClothCorner> corners = cloth.Corners(front);
        ASSERT_EQ(corners.size(), face.corners.size()) << where;
        // El vector nombra cada vértice por la primera partícula con su posición (dos pueden coincidir):
        // se comparan las posiciones.
        const auto particles = cloth.Particles();
        for (std::size_t c = 0; c < corners.size(); ++c)
        {
            ExpectSameBits(particles[static_cast<std::size_t>(corners[c].particle)].position,
                           particles[static_cast<std::size_t>(face.corners[c].particle)].position,
                           where + " esquina " + std::to_string(c));
            EXPECT_EQ(Bits(corners[c].uv.x), Bits(face.corners[c].uv.x)) << where << " u " << c;
            EXPECT_EQ(Bits(corners[c].uv.y), Bits(face.corners[c].uv.y)) << where << " v " << c;
        }
        ++next;
    }
}

// El modo directo del oráculo: las telas de la entrada, armadas en el primer frame, cada una con sus
// cinco pasos y su dibujo, frame por frame.
void ExpectTheDirectCase(const std::string& name)
{
    const ClothVectors vectors = Read(name);
    ASSERT_FALSE(vectors.direct.empty());
    ClothWorld world{.random = CrtRandom(vectors.randomState)};
    std::vector<Cloth> cloths(vectors.direct.size());
    std::vector<bool> broken(vectors.direct.size(), false);
    std::string previousBlend = "opaque";
    std::int64_t randoms = 0;
    for (std::size_t f = 0; f < vectors.frames.size(); ++f)
    {
        const ClothVectorFrame& frame = vectors.frames[f];
        PrepareTheWorld(world, frame);
        const ClothBody body = BodyOf(frame);
        if (f == 0)
        {
            for (std::size_t c = 0; c < cloths.size(); ++c)
            {
                const Client::Tests::ClothVectorShape& shape = vectors.direct[c];
                cloths[c].Build(ClothShape{.bone = shape.bone,
                                           .anchor = shape.anchor,
                                           .columns = shape.columns,
                                           .rows = shape.rows,
                                           .width = shape.width,
                                           .height = shape.height,
                                           .front = TextureSlot{shape.front},
                                           .back = TextureSlot{shape.back},
                                           .flags = shape.flags},
                                body);
                cloths[c].SetWindBytes(shape.windMinimum, shape.windRange);
                for (const auto& sphere : shape.spheres)
                {
                    cloths[c].AddSphere(sphere.local, sphere.radius, sphere.bone);
                }
            }
        }
        const float dt = frame.dt.value_or(std::bit_cast<float>(0x3ba3d70au));
        std::size_t next = 0;
        for (std::size_t c = 0; c < cloths.size(); ++c)
        {
            if (broken[c])
            {
                continue;
            }
            const std::string where = name + " frame " + std::to_string(f) + " tela " + std::to_string(c);
            bool ok = true;
            for (int iteration = 0; iteration < 5 && ok; ++iteration)
            {
                ok = cloths[c].Step(dt, body, world, frame.map);
                ASSERT_LT(next, frame.events.size()) << where;
                ASSERT_EQ(frame.events[next].kind, ClothVectorEvent::Kind::Step) << where;
                const ClothVectorStep& step = frame.events[next].step;
                EXPECT_EQ(step.cloth, static_cast<std::int32_t>(c)) << where;
                EXPECT_EQ(step.iteration, iteration) << where;
                EXPECT_EQ(ok, step.ok) << where << " paso " << iteration;
                ExpectTheStep(cloths[c], step, where + " paso " + std::to_string(iteration));
                ++next;
            }
            if (!ok)
            {
                ASSERT_LT(next, frame.events.size()) << where;
                EXPECT_EQ(frame.events[next].kind, ClothVectorEvent::Kind::Broken) << where;
                ++next;
                broken[c] = true;
                continue;
            }
            ExpectTheDrawing(cloths[c], frame.events, next, frame, previousBlend, where);
        }
        EXPECT_EQ(next, frame.events.size()) << name << " frame " << f << ": eventos de más en el binario";
        randoms += frame.randoms;
        CrtRandom expected(vectors.randomState);
        for (std::int64_t r = 0; r < randoms; ++r)
        {
            static_cast<void>(expected.Next());
        }
        EXPECT_EQ(world.random.State(), expected.State()) << name << " frame " << f << ": los rand() que sacó";
    }
}

// 0x69fd70: los primeros valores desde la semilla 1, los de la CRT de MSVC.
TEST(ClothTest, TheRandomIsTheOneOfTheCrt)
{
    CrtRandom random(1);
    EXPECT_EQ(random.Next(), 41);
    EXPECT_EQ(random.Next(), 18467);
    EXPECT_EQ(random.Next(), 6334);
}

// Las banderas que 0x50ba30 no usa: 0x100, 0x20000000, 0x2000 y 0x3000 con brillo, 0x400 y 0x800, los
// arcos 2 y 3 y el ancho que no crece, contra el binario.
TEST(ClothTest, TheDirectFlagsMatchTheBinaryStepByStep)
{
    ExpectTheDirectCase("f_direct_flags.txt");
}

// Las distancias mínimas (0,001 y 0,01), la restricción que separa, la rotura por el mínimo y el
// color nulo, contra el binario.
TEST(ClothTest, TheEdgesMatchTheBinaryStepByStep)
{
    ExpectTheDirectCase("g_direct_edges.txt");
}

// Los ángulos x e y del objeto distintos de 0: el cos del ángulo 2 de 0x5fc8f0, que queda en double,
// entra en la grilla y en el anclaje.
TEST(ClothTest, TheObjectAnglesMatchTheBinaryStepByStep)
{
    ExpectTheDirectCase("h_direct_angles.txt");
}

// Un resorte estirado entre 10 y 20 veces su largo no rompe la tela: la restricción lo trae.
TEST(ClothTest, AStretchUnderTwentyTimesIsPulledBack)
{
    ExpectTheDirectCase("i_direct_stretch.txt");
}

// Un renglón roto en un archivo de vectores da un error con el renglón, no una excepción.
TEST(ClothTest, ABrokenVectorFileIsAnError)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "muret-cloth-roto.txt";
    {
        std::ofstream file(path);
        file << "cloth-vectors 1\nrand abc\n";
    }
    ClothVectors vectors;
    const std::optional<std::string> error = Client::Tests::ReadClothVectors(path, vectors);
    std::filesystem::remove(path);
    ASSERT_TRUE(error.has_value());
    EXPECT_NE(error->find("rand abc"), std::string::npos) << *error;
}

// El paseo del viento base (0x456b10): a 0,001 por cada rand() % 200 − 100, entre −0,2 y 1.
TEST(ClothTest, TheWindBaseWalksBetweenItsLimits)
{
    ClothWorld world;
    world.windBase = 0.995f;
    world.random = CrtRandom(0);
    for (int i = 0; i < 2000; ++i)
    {
        Client::Render::Models::WalkTheWind(world);
        EXPECT_GE(world.windBase, -0.2f);
        EXPECT_LE(world.windBase, 1.0f);
    }
}

// 0x637700 en un paso de lógica: cinco paseos y el contador más 1, con los 4 rand() de su
// protección.
TEST(ClothTest, ALogicStepWalksFiveTimesAndCounts)
{
    ClothWorld world{.random = CrtRandom(7)};
    ClothWorld walked{.random = CrtRandom(7)};
    Client::Render::Models::StepTheClothWorld(world);
    for (int i = 0; i < 5; ++i)
    {
        Client::Render::Models::WalkTheWind(walked);
    }
    EXPECT_EQ(world.windBase, walked.windBase);
    EXPECT_EQ(world.logicSteps, 1);
    for (int i = 0; i < 4; ++i)
    {
        static_cast<void>(walked.random.Next());
    }
    EXPECT_EQ(world.random.State(), walked.random.State());
}

// 0x637f91 suma en 32 bits; el contador se sigue leyendo con signo después del guardado.
TEST(ClothTest, ALogicStepWrapsTheCounterAt32Bits)
{
    struct Transition
    {
        std::int32_t before;
        std::int32_t after;
    };
    constexpr auto Minimum = std::numeric_limits<std::int32_t>::min();
    constexpr auto Maximum = std::numeric_limits<std::int32_t>::max();
    constexpr std::array Transitions{Transition{Maximum, Minimum}, Transition{-1, 0}, Transition{Minimum, Minimum + 1}};
    for (const std::int32_t openAccesses : std::array<std::int32_t, 2>{0, 1})
    {
        ClothWorld walked{.random = CrtRandom(7)};
        for (int i = 0; i < 5; ++i)
        {
            Client::Render::Models::WalkTheWind(walked);
        }
        if (openAccesses == 0)
        {
            for (int i = 0; i < 4; ++i)
            {
                static_cast<void>(walked.random.Next());
            }
        }
        for (const auto& transition : Transitions)
        {
            SCOPED_TRACE(::testing::Message() << transition.before << ", accesos " << openAccesses);
            ClothWorld world{
                .random = CrtRandom(7), .logicSteps = transition.before, .openCounterAccesses = openAccesses};
            Client::Render::Models::StepTheClothWorld(world);
            EXPECT_EQ(world.logicSteps, transition.after);
            EXPECT_EQ(Bits(world.windBase), Bits(walked.windBase));
            EXPECT_EQ(world.random.State(), walked.random.State());
            EXPECT_EQ(world.openCounterAccesses, openAccesses);
        }
    }
}
} // namespace
