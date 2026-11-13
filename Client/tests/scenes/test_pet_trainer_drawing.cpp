#include "Scenes/PetTrainerDrawing.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Text/TextTable.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/ItemState.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// El dibujo del entrenador (0x44b510, CL9 hito 6, fase 4b): el menú (0x44b610), recuperar (0x44c850, con los rand()
// del registro propio de 0x44b190 y del suyo) y la caja (0x44d680), en 640 × 480. Los textos y el nombre son
// inventados y miden 6 columnas por letra; el mundo, en Lorencia (0) y Devias (2).
namespace
{
using Client::Render::SpriteBatch;
using Client::Render::Models::CrtRandom;
using Client::Render::Text::FontKind;
using Client::Scenes::GameHudContext;
using Client::Scenes::WorldState;
using Client::Tests::RecordedText;
using Client::World::ItemState;

constexpr Client::App::PixelSize Screen640{640, 480};
constexpr std::uint32_t Seed = 0x7a1e5u;
constexpr int Own = Client::Render::Models::OwnRecordCloseRandoms;
constexpr std::array<std::int32_t, 2> Maps{0, 2};
constexpr int NoTexture = -1;
constexpr int Marker = -2;
constexpr std::uint16_t Horse = 0x1a04;
constexpr std::size_t HorseSlot = 8;
constexpr std::uint8_t PetDurability = 255;

std::map<std::size_t, std::string> TrainerTexts()
{
    return {{584, "%s: %d%%"},    {586, "joya para %s"}, {587, "y nada mas"},   {1002, "cerrar"},
            {1187, "caballo"},    {1202, "elegi"},       {1203, "hola %s"},     {1204, "recuperar"},
            {1205, "mezclar"},    {1212, "mezcla"},      {1213, "sin %s"},      {1214, "cuervo"},
            {1227, "Entrenador"}, {1228, "a quien"},     {1229, "no esta"},     {1230, "esta entero"},
            {1231, "faltan %s"},  {1232, "cuesta %s"},   {1622, "zen %s (%s)"}, {1623, "tasa %d%%"}};
}

Client::Data::Tables::ItemTable SyntheticItems()
{
    using Client::Data::Tables::ItemRecordSize;
    std::vector<std::byte> plain(Client::Data::Tables::ItemTableSize);
    plain[std::size_t{Horse} * ItemRecordSize + 0x2d] = std::byte{PetDurability};
    const std::vector<std::byte> encrypted = Client::Tests::EncryptRecords(std::move(plain), ItemRecordSize);
    return Client::Data::Tables::ItemTable(std::span<const std::byte>(encrypted));
}

CrtRandom Advanced(CrtRandom random, int count)
{
    random.Discard(static_cast<std::uint32_t>(count));
    return random;
}

// Los slots de los quads del lote, en orden (NoTexture sin textura, Marker si es la marca de un texto).
std::vector<int> SlotsOf(const SpriteBatch& sprites)
{
    std::vector<int> slots;
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        int slot = NoTexture;
        if (command.texture == Client::Tests::TextMarkerSlot)
            slot = Marker;
        else if (command.texture)
            slot = command.texture->value;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
            slots.push_back(slot);
    }
    return slots;
}

// Los slots sin las marcas de los textos.
std::vector<int> QuadsOf(const SpriteBatch& sprites)
{
    std::vector<int> quads;
    for (const int slot : SlotsOf(sprites))
    {
        if (slot != Marker)
            quads.push_back(slot);
    }
    return quads;
}

// La x de un texto centrado en `x` (0x623690): 6 columnas por letra en 640.
std::int32_t Centered(std::int32_t x, const std::string& text)
{
    return x - static_cast<std::int32_t>(text.size()) * 3;
}

