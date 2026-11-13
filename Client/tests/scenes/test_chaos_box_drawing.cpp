#include "Scenes/ChaosBoxDrawing.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/GamePanels.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/ItemState.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// El dibujo de la caja del caos (0x5febf0, CL9 hito 6, fase 3; 06 § La caja del caos), en (0x104, 0) de 640 × 480:
// la ventana, la grilla, el título del modo con el impuesto, el nombre, la chance y el precio de la mezcla, los avisos
// y el botón de mezclar. Los textos son inventados y miden 6 columnas por letra. No saca rand().
namespace
{
using Client::Render::SpriteBatch;
using Client::Render::Models::CrtRandom;
using Client::Render::Text::FontKind;
using Client::Scenes::GameHudContext;
using Client::Scenes::NpcWindowState;
using Client::Scenes::WorldState;
using Client::Tests::RecordedText;
using Client::World::ItemState;

constexpr Client::App::PixelSize Screen640{640, 480};
constexpr std::uint32_t Seed = 0x0c4a05u;
constexpr std::int32_t Left = 0x104;
constexpr int NoTexture = -1;
constexpr int Marker = -2;
// 0x5ff6f0: el botón de mezclar, de 44 × 32 en (Left + 75, 300).
constexpr std::int32_t ButtonX = Left + 75;
constexpr std::int32_t ButtonY = 300;

std::map<std::size_t, std::string> ChaosTexts()
{
    return {{583, "Caos"},       {584, "%s: %d%%"},     {586, "joya para %s"}, {587, "y nada mas"},
            {588, "ojo con %s"}, {589, "puede fallar"}, {590, "o romperse"},   {591, "mezcla"},
            {601, "nada"},       {602, "arma"},         {603, "boleto"},       {604, "mas diez"},
            {606, "aviso uno"},  {607, "aviso dos"},    {608, "aviso tres"},   {610, "falla uno"},
            {611, "falla dos"},  {612, "unicornio"},    {614, "alas"},         {664, "+% d creativo"},
            {735, "Normal"},     {736, "Arma"},         {1172, "capa"},        {1412, "crear"},
            {1413, "bendicion"}, {1622, "zen %s (%s)"}, {1623, "tasa %d%%"},   {1636, "pocion %s (%s)"},
            {1863, "lobo"},      {1921, "cuerno"},      {1923, "fenrir"}};
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

struct Corner
{
    float x = 0.0f;
    float y = 0.0f;
};

// La esquina de arriba a la izquierda del quad `index` con `slot`, en 640 × 480.
std::optional<Corner> CornerOf(const SpriteBatch& sprites, int slot, std::size_t index = 0)
{
    const auto vertices = sprites.Vertices();
    std::size_t seen = 0;
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (!command.texture || command.texture->value != slot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            if (seen++ != index)
                continue;
            const Client::Render::SpriteVertex& first = vertices[command.firstVertex + quad * 6];
            return Corner{first.x, 480.0f - first.y};
        }
    }
    return std::nullopt;
}

class ChaosBoxDrawingTest : public ::testing::Test
{
protected:
    ChaosBoxDrawingTest() : m_world(std::make_unique<WorldState>()), m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
        Runtime().panels.chaosBox = 1;
        for (ItemState& cell : Npc().chaosBox)
            cell = ItemState{.id = 0xffff};
    }

    Client::Scenes::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    NpcWindowState& Npc()
    {
        return Runtime().npcWindows;
    }

    Client::Core::Expected<void, std::string> Draw(std::int32_t x = 0, std::int32_t y = 0, bool leftDown = false)
    {
        Client::Scenes::WorldRuntime& runtime = Runtime();
        Client::Scenes::ActorState& hero = m_world->Actors()[runtime.hero];
        GameHudContext context{.world = *m_world,
                               .random = m_random,
                               .ownRecordDepth = runtime.ownRecordDepth,
                               .cellXDepth = hero.cellXAccesses,
                               .cellYDepth = hero.cellYAccesses,
                               .hud = runtime.hud,
                               .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                               .sound = m_sound,
                               .sprites = m_sprites,
                               .text = m_text,
                               .texts = m_drawnTexts,
                               .mouse = {.x = x, .y = y},
                               .leftDown = leftDown};
        Client::Scenes::GamePanels panels(*m_world, m_random);
        return Client::Scenes::DrawTheChaosBox(context, panels.Registry());
    }

