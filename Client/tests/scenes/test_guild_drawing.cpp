#include "Scenes/GuildDrawing.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GuildMarks.h"
#include "Scenes/GuildState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/OldWindowDraw.h"
#include "UI/PanelDialog.h"
#include "UI/PanelRegistry.h"

#include "support/FakeTextureStore.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

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

// Scenes::DrawTheWarScore y DrawThePanelDialog (CL9 hito 8, fase 2): el marcador de la guerra de gremios (0x5a7640) y
// el diálogo de los paneles (0x660830), contra su ASM, en Lorencia (0), en Devias (2) y en Arena (6, la del fútbol de
// la guerra). Los nombres y los textos son inventados.
namespace
{
using Client::Render::BlendMode;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::Models::CrtRandom;
using Client::Scenes::GameHudContext;
using Client::Scenes::GuildMarkTextures;
using Client::Scenes::WorldState;

constexpr std::uint32_t Seed = 7;
constexpr Client::App::PixelSize Screen640{640, 480};
constexpr int NoTexture = -1;
constexpr int Marker = -2;
constexpr std::int32_t HeroGuild = 3;
constexpr std::int32_t RivalGuild = 5;

struct DrawnQuad
{
    int slot = NoTexture;
    BlendMode blend = BlendMode::Alpha;
    bool alphaTest = true;
    SpriteVertex first{};
    SpriteVertex third{};
};

std::vector<DrawnQuad> QuadsOf(const SpriteBatch& sprites)
{
    std::vector<DrawnQuad> quads;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        int slot = NoTexture;
        if (command.texture == Client::Tests::TextMarkerSlot)
            slot = Marker;
        else if (command.texture)
            slot = command.texture->value;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            quads.push_back(DrawnQuad{.slot = slot,
                                      .blend = command.blend,
                                      .alphaTest = command.alphaTest,
                                      .first = vertices[first],
                                      .third = vertices[first + 2]});
        }
    }
    return quads;
}

void ExpectQuadAt(const DrawnQuad& quad, int slot, float x, float y, float width, float height)
{
    constexpr float Tolerance = 1e-3f;
    EXPECT_EQ(quad.slot, slot);
    EXPECT_NEAR(quad.first.x, x, Tolerance);
    EXPECT_NEAR(quad.first.y, 480.0f - y, Tolerance);
    EXPECT_NEAR(quad.third.x, x + width, Tolerance);
    EXPECT_NEAR(quad.third.y, 480.0f - y - height, Tolerance);
}

template <std::size_t Size> void Fill(std::array<char, Size>& target, std::string_view text)
{
    target = {};
    for (std::size_t i = 0; i < text.size() && i + 1 < Size; ++i)
        target[i] = text[i];
}

int Draws(const CrtRandom& random)
{
    CrtRandom reference(Seed);
    for (int draws = 0; draws <= 1000; ++draws)
    {
        if (reference.State() == random.State())
            return draws;
        static_cast<void>(reference.Next());
    }
    return -1;
}

int MarkSlot(std::int32_t index)
{
    return GuildMarkTextures::SlotOf(index, true).value;
}

// Los textos de los botones del diálogo (0x65feb0), inventados.
Client::Data::Text::TextTable ButtonTexts()
{
    const std::map<std::size_t, std::string> texts{{228, "Dale"}, {229, "Nop"}, {1037, "Va"}, {1038, "Ni ahi"}};
    auto table = Client::Data::Text::LoadTextTable(Client::Tests::TextTableBytes(texts));
    EXPECT_TRUE(table.HasValue());
    return std::move(table).Value();
}

class GuildDrawingTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    GuildDrawingTest() : m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
        Runtime().map = GetParam();
        Client::Scenes::GuildState& guild = Runtime().guild;
        guild.tableCount = 6;
        guild.table[HeroGuild].key = 30;
        Fill(guild.table[HeroGuild].guildName, "Horneros");
        guild.table[HeroGuild].mark.fill(4);
        guild.table[RivalGuild].key = 50;
        Fill(guild.table[RivalGuild].guildName, "Teros");
        guild.table[RivalGuild].mark.fill(12);
        Fill(guild.rival, "Teros");
        guild.ourScore = 3;
        guild.theirScore = 12;
        Hero().word1f2 = 0xffff;
    }

    Client::Scenes::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    Client::Scenes::ActorState& Hero()
    {
        return m_world->Actors()[Runtime().hero];
    }

    Client::Core::Expected<void, std::string> Score(bool withMarks = true)
    {
        Client::Scenes::WorldRuntime& runtime = Runtime();
        Client::Scenes::ActorState& hero = Hero();
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
                               .guildMarks = withMarks ? &m_marks : nullptr};
        Client::Scenes::GamePanels panels(*m_world, m_random);
        return Client::Scenes::DrawTheWarScore(context, panels.Registry());
    }

    Client::UI::PanelDialogState& PanelDialog()
    {
        return Runtime().panels.delegated.panelDialog;
    }

    void Open(std::vector<std::string> lines, std::uint32_t flags, std::int32_t placed)
    {
        m_lines = std::move(lines);
        const auto opened = Client::UI::OpenThePanelDialog(
            PanelDialog(),
            Client::UI::PanelDialogRequest{.lines = m_lines,
                                           .flags = flags,
                                           .owner = Client::UI::PanelDialogOwner::Relationship,
                                           .placed = placed},
            m_text, Client::Render::Text::FontKind::Normal, 1000);
        ASSERT_EQ(opened, Client::UI::PanelDialogOpening::Opened);
    }

    Client::Core::Expected<void, std::string> Dialog(bool withTexts = true)
    {
        Client::Scenes::WorldRuntime& runtime = Runtime();
        Client::Scenes::ActorState& hero = Hero();
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
                               .texts = withTexts ? &m_texts : nullptr};
        return Client::Scenes::DrawThePanelDialog(context, PanelDialog());
    }

    std::unique_ptr<WorldState> m_world = std::make_unique<WorldState>();
    CrtRandom m_random{Seed};
    Client::Data::Text::TextTable m_texts = ButtonTexts();
    std::vector<std::string> m_lines;
    std::optional<std::int32_t> m_sound{};
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text;
    Client::Tests::FakeTextureStore m_store;
    GuildMarkTextures m_marks{m_store};
};

// 0x5a7649..0x5a7672: sin la guerra, o con el héroe sin gremio, solo la mezcla apagada y el fondo de los textos.
TEST_P(GuildDrawingTest, WithoutAWarOnlyTheStateChanges)
{
    m_sprites.SetBlend(BlendMode::Additive);
    Hero().word1f2 = HeroGuild;
    ASSERT_TRUE(Score().HasValue());
    Runtime().guild.warOn = 1;
    Hero().word1f2 = 0xffff;
    ASSERT_TRUE(Score().HasValue());
    EXPECT_TRUE(QuadsOf(m_sprites).empty());
    EXPECT_TRUE(m_text.texts.empty());
    EXPECT_EQ(m_sprites.Blend(), BlendMode::Opaque);
    EXPECT_EQ(Runtime().hud.textColors.background, 0x80000000u);
    EXPECT_EQ(m_store.loadCalls, 0);
    EXPECT_EQ(Draws(m_random), 0);
}

// Con la guerra: x = 640 · 0,5 − 20 = 300; la marca del héroe en (300, 4) y la del rival en (300, 19), de 8 × 8 y
// mezcladas; los puntajes en 313 y los nombres en 330, en la negrita, rojos los del héroe y naranjas los del rival
// sin equipo. Sin el inventario, 0x5bb8d0 no consulta nada.
TEST_P(GuildDrawingTest, TheScoreDrawsBothMarksScoresAndNames)
{
    Runtime().guild.warOn = 1;
    Hero().word1f2 = HeroGuild;
    ASSERT_TRUE(Score().HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 6u);
    ExpectQuadAt(quads[0], MarkSlot(HeroGuild), 300.0f, 4.0f, 8.0f, 8.0f);
    EXPECT_EQ(quads[0].blend, BlendMode::Alpha);
    EXPECT_FLOAT_EQ(quads[0].first.u, 0.0f);
    EXPECT_FLOAT_EQ(quads[0].third.u, 1.0f);
    ExpectQuadAt(quads[3], MarkSlot(RivalGuild), 300.0f, 19.0f, 8.0f, 8.0f);
    const auto& texts = m_text.texts;
    ASSERT_EQ(texts.size(), 4u);
    const std::array<std::string_view, 4> expected{"3", "Horneros", "12", "Teros"};
    const std::array<int, 4> xs{313, 330, 313, 330};
    const std::array<int, 4> ys{4, 4, 19, 19};
    const std::array<std::uint32_t, 4> colors{0xff003cffu, 0xff003cffu, 0xffff9600u, 0xffff9600u};
    for (std::size_t at = 0; at < texts.size(); ++at)
    {
        SCOPED_TRACE(at);
        EXPECT_EQ(texts[at].text, expected[at]);
        EXPECT_EQ(texts[at].x, xs[at]);
        EXPECT_EQ(texts[at].y, ys[at]);
        EXPECT_EQ(texts[at].style.colors.text, colors[at]);
        EXPECT_EQ(texts[at].style.colors.background, 0x80000000u);
        EXPECT_EQ(texts[at].style.font, Client::Render::Text::FontKind::Bold);
    }
    // Las dos marcas, subidas con el fondo transparente: roja (4) la del héroe y azul (12) la del rival.
    const auto ours = m_store.Find(Client::Render::TextureSlot{static_cast<std::uint16_t>(MarkSlot(HeroGuild))});
    const auto theirs = m_store.Find(Client::Render::TextureSlot{static_cast<std::uint16_t>(MarkSlot(RivalGuild))});
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(theirs.has_value());
    EXPECT_EQ(ours->texture.pixels[0], 0xff);
    EXPECT_EQ(theirs->texture.pixels[2], 0xff);
    EXPECT_EQ(Runtime().hud.dcFont, Client::Render::Text::FontKind::Bold);
    EXPECT_EQ(Runtime().hud.textColors.text, 0xffff9600u);
    EXPECT_EQ(m_sprites.Blend(), BlendMode::Alpha);
    EXPECT_EQ(Draws(m_random), 0);
}

