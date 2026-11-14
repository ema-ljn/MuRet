#include "Scenes/GameHud.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Tables/SkillTable.h"
#include "Protocol/Inventory.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatBubbles.h"
#include "Scenes/EventMapObject.h"
#include "Scenes/GameData.h"
#include "Scenes/GameProjection.h"
#include "Scenes/HudItems.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/TooltipLines.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/GameText.h"
#include "UI/SprintfFormat.h"
#include "UI/Widget.h"
#include "World/ItemState.h"
#include "support/GoldenDir.h"
#include "support/RecordingTextDrawer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

// El HUD del juego (0x5a7320), sus superposiciones (0x5b0550) y el cursor (0x5ac2d0), CL8 hito 2. La prueba grande es
// el frame 1 de la traza de GL de Lorencia (GLIntercept, 1024 × 768, líneas 124230..126903, fuera del repo): cada quad
// del 2D, con su textura, su mezcla, su color, la esquina de arriba a la izquierda y la de abajo a la derecha, tal
// como los imprime la traza. El estado del personaje sale de lo que dibuja: la vida 1421 de 31131 (la única que da
// el orbe de la traza), el maná 10613 lleno, el AG 17749 lleno, la experiencia sin avance, la celda (143, 120), la
// skill 0x2c, 653 pociones de vida y 760 de maná, un ayudante con la barra en 48 y el mouse en (356, 193).
namespace
{
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::Models::CrtRandom;
using Client::Render::Models::OwnRecordCloseRandoms;
using Client::Render::Models::SmallLogicalCloseRandoms;
using Client::Scenes::GameHudContext;
using Client::Scenes::WorldState;
using Client::World::ItemState;

constexpr std::uint32_t Seed = 0x2468ace0u;
constexpr Client::App::PixelSize Screen640{640, 480};
constexpr Client::App::PixelSize Screen1024{1024, 768};

// El estado base: 19 cierres del registro propio (las flechas, los doce avisos, la barra de abajo, la experiencia,
// el AG y los tres de la barra principal), las dos celdas del héroe y nueve consultas del comercio (0x20) y de la
// tienda de NPC (0x10000): los avisos, las ventanas, el 3D, las grillas y el IsOpen(0) del final.
constexpr int HudBaseDraws = 19 * OwnRecordCloseRandoms + 2 * SmallLogicalCloseRandoms + 9;
// La skill de la barra: su entrada de Skill.bmd (60) en 0x5a5926 y en las dos llamadas a 0x40ca20.
constexpr int SkillIconDraws = 3 * 60;

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

// Un quad del lote: su textura, la mezcla, el color de sus vértices y las esquinas 0 y 2 del fan (arriba a la
// izquierda y abajo a la derecha, en píxeles de GL con la y hacia arriba).
struct DrawnQuad
{
    std::optional<std::uint16_t> slot;
    BlendMode blend = BlendMode::Alpha;
    Color color{};
    SpriteVertex first{};
    SpriteVertex third{};
};

// Los quads del lote en orden, sin las marcas de los textos.
std::vector<DrawnQuad> QuadsOf(const SpriteBatch& sprites)
{
    std::vector<DrawnQuad> quads;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (command.texture == Client::Tests::TextMarkerSlot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            quads.push_back(
                DrawnQuad{.slot = command.texture ? std::optional<std::uint16_t>{command.texture->value} : std::nullopt,
                          .blend = command.blend,
                          .color = vertices[first].color,
                          .first = vertices[first],
                          .third = vertices[first + 2]});
        }
    }
    return quads;
}

// Un quad de la traza: la línea de su glBegin, el slot de MuRet de su textura (o ninguna), la mezcla, el color y
// las esquinas 0 y 2 con sus UV.
struct TraceQuad
{
    int line = 0;
    std::optional<std::uint16_t> slot;
    BlendMode blend = BlendMode::Alpha;
    Color color{};
    std::array<float, 4> first{};
    std::array<float, 4> third{};
};

constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color Digits{1.0f, 0.95f, 0.75f, 1.0f};
constexpr Color Grey{0.6f, 0.6f, 0.6f, 1.0f};
constexpr std::optional<std::uint16_t> NoTexture{};
constexpr std::uint16_t DigitSlot = 1;

