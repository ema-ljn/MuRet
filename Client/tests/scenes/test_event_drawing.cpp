#include "Scenes/EventDrawing.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ActorState.h"
#include "Scenes/EventMapObject.h"
#include "Scenes/GameHud.h"
#include "Scenes/GamePanels.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/PanelRegistry.h"

#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// CL9 hito 11, fase 2 (06 § Los eventos): los dibujos de los eventos en la pasada 2D, contra el ASM de c32bbd5f sin
// traza de GL (*estático*; lo que la VM tiene que confirmar está en 08, P-C294). Text.bmd es inventado, con los
// especificadores de los textos del original.
namespace
{
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::Models::CrtRandom;
using Client::Render::Text::FontKind;
using Client::Scenes::EventMapKind;
using Client::Scenes::EventMapObject;
using Client::Scenes::GameHudContext;
using Client::Scenes::WorldState;
using Client::Tests::RecordedText;
using Client::Tests::RecordingTextDrawer;

constexpr std::uint32_t Seed = 0x13579bdfu;
constexpr Client::App::PixelSize Screen640{640, 480};
constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr std::uint32_t WhiteText = 0xffffffffu;

std::map<std::size_t, std::string> EventTexts()
{
    return {
        {647, "bien hecho"},
        {648, "%s aguanto la prueba"},
        {680, "puesto"},
        {681, "quien"},
        {682, "puntos"},
        {683, "exp"},
        {684, "premio"},
        {685, "lo mio"},
        {857, "ganaste"},
        {858, "mision cumplida"},
        {859, "perdiste"},
        {860, "mision sin cumplir"},
        {861, "exp %d"},
        {862, "zen %d"},
        {863, "puntos %d"},
        {864, "bichos ( %d/%d )"},
        {865, "queda"},
        {866, "pelota ( %d/%d )"},
        {1151, "el guardia descansa"},
        {1152, "la mision"},
        {1153, "otra vez sera"},
        {1161, "rivales ( %d/%d )"},
        {1162, "monstruos %d"},
        {1163, "jugadores %d"},
        {1390, "faltan %d:0%d"},
        {1391, "faltan %d:%d"},
        {1392, "arranca en %d"},
    };
}

// El color de cada marca de texto del lote: el glColor que había al dibujarlo.
std::vector<Color> MarkerColors(const SpriteBatch& sprites)
{
    std::vector<Color> colors;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (command.texture != Client::Tests::TextMarkerSlot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
            colors.push_back(vertices[command.firstVertex + quad * 6].color);
    }
    return colors;
}

// El color que hay en el lote: el de un rectángulo sin textura dibujado ahora (lo último de la prueba).
Color ColorNow(SpriteBatch& sprites)
{
    sprites.ColorQuad(0.0f, 0.0f, 1.0f, 1.0f);
    return sprites.Vertices().back().color;
}

void ExpectColor(const Color& drawn, const Color& expected)
{
    EXPECT_FLOAT_EQ(drawn.red, expected.red);
    EXPECT_FLOAT_EQ(drawn.green, expected.green);
    EXPECT_FLOAT_EQ(drawn.blue, expected.blue);
    EXPECT_FLOAT_EQ(drawn.alpha, expected.alpha);
}

class EventDrawingTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        sprites.Begin(Screen640);
    }

    GameHudContext Context(bool projecting = false)
    {
        Client::Scenes::WorldRuntime& runtime = world->Runtime();
        Client::Scenes::ActorState& hero = world->Actors()[runtime.hero];
        return GameHudContext{.world = *world,
                              .random = random,
                              .ownRecordDepth = runtime.ownRecordDepth,
                              .cellXDepth = hero.cellXAccesses,
                              .cellYDepth = hero.cellYAccesses,
                              .hud = runtime.hud,
                              .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                              .sound = sound,
                              .projecting = projecting,
                              .sprites = sprites,
                              .text = text,
                              .texts = &texts,
                              .worldClock = worldClock};
    }

    // La x de 0x623690 con la medida del ITextDrawer de las pruebas.
    static int Centered(int x, std::string_view line)
    {
        return Client::Render::Text::CenteredAt(
            x, {static_cast<int>(line.size()) * RecordingTextDrawer::LetterWidth, RecordingTextDrawer::Height},
            Screen640);
    }

    // Un frame nuevo: el lote vacío y los globales del texto como los deja el HUD.
    void Restart()
    {
        world->Runtime().hud.dcFont = FontKind::Normal;
        world->Runtime().hud.textColors = {};
        text.texts.clear();
        sprites.Begin(Screen640);
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    CrtRandom random{Seed};
    std::optional<std::int32_t> sound;
    SpriteBatch sprites;
    RecordingTextDrawer text{sprites};
    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable(EventTexts());
    float worldClock = 0.0f;
};