class PetTrainerDrawingTest : public ::testing::Test
{
protected:
    PetTrainerDrawingTest() : m_world(std::make_unique<WorldState>()), m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
        m_world->OwnCharacter().ClearEquipmentForEntry();
        Runtime().darkHorseLevel = 3;
        Runtime().field78abb10 = 1;
        auto& name = m_world->Actors()[Runtime().hero].name;
        const std::string_view hero = "Domador";
        for (std::size_t i = 0; i < hero.size(); ++i)
            name[i] = static_cast<std::byte>(hero[i]);
    }

    Client::Scenes::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    Client::Core::Expected<void, std::string> Draw(std::int32_t mode, std::int32_t x = 0, std::int32_t y = 0,
                                                   bool leftDown = false, bool projecting = false)
    {
        Client::Scenes::WorldRuntime& runtime = Runtime();
        runtime.panels.delegated.trainer = mode;
        Client::Scenes::ActorState& hero = m_world->Actors()[runtime.hero];
        GameHudContext context{.world = *m_world,
                               .random = m_random,
                               .ownRecordDepth = runtime.ownRecordDepth,
                               .cellXDepth = hero.cellXAccesses,
                               .cellYDepth = hero.cellYAccesses,
                               .hud = runtime.hud,
                               .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                               .sound = m_sound,
                               .projecting = projecting,
                               .sprites = m_sprites,
                               .text = m_text,
                               .texts = &m_texts,
                               .items = &m_items,
                               .mouse = {.x = x, .y = y},
                               .leftDown = leftDown};
        return Client::Scenes::DrawTheTrainer(context);
    }

    void Restart()
    {
        m_text.texts.clear();
        m_sprites.Begin(Screen640);
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random{Seed};
    std::optional<std::int32_t> m_sound{};
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text;
    Client::Data::Text::TextTable m_texts = Client::Tests::MakeTextTable(TrainerTexts());
    Client::Data::Tables::ItemTable m_items = SyntheticItems();
};

void ExpectCentered(const RecordedText& text, std::int32_t x, std::int32_t y, const std::string& expected,
                    std::uint32_t color, FontKind font)
{
    EXPECT_EQ(text.text, expected);
    EXPECT_EQ(text.x, Centered(x, expected)) << expected;
    EXPECT_EQ(text.y, y) << expected;
    EXPECT_EQ(text.style.colors.text, color) << expected;
    EXPECT_EQ(text.style.font, font) << expected;
}

// 0x44b510 con 0, 4 o fuera de 1..3: nada.
TEST_F(PetTrainerDrawingTest, TheIdleModesDrawNothing)
{
    for (const std::int32_t mode : {0, 4, 5, -1})
    {
        ASSERT_TRUE(Draw(mode).HasValue());
        EXPECT_TRUE(m_sprites.Commands().empty()) << mode;
        EXPECT_TRUE(m_text.texts.empty()) << mode;
    }
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x44b610: cinco tiras de 0xfb, las dos líneas de 0xfc, los dos marcos de 0xf0 y el botón de cerrar; el título en la
// caja de 203 columnas, negro sobre gris; el 1202 y el 1203 con el nombre centrados en 0x13f; los dos rótulos en
// 0xb9 y 0xd7, el primero en gruesa con el mouse encima. Sin rand().
TEST_F(PetTrainerDrawingTest, TheMenuDrawsItsTitleLinesAndLabels)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        Restart();
        Runtime().map = map;
        ASSERT_TRUE(Draw(1, 0x103, 0xb4).HasValue());
        EXPECT_EQ(m_random.State(), Seed);
        EXPECT_EQ(QuadsOf(m_sprites), (std::vector<int>{0xfb, 0xfb, 0xfb, 0xfb, 0xfb, 0xfc, 0xfc, 0xf0, 0xf0, 0x118}));
        ASSERT_EQ(m_text.texts.size(), 5u);
        const RecordedText& title = m_text.texts[0];
        EXPECT_TRUE(title.windowed);
        EXPECT_EQ(title.text, "Entrenador");
        EXPECT_EQ(title.x, 0xda);
        EXPECT_EQ(title.y, 0x64);
        // cx + 2 · ((203 − cx) / 2): con un texto de ancho par, la caja impar pierde una columna.
        EXPECT_EQ(title.columns, 202);
        EXPECT_EQ(title.style.colors.text, 0u);
        EXPECT_EQ(title.style.colors.background, 0xc8c8c8c8u);
        EXPECT_EQ(title.style.font, FontKind::Bold);
        ExpectCentered(m_text.texts[1], 0x13f, 0x78, "elegi", 0xc8c8c8c8u, FontKind::Normal);
        ExpectCentered(m_text.texts[2], 0x13f, 0x8c, "hola Domador", 0xc8c8c8c8u, FontKind::Normal);
        ExpectCentered(m_text.texts[3], 0x13f, 0xb9, "recuperar", 0xff67bfdfu, FontKind::Bold);
        ExpectCentered(m_text.texts[4], 0x13f, 0xd7, "mezclar", 0xff67bfdfu, FontKind::Normal);
    }
}