// Los quads del 2D del frame 1 de Lorencia, sin el texto del ayudante (124259): la barra del ayudante (0x5a6af0),
// la barra de abajo (0x5a9290), la experiencia, el AG, los orbes, la skill, las celdas, la vida, el maná, las
// ranuras (0x5a9630) y los dos cursores (0x5ac2d0). Las texturas de GL 21, 18, 19, 20, 74, 22, 23, 879, 2 y 9 son
// los slots 0xe9, 0xe6, 0xe7, 0xe8, 0x101, 0xea, 0xeb, 0x12a, 1 y 2.
const std::vector<TraceQuad>& LorenciaFrameQuads()
{
    static const std::vector<TraceQuad> quads{
        {124272,
         NoTexture,
         BlendMode::Alpha,
         {0.0f, 0.0f, 0.0f, 0.5f},
         {921.600037f, 740.799988f},
         {1008.000061f, 731.200012f}},
        {124285,
         NoTexture,
         BlendMode::Additive,
         {0.0f, 0.2f, 0.2f, 1.0f},
         {920.0f, 742.400024f},
         {1006.400024f, 732.799988f}},
        {124292,
         NoTexture,
         BlendMode::Additive,
         {0.0f, 0.196078f, 0.196078f, 1.0f},
         {923.200012f, 739.200012f},
         {1003.200012f, 736.0f}},
        {124299,
         NoTexture,
         BlendMode::Additive,
         {0.0f, 0.784314f, 0.196078f, 1.0f},
         {923.200012f, 739.200012f},
         {1000.0f, 736.0f}},
        {124324,
         std::uint16_t{0xe9},
         BlendMode::Alpha,
         White,
         {0.0f, 148.799988f, 0.0f, 0.0f},
         {172.800003f, 76.799988f, 0.84375f, 0.703125f}},
        {124334,
         std::uint16_t{0xe9},
         BlendMode::Alpha,
         White,
         {851.200012f, 148.799988f, 0.84375f, 0.0f},
         {1024.0f, 76.799988f, 0.0f, 0.703125f}},
        {124348,
         std::uint16_t{0xe6},
         BlendMode::Opaque,
         White,
         {0.0f, 76.799988f, 0.0f, 0.0f},
         {409.600006f, -0.000015f, 1.0f, 0.75f}},
        {124359,
         std::uint16_t{0xe7},
         BlendMode::Opaque,
         White,
         {409.600006f, 76.799988f, 0.0f, 0.0f},
         {614.400024f, -0.000015f, 1.0f, 0.75f}},
        {124370,
         std::uint16_t{0xe8},
         BlendMode::Opaque,
         White,
         {614.400024f, 76.799988f, 0.0f, 0.0f},
         {1024.0f, -0.000015f, 1.0f, 0.75f}},
        {124388,
         NoTexture,
         BlendMode::Alpha,
         {0.92f, 0.8f, 0.34f, 1.0f},
         {353.600006f, 65.599976f},
         {353.600006f, 59.199974f}},
        {124398,
         DigitSlot,
         BlendMode::Alpha,
         {0.91f, 0.81f, 0.6f, 1.0f},
         {672.799988f, 73.599976f, 0.0f, 0.0f},
         {687.200012f, 57.599976f, 0.0625f, 0.5f}},
        {124418,
         std::uint16_t{0x101},
         BlendMode::Alpha,
         Digits,
         {881.600037f, 68.799988f, 0.0f, 0.0f},
         {905.600037f, 11.199986f, 0.9375f, 0.5625f}},
        {124429,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {877.600037f, 20.799988f, 0.0625f, 0.0f},
         {892.000061f, 4.799988f, 0.125f, 0.5f}},
        {124439,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {887.679993f, 20.799988f, 0.4375f, 0.0f},
         {902.080017f, 4.799988f, 0.5f, 0.5f}},
        {124449,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {897.759949f, 20.799988f, 0.4375f, 0.0f},
         {912.159973f, 4.799988f, 0.5f, 0.5f}},
        {124459,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {907.839966f, 20.799988f, 0.25f, 0.0f},
         {922.239990f, 4.799988f, 0.3125f, 0.5f}},
        {124469,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {917.919922f, 20.799988f, 0.5625f, 0.0f},
         {932.319946f, 4.799988f, 0.625f, 0.5f}},
        {126601,
         std::uint16_t{0xea},
         BlendMode::Alpha,
         White,
         {155.199997f, 3.505554f, 0.0f, 0.715766f},
         {240.0f, -72.149223f, 0.828125f, 1.465766f}},
        {126612,
         std::uint16_t{0xeb},
         BlendMode::Alpha,
         White,
         {782.400024f, 76.799988f, 0.0f, 0.0f},
         {867.200012f, -0.000015f, 0.828125f, 0.75f}},
        {126625,
         std::uint16_t{0x12a},
         BlendMode::Alpha,
         {1.0f, 0.5f, 0.5f, 1.0f},
         {486.399994f, 57.599976f, 0.375f, 0.703125f},
         {537.599976f, -0.000027f, 0.5f, 0.839844f}},
        {126637,
         DigitSlot,
         BlendMode::Alpha,
         Grey,
         {19.200001f, 28.799988f, 0.0625f, 0.0f},
         {32.0f, 14.399987f, 0.125f, 0.5f}},
        {126647,
         DigitSlot,
         BlendMode::Alpha,
         Grey,
         {28.160002f, 28.799988f, 0.25f, 0.0f},
         {40.960003f, 14.399987f, 0.3125f, 0.5f}},
        {126657,
         DigitSlot,
         BlendMode::Alpha,
         Grey,
         {37.120003f, 28.799988f, 0.1875f, 0.0f},
         {49.920002f, 14.399987f, 0.25f, 0.5f}},
        {126667,
         DigitSlot,
         BlendMode::Alpha,
         Grey,
         {57.600002f, 28.799988f, 0.0625f, 0.0f},
         {70.400002f, 14.399987f, 0.125f, 0.5f}},
        {126677,
         DigitSlot,
         BlendMode::Alpha,
         Grey,
         {66.559998f, 28.799988f, 0.125f, 0.0f},
         {79.360001f, 14.399987f, 0.1875f, 0.5f}},
        {126687,
         DigitSlot,
         BlendMode::Alpha,
         Grey,
         {75.519997f, 28.799988f, 0.0f, 0.0f},
         {88.320000f, 14.399987f, 0.0625f, 0.5f}},
        {126698,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {131.199997f, 20.799988f, 0.0625f, 0.0f},
         {145.599991f, 4.799988f, 0.125f, 0.5f}},
        {126708,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {141.280014f, 20.799988f, 0.25f, 0.0f},
         {155.680008f, 4.799988f, 0.3125f, 0.5f}},
        {126718,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {151.360016f, 20.799988f, 0.125f, 0.0f},
         {165.760010f, 4.799988f, 0.1875f, 0.5f}},
        {126728,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {161.440018f, 20.799988f, 0.0625f, 0.0f},
         {175.840012f, 4.799988f, 0.125f, 0.5f}},
        {126738,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {828.0f, 20.799988f, 0.0625f, 0.0f},
         {842.400024f, 4.799988f, 0.125f, 0.5f}},
        {126748,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {838.080017f, 20.799988f, 0.0f, 0.0f},
         {852.480042f, 4.799988f, 0.0625f, 0.5f}},
        {126758,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {848.159973f, 20.799988f, 0.375f, 0.0f},
         {862.559998f, 4.799988f, 0.4375f, 0.5f}},
        {126768,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {858.239929f, 20.799988f, 0.0625f, 0.0f},
         {872.639954f, 4.799988f, 0.125f, 0.5f}},
        {126778,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {868.319946f, 20.799988f, 0.1875f, 0.0f},
         {882.719971f, 4.799988f, 0.25f, 0.5f}},
        {126788,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {340.0f, 52.799988f, 0.375f, 0.0f},
         {354.399994f, 36.799988f, 0.4375f, 0.5f}},
        {126798,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {350.080017f, 52.799988f, 0.3125f, 0.0f},
         {364.480011f, 36.799988f, 0.375f, 0.5f}},
        {126808,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {360.160004f, 52.799988f, 0.1875f, 0.0f},
         {374.559998f, 36.799988f, 0.25f, 0.5f}},
        {126818,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {389.600006f, 52.799988f, 0.4375f, 0.0f},
         {404.0f, 36.799988f, 0.5f, 0.5f}},
        {126828,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {399.680023f, 52.799988f, 0.375f, 0.0f},
         {414.080017f, 36.799988f, 0.4375f, 0.5f}},
        {126838,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {409.760010f, 52.799988f, 0.0f, 0.0f},
         {424.160004f, 36.799988f, 0.0625f, 0.5f}},
        {126875,
         std::uint16_t{2},
         BlendMode::Alpha,
         White,
         {566.400024f, 462.399994f, 0.0f, 0.0f},
         {604.800049f, 424.0f, 1.0f, 1.0f}},
        {126886,
         std::uint16_t{2},
         BlendMode::Alpha,
         White,
         {566.400024f, 462.399994f, 0.0f, 0.0f},
         {604.800049f, 424.0f, 1.0f, 1.0f}},
    };
    return quads;
}