// ---------------------------------------------------------------------------------------------------------------
// 0x5a7eb0: el tiempo del F3 22.

// Con la palabra en 0, o con más de 10 minutos (660 s o más), no dibuja, no saca rand() (0x5bb8d0 no corre) y deja
// los colores y la fuente como estaban.
TEST_F(EventDrawingTest, TheEventTimeDrawsNothingWithoutTimeOrPastTenMinutes)
{
    for (const std::uint16_t time : {std::uint16_t{0}, std::uint16_t{660}, std::uint16_t{719}, std::uint16_t{0xffff}})
    {
        SCOPED_TRACE(time);
        world->Runtime().events.timeValue = time;
        const std::uint32_t before = random.State();
        GameHudContext context = Context();
        const auto drawn = Client::Scenes::DrawTheEventTime(context);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_TRUE(text.texts.empty());
        EXPECT_EQ(random.State(), before);
        EXPECT_EQ(world->Runtime().hud.dcFont, FontKind::Normal);
        EXPECT_EQ(world->Runtime().hud.textColors.text, WhiteText);
    }
}

// Hasta 10 minutos: 1390 ("%d:0%d") con menos de 10 segundos y 1391 ("%d:%d") con 10 o más, con la fuente grande,
// en 0xff0a0aff (0xff0affff con menos de un minuto) en (mitad del mundo, 350) y en blanco en (mitad + 1, 351). La
// mitad sale de 0x5bb8d0, con sus rand(). Igual en Lorencia, en Devias y en un castillo.
TEST_F(EventDrawingTest, TheEventTimeDrawsTwiceCenteredOnTheWorld)
{
    struct Case
    {
        std::uint16_t time;
        std::string_view line;
        std::uint32_t color;
    };
    const std::array<Case, 7> cases{{{1, "faltan 0:01", 0xff0affffu},
                                     {59, "faltan 0:59", 0xff0affffu},
                                     {60, "faltan 1:00", 0xff0a0affu},
                                     {70, "faltan 1:10", 0xff0a0affu},
                                     {125, "faltan 2:05", 0xff0a0affu},
                                     {600, "faltan 10:00", 0xff0a0affu},
                                     {659, "faltan 10:59", 0xff0a0affu}}};
    for (const std::int32_t map : {0, 2, 11})
    {
        SCOPED_TRACE(map);
        world->Runtime().map = map;
        for (const Case& probe : cases)
        {
            SCOPED_TRACE(probe.time);
            Restart();
            world->Runtime().events.timeValue = probe.time;
            world->Runtime().events.timeKind = 0;
            CrtRandom expected = random;
            const std::int32_t width = Client::Scenes::GamePanels(*world, expected).Registry().WorldWidth();
            const int half = width / 2;

            GameHudContext context = Context();
            const auto drawn = Client::Scenes::DrawTheEventTime(context);
            ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

            ASSERT_EQ(text.texts.size(), 2u);
            const RecordedText& first = text.texts[0];
            EXPECT_EQ(first.text, probe.line);
            EXPECT_EQ(first.x, Centered(half, probe.line));
            EXPECT_EQ(first.y, 350);
            EXPECT_EQ(first.style.font, FontKind::BigBold);
            EXPECT_EQ(first.style.colors.text, probe.color);
            EXPECT_EQ(first.style.colors.background, 0u);
            const RecordedText& second = text.texts[1];
            EXPECT_EQ(second.text, probe.line);
            EXPECT_EQ(second.x, Centered(half + 1, probe.line));
            EXPECT_EQ(second.y, 351);
            EXPECT_EQ(second.style.font, FontKind::BigBold);
            EXPECT_EQ(second.style.colors.text, WhiteText);
            EXPECT_EQ(random.State(), expected.State());
            // 0x5a7ff4..0x5a8006: la fuente normal y el texto blanco.
            EXPECT_EQ(world->Runtime().hud.dcFont, FontKind::Normal);
            EXPECT_EQ(world->Runtime().hud.textColors.text, WhiteText);
        }
    }
}