// 0x44b89b..0x44b8f6: la ayuda del botón de cerrar en [0x18a, 0x1a2) × [0x109, 0x121), una columna a la derecha del
// que cierra en el paso ([0x189, 0x1a1), Q-C958).
TEST_F(PetTrainerDrawingTest, TheMenuCloseHelpIsOneColumnRightOfTheStep)
{
    ASSERT_TRUE(Draw(1, 0x18a, 0x109).HasValue());
    const RecordedText& help = m_text.texts.back();
    EXPECT_EQ(help.text, "cerrar");
    EXPECT_EQ(help.x, 0x18a);
    EXPECT_EQ(help.y, 0xfc);
    // 0x44b8ec escribe 0xff000000 de fondo, que 0x554340 pisa en 0x55450f.
    EXPECT_EQ(help.style.colors.background, 0u);
    EXPECT_EQ(Runtime().hud.textColors.background, 0u);
    EXPECT_EQ(Runtime().hud.dcFont, FontKind::Normal);

    for (const auto& [x, y] : {std::pair{0x189, 0x109}, std::pair{0x1a2, 0x109}, std::pair{0x18a, 0x121}})
    {
        Restart();
        ASSERT_TRUE(Draw(1, x, y).HasValue());
        EXPECT_NE(m_text.texts.back().text, "cerrar") << x << ", " << y;
    }
}

// 0x44c850 con el caballo a 100 de 255 y sin el cuervo, sin zen: el caballo cuesta 221.000 y faltan todos (blanco
// sobre rojo); sin el cuervo, su rótulo en rojo con la fuente normal y "no esta", también sobre rojo. Los rand(): dos
// cierres de 0x44b190 y el del dibujo con el caballo, uno y el del dibujo sin el cuervo.
TEST_F(PetTrainerDrawingTest, TheRecoveryQuotesEachPetWithItsRecordCloses)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        Restart();
        Runtime().map = map;
        Runtime().field78abb10 = 1;
        m_world->OwnCharacter().equipment[HorseSlot] = ItemState{.id = Horse, .durability = 100, .occupied = 1};
        m_world->OwnCharacter().zen = 0;
        const CrtRandom expected = Advanced(m_random, 5 * Own);
        const auto drawn = Draw(2, 0x103, 0x96);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(m_random.State(), expected.State());
        EXPECT_EQ(Runtime().ownRecordDepth, 0);
        EXPECT_EQ(Runtime().field78abb10, 0);
        EXPECT_EQ(QuadsOf(m_sprites), (std::vector<int>{0xfb, 0xfb, 0xfb, 0xfb, 0xfb, 0xfc, 0xfc, 0xf0, 0xf0, 0x118}));
        ASSERT_EQ(m_text.texts.size(), 6u);
        EXPECT_EQ(m_text.texts[0].text, "Entrenador");
        ExpectCentered(m_text.texts[1], 0x13f, 0x78, "a quien", 0xffc8c8c8u, FontKind::Normal);
        ExpectCentered(m_text.texts[2], 0x13f, 0x9b, "caballo", 0xff67bfdfu, FontKind::Bold);
        ExpectCentered(m_text.texts[3], 0x13f, 0xb4, "faltan 221,000", 0xffffffffu, FontKind::Bold);
        EXPECT_EQ(m_text.texts[3].style.colors.background, 0xc80000ffu);
        ExpectCentered(m_text.texts[4], 0x13f, 0xd7, "cuervo", 0xff1933ffu, FontKind::Normal);
        ExpectCentered(m_text.texts[5], 0x13f, 0xf0, "no esta", 0xffffffffu, FontKind::Normal);
        EXPECT_EQ(Runtime().hud.textColors.background, 0u);
    }
}

