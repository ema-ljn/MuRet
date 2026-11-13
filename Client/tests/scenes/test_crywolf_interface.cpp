#include "Scenes/CrywolfInterfaceDrawing.h"

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
#include "Scenes/GameHud.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Widget.h"

#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// CL9 hito 11, fase 3 (06 § La interfaz de Crywolf): 0x4365c0 y 0x430180 contra el ASM de c32bbd5f, sin traza de GL
// (*estático*; lo que la VM tiene que confirmar está en 08, P-C298). GS 0.97 no tiene Crywolf. Text.bmd es inventado.
namespace
{
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::Models::CrtRandom;
using Client::Render::Text::FontKind;
using Client::Scenes::CrywolfInterface;
using Client::Scenes::GameHudContext;
using Client::Scenes::WorldState;
using Client::Tests::RecordedText;
using Client::Tests::RecordingTextDrawer;

constexpr Client::App::PixelSize Screen640{640, 480};
constexpr std::int32_t CrywolfMap = 34;
constexpr std::uint16_t DigitsSlot = 1;

std::map<std::size_t, std::string> CrywolfTexts()
{
    std::map<std::size_t, std::string> texts{
        {20, "mago"},       {21, "guerrero"},    {22, "arquera"},   {23, "gladiador"}, {24, "senor"},
        {25, "maestro"},    {26, "espadachin"},  {27, "musa"},      {680, "puesto"},   {681, "quien"},
        {1948, "lobos %d"}, {1949, "balgass"},   {1973, "clase"},   {1977, "puntos"},  {2000, "se defendio"},
        {2001, "el altar"}, {2009, "se perdio"}, {2010, "la aldea"}};
    for (std::size_t tip = 1953; tip <= 1972; ++tip)
        texts.emplace(tip, "consejo " + std::to_string(tip));
    return texts;
}

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

std::vector<std::uint16_t> SlotsOf(const std::vector<DrawnQuad>& quads)
{
    std::vector<std::uint16_t> slots;
    for (const DrawnQuad& quad : quads)
        slots.push_back(quad.slot.value_or(0));
    return slots;
}

// La esquina de arriba a la izquierda y el tamaño de un quad en 640 × 480, con la y hacia abajo.
void ExpectQuad(const DrawnQuad& quad, std::uint16_t slot, float x, float y, float width, float height)
{
    EXPECT_EQ(quad.slot, slot);
    EXPECT_FLOAT_EQ(quad.first.x, x);
    EXPECT_FLOAT_EQ(quad.first.y, 480.0f - y);
    // Las esquinas sí son exactas; la resta de dos floats cerca de 300 pierde los últimos bits.
    EXPECT_NEAR(quad.third.x - quad.first.x, width, 1e-4f);
    EXPECT_NEAR(quad.first.y - quad.third.y, height, 1e-4f);
}

class CrywolfInterfaceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        sprites.Begin(Screen640);
    }

    GameHudContext Context(CrywolfInterface* copy = nullptr)
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
                              .crywolfInterface = copy,
                              .sound = sound,
                              .sprites = sprites,
                              .text = text,
                              .texts = &texts,
                              .tickCount = tickCount,
                              .mouse = mouse};
    }

    // Un frame: el lote vacío, los textos borrados y 0x4365c0.
    void Frame(CrywolfInterface* copy = nullptr)
    {
        text.texts.clear();
        sprites.Begin(Screen640);
        GameHudContext context = Context(copy);
        const auto drawn = Client::Scenes::DrawTheCrywolfInterface(context);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    }

    // La mezcla que queda: la de un rectángulo sin textura dibujado ahora.
    BlendMode BlendNow()
    {
        sprites.ColorQuad(0.0f, 0.0f, 1.0f, 1.0f);
        return sprites.Commands().back().blend;
    }

    static int Centered(int x, std::string_view line)
    {
        return Client::Render::Text::CenteredAt(
            x, {static_cast<int>(line.size()) * RecordingTextDrawer::LetterWidth, RecordingTextDrawer::Height},
            Screen640);
    }

    CrywolfInterface& Ui()
    {
        return world->Runtime().crywolf.ui;
    }

    void Enter(std::int32_t map, std::uint8_t stage)
    {
        world->Runtime() = Client::Scenes::WorldRuntime{};
        world->Runtime().map = map;
        world->Runtime().crywolf.stage = stage;
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    CrtRandom random{0x2468ace0u};
    std::optional<std::int32_t> sound;
    SpriteBatch sprites;
    RecordingTextDrawer text{sprites};
    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable(CrywolfTexts());
    std::uint32_t tickCount = 50000;
    Client::UI::Point mouse{};
};