// La traza imprime seis decimales: medio millonésimo de redondeo, y algo de margen.
constexpr float TraceTolerance = 2e-6f;
// Los colores de 0x5a6af0 son bytes sobre 255, que la traza imprime con seis decimales.
constexpr float ColorTolerance = 1e-6f;

void ExpectTheTraceQuad(const DrawnQuad& drawn, const TraceQuad& trace)
{
    SCOPED_TRACE(trace.line);
    EXPECT_EQ(drawn.slot, trace.slot);
    EXPECT_EQ(drawn.blend, trace.blend);
    EXPECT_NEAR(drawn.color.red, trace.color.red, ColorTolerance);
    EXPECT_NEAR(drawn.color.green, trace.color.green, ColorTolerance);
    EXPECT_NEAR(drawn.color.blue, trace.color.blue, ColorTolerance);
    EXPECT_NEAR(drawn.color.alpha, trace.color.alpha, ColorTolerance);
    EXPECT_NEAR(drawn.first.x, trace.first[0], TraceTolerance);
    EXPECT_NEAR(drawn.first.y, trace.first[1], TraceTolerance);
    EXPECT_NEAR(drawn.third.x, trace.third[0], TraceTolerance);
    EXPECT_NEAR(drawn.third.y, trace.third[1], TraceTolerance);
    if (!trace.slot)
        return;
    EXPECT_NEAR(drawn.first.u, trace.first[2], TraceTolerance);
    EXPECT_NEAR(drawn.first.v, trace.first[3], TraceTolerance);
    EXPECT_NEAR(drawn.third.u, trace.third[2], TraceTolerance);
    EXPECT_NEAR(drawn.third.v, trace.third[3], TraceTolerance);
}

class GameHudTest : public Client::Tests::GoldenDataTest
{
protected:
    void SetUp() override
    {
        GoldenDataTest::SetUp();
        ASSERT_FALSE(HasFatalFailure());
        auto loaded = Client::Scenes::LoadGameData(m_dataRoot);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        data = std::move(loaded).Value();
        // Como lo deja la entrada al mundo.
        world.OwnCharacter().ClearEquipmentForEntry();
        Client::Scenes::OwnCharacterRecord& own = world.OwnCharacter();
        own.life = 100;
        own.maxLife = 100;
        own.mana = 100;
        own.maxMana = 100;
        sprites.Begin(Screen640);
    }

    GameHudContext Context(bool projecting = false)
    {
        Client::Scenes::WorldRuntime& runtime = world.Runtime();
        Client::Scenes::ActorState& hero = Hero();
        return GameHudContext{.world = world,
                              .random = random,
                              .ownRecordDepth = runtime.ownRecordDepth,
                              .cellXDepth = hero.cellXAccesses,
                              .cellYDepth = hero.cellYAccesses,
                              .hud = runtime.hud,
                              .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                              .sound = sound,
                              .projecting = projecting,
                              .projection = projection,
                              .sprites = sprites,
                              .text = text,
                              .texts = &*data.text,
                              .items = &data.items->table,
                              .skills = &*data.skills,
                              .worldClock = worldClock,
                              .mouse = mouse,
                              .leftDown = leftDown};
    }

    Client::Core::Expected<void, std::string> Hud(bool projecting = false)
    {
        GameHudContext context = Context(projecting);
        return Client::Scenes::DrawTheHud(context);
    }

    Client::Core::Expected<void, std::string> Cursor()
    {
        GameHudContext context = Context();
        return Client::Scenes::DrawTheGameCursor(context);
    }

    Client::Scenes::ActorState& Hero()
    {
        return world.Actors()[world.Runtime().hero];
    }

    // La cuenta de la ranura rápida: pociones en la fila 0 del inventario.
    void PutPotion(std::size_t cell, std::uint16_t type, std::uint8_t count)
    {
        ItemState& item = world.Inventory().cells[cell];
        item.id = type;
        item.durability = count;
        item.occupied = 1;
    }

    // El héroe del frame 1 de Lorencia.
    void TheLorenciaHero()
    {
        Client::Scenes::OwnCharacterRecord& own = world.OwnCharacter();
        own.level = 1;
        own.experience = 0;
        own.nextExperience = 100;
        own.life = 1421;
        own.maxLife = 31131;
        own.mana = 10613;
        own.maxMana = 10613;
        own.bp = 17749;
        own.maxBp = 17749;
        own.byte6a = 1;
        own.words6c[0] = 0x2c;
        own.equipment[8].id = 0x1a00;
        own.equipment[8].durability = 245;
        Client::Scenes::ActorState& hero = Hero();
        hero.cellX = 143;
        hero.cellY = 120;
        hero.skillIndex = 0;
        hero.pet.model = 0x1c12;
        PutPotion(0, 0x1c00, 255);
        PutPotion(1, 0x1c01, 255);
        PutPotion(2, 0x1c03, 143);
        PutPotion(8, 0x1c04, 255);
        PutPotion(9, 0x1c05, 255);
        PutPotion(10, 0x1c06, 250);
        mouse = {356, 193};
        sprites.Begin(Screen1024);
    }