// Con zen de sobra, el precio sin el doble (Q-C955) en el color de los rótulos; un dibujo intermedio de MuRet saca los
// mismos rand() pero no escribe 0x78abb10. Lleno, "esta entero" con un solo cierre en 0x44b190.
TEST_F(PetTrainerDrawingTest, TheRecoveryWithZenShowsTheCost)
{
    m_world->OwnCharacter().equipment[HorseSlot] = ItemState{.id = Horse, .durability = 100, .occupied = 1};
    m_world->OwnCharacter().zen = 2'000'000'000;
    const CrtRandom expected = Advanced(m_random, 5 * Own);
    ASSERT_TRUE(Draw(2, 0, 0, false, true).HasValue());
    EXPECT_EQ(m_random.State(), expected.State());
    EXPECT_EQ(Runtime().field78abb10, 1);
    ExpectCentered(m_text.texts[3], 0x13f, 0xb4, "cuesta 221,000", 0xff67bfdfu, FontKind::Normal);
    EXPECT_EQ(m_text.texts[3].style.colors.background, 0u);

    Restart();
    m_world->OwnCharacter().equipment[HorseSlot].durability = PetDurability;
    const CrtRandom full = Advanced(m_random, 4 * Own);
    ASSERT_TRUE(Draw(2).HasValue());
    EXPECT_EQ(m_random.State(), full.State());
    EXPECT_EQ(m_text.texts[3].text, "esta entero");
}

// 0x44d680 sin mezcla: la ventana (0x104, 0x105), la grilla de la caja (0x115 vacías, 0x116 con un tipo), las dos
// barras 0x117, el botón de mezclar 0x124 y el de cerrar; el título 1205 y la tasa en la caja de 120; "sin mezcla" en
// rojo en 0x32, la chance 0 en 65 y los avisos en 215 y 230. Sin rand().
TEST_F(PetTrainerDrawingTest, TheBoxWithoutAMixShowsTheEmptyName)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        Restart();
        Runtime().map = map;
        Runtime().npcWindows.trainerBox[1] = ItemState{.id = 0x1c0d, .occupied = 1};
        Runtime().npcWindows.chaosTaxRate = 4;
        Runtime().panels.delegated.field724584 = 0;
        ASSERT_TRUE(Draw(3).HasValue());
        EXPECT_EQ(m_random.State(), Seed);
        const std::vector<int> quads = QuadsOf(m_sprites);
        ASSERT_EQ(quads.size(), 2u + 32u + 4u);
        EXPECT_EQ(quads[0], 0x104);
        EXPECT_EQ(quads[1], 0x105);
        EXPECT_EQ(quads[2], 0x115);
        EXPECT_EQ(quads[3], 0x116);
        EXPECT_EQ(quads[34], 0x117);
        EXPECT_EQ(quads[35], 0x117);
        EXPECT_EQ(quads[36], 0x124);
        EXPECT_EQ(quads[37], 0x118);
        ASSERT_EQ(m_text.texts.size(), 6u);
        EXPECT_EQ(m_text.texts[0].text, "mezclar");
        EXPECT_EQ(m_text.texts[0].x, 0x127);
        EXPECT_EQ(m_text.texts[0].columns, 120);
        EXPECT_EQ(m_text.texts[1].text, "tasa 4%");
        EXPECT_EQ(m_text.texts[1].y, 0x14);
        ExpectCentered(m_text.texts[2], 0x163, 0x32, "sin mezcla", 0xff3030ffu, FontKind::Bold);
        ExpectCentered(m_text.texts[3], 0x163, 65, "mezcla: 0%", 0xffdcdcdcu, FontKind::Bold);
        ExpectCentered(m_text.texts[4], 0x163, 215, "joya para mezcla", 0xffdcdcdcu, FontKind::Bold);
        ExpectCentered(m_text.texts[5], 0x163, 230, "y nada mas", 0xffdcdcdcu, FontKind::Bold);
    }
}