// Con el tipo 3 (0x5767814), 1392 con los segundos y siempre en 0xff0affff, aunque falte más de un minuto.
TEST_F(EventDrawingTest, TheStartKindDrawsTheSecondsInTheLastMinuteColor)
{
    world->Runtime().events.timeKind = 3;
    for (const std::uint16_t time : {std::uint16_t{30}, std::uint16_t{125}})
    {
        SCOPED_TRACE(time);
        Restart();
        world->Runtime().events.timeValue = time;
        GameHudContext context = Context();
        const auto drawn = Client::Scenes::DrawTheEventTime(context);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        ASSERT_EQ(text.texts.size(), 2u);
        EXPECT_EQ(text.texts[0].text, time == 30 ? "arranca en 30" : "arranca en 5");
        EXPECT_EQ(text.texts[0].style.colors.text, 0xff0affffu);
        EXPECT_EQ(text.texts[1].style.colors.text, WhiteText);
    }
}

// El sprintf va a 28 bytes de la pila ([ebp-0x1c]): un texto que no entra corta antes de dibujar.
TEST_F(EventDrawingTest, AnEventTimeLongerThanItsStackBufferCuts)
{
    std::map<std::size_t, std::string> longTexts = EventTexts();
    longTexts[1391] = "un tiempo demasiado largo %d:%d";
    texts = Client::Tests::MakeTextTable(longTexts);
    world->Runtime().events.timeValue = 70;
    GameHudContext context = Context();
    const auto drawn = Client::Scenes::DrawTheEventTime(context);
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5a7f9b"), std::string::npos) << drawn.Error();
    EXPECT_TRUE(text.texts.empty());
}

// ---------------------------------------------------------------------------------------------------------------
// 0x40c2d0 y 0x405090: el tiempo y los puntos de Blood Castle y de Chaos Castle.

EventMapObject Castle(EventMapKind kind, std::uint8_t state, std::int32_t remaining, std::int32_t target,
                      std::int32_t kills)
{
    EventMapObject event;
    event.kind = kind;
    event.field10 = state;
    event.field14 = {900, remaining, target, kills};
    return event;
}

// Devil Square (0x40bdb0 es un ret), un castillo sin estado o fuera de sus mapas (Lorencia, Devias, el otro castillo)
// no dibujan ni tocan el lote.
TEST_F(EventDrawingTest, TheCastleTimeDrawsNothingOutsideItsMaps)
{
    struct Case
    {
        EventMapKind kind;
        std::uint8_t state;
        std::int32_t map;
    };
    for (const Case& probe : {Case{EventMapKind::DevilSquare, 1, 11}, Case{EventMapKind::BloodCastle, 0, 11},
                              Case{EventMapKind::BloodCastle, 1, 0}, Case{EventMapKind::BloodCastle, 1, 2},
                              Case{EventMapKind::BloodCastle, 1, 10}, Case{EventMapKind::BloodCastle, 1, 18},
                              Case{EventMapKind::ChaosCastle, 0, 18}, Case{EventMapKind::ChaosCastle, 6, 0},
                              Case{EventMapKind::ChaosCastle, 6, 2}, Case{EventMapKind::ChaosCastle, 6, 17},
                              Case{EventMapKind::ChaosCastle, 6, 24}})
    {
        SCOPED_TRACE(static_cast<int>(probe.kind));
        SCOPED_TRACE(probe.map);
        world->Runtime().map = probe.map;
        sprites.SetColor(Color{0.25f, 0.5f, 0.75f, 1.0f});
        EventMapObject event = Castle(probe.kind, probe.state, 100, 10, 41);
        GameHudContext context = Context();
        const auto drawn = Client::Scenes::DrawTheCastleTimeAndPoints(context, event, true);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_TRUE(text.texts.empty());
        ExpectColor(ColorNow(sprites), Color{0.25f, 0.5f, 0.75f, 1.0f});
        EXPECT_EQ(world->Runtime().hud.textColors.text, WhiteText);
        EXPECT_EQ(world->Runtime().chaosCastleArea, 0xff);
    }
}