// Sin resultados, sin aviso y fuera de la defensa en el 34: solo 0x620a00(1), que queda (no llega a 0x620980). La
// etapa 3 o 4 en Lorencia o en Devias tampoco dibuja el estado (0x4303c0).
TEST_F(CrywolfInterfaceTest, OutsideItsStatesItOnlyLeavesTheNormalBlend)
{
    for (const std::int32_t map : {0, 2, CrywolfMap})
    {
        for (const std::uint8_t stage : {std::uint8_t{0}, std::uint8_t{3}, std::uint8_t{4}, std::uint8_t{5}})
        {
            if (map == CrywolfMap && (stage == 3 || stage == 4))
                continue;
            SCOPED_TRACE(map);
            SCOPED_TRACE(static_cast<int>(stage));
            Enter(map, stage);
            sprites.SetBlend(BlendMode::Opaque);
            Frame();
            EXPECT_TRUE(QuadsOf(sprites).empty());
            EXPECT_TRUE(text.texts.empty());
            EXPECT_EQ(BlendNow(), BlendMode::Alpha);
            EXPECT_EQ(Ui().byte723bf8, 0);
        }
    }
}

// La etapa 4 en el 34: el marco, el adorno en 98, los cinco altares de la .data (2: 0x6be), 0x6bc sin 0x723bd8, la
// barra de abajo, el 1948 en (57, 85) en 0xff1594ff con glColor (1; 0,6; 0,3), el reloj en blanco y la luz; termina
// sin mezcla. Sin consejos (son de la etapa 3).
TEST_F(CrywolfInterfaceTest, TheWarningStageDrawsTheStateInCrywolf)
{
    Enter(CrywolfMap, 4);
    Ui().light = 100;
    Frame();
    const std::vector<DrawnQuad> quads = QuadsOf(sprites);
    ASSERT_GE(quads.size(), 11u);
    ExpectQuad(quads[0], 0x6b9, 0.0f, 1.0f, 122.0f, 98.0f);
    ExpectQuad(quads[1], 0x6b8, 4.0f, 98.0f, 20.0f, 9.0f);
    const std::array<std::array<float, 2>, 5> altars{{{4, 29}, {16, 17}, {30, 9}, {46, 5}, {62, 4}}};
    for (std::size_t altar = 0; altar < altars.size(); ++altar)
        ExpectQuad(quads[2 + altar], 0x6be, altars[altar][0], altars[altar][1], 13.0f, 13.0f);
    ExpectQuad(quads[7], 0x6bc, 4.0f, 81.0f, 15.0f, 15.0f);
    ExpectQuad(quads[8], 0x6c2, 240.0f, 396.0f, 156.0f, 36.0f);
    // Los minutos y los segundos en 0: una cifra cada uno, en blanco.
    EXPECT_EQ(quads[9].slot, DigitsSlot);
    EXPECT_EQ(quads[10].slot, DigitsSlot);
    EXPECT_FLOAT_EQ(quads[9].color.green, 1.0f);
    ASSERT_EQ(quads.size(), 12u);
    ExpectQuad(quads[11], 0x6bf, 4.0f, 46.0f, static_cast<float>(100.0 * static_cast<double>(0.89f)), 30.0f);
    EXPECT_FLOAT_EQ(quads[11].third.u - quads[11].first.u,
                    static_cast<float>(100.0 * static_cast<double>(0.88f) * 0.0078125));
    ASSERT_EQ(text.texts.size(), 1u);
    EXPECT_EQ(text.texts[0].text, "lobos 0");
    EXPECT_EQ(text.texts[0].x, Centered(0x39, "lobos 0"));
    EXPECT_EQ(text.texts[0].y, 0x55);
    EXPECT_EQ(text.texts[0].style.font, FontKind::Normal);
    EXPECT_EQ(text.texts[0].style.colors.text, 0xff1594ffu);
    EXPECT_EQ(BlendNow(), BlendMode::Opaque);
    EXPECT_EQ(Ui().byte723bf8, 1);
}