// Con la mezcla del caballo (1) y la del cuervo (2): el nombre "%s %s" en celeste, la chance 60 y el precio con el
// impuesto (5.000.000 y 1.000.000 al 4%) en 80. Con otra mezcla corta.
TEST_F(PetTrainerDrawingTest, TheBoxShowsTheMixOfTheLastStep)
{
    Runtime().npcWindows.chaosTaxRate = 4;
    Runtime().panels.delegated.field724584 = 1;
    ASSERT_TRUE(Draw(3).HasValue());
    ASSERT_EQ(m_text.texts.size(), 7u);
    ExpectCentered(m_text.texts[2], 0x163, 0x32, "caballo mezcla", 0xff19ccffu, FontKind::Bold);
    ExpectCentered(m_text.texts[3], 0x163, 65, "mezcla: 60%", 0xffdcdcdcu, FontKind::Bold);
    ExpectCentered(m_text.texts[4], 0x163, 80, "zen 5,200,000 (5,000,000)", 0xffdcdcdcu, FontKind::Bold);
    EXPECT_EQ(m_text.texts[5].y, 215);

    Restart();
    Runtime().panels.delegated.field724584 = 2;
    ASSERT_TRUE(Draw(3).HasValue());
    ExpectCentered(m_text.texts[2], 0x163, 0x32, "cuervo mezcla", 0xff19ccffu, FontKind::Bold);
    ExpectCentered(m_text.texts[4], 0x163, 80, "zen 1,040,000 (1,000,000)", 0xffdcdcdcu, FontKind::Bold);

    Restart();
    Runtime().panels.delegated.field724584 = 3;
    const auto cut = Draw(3);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x44da59"), std::string::npos);
}

// 0x44d76d..0x44d834: con el mouse en [335, 379) × [300, 332) la ayuda 1212 en (0x14f, 0x11f) y, con el botón abajo,
// el 0x125; 0x44db2b..0x44dba1: la de cerrar en [280, 304) × [395, 419), en (0x118, 0x17e).
TEST_F(PetTrainerDrawingTest, TheBoxButtonsShowTheirHelp)
{
    ASSERT_TRUE(Draw(3, 378, 331, true).HasValue());
    std::vector<int> quads = QuadsOf(m_sprites);
    EXPECT_EQ(std::count(quads.begin(), quads.end(), 0x125), 1);
    ASSERT_FALSE(m_text.texts.empty());
    EXPECT_EQ(m_text.texts[0].text, "mezcla");
    EXPECT_EQ(m_text.texts[0].x, 0x14f);
    EXPECT_EQ(m_text.texts[0].y, 0x11f);
    EXPECT_EQ(m_text.texts[0].style.colors.background, 0u);
    EXPECT_EQ(m_text.texts[1].text, "mezclar");

    Restart();
    ASSERT_TRUE(Draw(3, 379, 300, true).HasValue());
    quads = QuadsOf(m_sprites);
    EXPECT_EQ(std::count(quads.begin(), quads.end(), 0x125), 0);
    EXPECT_EQ(m_text.texts[0].text, "mezclar");

    Restart();
    ASSERT_TRUE(Draw(3, 303, 418).HasValue());
    const RecordedText& help = m_text.texts.back();
    EXPECT_EQ(help.text, "cerrar");
    EXPECT_EQ(help.x, 0x118);
    EXPECT_EQ(help.y, 0x17e);
    EXPECT_EQ(Runtime().hud.dcFont, FontKind::Normal);

    Restart();
    ASSERT_TRUE(Draw(3, 304, 418).HasValue());
    EXPECT_NE(m_text.texts.back().text, "cerrar");
}
} // namespace