    WorldState world;
    CrtRandom random{Seed};
    std::optional<std::int32_t> sound;
    SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    float worldClock = 0.0f;
    Client::UI::Point mouse{};
    bool leftDown = false;
    const Client::Scenes::GameProjection* projection = nullptr;
    Client::Scenes::GameData data;
};

TEST_F(GameHudTest, TheLorenciaFrameDrawsTheQuadsOfTheTrace)
{
    TheLorenciaHero();
    // El ayudante (0x5ab26c) abre el registro propio una vez más para su durabilidad.
    const CrtRandom expected = Advanced(random, HudBaseDraws + OwnRecordCloseRandoms + SkillIconDraws);

    const auto drawn = Hud();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ASSERT_TRUE(Cursor().HasValue());
    ASSERT_TRUE(Cursor().HasValue());

    const std::vector<DrawnQuad> quads = QuadsOf(sprites);
    const std::vector<TraceQuad>& trace = LorenciaFrameQuads();
    ASSERT_EQ(quads.size(), trace.size());
    for (std::size_t i = 0; i < quads.size(); ++i)
        ExpectTheTraceQuad(quads[i], trace[i]);
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(world.Runtime().movement.skillDepths[0x2c], 0);
    // La vida por debajo del 20 %: el sonido 0x22 de 0x5a97c3.
    EXPECT_EQ(sound, 0x22);
    // El único texto es el nombre del ayudante, que termina en x = 575 + 50.
    ASSERT_EQ(text.texts.size(), 1u);
    const auto record = data.items->table.Record(0x1a00);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(text.texts[0].text, Client::Scenes::ItemRecordName(*record));
    EXPECT_EQ(text.texts[0].y, 4);
}

TEST_F(GameHudTest, TheBaseStateSpendsTheRandomsOfItsAccessesAndLeavesTheTextColors)
{
    const CrtRandom expected = Advanced(random, HudBaseDraws);

    const auto drawn = Hud();

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(Hero().cellXAccesses, 0);
    EXPECT_EQ(Hero().cellYAccesses, 0);
    EXPECT_TRUE(text.texts.empty());
    EXPECT_FALSE(sound.has_value());
    // La barra de abajo deja el blanco sobre negro (0x5a92a6) y apaga el aviso de durabilidad.
    const Client::Scenes::HudState& hud = world.Runtime().hud;
    EXPECT_EQ(hud.textColors.text, 0xffffffffu);
    EXPECT_EQ(hud.textColors.background, 0xff000000u);
    EXPECT_EQ(hud.durabilityWarning, 0);
}

// 0x5a9796..0x5a97b8: el 26 (0x4d3510) y el 27 (0x4d3820) escriben la vida y el maná que leen los orbes: con 25 de
// 100 al orbe de la vida le faltan 0,75 (v = 36 / 64) y con 50 de 100 al del maná 0,5 (v = 24 / 64).
TEST_F(GameHudTest, TheLifeAndTheManaPacketsMoveTheOrbs)
{
    const auto orbV = [&](std::uint16_t slot)
    {
        const std::vector<DrawnQuad> quads = QuadsOf(sprites);
        const auto orb = std::ranges::find(quads, std::optional<std::uint16_t>{slot}, &DrawnQuad::slot);
        return orb == quads.end() ? -1.0f : orb->first.v;
    };
    ASSERT_TRUE(Hud().HasValue());
    EXPECT_EQ(orbV(0xea), 0.0f);
    EXPECT_EQ(orbV(0xeb), 0.0f);

    namespace Inventory = Client::Protocol::Inventory;
    ASSERT_TRUE(Client::Scenes::ApplyLifeRefill(Inventory::LifeRefill{.place = Inventory::CurrentPlace, .life = 25},
                                                world, random)
                    .HasValue());
    ASSERT_TRUE(Client::Scenes::ApplyManaRefill(
                    Inventory::ManaRefill{.place = Inventory::CurrentPlace, .mana = 50, .bp = 0}, world, random)
                    .HasValue());
    sprites.Begin(Screen640);
    ASSERT_TRUE(Hud().HasValue());
    EXPECT_EQ(orbV(0xea), 0.5625f);
    EXPECT_EQ(orbV(0xeb), 0.375f);
}

// 0x5a7539..0x5a7604: en Tarkan, las dos arenas aditivas con (0,3; 0,3; 0,25), primero la lenta y después la rápida,
// con la u del reloj: ((int) T % 100000) · velocidad.
TEST_F(GameHudTest, TarkanSlidesItsTwoSandsWithTheClock)
{
    world.Runtime().map = 8;
    worldClock = 123456.75f;
    ASSERT_TRUE(Hud().HasValue());

    const std::vector<DrawnQuad> quads = QuadsOf(sprites);
    ASSERT_GE(quads.size(), 2u);
    EXPECT_EQ(quads[0].slot, 0x5c2);
    EXPECT_EQ(quads[1].slot, 0x5c3);
    for (std::size_t i = 0; i < 2; ++i)
    {
        EXPECT_EQ(quads[i].blend, BlendMode::Additive);
        EXPECT_EQ(quads[i].color, (Color{0.3f, 0.3f, 0.25f, 1.0f}));
    }
    EXPECT_EQ(quads[0].first.u, static_cast<float>(23456.0 * static_cast<double>(0.0002f)));
    EXPECT_EQ(quads[1].first.u, static_cast<float>(23456.0 * static_cast<double>(0.001f)));
}

