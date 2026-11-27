#include "Terrain/GroundMarks.h"

#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Kernel/WorldRuntime.h"
#include "Terrain/TerrainDecal.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

// Client::Terrain::AddGroundMark, StepGroundMarks and DrawGroundMarks: the table at 0x760cab0 that 0x548fe0 fills,
// 0x549090 ages and 0x549130 draws (06 § Las marcas del suelo).
namespace
{
using Client::Crt::CrtRandom;
using Client::Render::ModelBatch;
using Client::Render::ModelBlend;
using Client::Render::ModelDrawState;
using Client::Render::TextureSlot;
using Client::Terrain::AddGroundMark;
using Client::Terrain::DrawGroundMarks;
using Client::Terrain::GroundMark;
using Client::Terrain::GroundMarkRequest;
using Client::Terrain::GroundMarkTable;
using Client::Terrain::SlotComponents;
using Client::Terrain::StepGroundMarks;
using Client::Terrain::TerrainDecal;

constexpr std::uint32_t Seed = 0x2468ace0u;
constexpr std::int32_t BloodType = 0x5e3;

// What 0x52e810 asks for: a 0x5e3 with the object's light and a turn of rand() % 360.
GroundMarkRequest Blood()
{
    return GroundMarkRequest{.type = BloodType,
                             .position = glm::vec3(1000.0f, 2000.0f, 170.0f),
                             .angle = 123.0f,
                             .color = glm::vec3(0.5f, 0.25f, 0.125f),
                             .size = 0.65f};
}

// A live mark, as 0x548fe0 leaves it with `life`.
GroundMark LiveMark(std::int32_t type, std::int32_t life)
{
    return GroundMark{.live = true,
                      .type = type,
                      .size = 1.0f,
                      .position = glm::vec3(1000.0f, 2000.0f, 0.0f),
                      .angle = 0.0f,
                      .color = glm::vec3(1.0f, 1.0f, 1.0f),
                      .alpha = 1.0f,
                      .life = life};
}

// The components of Effect\blood01.tga (a 32-bit TGA) and of a JPEG.
SlotComponents Components(std::uint8_t components)
{
    return [components](std::int32_t) { return std::optional<std::uint8_t>(components); };
}

std::unique_ptr<Client::Data::Terrain::TerrainHeights> Heights()
{
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    for (std::size_t cell = 0; cell < heights->cells.size(); ++cell)
    {
        heights->cells[cell] = static_cast<float>(cell % 5) * 3.0f;
    }
    return heights;
}

// A batch with what 0x5fc260 leaves before 0x549130: the grass's normal blend, without depth test and with culling.
ModelBatch BatchBeforeTheMarks()
{
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    batch.SetState(ModelDrawState{
        .texture = TextureSlot{0x32}, .blend = ModelBlend::Alpha, .depthTest = false, .cullEnabled = true});
    return batch;
}
} // namespace

// In the .bss: all 100 off at startup, and that is how they reach WorldRuntime.
TEST(GroundMarksTest, TheTableStartsWithEveryMarkOff)
{
    const Client::Kernel::WorldRuntime runtime{};

    ASSERT_EQ(runtime.groundMarks.size(), 100u);
    for (const GroundMark& mark : runtime.groundMarks)
    {
        EXPECT_FALSE(mark.live);
        EXPECT_EQ(mark.life, 0);
    }
}

// 0x549012..0x549087: the first place that is off, with everything it asks for, the alpha at 1 and the life at
// rand() % 32 + 50, a single rand().
TEST(GroundMarksTest, AddingPutsTheMarkInTheFirstFreePlaceWithARandomLife)
{
    GroundMarkTable marks{};
    marks[0] = LiveMark(BloodType, 70);
    CrtRandom random(Seed);
    CrtRandom replay(Seed);

    ASSERT_TRUE(AddGroundMark(marks, Blood(), random).HasValue());

    const GroundMark& added = marks[1];
    EXPECT_TRUE(added.live);
    EXPECT_EQ(added.type, BloodType);
    EXPECT_EQ(added.position, glm::vec3(1000.0f, 2000.0f, 170.0f));
    EXPECT_EQ(added.angle, 123.0f);
    EXPECT_EQ(added.color, glm::vec3(0.5f, 0.25f, 0.125f));
    EXPECT_EQ(added.size, 0.65f);
    EXPECT_EQ(added.alpha, 1.0f);
    EXPECT_EQ(added.life, (replay.Next() % 32) + 50);
    EXPECT_EQ(random.State(), replay.State());
    EXPECT_FALSE(marks[2].live);
}