// En su mapa, el estado de mezcla, el blanco y los colores (0xff0096ff sobre 0) van antes de mirar el estado: con un
// estado que no es de juego, o sin tiempo, quedan puestos y no dibuja.
TEST_F(EventDrawingTest, TheCastleColorsAreSetBeforeTheStateIsRead)
{
    struct Case
    {
        EventMapKind kind;
        std::uint8_t state;
        std::int32_t remaining;
        std::int32_t map;
    };
    for (const Case& probe : {Case{EventMapKind::BloodCastle, 3, 100, 11}, Case{EventMapKind::BloodCastle, 4, 100, 17},
                              Case{EventMapKind::BloodCastle, 6, 100, 11}, Case{EventMapKind::BloodCastle, 1, 0, 11},
                              Case{EventMapKind::BloodCastle, 1, -1, 11}, Case{EventMapKind::ChaosCastle, 5, 100, 18},
                              Case{EventMapKind::ChaosCastle, 9, 100, 23}, Case{EventMapKind::ChaosCastle, 7, 0, 18}})
    {
        SCOPED_TRACE(static_cast<int>(probe.state));
        Restart();
        world->Runtime().map = probe.map;
        world->Runtime().hud.textColors = {.text = WhiteText, .background = 0x80000000u};
        sprites.SetColor(Color{0.25f, 0.5f, 0.75f, 1.0f});
        sprites.SetBlend(BlendMode::Opaque);
        EventMapObject event = Castle(probe.kind, probe.state, probe.remaining, 10, 41);
        GameHudContext context = Context();
        const auto drawn = Client::Scenes::DrawTheCastleTimeAndPoints(context, event, true);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_TRUE(text.texts.empty());
        EXPECT_EQ(sprites.Blend(), BlendMode::Alpha);
        ExpectColor(ColorNow(sprites), White);
        EXPECT_EQ(world->Runtime().hud.textColors.text, 0xff0096ffu);
        EXPECT_EQ(world->Runtime().hud.textColors.background, 0u);
        EXPECT_EQ(world->Runtime().chaosCastleArea, 0xff);
    }
}

