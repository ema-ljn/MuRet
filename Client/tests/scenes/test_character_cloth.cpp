#include "Actors/Drawing/CharacterCloth.h"

#include "Core/IClock.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Models/Animation.h"
#include "Models/Cloth.h"
#include "Math/HalfLifeMath.h"
#include "Render/Pass3D.h"
#include "Render/SpriteVertex.h"
#include "support/ClothVectors.h"

#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Client::Actors::Drawing::CharacterCloth: the block 0x50d58b..0x50db57 of 0x50ba30, frame by frame, against the real
// code of the emulated main.exe (tests/vectors/cloth): which cloths it builds, the fourth, the break and the rebuild,
// the state of each one at the end of its five steps and each triangle it draws.
namespace
{
using Client::Actors::Drawing::CharacterCloth;
using Client::Actors::Drawing::ClothWearer;
using Client::Crt::CrtRandom;
using Client::Models::Cloth;
using Client::Models::ClothBody;
using Client::Models::ClothWorld;
using Client::Render::ModelBatch;
using Client::Render::ModelBlend;
using Client::Tests::ClothVectorEvent;
using Client::Tests::ClothVectorFrame;
using Client::Tests::ClothVectors;
using Client::Tests::ClothVectorStep;

using Client::Math::Bits;

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

// A vertex of the batch, with the state of its stretch.
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
            Client::Models::WalkTheWind(world);
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
                    frame.color.value_or(Client::Actors::Drawing::PlainClothColor));

        // What the binary left: the last step of each cloth, its faces, the sprites and whether it broke.
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

// The login's Dark Lord (0x517270(2, 4, …)): the three cloths in map 0x37, three frames.
TEST(CharacterClothTest, TheLoginDarkLordMatchesTheBinary)
{
    ExpectTheBlockCase("a_login_dark_lord.txt");
}

// The fourth cloth with ArmorClass05: built unused, built again on top (with the sphere twice), released and built
// again; the third without the cape; and the color of a duel, which lets it go.
TEST(CharacterClothTest, TheFourthClothAndTheCapeMatchTheBinary)
{
    ExpectTheBlockCase("b_fourth_cloth_and_wings.txt");
}

// Class 3: a cloth with Robe01 and Robe02, outside 0x37 (the base wind walked) and with another access to the open
// counter.
TEST(CharacterClothTest, TheClassThreeRobeMatchesTheBinary)
{
    ExpectTheBlockCase("c_class3_robe.txt");
}

// Monster 0x37: its Robe03 cloth, and the object with 0x1000 (dt at 0) and with 0x4000 (neither step nor draw).
TEST(CharacterClothTest, TheMonsterClothMatchesTheBinary)
{
    ExpectTheBlockCase("d_monster_0x37.txt");
}

// The break: the bones jump, a spring goes past 20 times its length, all of them are released and the next frame they
// are built again from that moment's pose.
TEST(CharacterClothTest, ABrokenClothIsBuiltAgainTheNextFrame)
{
    ExpectTheBlockCase("e_broken.txt");
}

// bVar23 for a player: class 3 or 4, except in maps 0x12 to 0x17.
TEST(CharacterClothTest, OnlyClassesThreeAndFourWearACloth)
{
    EXPECT_TRUE(Client::Actors::Drawing::WearsACloth(4, 0x37));
    EXPECT_TRUE(Client::Actors::Drawing::WearsACloth(3, 0x36));
    EXPECT_TRUE(Client::Actors::Drawing::WearsACloth(4 | 8, 0x37));
    EXPECT_FALSE(Client::Actors::Drawing::WearsACloth(0, 0x37));
    EXPECT_FALSE(Client::Actors::Drawing::WearsACloth(2, 0x37));
    EXPECT_FALSE(Client::Actors::Drawing::WearsACloth(4, 0x12));
    EXPECT_FALSE(Client::Actors::Drawing::WearsACloth(4, 0x17));
    EXPECT_TRUE(Client::Actors::Drawing::WearsACloth(4, 0x18));
}
} // namespace