// Los altares por su byte: 0x1? en el nibble alto da 0x6cd, 0x6ce o 0x6cf por el bajo; si no, 0x6bd con 1, 0x6be con
// 2 y nada con otro. Con 0x723bd8, el ícono 0x6bb.
TEST_F(CrywolfInterfaceTest, TheAltarsPickTheirSlotByTheirByte)
{
    Enter(CrywolfMap, 3);
    Ui().altars = {0x11, 0x12, 0x15, 0x01, 0x23};
    Ui().dword723bd8 = 2;
    Frame();
    const std::vector<std::uint16_t> slots = SlotsOf(QuadsOf(sprites));
    ASSERT_GE(slots.size(), 8u);
    EXPECT_EQ(std::vector<std::uint16_t>(slots.begin() + 2, slots.begin() + 7),
              (std::vector<std::uint16_t>{0x6cd, 0x6ce, 0x6cf, 0x6bd, 0x6bb}));
}

// La barra de Balgass con 0x723a3d en 1: el adorno baja de a 1 (98 + 0x723a40) hasta 21; ya abajo, el fondo 0x6b6, el
// ícono 0x6ba, el 1949 en (38, 101) y la barra 0x6b7 de 0x6c73d4 · 0,68f. Con 0x723a3d en 0 vuelve de a 1. El reloj
// va en (1; 0,3; 0,3) con la barra.
TEST_F(CrywolfInterfaceTest, TheBossBarSlidesDownAndBack)
{
    Enter(CrywolfMap, 4);
    Ui().byte723a3d = 1;
    Ui().dword6c73d4 = 80;
    for (int frame = 0; frame < 21; ++frame)
    {
        Frame();
        const std::vector<DrawnQuad> quads = QuadsOf(sprites);
        EXPECT_FLOAT_EQ(quads[1].first.y, 480.0f - (98.0f + static_cast<float>(frame)));
        EXPECT_FLOAT_EQ(Ui().float723a40, static_cast<float>(frame + 1));
    }
    Frame();
    std::vector<DrawnQuad> quads = QuadsOf(sprites);
    EXPECT_FLOAT_EQ(Ui().float723a40, 21.0f);
    ASSERT_GE(quads.size(), 12u);
    ExpectQuad(quads[9], 0x6b6, 0.0f, 98.0f, 97.0f, 21.0f);
    ExpectQuad(quads[10], 0x6ba, 4.0f, 101.0f, 15.0f, 15.0f);
    ExpectQuad(quads[11], 0x6b7, 25.0f, 109.0f, static_cast<float>(80.0 * static_cast<double>(0.68f)), 8.0f);
    EXPECT_FLOAT_EQ(quads[11].third.u - quads[11].first.u,
                    static_cast<float>(80.0 * static_cast<double>(0.67f) * 0.0078125));
    ASSERT_EQ(text.texts.size(), 2u);
    EXPECT_EQ(text.texts[1].text, "balgass");
    EXPECT_EQ(text.texts[1].x, Centered(0x26, "balgass"));
    EXPECT_EQ(text.texts[1].y, 0x65);
    EXPECT_EQ(quads[12].slot, DigitsSlot);
    EXPECT_FLOAT_EQ(quads[12].color.green, 0.3f);
    Ui().byte723a3d = 0;
    Frame();
    EXPECT_FLOAT_EQ(Ui().float723a40, 20.0f);
}