    // Lo que se dibujó desde el renglón del nombre, sin el título.
    [[nodiscard]] std::vector<RecordedText> Lines() const
    {
        std::vector<RecordedText> lines;
        for (const RecordedText& text : m_text.texts)
        {
            if (!text.windowed)
                lines.push_back(text);
        }
        return lines;
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
    Client::Data::Text::TextTable m_texts = Client::Tests::MakeTextTable(ChaosTexts());
    const Client::Data::Text::TextTable* m_drawnTexts = &m_texts;
};

void ExpectLine(const RecordedText& line, std::int32_t y, const std::string& text, std::uint32_t color)
{
    EXPECT_EQ(line.x, Left + 0x14) << text;
    EXPECT_EQ(line.y, y) << text;
    EXPECT_EQ(line.text, text);
    EXPECT_EQ(line.style.colors.text, color) << text;
    EXPECT_EQ(line.style.colors.background, 0x80282828u) << text;
    EXPECT_EQ(line.style.font, FontKind::Bold) << text;
}

// 0x5febf0 con el 3 y la suerte, el modo 0 y el impuesto en 3, en Lorencia, Devias y Noria (la del duende del caos):
// la ventana (0x104 y 0x105), las 32 casillas (0x115 vacías, 0x116 con la espada), el título 735 y la tasa en la caja
// de 120 columnas, el nombre 604 en celeste, la chance 50 + 25, el precio de 2.000.000 con el impuesto (2.060.000),
// la joya del caos, los tres avisos del 3 y el botón 0x124. Deja el precio en 0x7d2dca0, y la esquina en (0x104, 0).
TEST_F(ChaosBoxDrawingTest, TheUpgradeShowsItsNameRateAndTaxedPrice)
{
    for (const std::int32_t map : {0, 2, 3})
    {
        SCOPED_TRACE(map);
        Restart();
        Runtime().map = map;
        Runtime().hud.npcWindowX = 0;
        Npc().chaosBox[0] = ItemState{.id = 0x0001, .occupied = 1};
        Npc().chaosBox[8] = ItemState{.id = 0x0001};
        Npc().mixKind = 3;
        Npc().mixLuck = 1;
        Npc().chaosMixMode = 0;
        Npc().chaosTaxRate = 3;

        const auto drawn = Draw();
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

        EXPECT_EQ(m_random.State(), Seed);
        EXPECT_EQ(Runtime().hud.npcWindowX, Left);
        EXPECT_EQ(Runtime().hud.npcWindowY, 0);
        EXPECT_EQ(Npc().mixPrice, 2'000'000u);
        const std::vector<int> slots = SlotsOf(m_sprites);
        ASSERT_GE(slots.size(), 34u);
        EXPECT_EQ(slots[0], 0x104);
        EXPECT_EQ(slots[1], 0x105);
        EXPECT_EQ(slots[2], 0x116);
        EXPECT_EQ(slots[3], 0x115);
        EXPECT_EQ(slots[10], 0x116);
        EXPECT_EQ(slots[33], 0x115);
        EXPECT_EQ(slots.back(), 0x124);
        const auto button = CornerOf(m_sprites, 0x124);
        ASSERT_TRUE(button.has_value());
        EXPECT_FLOAT_EQ(button->x, static_cast<float>(ButtonX));
        EXPECT_FLOAT_EQ(button->y, static_cast<float>(ButtonY));
        const auto box = CornerOf(m_sprites, 0x116, 1);
        ASSERT_TRUE(box.has_value());
        EXPECT_FLOAT_EQ(box->x, Left + 15.0f);
        EXPECT_FLOAT_EQ(box->y, 130.0f);

        ASSERT_GE(m_text.texts.size(), 2u);
        const RecordedText& title = m_text.texts[0];
        EXPECT_TRUE(title.windowed);
        EXPECT_EQ(title.text, "Normal");
        EXPECT_EQ(title.x, Left + 0x23);
        EXPECT_EQ(title.y, 8);
        EXPECT_EQ(title.columns, 120);
        EXPECT_EQ(title.style.colors.text, 0xffdcdcdcu);
        EXPECT_EQ(title.style.colors.background, 0xff141414u);
        EXPECT_EQ(m_text.texts[1].text, "tasa 3%");
        EXPECT_EQ(m_text.texts[1].y, 0x14);

        const std::vector<RecordedText> lines = Lines();
        ASSERT_EQ(lines.size(), 8u);
        ExpectLine(lines[0], 0x32, "mas diez", 0xff30ffffu);
        ExpectLine(lines[1], 0x46, "mezcla: 75%", 0xffffe6d2u);
        ExpectLine(lines[2], 0x5a, "zen 2,060,000 (2,000,000)", 0xffffe6d2u);
        ExpectLine(lines[3], 0xd2, "joya para mezcla", 0xff1432ffu);
        ExpectLine(lines[4], 0xe1, "y nada mas", 0xff1432ffu);
        ExpectLine(lines[5], 0xf0, "aviso uno", 0xff1432ffu);
        ExpectLine(lines[6], 0xff, "aviso dos", 0xff1432ffu);
        ExpectLine(lines[7], 0x10e, "aviso tres", 0xff1432ffu);
    }
}

// 0x5ff6f0..0x5ff816: con el mouse sobre el botón (x87: [x, x + 44) × [y, y + 32)), el botón apretado (0x125) si el
// izquierdo está abajo, y la ayuda 591 con la fuente normal en (x, y − 13). Fuera, nada más.
TEST_F(ChaosBoxDrawingTest, TheMixButtonPressesAndShowsItsHelp)
{
    Npc().mixKind = 0;
    ASSERT_TRUE(Draw(ButtonX + 43, ButtonY + 31, true).HasValue());
    EXPECT_EQ(SlotsOf(m_sprites).back(), Marker);
    EXPECT_TRUE(CornerOf(m_sprites, 0x125).has_value());
    const RecordedText& help = m_text.texts.back();
    EXPECT_EQ(help.text, "mezcla");
    EXPECT_EQ(help.x, ButtonX);
    EXPECT_EQ(help.y, ButtonY - 13);
    EXPECT_EQ(help.style.font, FontKind::Normal);
    EXPECT_EQ(Runtime().hud.dcFont, FontKind::Normal);
    EXPECT_EQ(Runtime().hud.textColors.text, 0xffffffffu);
    EXPECT_EQ(Runtime().hud.textColors.background, 0u);

    Restart();
    ASSERT_TRUE(Draw(ButtonX + 10, ButtonY + 10, false).HasValue());
    EXPECT_FALSE(CornerOf(m_sprites, 0x125).has_value());
    EXPECT_EQ(m_text.texts.back().text, "mezcla");

    for (const auto& [x, y] : {std::pair{ButtonX - 1, ButtonY}, std::pair{ButtonX + 44, ButtonY},
                               std::pair{ButtonX, ButtonY - 1}, std::pair{ButtonX, ButtonY + 32}})
    {
        SCOPED_TRACE(testing::Message() << x << ", " << y);
        Restart();
        ASSERT_TRUE(Draw(x, y, true).HasValue());
        EXPECT_EQ(SlotsOf(m_sprites).back(), 0x124);
        EXPECT_EQ(Runtime().hud.dcFont, FontKind::Bold);
    }
}

// 0x5ff0c5..0x5ff2cd y 0x5ff2f2..0x5ff3e4: sin mezcla (EDI 0) el nombre 601 en rojo, la chance 0 y el precio "0"; en
// el modo 1, los avisos 588 (con el 591), 589 y 590 y el título 736. El 0xb se muestra como las alas (614, celeste).
// El 2 con 0x7d2dc9d suma "+ n%" en celeste sobre el mismo buffer.
TEST_F(ChaosBoxDrawingTest, TheNameAndRateFollowTheKind)
{
    Npc().mixKind = 0;
    Npc().chaosMixMode = 1;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(m_text.texts[0].text, "Arma");
    std::vector<RecordedText> lines = Lines();
    ASSERT_EQ(lines.size(), 8u);
    ExpectLine(lines[0], 0x32, "nada", 0xff3030ffu);
    ExpectLine(lines[1], 0x46, "mezcla: 0%", 0xffffe6d2u);
    ExpectLine(lines[2], 0x5a, "zen 0 (0)", 0xffffe6d2u);
    ExpectLine(lines[5], 0xf0, "ojo con mezcla", 0xff1432ffu);
    ExpectLine(lines[6], 0xff, "puede fallar", 0xff1432ffu);
    ExpectLine(lines[7], 0x10e, "o romperse", 0xff1432ffu);
    EXPECT_EQ(Npc().mixPrice, 0u);

    Restart();
    Npc().mixKind = 0xb;
    Npc().chaosMixMode = 0;
    ASSERT_TRUE(Draw().HasValue());
    lines = Lines();
    ASSERT_EQ(lines.size(), 5u);
    ExpectLine(lines[0], 0x32, "alas", 0xff30ffffu);

    Restart();
    Npc().mixKind = 2;
    Npc().mixLevel = 2;
    Npc().field7d2dc9d = 5;
    ASSERT_TRUE(Draw().HasValue());
    lines = Lines();
    ExpectLine(lines[0], 0x32, "boleto", 0xff30ffffu);
    ExpectLine(lines[1], 0x46, "mezcla: 75% + 5%", 0xff30ffffu);
    ExpectLine(lines[2], 0x5a, "zen 100,000 (100,000)", 0xffffe6d2u);
}

// 0x5ff109..0x5ff280: los nombres armados (0x18 con el 1172, 0x16 con el 664 y el tipo − 10, el 0xf con "%s %s" y el
// 1412, el 0x1c con "%s %s %s" y la capa) y el precio de las pociones (1636, con el 590 abajo dibujado antes); el 5
// con los avisos 610 y 611.
TEST_F(ChaosBoxDrawingTest, TheBuiltNamesAndThePotionPrice)
{
    Npc().mixKind = 0x18;
    Npc().mixWingRate = 40;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(Lines()[0].text, "capa");
    EXPECT_EQ(Lines()[1].text, "mezcla: 40%");

    Restart();
    Npc().mixKind = 0x16;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(Lines()[0].text, "+ 12 creativo");

    Restart();
    Npc().mixKind = 0xf;
    ASSERT_TRUE(Draw().HasValue());
    std::vector<RecordedText> lines = Lines();
    ASSERT_EQ(lines.size(), 6u);
    EXPECT_EQ(lines[0].text, "bendicion crear");
    ExpectLine(lines[2], 0x10e, "o romperse", 0xffffe6d2u);
    ExpectLine(lines[3], 0x5a, "pocion 100,000 (100,000)", 0xffffe6d2u);

    Restart();
    Npc().mixKind = 0x1c;
    Npc().mixCapeKind = 3;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(Lines()[0].text, "fenrir lobo crear");

    Restart();
    Npc().mixKind = 0x19;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(Lines()[0].text, "cuerno crear");

    Restart();
    Npc().mixKind = 5;
    ASSERT_TRUE(Draw().HasValue());
    lines = Lines();
    ASSERT_EQ(lines.size(), 7u);
    EXPECT_EQ(lines[5].text, "falla uno");
    EXPECT_EQ(lines[6].text, "falla dos");
}

// 0x5fecaa..0x5fece7 y 0x5feda3: el modo 2 en adelante pone el título 583 en 0xc sin la tasa; con la mezcla pedida
// (0x7d2dc80 en 2), el precio en 0 y nada más que la ventana, la grilla y el título. Cerrada, nada.
TEST_F(ChaosBoxDrawingTest, TheOtherModesAndTheRequestedMix)
{
    Npc().chaosMixMode = 2;
    Npc().chaosMixState = 2;
    Npc().mixPrice = 77;
    ASSERT_TRUE(Draw().HasValue());
    ASSERT_EQ(m_text.texts.size(), 1u);
    EXPECT_EQ(m_text.texts[0].text, "Caos");
    EXPECT_EQ(m_text.texts[0].y, 0xc);
    EXPECT_EQ(Npc().mixPrice, 0u);
    EXPECT_EQ(SlotsOf(m_sprites).size(), 2u + 32u + 1u);

    Restart();
    Runtime().panels.chaosBox = 0;
    Runtime().hud.npcWindowX = 7;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_TRUE(SlotsOf(m_sprites).empty());
    EXPECT_EQ(Runtime().hud.npcWindowX, 7);
}

// 0x5fee3a (P-C197): el 2 de nivel 8 o más lee la tasa después de las seis de 0x6c9da8 y corta; sin Text.bmd corta
// con la dirección del título.
TEST_F(ChaosBoxDrawingTest, WhatTheOriginalReadsOutOfItsDataCuts)
{
    Npc().mixKind = 2;
    Npc().mixLevel = 8;
    const auto past = Draw();
    ASSERT_FALSE(past.HasValue());
    EXPECT_NE(past.Error().find("P-C197"), std::string::npos) << past.Error();

    Restart();
    m_drawnTexts = nullptr;
    const auto missing = Draw();
    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("0x5fed39"), std::string::npos) << missing.Error();
}
} // namespace