namespace
{
// The Dark Lord of the login's first frame (case A), for the pace tests.
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

TEST(CharacterClothTest, TheGridMonsterRandomizesWindOnlyOnTheLoginMap)
{
    const LoginLord lord = ReadTheLoginLord();
    for (unsigned map = 0; map <= 0xff; ++map)
    {
        SCOPED_TRACE(map);
        CharacterCloth cloth;
        ClothWorld world{.random = CrtRandom(7), .windBase = 0.3f, .openCounterAccesses = 1};
        ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});

        cloth.FrameTheMonsterCloth(batch, 0x4a, lord.body, world, static_cast<std::uint8_t>(map), 0.0f);

        ASSERT_EQ(cloth.Cloths().size(), 1u);
        const bool login = map == 0x37;
        EXPECT_EQ(Bits(cloth.Cloths()[0].Wind()), login ? 0x3e6147aeu : Bits(world.windBase));
        EXPECT_EQ(world.random.State(), login ? 3164688850u : 7u);
    }
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

// Q-C124 fixed as Q-C20: the cloth runs one original frame (5 steps) every 46.875 ms of clock, at any FPS. A second at
// 60 or at 144 FPS is the same as 1 + ⌊t / 46.875 ms⌋ original frames, with t the last one's clock: the first builds
// the cloth.
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
        const auto originals = 1 + clock / Client::Models::OriginalFrameTime;
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

// In the faithful mode each frame lasts as long as one of the original: one frame of the block per drawn frame.
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
        cloth.Advance(ours, lord.wearer, lord.body, world, lord.map, Client::Models::OriginalFrameTime * frame, true);
        ModelBatch theirs;
        theirs.Begin(Client::Render::Pass3D{});
        reference.Frame(theirs, lord.wearer, lord.body, referenceWorld, lord.map, 0.0f, true);
        ASSERT_EQ(world.random.State(), referenceWorld.random.State()) << "frame " << frame;
    }
    ExpectTheSameCloths(cloth, reference);
}

// And even if the faithful frame does not last exactly 46.875 ms: with OncePerDraw, the first late and the others
// early, each draw runs the block once (by the clock it would run 0 and then 2).
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
        cloth.Advance(ours, lord.wearer, lord.body, world, lord.map, Client::Models::OriginalFrameTime * frame + late,
                      true, Client::Actors::Drawing::PlainClothColor, Client::Models::OriginalFramePace::OncePerDraw);
        ModelBatch theirs;
        theirs.Begin(Client::Render::Pass3D{});
        reference.Frame(theirs, lord.wearer, lord.body, referenceWorld, lord.map, 0.0f, true);
        ASSERT_EQ(world.random.State(), referenceWorld.random.State()) << "frame " << frame;
        EXPECT_EQ(ours.Vertices().size(), theirs.Vertices().size()) << "frame " << frame;
    }
    ExpectTheSameCloths(cloth, reference);
}