// El reloj desde el GetTickCount del BD 04: los minutos con la división sin signo por 60.000 y los segundos que van,
// uno por cada segundo nuevo de _ftol(ms · 1,6666667e-5f · 60f), de 0 a 59; un minuto nuevo los vuelve a 0. Con los
// minutos pasados, + 60.
TEST_F(CrywolfInterfaceTest, TheClockCountsTheMinutesLeftAndTheSecondsGone)
{
    Enter(CrywolfMap, 4);
    Ui().dwords723bdc[3] = 10;
    Ui().dword7239f4 = 1000;
    tickCount = 1000 + 61500;
    Frame();
    EXPECT_EQ(Ui().dword723c00, 61);
    EXPECT_EQ(Ui().dword723bf4, 9);
    EXPECT_EQ(Ui().dword723bfc, 0);
    tickCount += 1000;
    Frame();
    EXPECT_EQ(Ui().dword723c00, 62);
    EXPECT_EQ(Ui().dword723bfc, 1);
    // 60 − 1 = 59: dos cifras después de la del 9.
    const std::vector<DrawnQuad> quads = QuadsOf(sprites);
    EXPECT_EQ(quads[9].slot, DigitsSlot);
    EXPECT_EQ(quads[10].slot, DigitsSlot);
    EXPECT_EQ(quads[11].slot, DigitsSlot);
    EXPECT_EQ(quads[12].slot, 0x6bf);
    tickCount += 100;
    Frame();
    EXPECT_EQ(Ui().dword723bfc, 1);
    // 11 minutos pasados de 10: −1 + 60.
    tickCount = 1000 + 11 * 60000;
    Frame();
    EXPECT_EQ(Ui().dword723bf4, 59);
    EXPECT_EQ(Ui().dword723bfc, 0);
    // Sin minutos, 0.
    Ui().dwords723bdc[3] = 0;
    Frame();
    EXPECT_EQ(Ui().dword723bf4, 0);
}

// 0x430180 con la etapa 3: la fuente 0x5763558, los cuatro renglones de 1957 + 4 · consejo en (0xbe, 0x3f + 13 · i),
// el primero en 0xffffc864 y los otros en 0xffff9664 sobre 0xaa000000. 0x63b0e3(10000) vence en la primera llamada
// (el reloj en 0) y vuelve a vencer 10.000 ms después de la llamada que sigue; el consejo pasa de −1 a 0 y da la vuelta
// en 4.
TEST_F(CrywolfInterfaceTest, TheDefenseStageRotatesTheTips)
{
    Enter(CrywolfMap, 3);
    tickCount = 20000;
    Frame();
    EXPECT_EQ(Ui().dwords6c73e0[0], 0);
    ASSERT_EQ(text.texts.size(), 5u);
    for (std::size_t row = 0; row < 4; ++row)
    {
        const RecordedText& line = text.texts[1 + row];
        EXPECT_EQ(line.text, "consejo " + std::to_string(1957 + row));
        EXPECT_EQ(line.x, 0xbe);
        EXPECT_EQ(line.y, 0x3f + 13 * static_cast<int>(row));
        EXPECT_EQ(line.style.font, FontKind::Bold);
        EXPECT_EQ(line.style.colors.text, row == 0 ? 0xffffc864u : 0xffff9664u);
        EXPECT_EQ(line.style.colors.background, 0xaa000000u);
    }
    tickCount = 25000;
    Frame();
    EXPECT_EQ(Ui().tipTimerStart, 25000u);
    EXPECT_EQ(Ui().dwords6c73e0[0], 0);
    tickCount = 35000;
    Frame();
    EXPECT_EQ(Ui().dwords6c73e0[0], 1);
    EXPECT_EQ(text.texts[1].text, "consejo 1961");
    for (const std::uint32_t at : {35001u, 45001u, 45002u, 55002u, 55003u, 65003u})
    {
        tickCount = at;
        Frame();
    }
    EXPECT_EQ(Ui().dwords6c73e0[0], 0);
}