// Blood Castle con +0x10 en 1, 2 o 5 y tiempo: los puntos (864, o 866 con el 5) centrados en (570, 345) con la
// fuente 0x5763558, 865 en (570, 357) y el tiempo de 0x40bbf0 en (570, 367) con la grande. Con 125 s quedan 2 minutos:
// " 02 : 05" en 0xff2020ff y las centésimas del reloj del mundo (1234 ms → 34).
TEST_F(EventDrawingTest, BloodCastleDrawsThePointsTheTitleAndTheTime)
{
    worldClock = 1234.0f;
    for (const std::int32_t map : {11, 14, 17})
    {
        for (const std::uint8_t state : {std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{5}})
        {
            SCOPED_TRACE(map);
            SCOPED_TRACE(static_cast<int>(state));
            Restart();
            world->Runtime().map = map;
            EventMapObject event = Castle(EventMapKind::BloodCastle, state, 125, 40, 12);
            GameHudContext context = Context();
            const auto drawn = Client::Scenes::DrawTheCastleTimeAndPoints(context, event, true);
            ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

            ASSERT_EQ(text.texts.size(), 3u);
            const std::string points = state == 5 ? "pelota ( 12/40 )" : "bichos ( 12/40 )";
            EXPECT_EQ(text.texts[0].text, points);
            EXPECT_EQ(text.texts[0].x, Centered(570, points));
            EXPECT_EQ(text.texts[0].y, 345);
            EXPECT_EQ(text.texts[0].style.font, FontKind::Bold);
            EXPECT_EQ(text.texts[0].style.colors.text, 0xff0096ffu);
            EXPECT_EQ(text.texts[1].text, "queda");
            EXPECT_EQ(text.texts[1].x, Centered(570, "queda"));
            EXPECT_EQ(text.texts[1].y, 357);
            EXPECT_EQ(text.texts[1].style.font, FontKind::Bold);
            EXPECT_EQ(text.texts[1].style.colors.text, 0xff0096ffu);
            EXPECT_EQ(text.texts[2].text, " 02 : 05: 34");
            EXPECT_EQ(text.texts[2].x, Centered(570, " 02 : 05: 34"));
            EXPECT_EQ(text.texts[2].y, 367);
            EXPECT_EQ(text.texts[2].style.font, FontKind::BigBold);
            EXPECT_EQ(text.texts[2].style.colors.text, 0xff2020ffu);
            for (const Color& color : MarkerColors(sprites))
                ExpectColor(color, White);
            // Blood Castle no escribe el área de la trampa.
            EXPECT_EQ(world->Runtime().chaosCastleArea, 0xff);
        }
    }
}

// Sin meta (+0x1c en 0xffff), sin puntos: 865 en 345 y el tiempo en 355. De 5 a 14 minutos, sin rojo y con las
// centésimas; de 15 en adelante, sin ellas.
TEST_F(EventDrawingTest, TheCastleTimeWithoutATargetAndItsMinutes)
{
    worldClock = 59999.0f;
    struct Case
    {
        std::int32_t remaining;
        std::string_view line;
        std::uint32_t color;
    };
    for (const Case& probe : {Case{299, " 04 : 59: 59", 0xff2020ffu}, Case{300, " 05 : 00: 59", 0xff0096ffu},
                              Case{899, " 14 : 59: 59", 0xff0096ffu}, Case{900, " 15 : 00", 0xff0096ffu},
                              Case{3725, " 62 : 05", 0xff0096ffu}})
    {
        SCOPED_TRACE(probe.remaining);
        Restart();
        world->Runtime().map = 13;
        EventMapObject event = Castle(EventMapKind::BloodCastle, 1, probe.remaining, 0xffff, 3);
        GameHudContext context = Context();
        const auto drawn = Client::Scenes::DrawTheCastleTimeAndPoints(context, event, true);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        ASSERT_EQ(text.texts.size(), 2u);
        EXPECT_EQ(text.texts[0].text, "queda");
        EXPECT_EQ(text.texts[0].y, 345);
        EXPECT_EQ(text.texts[1].text, probe.line);
        EXPECT_EQ(text.texts[1].y, 355);
        EXPECT_EQ(text.texts[1].style.colors.text, probe.color);
    }
}

// Chaos Castle, de 6 a 8 en sus mapas: 1161 con los muertos y la meta, y después el área de la trampa (0x6c634c) por
// +0x20: (40, 46] → 0, (30, 36] → 3, (20, 26] → 6; fuera de esos tramos queda como estaba.
TEST_F(EventDrawingTest, ChaosCastleDrawsItsPointsAndPicksTheTrapArea)
{
    struct Case
    {
        std::int32_t kills;
        std::uint8_t area;
    };
    for (const std::int32_t map : {18, 20, 23})
    {
        for (const Case& probe : {Case{47, 0xff}, Case{46, 0}, Case{41, 0}, Case{40, 0xff}, Case{36, 3}, Case{31, 3},
                                  Case{30, 0xff}, Case{26, 6}, Case{21, 6}, Case{20, 0xff}, Case{0, 0xff}})
        {
            SCOPED_TRACE(map);
            SCOPED_TRACE(probe.kills);
            Restart();
            world->Runtime().map = map;
            world->Runtime().chaosCastleArea = 0xff;
            EventMapObject event = Castle(EventMapKind::ChaosCastle, 7, 1000, 70, probe.kills);
            GameHudContext context = Context();
            const auto drawn = Client::Scenes::DrawTheCastleTimeAndPoints(context, event, true);
            ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
            ASSERT_EQ(text.texts.size(), 3u);
            EXPECT_EQ(text.texts[0].text, "rivales ( " + std::to_string(probe.kills) + "/70 )");
            EXPECT_EQ(text.texts[0].y, 345);
            EXPECT_EQ(text.texts[1].text, "queda");
            EXPECT_EQ(text.texts[2].text, " 16 : 40");
            EXPECT_EQ(text.texts[2].y, 367);
            EXPECT_EQ(world->Runtime().chaosCastleArea, probe.area);
        }
    }
}