// 0x5a760e: en Crywolf, el humo de 0x435930 una sola vez: la rápida con la mezcla normal y (0,4; 0,4; 0,45), la lenta
// aditiva, y 0x576353c en 0.
TEST_F(GameHudTest, CrywolfDrawsItsSmokeOnce)
{
    world.Runtime().map = 34;
    world.Runtime().crywolf.sandOverlay = 1;
    ASSERT_TRUE(Hud().HasValue());

    std::vector<DrawnQuad> quads = QuadsOf(sprites);
    ASSERT_GE(quads.size(), 2u);
    EXPECT_EQ(quads[0].slot, 0x5c3);
    EXPECT_EQ(quads[0].blend, BlendMode::Alpha);
    EXPECT_EQ(quads[0].color, (Color{0.4f, 0.4f, 0.45f, 1.0f}));
    EXPECT_EQ(quads[1].slot, 0x5c2);
    EXPECT_EQ(quads[1].blend, BlendMode::Additive);
    EXPECT_EQ(world.Runtime().crywolf.sandOverlay, 0);

    sprites.Begin(Screen640);
    ASSERT_TRUE(Hud().HasValue());
    quads = QuadsOf(sprites);
    ASSERT_FALSE(quads.empty());
    EXPECT_NE(quads[0].slot, 0x5c3);
}

// 0x5aac20: un ítem puesto con poca durabilidad saca su aviso con el nombre de Item.bmd y la cuenta, de color, y deja
// su número en D+0x44; un dibujo intermedio de MuRet no lo escribe.
TEST_F(GameHudTest, AWornItemShowsItsWarningAndMarksIt)
{
    ItemState& sword = world.OwnCharacter().equipment[0];
    sword.id = 0;
    sword.durability = 1;
    const auto record = data.items->table.Record(0);
    ASSERT_TRUE(record.has_value());
    const std::uint16_t maximum = Client::Scenes::MaxDurability(sword, *record, 0);
    ASSERT_GT(maximum, 5);

    ASSERT_TRUE(Hud(true).HasValue());
    EXPECT_EQ(sword.warning, 0) << "el dibujo intermedio no escribe los avisos";
    ASSERT_EQ(text.texts.size(), 1u);
    EXPECT_EQ(text.texts[0].text, Client::Scenes::ItemRecordName(*record) + " (1/" + std::to_string(maximum) + ")");
    EXPECT_EQ(text.texts[0].style.colors.text, 0xff0a0affu);
    EXPECT_EQ(text.texts[0].style.colors.background, 0x80000000u);
    EXPECT_EQ(text.texts[0].y, 15);
    // Modo 4: termina en el ancho del mundo.
    EXPECT_EQ(text.texts[0].x +
                  static_cast<int>(text.texts[0].text.size()) * Client::Tests::RecordingTextDrawer::LetterWidth,
              640);

    text.texts.clear();
    ASSERT_TRUE(Hud().HasValue());
    EXPECT_EQ(sword.warning, 7);

    // Roto: el 8, en blanco sobre rojo.
    sword.durability = 0;
    text.texts.clear();
    ASSERT_TRUE(Hud().HasValue());
    EXPECT_EQ(sword.warning, 8);
    ASSERT_EQ(text.texts.size(), 1u);
    EXPECT_EQ(text.texts[0].style.colors.background, 0x800a0affu);

    // Sin la fuerza que pide (D+0x1e contra +0x2a + la fuerza), el 3, aunque la durabilidad alcance.
    sword.durability = static_cast<std::uint8_t>(maximum);
    sword.word1e = 10;
    ASSERT_TRUE(Hud().HasValue());
    EXPECT_EQ(sword.warning, 3);
}

// 0x5ab810: las flechas en la mano derecha muestran el Text 351 con las suyas y las del inventario, en x = ancho −
// 0x5a con 640 de ancho, y el ayudante baja a y = 24.
TEST_F(GameHudTest, TheArrowsShowTheirCountAndPushTheHelperDown)
{
    ItemState& arrows = world.OwnCharacter().equipment[0];
    arrows.id = 0x80f;
    arrows.durability = 100;
    world.OwnCharacter().equipment[8].id = 0x1a00;
    world.OwnCharacter().equipment[8].durability = 255;
    Hero().pet.model = 0x1c12;

    ASSERT_TRUE(Hud().HasValue());

    ASSERT_GE(text.texts.size(), 2u);
    const auto format = Client::UI::TextAt(&*data.text, 351);
    ASSERT_TRUE(format.has_value());
    const std::array<std::string_view, 2> arguments{"100", "0"};
    EXPECT_EQ(text.texts[0].text, Client::UI::PlaceArguments(*format, arguments));
    EXPECT_EQ(text.texts[0].x, 640 - 0x5a);
    EXPECT_EQ(text.texts[0].y, 4);
    EXPECT_EQ(world.Runtime().hud.arrowsTextWidth,
              static_cast<std::int32_t>(text.texts[0].text.size()) * Client::Tests::RecordingTextDrawer::LetterWidth);
    EXPECT_EQ(text.texts[1].y, 24);
}

// 10 y 22 (CL8 hito 3): el freno de las macros dibuja "Macro Time" centrado en el mundo, en y = 0x188; con el campo
// del chat abierto, la caja sin mezcla (0xf8 y 0xf9) y, con el modo de F3, su indicador (0xef).
TEST_F(GameHudTest, TheMacroTimeAndTheChatBoxAreDrawn)
{
    world.Runtime().hud.macroTime = Client::UI::MacroLockSteps;
    world.Runtime().ui.chatOpen = true;

    ASSERT_TRUE(Hud().HasValue());

    const auto macro = std::ranges::find_if(text.texts, [](const auto& drawn) { return drawn.text == "Macro Time"; });
    ASSERT_NE(macro, text.texts.end());
    EXPECT_EQ(macro->x, (640 - 50) / 2);
    EXPECT_EQ(macro->y, 0x188);
    std::vector<std::uint16_t> chatSlots;
    for (const DrawnQuad& quad : QuadsOf(sprites))
    {
        if (!quad.slot || (*quad.slot != 0xf8 && *quad.slot != 0xf9 && *quad.slot != 0xef))
            continue;
        chatSlots.push_back(*quad.slot);
        EXPECT_EQ(quad.blend, *quad.slot == 0xef ? BlendMode::Alpha : BlendMode::Opaque);
    }
    EXPECT_EQ(chatSlots, (std::vector<std::uint16_t>{0xf8, 0xf9, 0xef}));
}