// El aviso en 1: la cuenta sube cada frame y el cartel 0x6c0 entra desde 15 · cuenta − 329 con el alfa de la ola
// ((2 · cuenta) % 140); el rango (0x6d1) llega de a 15 desde 0x186 + 250 y, en 0, la letra 0x6d2 + 0x723bed, el rótulo
// y las nueve cifras de la experiencia (0x6d7 + cifra, desde 409 de a 15 a la izquierda).
TEST_F(CrywolfInterfaceTest, TheNoticeBannerEntersAndTheRankArrives)
{
    for (const std::int32_t map : {0, 2, CrywolfMap})
    {
        SCOPED_TRACE(map);
        Enter(map, 5);
        Ui().message = 1;
        Ui().byte723bed = 2;
        Ui().dword723bf0 = 12345;
        Frame();
        std::vector<DrawnQuad> quads = QuadsOf(sprites);
        ASSERT_EQ(quads.size(), 3u);
        ExpectQuad(quads[0], 0x6c0, 30.0f - 329.0f, 50.0f, 329.0f, 94.0f);
        EXPECT_FLOAT_EQ(quads[0].color.alpha,
                        static_cast<float>(4.0 * static_cast<double>(0.01f) + static_cast<double>(0.3f)));
        ExpectQuad(quads[1], 0x6d0, 230.0f, 150.0f, 196.0f, 141.0f);
        ExpectQuad(quads[2], 0x6d1, static_cast<float>(0x186 - 15 + 250), 188.0f, 110.0f, 28.0f);
        EXPECT_EQ(Ui().dword6c73d8, 2);
        // Hasta la cuenta 40: 2 · 40 % 140 = 80, el alfa baja.
        for (int frame = 0; frame < 38; ++frame)
            Frame();
        quads = QuadsOf(sprites);
        EXPECT_EQ(Ui().dword6c73d8, 40);
        ExpectQuad(quads[0], 0x6c0, 150.0f, 50.0f, 329.0f, 94.0f);
        EXPECT_FLOAT_EQ(quads[0].color.alpha, static_cast<float>(1.0 - 10.0 * static_cast<double>(0.01f)));
        ASSERT_EQ(quads.size(), 3u + 1u + 1u + 9u);
        EXPECT_EQ(Ui().dword6c73f8, 0);
        ExpectQuad(quads[3], 0x6d4, 370.0f, 188.0f, 29.0f, 28.0f);
        ExpectQuad(quads[4], 0x6e1, 229.0f, 235.0f, 60.0f, 19.0f);
        const std::array<std::uint16_t, 9> digits{5, 4, 3, 2, 1, 0, 0, 0, 0};
        for (std::size_t digit = 0; digit < digits.size(); ++digit)
            ExpectQuad(quads[5 + digit], static_cast<std::uint16_t>(0x6d7 + digits[digit]),
                       409.0f - 15.0f * static_cast<float>(digit), 235.0f, 15.0f, 19.0f);
    }
}

// En 400 el aviso pasa a 0 y la cuenta a 0: el cartel sale desde 15 · cuenta + 150 y, pasado 479, el aviso queda en
// −1, la cuenta en 0 y el rango vuelve a 0x186.
TEST_F(CrywolfInterfaceTest, TheNoticeBannerLeavesAfterFourHundredFrames)
{
    Enter(CrywolfMap, 5);
    Ui().message = 1;
    Ui().dword6c73d8 = 399;
    Frame();
    EXPECT_EQ(Ui().message, 0);
    EXPECT_EQ(Ui().dword6c73d8, 1);
    ExpectQuad(QuadsOf(sprites)[0], 0x6c0, 150.0f, 50.0f, 329.0f, 94.0f);
    Frame();
    ExpectQuad(QuadsOf(sprites)[0], 0x6c0, 165.0f, 50.0f, 329.0f, 94.0f);
    Ui().dword6c73d8 = 31;
    Frame();
    EXPECT_EQ(Ui().message, -1);
    EXPECT_EQ(Ui().dword6c73d8, 0);
    EXPECT_EQ(Ui().dword6c73f8, 0x186);
    Frame();
    EXPECT_TRUE(QuadsOf(sprites).empty());
}