// Un dibujo intermedio de MuRet (projecting) dibuja igual pero no escribe el área.
TEST_F(EventDrawingTest, AnIntermediateChaosCastleDrawDoesNotWriteTheArea)
{
    world->Runtime().map = 18;
    EventMapObject event = Castle(EventMapKind::ChaosCastle, 6, 1000, 70, 45);
    GameHudContext context = Context(true);
    const auto drawn = Client::Scenes::DrawTheCastleTimeAndPoints(context, event, false);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(text.texts.size(), 3u);
    EXPECT_EQ(world->Runtime().chaosCastleArea, 0xff);
}

// 0x40bc96: el _ftol del reloj del mundo fuera del perfil de MuRet (un infinito) corta, con menos de 15 minutos; uno
// grande da su dword bajo.
TEST_F(EventDrawingTest, ACastleClockOutsideTheFtolProfileCuts)
{
    worldClock = std::numeric_limits<float>::infinity();
    world->Runtime().map = 11;
    EventMapObject event = Castle(EventMapKind::BloodCastle, 1, 100, 0xffff, 0);
    GameHudContext context = Context();
    const auto drawn = Client::Scenes::DrawTheCastleTimeAndPoints(context, event, true);
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x40bc96"), std::string::npos) << drawn.Error();
}

// ---------------------------------------------------------------------------------------------------------------
// 0x632a2b → el método +0x10: los textos de las cajas de los resultados.

void PutRecord(EventMapObject& event, std::size_t record, std::string_view name, std::int32_t points,
               std::int32_t experience, std::int32_t zen)
{
    auto* bytes = reinterpret_cast<unsigned char*>(event.field2c.data());
    for (std::size_t at = 0; at < name.size(); ++at)
        bytes[record * 24 + at] = static_cast<unsigned char>(name[at]);
    event.field2c[(record * 24 + 0xc) / 4] = points;
    event.field2c[(record * 24 + 0x10) / 4] = experience;
    event.field2c[(record * 24 + 0x14) / 4] = zen;
}

void NameTheHero(WorldState& world, std::string_view name)
{
    Client::Scenes::ActorState& hero = world.Actors()[world.Runtime().hero];
    hero.name = {};
    for (std::size_t at = 0; at < name.size(); ++at)
        hero.name[at] = static_cast<std::byte>(name[at]);
}