// 0x5a5860 con una skill de comando (0x78 a 0x7c): el atlas command (300) y su número desde 1, sin la espera.
TEST_F(GameHudTest, ACommandSkillDrawsItsIconAndItsNumber)
{
    world.OwnCharacter().byte6a = 1;
    Hero().skillIndex = 0x79;
    const CrtRandom expected = Advanced(random, HudBaseDraws + SkillIconDraws);

    ASSERT_TRUE(Hud().HasValue());

    EXPECT_EQ(random.State(), expected.State());
    const std::vector<DrawnQuad> quads = QuadsOf(sprites);
    const auto icon = std::ranges::find_if(quads, [](const DrawnQuad& quad) { return quad.slot == 300; });
    ASSERT_NE(icon, quads.end());
    // La segunda del atlas: u de 32/256 a 64/256.
    EXPECT_FLOAT_EQ(icon->first.u, 0.125f);
    EXPECT_FLOAT_EQ(icon->third.u, 0.25f);
    const auto number = std::next(icon);
    ASSERT_NE(number, quads.end());
    EXPECT_EQ(number->slot, DigitSlot);
    EXPECT_EQ(number->color, (Color{1.0f, 0.9f, 0.8f, 1.0f}));
    // El 2, en (308 − 4,5; 470).
    EXPECT_FLOAT_EQ(number->first.u, 0.125f);
    EXPECT_FLOAT_EQ(number->first.x, 303.5f);
}

// 18. 0x5f7440: la party (0x5f0be0, lo primero) y los comandos (0x40a650, casi al final) en (0x1c2, 0), con el fondo
// de Item_Back01 y 02 y el botón de cerrar; los comandos, con sus once botones de Message_box. En Lorencia y en Devias.
TEST_F(GameHudTest, TheEighteenthChildDrawsThePartyAndTheCommands)
{
    const auto windowAt = [](const std::vector<DrawnQuad>& quads)
    {
        return std::ranges::find_if(quads, [](const DrawnQuad& quad)
                                    { return quad.slot == std::uint16_t{0x104} && quad.first.x == 450.0f; });
    };
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        world.Runtime() = Client::Scenes::WorldRuntime{};
        world.Runtime().map = map;
        world.Runtime().panels.party = 1;
        sprites.Begin(Screen640);
        const auto party = Hud();
        ASSERT_TRUE(party.HasValue()) << party.Error();
        const std::vector<DrawnQuad> partyQuads = QuadsOf(sprites);
        const auto partyWindow = windowAt(partyQuads);
        ASSERT_NE(partyWindow, partyQuads.end());
        ASSERT_GE(std::distance(partyWindow, partyQuads.end()), 3);
        EXPECT_EQ(std::next(partyWindow)->slot, std::uint16_t{0x105});
        EXPECT_EQ(std::next(partyWindow, 2)->slot, std::uint16_t{0x118});
        EXPECT_EQ(world.Runtime().hud.partyX, 0x1c2);

        world.Runtime() = Client::Scenes::WorldRuntime{};
        world.Runtime().map = map;
        world.Runtime().panels.delegated.commands = 1;
        sprites.Begin(Screen640);
        const auto commands = Hud();
        ASSERT_TRUE(commands.HasValue()) << commands.Error();
        const std::vector<DrawnQuad> commandQuads = QuadsOf(sprites);
        const auto commandWindow = windowAt(commandQuads);
        ASSERT_NE(commandWindow, commandQuads.end());
        EXPECT_EQ(std::count_if(commandWindow, commandQuads.end(),
                                [](const DrawnQuad& quad) { return quad.slot == std::uint16_t{0xf0}; }),
                  11);
        EXPECT_EQ(world.Runtime().hud.commandsX, 0x1c2);
    }
}

// Lo que es de otra ola corta con su dirección donde su condición se cumple.
TEST_F(GameHudTest, ThePiecesOfOtherWavesStopWithTheirAddress)
{
    struct Case
    {
        std::string_view address;
        void (*arrange)(WorldState&);
    };
    const std::array<Case, 6> cases{{
        {"0x5a89b0", [](WorldState& state) { state.Runtime().hud.partyCount = 2; }},
        {"0x5f7180", [](WorldState& state) { state.Runtime().panels.panel2000 = 1; }},
        {"0x40bab0",
         [](WorldState& state) { state.Runtime().eventObject = Client::Scenes::EventMapObject{.field08 = 5}; }},
        {"0x40c2d0",
         [](WorldState& state)
         {
             state.Runtime().map = 11;
             state.Runtime().eventObject =
                 Client::Scenes::EventMapObject{.kind = Client::Scenes::EventMapKind::BloodCastle, .field10 = 1};
         }},
        {"0x4125b0",
         [](WorldState& state)
         {
             state.Runtime().map = 30;
             state.Runtime().siegePanelKind = std::uint8_t{0};
         }},
        {"0x5e9b10", [](WorldState& state) { state.Runtime().ui.field78abb20 = 1; }},
    }};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.address);
        world.Runtime() = Client::Scenes::WorldRuntime{};
        probe.arrange(world);
        const auto drawn = Hud();
        ASSERT_FALSE(drawn.HasValue());
        EXPECT_NE(drawn.Error().find(probe.address), std::string::npos) << drawn.Error();
    }
}