// Los resultados con 0x723bf9: el marco 0x6e2, los títulos en 0xff1594ff centrados en la y 0xa0, las filas en blanco
// desde 0xaf de a 15 (la de −1 no se dibuja pero cuenta), la clase por sus bits (con 5 o más, el nombre otra vez) y el
// texto del aviso en 0xffff00ff; el botón 0x6c9 sin el mouse.
TEST_F(CrywolfInterfaceTest, TheResultsListTheFivePlaces)
{
    for (const std::int32_t map : {0, 2, CrywolfMap})
    {
        SCOPED_TRACE(map);
        Enter(map, 0);
        CrywolfInterface& ui = Ui();
        ui.byte723bf9 = 1;
        ui.dwords6c73e0 = {-1, 500, -1, 300, 200, 100};
        const std::array<std::string, 5> names{"LoboGris01", "", "Aullido", "Luna", "Colmillo"};
        for (std::size_t place = 0; place < names.size(); ++place)
            std::copy(names[place].begin(), names[place].end(), ui.rankingNames[place].begin());
        ui.rankingClasses = {0x08, 0, 3, 0x0b, 5};
        Frame();
        const std::vector<DrawnQuad> quads = QuadsOf(sprites);
        ASSERT_EQ(quads.size(), 2u);
        ExpectQuad(quads[0], 0x6e2, 200.0f, 110.0f, 252.0f, 240.0f);
        ExpectQuad(quads[1], 0x6c9, 300.0f, 300.0f, 54.0f, 30.0f);
        struct Line
        {
            int x;
            int y;
            std::string text;
            std::uint32_t color;
        };
        const std::vector<Line> expected{
            {0xf0, 0xa0, "puesto", 0xff1594ffu},     {0x11d, 0xa0, "quien", 0xff1594ffu},
            {0x14d, 0xa0, "clase", 0xff1594ffu},     {0x183, 0xa0, "puntos", 0xff1594ffu},
            {0xf0, 0xaf, "1", 0xffffffffu},          {0x11d, 0xaf, "LoboGris01", 0xffffffffu},
            {0x14f, 0xaf, "maestro", 0xffffffffu},   {0x181, 0xaf, "500", 0xffffffffu},
            {0xf0, 0xcd, "3", 0xffffffffu},          {0x11d, 0xcd, "Aullido", 0xffffffffu},
            {0x14f, 0xcd, "gladiador", 0xffffffffu}, {0x181, 0xcd, "300", 0xffffffffu},
            {0xf0, 0xdc, "4", 0xffffffffu},          {0x11d, 0xdc, "Luna", 0xffffffffu},
            {0x14f, 0xdc, "gladiador", 0xffffffffu}, {0x181, 0xdc, "200", 0xffffffffu},
            {0xf0, 0xeb, "5", 0xffffffffu},          {0x11d, 0xeb, "Colmillo", 0xffffffffu},
            {0x14f, 0xeb, "Colmillo", 0xffffffffu},  {0x181, 0xeb, "100", 0xffffffffu},
            {0x14a, 260, "se perdio", 0xffff00ffu},  {0x148, 277, "la aldea", 0xffff00ffu}};
        ASSERT_EQ(text.texts.size(), expected.size());
        for (std::size_t line = 0; line < expected.size(); ++line)
        {
            SCOPED_TRACE(line);
            EXPECT_EQ(text.texts[line].text, expected[line].text);
            EXPECT_EQ(text.texts[line].x, Centered(expected[line].x, expected[line].text));
            EXPECT_EQ(text.texts[line].y, expected[line].y);
            EXPECT_EQ(text.texts[line].style.font, FontKind::Normal);
            EXPECT_EQ(text.texts[line].style.colors.text, expected[line].color);
        }
        EXPECT_EQ(ui.byte723bf9, 1);
    }
}