// Devil Square (0x40bdd0): 647 y 648 con el nombre del héroe, los títulos 680, 682 (diez más a la derecha), 683 y 684
// (sin el 681), y las filas: el registro (fila + 1) % cuenta, y en la última el propio (el 0) bajo 685, con el puesto
// de +0x28. Los nombres llegan hasta 10 bytes.
TEST_F(EventDrawingTest, TheDevilSquareRankingDrawsTheTitlesAndTheRows)
{
    NameTheHero(*world, "Heroe");
    EventMapObject event;
    event.kind = EventMapKind::DevilSquare;
    event.field24 = 3;
    event.field28 = 17;
    PutRecord(event, 0, "Heroe", 500, 60, 7);
    PutRecord(event, 1, "Primerito", 900, 80, 9);
    PutRecord(event, 2, "Segundon01", 700, 70, 8);
    GameHudContext context = Context();
    const auto drawn = Client::Scenes::DrawTheEventBoxText(context, event);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

    struct Expected
    {
        int x;
        int y;
        std::string_view text;
        std::uint32_t color;
    };
    // Los colores del texto de los títulos son los que había (blanco); las filas van con los suyos.
    const std::vector<Expected> expected{
        {0xe5, 0x46, "bien hecho", WhiteText},
        {0xe5, 0x56, "Heroe aguanto la prueba", WhiteText},
        {0xdb, 0x6e, "puesto", WhiteText},
        {0x11f + 10, 0x6e, "puntos", WhiteText},
        {0x159, 0x6e, "exp", WhiteText},
        {0x17f, 0x6e, "premio", WhiteText},
        {0xdb, 0x7e, " 1", WhiteText},
        {0xeb, 0x7e, "Primerito", WhiteText},
        {0x11f, 0x7e, "       900", 0xffffd2d2u},
        {0x159, 0x7e, "    80", 0xffd2ffd2u},
        {0x17f, 0x7e, "     9", 0xffd2d2ffu},
        {0xdb, 0x8e, " 2", WhiteText},
        {0xeb, 0x8e, "Segundon01", WhiteText},
        {0x11f, 0x8e, "       700", 0xffffd2d2u},
        {0x159, 0x8e, "    70", 0xffd2ffd2u},
        {0x17f, 0x8e, "     8", 0xffd2d2ffu},
        {0xdb, 0x11e, "lo mio", 0xffd2d2ffu},
        {0xdb, 0x12e, "17", WhiteText},
        {0xeb, 0x12e, "Heroe", WhiteText},
        {0x11f, 0x12e, "       500", 0xffffd2d2u},
        {0x159, 0x12e, "    60", 0xffd2ffd2u},
        {0x17f, 0x12e, "     7", 0xffd2d2ffu},
    };
    ASSERT_EQ(text.texts.size(), expected.size());
    for (std::size_t at = 0; at < expected.size(); ++at)
    {
        SCOPED_TRACE(at);
        EXPECT_EQ(text.texts[at].x, expected[at].x);
        EXPECT_EQ(text.texts[at].y, expected[at].y);
        EXPECT_EQ(text.texts[at].text, expected[at].text);
        EXPECT_EQ(text.texts[at].style.colors.text, expected[at].color);
    }
    // glColor3f: (0,5, 1, 0,5) los títulos, (0,5, 0,5, 1) las columnas, blanco las filas y (0,9, 0,9, 0) la propia.
    const std::vector<Color> colors = MarkerColors(sprites);
    ASSERT_EQ(colors.size(), expected.size());
    const Color title{0.5f, 1.0f, 0.5f, 1.0f};
    const Color header{0.5f, 0.5f, 1.0f, 1.0f};
    const Color own{0.9f, 0.9f, 0.0f, 1.0f};
    for (std::size_t at = 0; at < colors.size(); ++at)
    {
        SCOPED_TRACE(at);
        ExpectColor(colors[at], at < 2 ? title : at < 6 ? header : at < 16 ? White : own);
    }
}

// Con más de 11 registros, (fila + 1) % cuenta lee fuera de los 0x108 bytes de +0x2c: corta.
TEST_F(EventDrawingTest, ADevilSquareRankingPastItsRecordsCuts)
{
    EventMapObject event;
    event.kind = EventMapKind::DevilSquare;
    event.field24 = 12;
    GameHudContext context = Context();
    const auto drawn = Client::Scenes::DrawTheEventBoxText(context, event);
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x40bf1b"), std::string::npos) << drawn.Error();
}