// 0x54905c..0x54906c: only 0x5e3, 0x5e4 and 0x606 draw a life; another type keeps the place's, without rand().
TEST(GroundMarksTest, OnlyTheFadingTypesDrawALife)
{
    for (const std::int32_t type : {0x5e4, 0x606, 7, 0x5e2, 0x5e5})
    {
        SCOPED_TRACE(type);
        GroundMarkTable marks{};
        marks[0].life = -12;
        CrtRandom random(Seed);
        GroundMarkRequest request = Blood();
        request.type = type;

        ASSERT_TRUE(AddGroundMark(marks, request, random).HasValue());

        const bool fades = type == 0x5e4 || type == 0x606;
        if (fades)
        {
            CrtRandom replay(Seed);
            EXPECT_EQ(marks[0].life, (replay.Next() % 32) + 50);
            EXPECT_EQ(random.State(), replay.State());
        }
        else
        {
            EXPECT_EQ(marks[0].life, -12);
            EXPECT_EQ(random.State(), CrtRandom(Seed).State());
        }
    }
}

// Q-C231: without a place that is off, 0x548fe0 turns off the one with the least life (signed, the first on a tie)
// and does not place the new one, without rand().
TEST(GroundMarksTest, AFullTableTurnsOffTheWeakestAndDropsTheNewMark)
{
    GroundMarkTable marks{};
    for (std::size_t place = 0; place < marks.size(); ++place)
    {
        marks[place] = LiveMark(BloodType, 60 + static_cast<std::int32_t>(place % 7));
    }
    marks[30].life = -5;
    marks[64].life = -5;
    const GroundMarkTable before = marks;
    CrtRandom random(Seed);

    ASSERT_TRUE(AddGroundMark(marks, Blood(), random).HasValue());

    EXPECT_FALSE(marks[30].live);
    for (std::size_t place = 0; place < marks.size(); ++place)
    {
        if (place != 30)
        {
            EXPECT_TRUE(marks[place].live) << place;
            EXPECT_EQ(marks[place].angle, before[place].angle) << place;
        }
    }
    EXPECT_EQ(marks[30].type, BloodType);
    EXPECT_EQ(marks[30].angle, 0.0f) << "la nueva no entra";
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());
}

// Q-C231: full and without any life below 9,999, the original writes a 0 at the type's address; MuRet fails without
// touching the table.
TEST(GroundMarksTest, AFullTableWithoutALifeBelow9999Stops)
{
    GroundMarkTable marks{};
    marks.fill(LiveMark(BloodType, 9999));
    CrtRandom random(Seed);

    const auto added = AddGroundMark(marks, Blood(), random);

    ASSERT_FALSE(added.HasValue());
    EXPECT_NE(added.Error().find("0x54900c"), std::string::npos) << added.Error();
    for (const GroundMark& mark : marks)
    {
        EXPECT_TRUE(mark.live);
    }
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());
}