// Con el equipo (0x576796e) los colores se dan vuelta; un rival que no está en la tabla dibuja la marca de la entrada
// 0 (0x5a78ba); un nombre vacío no se dibuja. Con el inventario abierto, el mundo de 450 deja x en 205 y la única
// llamada a 0x5bb8d0 consulta la tienda y el comercio: 2 rand().
TEST_P(GuildDrawingTest, TheTeamSwapsTheColorsAndAnUnknownRivalShowsTheFirstMark)
{
    Client::Scenes::GuildState& guild = Runtime().guild;
    guild.warOn = 1;
    guild.warTeam = 1;
    Fill(guild.rival, "Benteveo");
    Hero().word1f2 = HeroGuild;
    Runtime().panels.inventory = 1;
    ASSERT_TRUE(Score().HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 6u);
    ExpectQuadAt(quads[0], MarkSlot(HeroGuild), 205.0f, 4.0f, 8.0f, 8.0f);
    ExpectQuadAt(quads[3], MarkSlot(0), 205.0f, 19.0f, 8.0f, 8.0f);
    ASSERT_EQ(m_text.texts.size(), 4u);
    EXPECT_EQ(m_text.texts[0].x, 218);
    EXPECT_EQ(m_text.texts[0].style.colors.text, 0xffff9600u);
    EXPECT_EQ(m_text.texts[3].text, "Benteveo");
    EXPECT_EQ(m_text.texts[3].style.colors.text, 0xff003cffu);
    EXPECT_EQ(Draws(m_random), 2);

    m_sprites.Begin(Screen640);
    m_text.texts.clear();
    guild.table[HeroGuild].guildName = {};
    ASSERT_TRUE(Score().HasValue());
    ASSERT_EQ(m_text.texts.size(), 3u);
    EXPECT_EQ(m_text.texts[1].text, "12");
}

// Sin la tabla de las marcas, la guerra corta; sin la guerra, no hace falta.
TEST_P(GuildDrawingTest, WithoutTheMarksTheWarIsCut)
{
    Hero().word1f2 = HeroGuild;
    ASSERT_TRUE(Score(false).HasValue());
    Runtime().guild.warOn = 1;
    const auto cut = Score(false);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5a76d8"), std::string::npos);
}