// After a held frame (Q-C85, P-C27 f), RestartTheClock: the next Advance runs the block once, like the original in the
// first frame it draws after the connect, although 5 s passed (by the clock it would be 64). The cloths go on from
// where they were.
TEST(CharacterClothTest, AfterARestartTheNextAdvanceRunsTheBlockOnce)
{
    using namespace std::chrono_literals;
    using Client::Models::OriginalFrameTime;
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

// A character that goes from class 3 to 4 with the same cloth: the array of one is not enough for the Dark Lord's four,
// and the cloth is built again instead of reading outside it.
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

// Between two original frames, the cloth is drawn carried with the bone it hangs from. An intermediate draw falls at 20
// ms, before the original's second frame.
constexpr Client::Core::Duration BetweenFrames = std::chrono::milliseconds{20};
// How far a carried point strays from the one the exact computation would give: the cloth is some 10,000 units from the
// world's origin, where a float has an ulp of 0.001.
constexpr float CarryTolerance = 0.01f;

TEST(CharacterClothTest, ReleasingResourcesPreservesTheOriginalFrameClock)
{
    const LoginLord lord = ReadTheLoginLord();
    ClothWorld world{.random = CrtRandom(17)};
    CharacterCloth cloth;
    const auto advance = [&](Client::Core::Duration clock)
    {
        ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        cloth.Advance(batch, lord.wearer, lord.body, world, lord.map, clock, true);
        return batch.Vertices().size();
    };

    EXPECT_GT(advance(Client::Core::Duration{}), 0u);
    cloth.ReleaseResources();
    EXPECT_TRUE(cloth.Cloths().empty());
    EXPECT_EQ(cloth.Count(), 0u);
    EXPECT_EQ(advance(BetweenFrames), 0u);
    EXPECT_GT(advance(Client::Models::OriginalFrameTime), 0u);
}

std::vector<glm::vec3> PositionsOf(const ModelBatch& batch)
{
    std::vector<glm::vec3> positions;
    for (const Client::Render::ModelVertex& vertex : batch.Vertices())
    {
        positions.push_back(vertex.position);
    }
    return positions;
}

// The original's first frame, which builds the cloth and draws it with the body of `lord`.
std::vector<glm::vec3> TheFirstFrame(CharacterCloth& cloth, const LoginLord& lord)
{
    ClothWorld world{.random = CrtRandom(11)};
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    cloth.Advance(batch, lord.wearer, lord.body, world, lord.map, Client::Core::Duration{}, true);
    return PositionsOf(batch);
}

// A draw between original frames with the body `body`, through DrawCurrent (the game) or through Advance with no
// original frames to run (the login and the selection).
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

// With the body still (the same bones), the intermediate draw is the last original frame's, bit for bit. The original
// frames, and all those of the faithful mode, simulate and do not come through here.
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

// The character that took a step between two original frames: the cloth goes with it, shifted the same as the body, and
// does not stay where it was until the next frame.
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

// The cloth built with the character hidden (0x4000: it is built and not simulated, 0x50da67) stays on that body. If
// the next draw already sees it and the character walked, it carries it from the body it was built on.
TEST(CharacterClothTest, AClothBuiltWhileHiddenWalksFromTheBodyItWasBuiltOn)
{
    const LoginLord lord = ReadTheLoginLord();
    CharacterCloth cloth;
    LoginLord hidden = lord;
    hidden.wearer.objectFlags |= 0x4000;
    ClothWorld world{.random = CrtRandom(11)};
    ModelBatch first;
    first.Begin(Client::Render::Pass3D{});
    cloth.Advance(first, hidden.wearer, hidden.body, world, hidden.map, Client::Core::Duration{}, true);
    ASSERT_TRUE(first.Vertices().empty());
    const std::vector<glm::vec3> built = ADrawBetweenFrames(cloth, lord, lord.body, false);
    constexpr glm::vec3 Step{60.0f, -25.0f, 3.0f};
    ClothBody walked = lord.body;
    walked.position += Step;

    const std::vector<glm::vec3> between = ADrawBetweenFrames(cloth, lord, walked, false);

    ASSERT_FALSE(built.empty());
    ASSERT_EQ(between.size(), built.size());
    for (std::size_t vertex = 0; vertex < built.size(); ++vertex)
    {
        ExpectNear(between[vertex], built[vertex] + Step, vertex);
    }
}

// The character that turned 90° between two original frames: the bones bring the turn, and the cloth turns with them
// around the character's position.
TEST(CharacterClothTest, BetweenOriginalFramesTheClothTurnsWithTheBody)
{
    const LoginLord lord = ReadTheLoginLord();
    CharacterCloth cloth;
    const std::vector<glm::vec3> first = TheFirstFrame(cloth, lord);
    const glm::mat3 quarterTurn{{0.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    std::vector<Client::Math::BoneTransform> turnedBones;
    for (const Client::Math::BoneTransform& bone : lord.body.bones)
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