// 0x5490a1..0x549119 with 0x5e3: the life drops by 1, the side grows 0.004 (stored as float) and the color becomes
// (0.1, 0, 0). With 50 or more life the alpha stays; with less it is life · 0.02; without life it turns off.
TEST(GroundMarksTest, ABloodMarkGrowsReddensAndFades)
{
    GroundMarkTable marks{};
    marks[0] = LiveMark(BloodType, 51);
    marks[1] = LiveMark(BloodType, 50);
    marks[2] = LiveMark(BloodType, 1);
    marks[3] = LiveMark(BloodType, 60);
    marks[3].live = false;

    StepGroundMarks(marks);

    EXPECT_EQ(marks[0].life, 50);
    EXPECT_EQ(marks[0].alpha, 1.0f);
    EXPECT_EQ(marks[0].size, static_cast<float>(1.0 + static_cast<double>(0.004f)));
    EXPECT_EQ(marks[0].color, glm::vec3(0.1f, 0.0f, 0.0f));
    EXPECT_TRUE(marks[0].live);
    EXPECT_EQ(marks[1].life, 49);
    EXPECT_EQ(marks[1].alpha, static_cast<float>(49.0 * static_cast<double>(0.02f)));
    EXPECT_TRUE(marks[1].live);
    EXPECT_EQ(marks[2].life, 0);
    EXPECT_EQ(marks[2].alpha, 0.0f);
    EXPECT_FALSE(marks[2].live);
    EXPECT_EQ(marks[3].life, 60) << "una apagada no cambia";
    EXPECT_EQ(marks[3].color, glm::vec3(1.0f, 1.0f, 1.0f));
}

// 0x5490b0..0x5490eb: 0x5e4 and 0x606 fade like 0x5e3 but do not grow; 7 shrinks by 0.05 and turns off below 0.1;
// the rest only lose life, even below 0.
TEST(GroundMarksTest, EachTypeAgesItsOwnWay)
{
    GroundMarkTable marks{};
    marks[0] = LiveMark(0x5e4, 10);
    marks[1] = LiveMark(0x606, 10);
    marks[2] = LiveMark(7, 10);
    marks[2].size = 0.5f;
    marks[3] = LiveMark(7, 10);
    marks[3].size = 0.15f;
    marks[4] = LiveMark(7, 10);
    marks[4].size = 0.125f;
    marks[5] = LiveMark(0x5ac, 0);

    StepGroundMarks(marks);

    for (std::size_t place : {0u, 1u})
    {
        EXPECT_EQ(marks[place].size, 1.0f) << place;
        EXPECT_EQ(marks[place].color, glm::vec3(0.1f, 0.0f, 0.0f)) << place;
        EXPECT_EQ(marks[place].alpha, static_cast<float>(9.0 * static_cast<double>(0.02f))) << place;
    }
    EXPECT_EQ(marks[2].size, static_cast<float>(0.5 - static_cast<double>(0.05f)));
    EXPECT_TRUE(marks[2].live);
    EXPECT_TRUE(marks[3].live) << "0,15 − 0,05 no baja de 0,1";
    EXPECT_FALSE(marks[4].live);
    for (std::size_t place : {2u, 3u, 4u})
    {
        EXPECT_EQ(marks[place].life, 9) << place;
        EXPECT_EQ(marks[place].color, glm::vec3(1.0f, 1.0f, 1.0f)) << place;
        EXPECT_EQ(marks[place].alpha, 1.0f) << place;
    }
    EXPECT_EQ(marks[5].life, -1);
    EXPECT_TRUE(marks[5].live);
    EXPECT_EQ(marks[5].size, 1.0f);
}

// 0x549135 with the table empty: the additive blend and nothing else, not a vertex or a span.
TEST(GroundMarksTest, AnEmptyTableDrawsNothing)
{
    const auto heights = Heights();
    ModelBatch batch = BatchBeforeTheMarks();
    const GroundMarkTable marks{};

    ASSERT_TRUE(DrawGroundMarks(batch, marks, *heights, Components(4)).HasValue());

    EXPECT_TRUE(batch.Commands().empty());
    EXPECT_TRUE(batch.Vertices().empty());
    EXPECT_EQ(batch.State().blend, ModelBlend::Additive);
    EXPECT_FALSE(batch.State().depthTest);
    EXPECT_FALSE(batch.State().cullEnabled.has_value()) << "0x620a90 apaga el culling de 0x6208d0";
}