// Blood Castle (0x40c440): 857 y 858 si ganó (+0x24), 859 y 860 si no, centrados en 0x140; después, con la fuente
// 0x5763558, la experiencia (+0x3c), el zen (+0x40) solo si ganó y los puntos (+0x38).
TEST_F(EventDrawingTest, TheBloodCastleResultDrawsWonAndLost)
{
    for (const bool won : {true, false})
    {
        SCOPED_TRACE(won);
        Restart();
        EventMapObject event;
        event.kind = EventMapKind::BloodCastle;
        event.field24 = won ? 1 : 0;
        event.field2c[(0x38 - 0x2c) / 4] = 321;
        event.field2c[(0x3c - 0x2c) / 4] = 4500;
        event.field2c[(0x40 - 0x2c) / 4] = 20000;
        GameHudContext context = Context();
        const auto drawn = Client::Scenes::DrawTheEventBoxText(context, event);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

        struct Expected
        {
            int y;
            std::string_view text;
            FontKind font;
            std::uint32_t color;
        };
        std::vector<Expected> expected{
            {0x46, won ? "ganaste" : "perdiste", FontKind::Normal, WhiteText},
            {0x56, won ? "mision cumplida" : "mision sin cumplir", FontKind::Normal, WhiteText},
            {0x6a, "exp 4500", FontKind::Bold, 0xffd2ffd2u}};
        if (won)
            expected.push_back({0x82, "zen 20000", FontKind::Bold, 0xffd2d2ffu});
        expected.push_back({won ? 0x9a : 0x82, "puntos 321", FontKind::Bold, 0xffffd2d2u});
        ASSERT_EQ(text.texts.size(), expected.size());
        const std::vector<Color> colors = MarkerColors(sprites);
        ASSERT_EQ(colors.size(), expected.size());
        for (std::size_t at = 0; at < expected.size(); ++at)
        {
            SCOPED_TRACE(at);
            EXPECT_EQ(text.texts[at].x, Centered(0x140, expected[at].text));
            EXPECT_EQ(text.texts[at].y, expected[at].y);
            EXPECT_EQ(text.texts[at].text, expected[at].text);
            EXPECT_EQ(text.texts[at].style.font, expected[at].font);
            EXPECT_EQ(text.texts[at].style.colors.text, expected[at].color);
            ExpectColor(colors[at], at < 2 ? Color{0.5f, 1.0f, 0.5f, 1.0f} : White);
        }
    }
}

// Chaos Castle (0x405220): 1151 y "%s %s" con 1152 y 858 si ganó; "%s %s" con 1152 y 860 y 1153 si no; después la
// experiencia (+0x3c), los monstruos (+0x38) y los jugadores (+0x40).
TEST_F(EventDrawingTest, TheChaosCastleResultDrawsWonAndLost)
{
    for (const bool won : {true, false})
    {
        SCOPED_TRACE(won);
        Restart();
        EventMapObject event;
        event.kind = EventMapKind::ChaosCastle;
        event.field24 = won ? 1 : 0;
        event.field2c[(0x38 - 0x2c) / 4] = 33;
        event.field2c[(0x3c - 0x2c) / 4] = 9000;
        event.field2c[(0x40 - 0x2c) / 4] = 4;
        GameHudContext context = Context();
        const auto drawn = Client::Scenes::DrawTheEventBoxText(context, event);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

        const std::vector<std::string_view> lines =
            won ? std::vector<std::string_view>{"el guardia descansa", "la mision mision cumplida", "exp 9000",
                                                "monstruos 33", "jugadores 4"}
                : std::vector<std::string_view>{"la mision mision sin cumplir", "otra vez sera", "exp 9000",
                                                "monstruos 33", "jugadores 4"};
        const std::array<int, 5> rows{0x50, 0x60, 0x7e, 0x92, 0xa6};
        ASSERT_EQ(text.texts.size(), lines.size());
        for (std::size_t at = 0; at < lines.size(); ++at)
        {
            SCOPED_TRACE(at);
            EXPECT_EQ(text.texts[at].text, lines[at]);
            EXPECT_EQ(text.texts[at].x, Centered(0x140, lines[at]));
            EXPECT_EQ(text.texts[at].y, rows[at]);
            EXPECT_EQ(text.texts[at].style.font, at < 2 ? FontKind::Normal : FontKind::Bold);
            EXPECT_EQ(text.texts[at].style.colors.text, at < 2 ? WhiteText : 0xffd2ffd2u);
        }
    }
}
} // namespace