// 12. 0x5a9050: los globos, con la proyección del pase del mundo (sin ella, un globo vivo corta). El globo prende
// 0x77f3b66 y la barra de abajo (0x5a9290) lo apaga.
TEST_F(GameHudTest, TheTwelfthChildDrawsTheChatBubbles)
{
    Client::Scenes::ActorState& owner = world.Actors()[4];
    owner.alive = true;
    owner.seen = true;
    owner.boundsTop = 120.0f;
    Client::Scenes::ChatBubble& bubble = world.Runtime().chat.bubbles[0];
    bubble.name = "Tero";
    bubble.nameTime = 100;
    bubble.owner = 4;

    const auto cut = Hud();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5a90be"), std::string::npos) << cut.Error();

    // La vista deja el ojo en (x, y, 1): el dueño en el origen va al centro.
    Client::Scenes::GameProjection flat;
    flat.view = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    flat.horizontalScale = 1.0f;
    flat.verticalScale = 1.0f;
    flat.centerX = 320;
    flat.centerY = 240;
    flat.screenWidth = 640;
    flat.screenHeight = 480;
    projection = &flat;
    text.texts.clear();
    const auto drawn = Hud();

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ASSERT_EQ(text.texts.size(), 1u);
    EXPECT_EQ(text.texts[0].text, "Tero");
    EXPECT_TRUE(text.texts[0].box.has_value());
    EXPECT_EQ(bubble.x, 320 - 27 / 2);
    EXPECT_EQ(bubble.y, 240 - 0x24);
    EXPECT_EQ(world.Runtime().hud.durabilityWarning, 0);
}

// 5. 0x42a4d0: las barras de vida de la cola, con la proyección del pase del mundo (sin ella, una barra corta). Cada
// frame dibuja la mitad redondeada para arriba y deja el resto; un dibujo intermedio vacía una copia.
TEST_F(GameHudTest, TheFifthChildDrawsHalfTheHealthBars)
{
    std::deque<Client::Scenes::QueuedHealthBar>& bars = world.Runtime().healthBars;
    for (std::uint8_t stage = 0; stage < 3; ++stage)
        bars.push_back(Client::Scenes::QueuedHealthBar{.position = {0.0f, 0.0f, 0.0f}, .fill = stage});

    const auto cut = Hud();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x42a529"), std::string::npos) << cut.Error();
    EXPECT_EQ(bars.size(), 3u);

    Client::Scenes::GameProjection flat;
    flat.view = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    flat.horizontalScale = 1.0f;
    flat.verticalScale = 1.0f;
    flat.centerX = 320;
    flat.centerY = 240;
    flat.screenWidth = 640;
    flat.screenHeight = 480;
    projection = &flat;
    const auto projected = Hud(true);
    ASSERT_TRUE(projected.HasValue()) << projected.Error();
    EXPECT_EQ(bars.size(), 3u);

    const auto drawn = Hud();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ASSERT_EQ(bars.size(), 1u);
    EXPECT_EQ(bars.front().fill, 2);
}

// Devil Square tiene su objeto de evento, pero su método +4 es un ret (0x40bdb0) y la cuenta (0x40bab0) sale con +8
// en 0: el HUD sigue.
TEST_F(GameHudTest, TheEventObjectsWithoutTheirPacketsDrawNothing)
{
    for (const auto& [map, kind] : {std::pair{9, Client::Scenes::EventMapKind::DevilSquare},
                                    std::pair{11, Client::Scenes::EventMapKind::BloodCastle},
                                    std::pair{18, Client::Scenes::EventMapKind::ChaosCastle}})
    {
        SCOPED_TRACE(map);
        world.Runtime().map = map;
        world.Runtime().eventObject = Client::Scenes::EventMapObject{.kind = kind};
        const auto drawn = Hud();
        EXPECT_TRUE(drawn.HasValue()) << drawn.Error();
    }
}

// 0x5ac2d0: el cuadro de 24 × 24 en (x − 2, y − 2) del slot que elige su tabla.
TEST_F(GameHudTest, TheCursorPicksItsSlot)
{
    mouse = {100, 200};
    const auto slotOf = [this]() -> std::optional<std::uint16_t>
    {
        sprites.Begin(Screen640);
        const auto drawn = Cursor();
        EXPECT_TRUE(drawn.HasValue()) << drawn.Error();
        const std::vector<DrawnQuad> quads = QuadsOf(sprites);
        if (quads.size() != 1u)
            return std::nullopt;
        EXPECT_FLOAT_EQ(quads[0].first.x, 98.0f);
        EXPECT_FLOAT_EQ(quads[0].first.y, 480.0f - 198.0f);
        return quads[0].slot;
    };
    Client::Scenes::WorldRuntime& runtime = world.Runtime();
    EXPECT_EQ(slotOf(), 2);
    leftDown = true;
    EXPECT_EQ(slotOf(), 3);
    runtime.hud.pushCursor = 1;
    EXPECT_EQ(slotOf(), 0xa);
    runtime.panels.repairMode = 1;
    EXPECT_EQ(slotOf(), 7);
    runtime.panels.delegated.commands = 1;
    EXPECT_EQ(slotOf(), 0x65a);
    Hero().safeZone = 1;
    runtime.selectedTargets[2] = 3;
    EXPECT_EQ(slotOf(), 0x65a) << "en la zona segura, el personaje elegido no cambia el cursor";
    runtime.selectedTargets[0] = 7;
    EXPECT_EQ(slotOf(), 5);
    runtime.dialogs.current = 0x6e;
    EXPECT_EQ(slotOf(), 2);
}

// El de hablar (6) anima sus cuartos con ftol(T · 0,01) % 6: u en 0,5 con 1, 3 y 5; v en 0,5 con 2, 3 y 4. El 0,01
// es un float (0x6b88d4) un poco menor que 0,01, así que con T = 100 todavía es el cuadro 0.
TEST_F(GameHudTest, TheTalkCursorWalksItsQuarters)
{
    world.Runtime().selectedTargets[1] = 4;
    for (const auto& [clock, u, v] :
         {std::tuple{0.0f, 0.0f, 0.0f}, std::tuple{100.0f, 0.0f, 0.0f}, std::tuple{150.0f, 0.5f, 0.0f},
          std::tuple{250.0f, 0.0f, 0.5f}, std::tuple{350.0f, 0.5f, 0.5f}, std::tuple{450.0f, 0.0f, 0.5f},
          std::tuple{550.0f, 0.5f, 0.0f}, std::tuple{650.0f, 0.0f, 0.0f}})
    {
        SCOPED_TRACE(clock);
        worldClock = clock;
        sprites.Begin(Screen640);
        ASSERT_TRUE(Cursor().HasValue());
        const std::vector<DrawnQuad> quads = QuadsOf(sprites);
        ASSERT_EQ(quads.size(), 1u);
        EXPECT_EQ(quads[0].slot, 6);
        EXPECT_EQ(quads[0].first.u, u);
        EXPECT_EQ(quads[0].first.v, v);
        EXPECT_EQ(quads[0].third.u, u + 0.5f);
    }
}

