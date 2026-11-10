#include "World/GroundMarks.h"

#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/WorldRuntime.h"
#include "World/TerrainDecal.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

// Client::World::AddGroundMark, StepGroundMarks y DrawGroundMarks: la tabla de 0x760cab0 que llenan 0x548fe0,
// envejece 0x549090 y dibuja 0x549130 (06 § Las marcas del suelo).
namespace
{
using Client::Render::ModelBatch;
using Client::Render::ModelBlend;
using Client::Render::ModelDrawState;
using Client::Render::TextureSlot;
using Client::Render::Models::CrtRandom;
using Client::World::AddGroundMark;
using Client::World::DrawGroundMarks;
using Client::World::GroundMark;
using Client::World::GroundMarkRequest;
using Client::World::GroundMarkTable;
using Client::World::SlotComponents;
using Client::World::StepGroundMarks;
using Client::World::TerrainDecal;

constexpr std::uint32_t Seed = 0x2468ace0u;
constexpr std::int32_t BloodType = 0x5e3;

// Lo que pide 0x52e810: un 0x5e3 con la luz del objeto y un giro de rand() % 360.
GroundMarkRequest Blood()
{
    return GroundMarkRequest{.type = BloodType,
                             .position = glm::vec3(1000.0f, 2000.0f, 170.0f),
                             .angle = 123.0f,
                             .color = glm::vec3(0.5f, 0.25f, 0.125f),
                             .size = 0.65f};
}

// Una marca prendida, como la deja 0x548fe0 con `life`.
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

// Los componentes de Effect\blood01.tga (un TGA de 32 bits) y de un JPEG.
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

// Un lote con lo que deja 0x5fc260 antes de 0x549130: la mezcla normal del pasto, sin depth test y con culling.
ModelBatch BatchBeforeTheMarks()
{
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    batch.SetState(ModelDrawState{
        .texture = TextureSlot{0x32}, .blend = ModelBlend::Alpha, .depthTest = false, .cullEnabled = true});
    return batch;
}
} // namespace

// En la .bss: las 100 apagadas al arrancar, y así llegan a WorldRuntime.
TEST(GroundMarksTest, TheTableStartsWithEveryMarkOff)
{
    const Client::Scenes::WorldRuntime runtime{};

    ASSERT_EQ(runtime.groundMarks.size(), 100u);
    for (const GroundMark& mark : runtime.groundMarks)
    {
        EXPECT_FALSE(mark.live);
        EXPECT_EQ(mark.life, 0);
    }
}

// 0x549012..0x549087: el primer lugar apagado, con todo lo que pide, el alfa en 1 y la vida en rand() % 32 + 50,
// un solo rand().
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

// 0x54905c..0x54906c: la vida solo la sortean el 0x5e3, el 0x5e4 y el 0x606; otro tipo deja la del lugar, sin
// rand().
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

// Q-C231: sin lugar apagado, 0x548fe0 apaga el de menos vida (con signo, el primero si empatan) y no pone la nueva,
// sin rand().
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

// Q-C231: llena y sin ninguna vida debajo de 9.999, el original escribe un 0 en la dirección del tipo; MuRet corta
// sin tocar la tabla.
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

// 0x5490a1..0x549119 con el 0x5e3: la vida baja 1, el lado crece 0,004 (guardado en float) y el color pasa a
// (0,1; 0; 0). Con 50 o más de vida el alfa queda; con menos es vida · 0,02; sin vida se apaga.
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

// 0x5490b0..0x5490eb: el 0x5e4 y el 0x606 se borran como el 0x5e3 pero no crecen; el 7 achica 0,05 y se apaga
// debajo de 0,1; los demás solo pierden vida, aunque baje de 0.
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

// 0x549135 con la tabla vacía: la mezcla aditiva y nada más, sin un vértice ni un tramo.
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

// 0x54913f..0x5491a9: cada marca prendida, en el orden de la tabla, es el calco de 0x5fb2f0 con su tipo, su lugar,
// su lado en los dos lados, su color, el giro cambiado de signo, su alfa y 5 de alto; la textura de 4 componentes
// (blood01.tga) va con la mezcla normal y una de 3, con la aditiva. Sin depth test, como lo dejó 0x5fc260.
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
            ASSERT_TRUE(Client::World::DrawTerrainDecal(expected, *heights, decal).HasValue());
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
        // n = 1 con 0,654 (9 celdas) y n = 2 con 1,5 en la fila 128 desde la columna 252 (3 · 5 = 15).
        EXPECT_EQ(batch.Vertices().size(), (9u + 15u) * 6u);
    }
}

// 0x549152: el byte +0x28 del slot de un tipo que MuRet no subió no se sabe: corta en esa marca, con lo de antes ya
// dibujado.
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