// 0x660830 con Confirmar y Cancelar y dos renglones: alto 2 · (13 + 2) + 0x3f = 93 e y = 0x82 − 46 = 84. La caja
// (0x6576a0) sin alpha test y con el alfa en 0,8: el filo, dos filas de 40, el resto de 83 − 80 = 3 (dv 3 · 0,025 ·
// 0,625) y el otro filo. Los renglones centrados en 0x140 desde 104, de 15 en 15, en la fuente normal y en blanco; los
// botones en y = 104 + 30 + 9 = 143, en 0x104 y 0x14a, con sus textos. No saca rand().
TEST_P(GuildDrawingTest, ThePanelDialogDrawsItsBoxLinesAndButtons)
{
    Open({"Una alianza", "con Teros"}, Client::UI::PanelDialogConfirm, 0);
    ASSERT_EQ(PanelDialog().height, 93);
    Runtime().hud.dcFont = Client::Render::Text::FontKind::Bold;
    ASSERT_TRUE(Dialog().HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 11u);
    ExpectQuadAt(quads[0], 0xfc, 214.0f, 84.0f, 213.0f, 5.0f);
    ExpectQuadAt(quads[1], 0xfb, 214.0f, 89.0f, 213.0f, 40.0f);
    ExpectQuadAt(quads[2], 0xfb, 214.0f, 129.0f, 213.0f, 40.0f);
    ExpectQuadAt(quads[3], 0xfb, 214.0f, 169.0f, 213.0f, 3.0f);
    ExpectQuadAt(quads[4], 0xfc, 214.0f, 172.0f, 213.0f, 5.0f);
    for (std::size_t at = 0; at < 5; ++at)
    {
        SCOPED_TRACE(at);
        EXPECT_EQ(quads[at].blend, BlendMode::Alpha);
        EXPECT_FALSE(quads[at].alphaTest);
        EXPECT_FLOAT_EQ(quads[at].first.color.alpha, 0.8f);
        EXPECT_FLOAT_EQ(quads[at].third.u, 0.83203125f);
    }
    EXPECT_FLOAT_EQ(quads[3].first.v, 0.0f);
    EXPECT_NEAR(quads[3].third.v, 0.046875f, 1e-7f);
    EXPECT_NEAR(quads[2].third.v, 0.625f, 1e-7f);
    EXPECT_EQ(quads[5].slot, Marker);
    EXPECT_EQ(quads[6].slot, Marker);
    ExpectQuadAt(quads[7], Client::UI::WindowButtonSlot, 260.0f, 143.0f, 50.0f, 18.0f);
    ExpectQuadAt(quads[9], Client::UI::WindowButtonSlot, 330.0f, 143.0f, 50.0f, 18.0f);
    const auto& texts = m_text.texts;
    ASSERT_EQ(texts.size(), 4u);
    EXPECT_EQ(texts[0].text, "Una alianza");
    EXPECT_EQ(texts[0].x, 0x140 - 33);
    EXPECT_EQ(texts[0].y, 104);
    EXPECT_EQ(texts[1].text, "con Teros");
    EXPECT_EQ(texts[1].x, 0x140 - 27);
    EXPECT_EQ(texts[1].y, 119);
    for (std::size_t at = 0; at < 2; ++at)
    {
        EXPECT_EQ(texts[at].style.font, Client::Render::Text::FontKind::Normal);
        EXPECT_EQ(texts[at].style.colors.text, 0xffffffffu);
        EXPECT_EQ(texts[at].style.colors.background, 0u);
    }
    EXPECT_EQ(texts[2].text, "Va");
    EXPECT_EQ(texts[3].text, "Ni ahi");
    EXPECT_EQ(PanelDialog().buttons[Client::UI::PanelOkButton].label, "Dale");
    EXPECT_EQ(PanelDialog().buttons[Client::UI::PanelConfirmButton].base.y, 143);
    EXPECT_EQ(m_sprites.Blend(), BlendMode::Opaque);
    EXPECT_EQ(Draws(m_random), 0);
}

// Con +0xa24 la caja va en y = 10; un renglón: alto 15 + 0x3f = 78, una fila y el resto de 28 (dv 0,4375). Aceptar
// solo, en 0x127 e y = 30 + 15 + 9 = 54.
TEST_P(GuildDrawingTest, AtTheTopWithOneButton)
{
    Open({"Listo"}, Client::UI::PanelDialogOk, 1);
    ASSERT_TRUE(Dialog().HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 7u);
    ExpectQuadAt(quads[0], 0xfc, 214.0f, 10.0f, 213.0f, 5.0f);
    ExpectQuadAt(quads[1], 0xfb, 214.0f, 15.0f, 213.0f, 40.0f);
    ExpectQuadAt(quads[2], 0xfb, 214.0f, 55.0f, 213.0f, 28.0f);
    EXPECT_NEAR(quads[2].third.v, 0.4375f, 1e-7f);
    ExpectQuadAt(quads[3], 0xfc, 214.0f, 83.0f, 213.0f, 5.0f);
    ExpectQuadAt(quads[5], Client::UI::WindowButtonSlot, 295.0f, 54.0f, 50.0f, 18.0f);
    ASSERT_EQ(m_text.texts.size(), 2u);
    EXPECT_EQ(m_text.texts[0].y, 30);
    EXPECT_EQ(m_text.texts[1].text, "Dale");
}

// Cerrado no dibuja; el campo de texto y el reloj cortan con su dirección, y sin Text.bmd los botones también.
TEST_P(GuildDrawingTest, ThePanelDialogCutsWhatMuRetDoesNotOpen)
{
    ASSERT_TRUE(Dialog().HasValue());
    EXPECT_TRUE(QuadsOf(m_sprites).empty());
    Open({"Che"}, Client::UI::PanelDialogConfirm | Client::UI::PanelDialogTimed, 0);
    auto cut = Dialog();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x660ad6"), std::string::npos);
    PanelDialog().flags = Client::UI::PanelDialogConfirm | Client::UI::PanelDialogTextField;
    cut = Dialog();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x66097a"), std::string::npos);
    PanelDialog().flags = Client::UI::PanelDialogConfirm;
    cut = Dialog(false);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x65ff12"), std::string::npos);
}

INSTANTIATE_TEST_SUITE_P(Maps, GuildDrawingTest, ::testing::Values(0, 2, 6));
} // namespace