// Con el modo 2 de reparar, el 7 girado por el signo de sin(T · 0,02): 45° en (x + 5, y + 18) o derecho en
// (x + 10, y + 10), centrado ahí.
TEST_F(GameHudTest, TheRepairCursorTurnsWithTheClock)
{
    world.Runtime().panels.repairMode = 2;
    mouse = {100, 200};
    for (const auto& [clock, x, y] : {std::tuple{0.0f, 105.0f, 218.0f}, std::tuple{100.0f, 110.0f, 210.0f}})
    {
        SCOPED_TRACE(clock);
        worldClock = clock;
        sprites.Begin(Screen640);
        ASSERT_TRUE(Cursor().HasValue());
        const std::vector<DrawnQuad> quads = QuadsOf(sprites);
        ASSERT_EQ(quads.size(), 1u);
        EXPECT_EQ(quads[0].slot, 7);
        EXPECT_NEAR((quads[0].first.x + quads[0].third.x) * 0.5f, x, 1e-4f);
        EXPECT_NEAR((quads[0].first.y + quads[0].third.y) * 0.5f, 480.0f - y, 1e-4f);
    }
}

// Los cursores que leen otra ola: el de los objetos (CL9.1) y el de atacar fuera de la zona segura (CL9.2).
TEST_F(GameHudTest, TheCursorsOfOtherWavesStop)
{
    world.Runtime().selectedTargets[3] = 1;
    auto drawn = Cursor();
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5ac3d8"), std::string::npos) << drawn.Error();

    world.Runtime().selectedTargets[3] = -1;
    world.Runtime().selectedTargets[2] = 1;
    Hero().safeZone = 0;
    drawn = Cursor();
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x558bb0"), std::string::npos) << drawn.Error();
}

// 6. 0x5a6600 (desde 0x42a735): el mouse sobre un ícono de estado dibuja su ayuda en la caja de los tooltips, en
// (x + 10, 0x1d), con el registro propio y la entrada de Skill.bmd del ícono abiertos alrededor (3.848 y 60 rand()).
// El 0xcc escribe tres textos y deja los colores del 3 y del 4 (Q-C597).
TEST_F(GameHudTest, TheMouseOverAStatusIconShowsItsHelp)
{
    Hero().flags78 = 0x10000;
    Client::Scenes::TooltipLines& lines = world.Runtime().hud.tooltip;
    lines.colors[3] = Client::Scenes::TooltipWhiteOnRed;
    const CrtRandom before = random;
    mouse = {320, 300};
    ASSERT_TRUE(Hud().HasValue());
    CrtRandom away = random;
    for (std::uint32_t i = 0; i < OwnRecordCloseRandoms + Client::Data::Tables::SkillRecordSize; ++i)
        static_cast<void>(away.Next());

    random = before;
    sprites.Begin(Screen640);
    mouse = {190, 10};
    const auto drawn = Hud();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(random.State(), away.State());
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
    const auto record = data.skills->Record(0xcc);
    ASSERT_TRUE(record.has_value());
    std::string name;
    for (const std::byte value : *record)
    {
        if (value == std::byte{0})
            break;
        name.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
    }
    EXPECT_EQ(lines.text[0], "\n");
    ASSERT_FALSE(name.empty());
    EXPECT_EQ(lines.text[1], name);
    EXPECT_TRUE(std::ranges::any_of(text.texts, [&name](const auto& drawnText) { return drawnText.text == name; }));
    EXPECT_EQ(lines.colors[1], Client::Scenes::TooltipBlue);
    EXPECT_EQ(lines.bold[1], 1);
    EXPECT_EQ(lines.text[2], "\n");
    const std::array<std::size_t, 3> texts{1638, 1639, 1472};
    for (std::size_t i = 0; i < texts.size(); ++i)
    {
        const std::optional<std::string_view> help = Client::UI::TextAt(&*data.text, texts[i]);
        ASSERT_TRUE(help.has_value());
        EXPECT_EQ(lines.text[3 + i], Client::UI::Sprintf(*help)) << i;
    }
    EXPECT_EQ(lines.colors[3], Client::Scenes::TooltipWhiteOnRed);
    EXPECT_EQ(lines.colors[5], Client::Scenes::TooltipWhite);
    EXPECT_EQ(lines.text[6], "\n");
}

// 0x5a6600 con un ícono sin texto en la tabla de 0x5a69fc (el 0x35): el renglón 3 queda con lo que tenía, en blanco
// (Q-C597).
TEST_F(GameHudTest, AStatusIconWithoutTextKeepsTheThirdLine)
{
    Hero().flags78 = 0x400;
    Client::Scenes::TooltipLines& lines = world.Runtime().hud.tooltip;
    lines.text[3] = "previo";
    lines.colors[3] = Client::Scenes::TooltipRed;
    mouse = {190, 10};
    const auto drawn = Hud();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(lines.text[3], "previo");
    EXPECT_EQ(lines.colors[3], Client::Scenes::TooltipWhite);
    EXPECT_EQ(lines.text[4], "\n");
    // El nombre de la entrada 0x35 de Skill.bmd está vacío: la caja mide hasta el renglón 1 y no escribe nada.
    EXPECT_EQ(lines.text[1], "");
    EXPECT_TRUE(text.texts.empty());
}
} // namespace