// 0x54913f..0x5491a9: each live mark, in table order, is the decal of 0x5fb2f0 with its type, its place, its side on
// both sides, its color, the turn with its sign flipped, its alpha and a lift of 5; a 4-component texture
// (blood01.tga) goes with the normal blend and a 3-component one with the additive. No depth test, as 0x5fc260 left it.
TEST(GroundMarksTest, EachLiveMarkIsADecalWithTheBlendOfItsTexture)
{
    const auto heights = Heights();
    for (const std::uint8_t components : {std::uint8_t{4}, std::uint8_t{3}})
    {
        SCOPED_TRACE(static_cast<int>(components));
        GroundMarkTable marks{};
        marks[2] = LiveMark(BloodType, 40);
        marks[2].angle = 30.0f;
        marks[2].alpha = 0.8f;
        marks[2].color = glm::vec3(0.1f, 0.0f, 0.0f);
        marks[2].size = 0.654f;
        marks[7] = LiveMark(0x5e4, 60);
        marks[7].position = glm::vec3(25450.0f, 12850.0f, 0.0f);
        marks[7].size = 1.5f;
        marks[9] = LiveMark(BloodType, 60);
        marks[9].live = false;
        ModelBatch batch = BatchBeforeTheMarks();

        ASSERT_TRUE(DrawGroundMarks(batch, marks, *heights, Components(components)).HasValue());

        const ModelBlend blend = components == 3 ? ModelBlend::Additive : ModelBlend::Alpha;
        ModelBatch expected = BatchBeforeTheMarks();
        expected.SetState(ModelDrawState{.texture = std::nullopt, .blend = blend, .depthTest = false});
        for (const std::size_t place : {2u, 7u})
        {
            const GroundMark& mark = marks[place];
            const TerrainDecal decal{.slot = mark.type,
                                     .x = mark.position.x,
                                     .y = mark.position.y,
                                     .width = mark.size,
                                     .height = mark.size,
                                     .color = mark.color,
                                     .angle = -mark.angle,
                                     .alpha = mark.alpha,
                                     .lift = 5.0f};
            ASSERT_TRUE(Client::Terrain::DrawTerrainDecal(expected, *heights, decal).HasValue());
        }
        ASSERT_EQ(batch.Commands().size(), 2u);
        EXPECT_EQ(batch.Commands()[0].state,
                  (ModelDrawState{.texture = TextureSlot{0x5e3}, .blend = blend, .depthTest = false}));
        EXPECT_EQ(batch.Commands()[1].state,
                  (ModelDrawState{.texture = TextureSlot{0x5e4}, .blend = blend, .depthTest = false}));
        ASSERT_EQ(batch.Vertices().size(), expected.Vertices().size());
        for (std::size_t vertex = 0; vertex < batch.Vertices().size(); ++vertex)
        {
            EXPECT_EQ(batch.Vertices()[vertex].position, expected.Vertices()[vertex].position) << vertex;
            EXPECT_EQ(batch.Vertices()[vertex].uv, expected.Vertices()[vertex].uv) << vertex;
            EXPECT_EQ(batch.Vertices()[vertex].color, expected.Vertices()[vertex].color) << vertex;
        }
        // n = 1 with 0.654 (9 cells) and n = 2 with 1.5 on row 128 from column 252 (3 · 5 = 15).
        EXPECT_EQ(batch.Vertices().size(), (9u + 15u) * 6u);
    }
}

// 0x549152: the +0x28 byte of the slot of a type MuRet did not upload is unknown: it stops at that mark, with the
// earlier ones already drawn.
TEST(GroundMarksTest, AMarkWhoseSlotIsUnknownStops)
{
    const auto heights = Heights();
    GroundMarkTable marks{};
    marks[0] = LiveMark(0x5e4, 60);
    marks[1] = LiveMark(BloodType, 60);
    ModelBatch batch = BatchBeforeTheMarks();
    const SlotComponents onlyTheFirst = [](std::int32_t slot)
    { return slot == 0x5e4 ? std::optional<std::uint8_t>(std::uint8_t{4}) : std::nullopt; };

    const auto drawn = DrawGroundMarks(batch, marks, *heights, onlyTheFirst);

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x549152"), std::string::npos) << drawn.Error();
    EXPECT_NE(drawn.Error().find("0x5e3"), std::string::npos) << drawn.Error();
    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].state.texture, TextureSlot{0x5e4});
}