// El botón: con el mouse dentro de (300, 354) × (300, 330), 0x6ca; y con 0x723bdc en 5, 0x6cb y la ventana se cierra
// (0x723a44, 0x723bdc y 0x723bf9 en 0). En el borde no está dentro. Con el aviso en 1, los textos 2000 y 2001.
TEST_F(CrywolfInterfaceTest, TheResultsButtonClosesWithTheUiMark)
{
    Enter(CrywolfMap, 0);
    CrywolfInterface& ui = Ui();
    ui.byte723bf9 = 1;
    ui.message = 1;
    mouse = {300, 315};
    Frame();
    EXPECT_EQ(QuadsOf(sprites)[1].slot, 0x6c9);
    EXPECT_EQ(text.texts.back().text, "el altar");
    mouse = {301, 329};
    Frame();
    EXPECT_EQ(QuadsOf(sprites)[1].slot, 0x6ca);
    EXPECT_EQ(ui.byte723bf9, 1);
    ui.dwords723bdc[0] = 5;
    ui.byte723a44 = 1;
    Frame();
    EXPECT_EQ(QuadsOf(sprites)[1].slot, 0x6cb);
    EXPECT_EQ(ui.byte723bf9, 0);
    EXPECT_EQ(ui.byte723a44, 0);
    EXPECT_EQ(ui.dwords723bdc[0], 0);
}

// Un dibujo intermedio de MuRet escribe en su copia: la de WorldRuntime queda.
TEST_F(CrywolfInterfaceTest, AProjectionWritesItsCopy)
{
    Enter(CrywolfMap, 3);
    Ui().message = 1;
    Ui().byte723a3d = 1;
    CrywolfInterface copy = Ui();
    Frame(&copy);
    EXPECT_EQ(copy.dword6c73d8, 2);
    EXPECT_FLOAT_EQ(copy.float723a40, 1.0f);
    EXPECT_EQ(copy.byte723bf8, 1);
    EXPECT_EQ(Ui().dword6c73d8, 1);
    EXPECT_FLOAT_EQ(Ui().float723a40, 0.0f);
    EXPECT_EQ(Ui().byte723bf8, 0);
}

// 0x436240 (el hijo 26 del HUD): con la etapa 3 o 4 en el 34 deja 0xff1594ff sobre 0 para lo que sigue, sin dibujar;
// en Lorencia, en Devias o con otra etapa no toca el color. Con la ventana del contrato (0x723a44 en 1 o 2) corta.
TEST_F(CrywolfInterfaceTest, TheContractStepOnlyLeavesTheTitleColorInTheDefense)
{
    const Client::Render::Text::TextColors before{.text = 0xff102030u, .background = 0xff405060u};
    for (const std::int32_t map : {0, 2, CrywolfMap})
    {
        for (const std::uint8_t stage : {std::uint8_t{2}, std::uint8_t{3}, std::uint8_t{4}})
        {
            SCOPED_TRACE(map);
            SCOPED_TRACE(static_cast<int>(stage));
            Enter(map, stage);
            world->Runtime().hud.textColors = before;
            sprites.Begin(Screen640);
            GameHudContext context = Context();
            ASSERT_TRUE(Client::Scenes::DrawTheCrywolfContract(context).HasValue());
            EXPECT_TRUE(QuadsOf(sprites).empty());
            const bool defense = map == CrywolfMap && stage != 2;
            EXPECT_EQ(world->Runtime().hud.textColors.text, defense ? 0xff1594ffu : before.text);
            EXPECT_EQ(world->Runtime().hud.textColors.background, defense ? 0u : before.background);
        }
    }
    Enter(CrywolfMap, 3);
    Ui().byte723a44 = 2;
    GameHudContext context = Context();
    const auto cut = Client::Scenes::DrawTheCrywolfContract(context);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x436274"), std::string::npos) << cut.Error();
}
} // namespace
