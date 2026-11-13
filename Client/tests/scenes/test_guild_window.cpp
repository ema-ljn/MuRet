#include "Scenes/GuildWindow.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Network/Crypto/Xor32.h"
#include "Data/Text/TextTable.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Render/Text/FontSet.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GuildDrawing.h"
#include "Scenes/GuildMarks.h"
#include "Scenes/GuildPanels.h"
#include "Scenes/GuildState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatLog.h"
#include "UI/Dialogs.h"
#include "UI/MessageArea.h"
#include "UI/OldControls.h"
#include "UI/OldWindowBase.h"
#include "UI/PanelDialog.h"
#include "UI/PanelRegistry.h"

#include "support/FakeTextureStore.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// La ventana del gremio (CL9 hito 8, fases 4 y 5): 0x64f3e0 con su paso (0x45aa80(0) y 0x651840) y su dibujo
// (0x651c20): el marco, el título, el gremio con el puntaje o los textos sin gremio, el botón de cerrar, las pestañas y
// el pedido de la alianza (E9); y las tres pestañas con sus pasos (0x64fc00, 0x650350 y 0x650c10), sus dibujos
// (0x64fdf0, 0x6507d0 y 0x651400) y sus listas. En Lorencia (0) y en Devias (2). Los gremios, los nombres y los textos
// son inventados.
namespace
{
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::Models::CrtRandom;
using Client::Scenes::GameHudContext;
using Client::Scenes::GuildMarkTextures;
using Client::Scenes::GuildState;
using Client::Scenes::WorldState;

constexpr std::uint32_t Seed = 23;
constexpr Client::App::PixelSize Screen640{640, 480};
constexpr int NoTexture = -1;
constexpr int Marker = -2;
constexpr float Left = 450.0f;
constexpr std::int32_t WindowX = 0x1c2;
constexpr std::uint16_t InTheGuild = 0x3;
constexpr std::uint8_t MemberStatus = 0x00;
constexpr std::uint8_t MasterStatus = 0x80;
constexpr std::uint8_t BattleMasterStatus = 0x20;
constexpr std::uint32_t Now = 5000;
constexpr std::uint8_t PageUp = 0x21;
constexpr std::uint8_t PageDown = 0x22;
constexpr std::uint32_t ChosenText = 0xff000000u;
constexpr std::uint32_t RowText = 0xffc8dce6u;

std::vector<std::byte> BytesOf(std::initializer_list<int> values)
{
    std::vector<std::byte> bytes;
    for (const int value : values)
        bytes.push_back(static_cast<std::byte>(value));
    return bytes;
}

std::vector<std::byte> PlainOf(const Client::Protocol::Outgoing& sent)
{
    const auto plain = Client::Network::Crypto::Xor32Decode(sent.packet);
    return {plain.Bytes().begin(), plain.Bytes().end()};
}

struct DrawnQuad
{
    int slot = NoTexture;
    SpriteVertex first{};
    SpriteVertex third{};
};

// Los quads del lote, sin los marcadores de los textos.
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
        if (slot == Marker)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            quads.push_back(DrawnQuad{.slot = slot, .first = vertices[first], .third = vertices[first + 2]});
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

// Cuántos quads del lote van con `slot` en (x, y) de w × h.
std::size_t CountQuadsAt(const std::vector<DrawnQuad>& quads, int slot, float x, float y, float width, float height)
{
    constexpr float Tolerance = 1e-3f;
    return static_cast<std::size_t>(std::count_if(quads.begin(), quads.end(),
                                                  [&](const DrawnQuad& quad)
                                                  {
                                                      return quad.slot == slot &&
                                                             std::abs(quad.first.x - x) < Tolerance &&
                                                             std::abs(quad.first.y - (480.0f - y)) < Tolerance &&
                                                             std::abs(quad.third.x - (x + width)) < Tolerance &&
                                                             std::abs(quad.third.y - (480.0f - y - height)) < Tolerance;
                                                  }));
}

// El lugar del primer texto `text` desde `from`, o el final.
std::size_t FindText(const std::vector<Client::Tests::RecordedText>& texts, std::string_view text, std::size_t from = 0)
{
    for (std::size_t at = from; at < texts.size(); ++at)
    {
        if (texts[at].text == text)
            return at;
    }
    return texts.size();
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
    for (int draws = 0; draws <= 10000; ++draws)
    {
        if (reference.State() == random.State())
            return draws;
        static_cast<void>(reference.Next());
    }
    return -1;
}

Client::Data::Text::TextTable WindowTexts()
{
    std::map<std::size_t, std::string> texts{{180, "Gremio"},
                                             {228, "Aceptar"},
                                             {229, "Cancelar"},
                                             {182, "Gremios"},
                                             {185, "Sin gremio"},
                                             {186, "busca uno"},
                                             {187, "o arma otro"},
                                             {188, "Disolver"},
                                             {189, "Dejar"},
                                             {220, "Salir"},
                                             {946, "Aviso"},
                                             {1022, "Server"},
                                             {1256, "Puntos: %d"},
                                             {1270, "No deja el gremio"},
                                             {1271, "No deja la alianza"},
                                             {1300, "Jefe"},
                                             {1301, "Ayudante"},
                                             {1302, "Guerrero"},
                                             {1307, "Cargo"},
                                             {1308, "Quitar"},
                                             {1309, "Echar"},
                                             {1310, "Socios: %d"},
                                             {1311, "Hacerlo ayudante"},
                                             {1312, "Hacerlo guerrero"},
                                             {1314, "%s sera %s"},
                                             {1315, "Elegi el cargo"},
                                             {1321, "Aviso de"},
                                             {1323, "El aviso"},
                                             {1324, "Romperla"},
                                             {1325, "Dejarla"},
                                             {1330, "Miembros"},
                                             {1332, "Clan"},
                                             {1352, "Alianza"},
                                             {1361, "Nadie"},
                                             {1362, "Socios: %d/%d"},
                                             {1363, "Disolver 1"},
                                             {1364, "Disolver 2"},
                                             {1365, "Disolver 3"},
                                             {1366, "Disolver 4"},
                                             {1367, "Miembro: %s"},
                                             {1368, "pierde el cargo"},
                                             {1369, "se va del gremio"},
                                             {1387, "Dejaste la alianza"},
                                             {1389, "Nombre"},
                                             {1422, "Echarlo"},
                                             {1423, "Gremio: %s"}};
    for (std::size_t line = 0; line < 11; ++line)
        texts.emplace(1257 + line, std::format("Sin alianza {}", line));
    auto table = Client::Data::Text::LoadTextTable(Client::Tests::TextTableBytes(texts));
    EXPECT_TRUE(table.HasValue());
    return std::move(table).Value();
}

class GuildWindowTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    GuildWindowTest() : m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
        Runtime().map = GetParam();
        Hero().guildStatus = 0xff;
        Hero().word1f2 = 0xffff;
        Runtime().panels.delegated.guild = 1;
    }

    Client::Scenes::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    Client::Scenes::ActorState& Hero()
    {
        return m_world->Actors()[Runtime().hero];
    }

    GuildState& Guild()
    {
        return Runtime().guild;
    }

    // El héroe en el gremio 3 ("Talas", 17 puntos), con o sin alianza.
    void JoinTheGuild(std::string_view alliance)
    {
        Hero().guildStatus = MemberStatus;
        Hero().word1f2 = InTheGuild;
        Fill(Guild().table[InTheGuild].guildName, "Talas");
        Fill(Guild().table[InTheGuild].unionName, alliance);
        Guild().score = 17;
    }

    Client::Scenes::GuildSender Sender()
    {
        return [this](Client::Protocol::Outgoing packet) { m_sent.push_back(std::move(packet)); };
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Scenes::StepTheGuildWindow(Client::Scenes::GuildWindowStepContext{
            .world = *m_world,
            .mouseX = m_mouseX,
            .mouseY = m_mouseY,
            .screen = Screen640,
            .send = Sender(),
            .keyEdge =
                [this](std::uint8_t key)
            {
                const auto found = std::find(m_edges.begin(), m_edges.end(), key);
                if (found == m_edges.end())
                    return false;
                m_edges.erase(found);
                return true;
            },
            .keyDown = [](std::uint8_t) { return false; },
            .now = Now,
            .measure = &m_text,
            .texts = &m_texts});
    }

    // Un miembro del 52: en la tabla de 0x787a5e8 (en el orden del paquete) y en la lista (0x6517a0).
    void AddMember(std::string_view name, std::uint8_t status, std::uint8_t server = 0)
    {
        Client::Scenes::GuildMemberSlot& slot = Guild().members[static_cast<std::size_t>(Guild().memberCount)];
        Fill(slot.name, name);
        slot.status = status;
        slot.server = server;
        ++Guild().memberCount;
        Client::Scenes::AddTheGuildMember(*m_world, slot);
    }

    // Un gremio de la alianza del E9 (0x6517e0), con la marca de un solo color.
    void AddUnion(std::string_view name, std::uint8_t number, std::uint8_t color)
    {
        std::array<std::uint8_t, Client::Scenes::GuildMarkNibbles> mark{};
        mark.fill(color);
        Client::Scenes::AddTheUnion(Guild().window, mark, name, number);
    }

    // El aviso del 0D tipo 2 (0x651680).
    void AddNotice(std::string_view text)
    {
        ASSERT_TRUE(Client::Scenes::AddTheGuildNotice(Guild().window, m_text, Runtime().hud.dcFont, Screen640, text)
                        .HasValue());
    }

    std::string MemberAt(std::size_t at)
    {
        return std::string(Client::Scenes::GuildTextOf(Guild().window.members.at(at).slot.name));
    }

    // 0x45b200: el flanco sobre el botón y, el cuadro que sigue, soltarlo encima.
    Client::Core::Expected<void, std::string> Click(std::size_t button)
    {
        return ClickOn(Guild().window.memberButtons.at(button));
    }

    // Lo mismo sobre +0x42c, +0x64c o +0x6d4.
    Client::Core::Expected<void, std::string> ClickTheTabButton(std::size_t button)
    {
        return ClickOn(Guild().window.tabButtons.at(button));
    }

    Client::Core::Expected<void, std::string> ClickOn(const Client::UI::OldButton& target)
    {
        m_mouseX = target.base.x + target.base.width / 2;
        m_mouseY = target.base.y + target.base.height / 2;
        Client::Scenes::GameMovementState& movement = Runtime().movement;
        movement.pendingClick = true;
        movement.leftHeld = true;
        if (auto pressed = Step(); !pressed.HasValue())
            return pressed;
        movement.pendingClick = false;
        movement.leftHeld = false;
        movement.releasedClick = true;
        auto released = Step();
        movement.releasedClick = false;
        return released;
    }

    Client::Core::Expected<void, std::string> Draw()
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
                               .texts = &m_texts,
                               .mouse = {m_mouseX, m_mouseY},
                               .guildMarks = &m_marks};
        return Client::Scenes::DrawTheGuildWindow(context);
    }

    // 0x6605b0 por el registro (LendThePanelDialogStepTo), con la respuesta de LendTheGuildTo.
    Client::Core::Expected<void, std::string> StepTheDialog()
    {
        Client::UI::PanelContext panels{.random = m_random, .dialogs = Runtime().dialogs};
        Client::Scenes::LendTheGuildTo(panels, *m_world, Sender());
        Client::Scenes::LendThePanelDialogStepTo(panels, *m_world, Sender(), m_mouseX, m_mouseY, Now);
        return panels.panelDialogStep();
    }

    // Un clic en (x, y) del paso del diálogo: el flanco y el soltado en dos cuadros.
    Client::Core::Expected<void, std::string> ClickTheDialogAt(std::int32_t x, std::int32_t y)
    {
        m_mouseX = x;
        m_mouseY = y;
        Client::Scenes::GameMovementState& movement = Runtime().movement;
        movement.pendingClick = true;
        movement.leftHeld = true;
        if (auto pressed = StepTheDialog(); !pressed.HasValue())
            return pressed;
        movement.pendingClick = false;
        movement.leftHeld = false;
        movement.releasedClick = true;
        auto released = StepTheDialog();
        movement.releasedClick = false;
        return released;
    }

    Client::Core::Expected<void, std::string> ClickThePositionsButton(std::size_t button)
    {
        const Client::UI::OldButton& target = Guild().positions.buttons.at(button);
        return ClickTheDialogAt(target.base.x + target.base.width / 2, target.base.y + target.base.height / 2);
    }

    // 0x660830 (el último de 0x6579e0).
    Client::Core::Expected<void, std::string> DrawTheDialog()
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
                               .texts = &m_texts,
                               .mouse = {m_mouseX, m_mouseY},
                               .guildMarks = &m_marks};
        return Client::Scenes::DrawThePanelDialog(context, runtime.panels.delegated.panelDialog);
    }

    // El maestro elige a `name` (con su cargo) y aprieta el botón del cargo.
    void OpenThePositionsFor(std::string_view name, std::uint8_t status)
    {
        JoinTheGuild("");
        Hero().guildStatus = MasterStatus;
        AddMember(name, status);
        const auto clicked = Click(Client::Scenes::GuildAssignButton);
        ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    }

    // El mouse sobre la pestaña `tab` del paso (0x34 × 0x10 en x + 0xf, 0x43 y 0x77, y + 0x4b).
    void PointAtTheTab(std::int32_t tab)
    {
        constexpr std::array<std::int32_t, 3> Lefts{0xf, 0x43, 0x77};
        m_mouseX = WindowX + Lefts[static_cast<std::size_t>(tab)] + 1;
        m_mouseY = 0x4b + 1;
    }

    std::unique_ptr<WorldState> m_world = std::make_unique<WorldState>();
    CrtRandom m_random{Seed};
    Client::Data::Text::TextTable m_texts = WindowTexts();
    std::optional<std::int32_t> m_sound{};
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text;
    Client::Tests::FakeTextureStore m_store;
    GuildMarkTextures m_marks{m_store};
    std::vector<Client::Protocol::Outgoing> m_sent;
    std::vector<std::uint8_t> m_edges;
    std::int32_t m_mouseX = 0;
    std::int32_t m_mouseY = 300;
};

// 0x651c20 con el héroe sin gremio (+0x368 en 0xff): el fondo, el título en negrita y los textos 185 a 187 en la
// normal, de a 0xf, y el botón de cerrar; ni el puntaje ni las pestañas. El paso tampoco llega a las pestañas. Sin
// rand().
TEST_P(GuildWindowTest, WithoutAGuildItShowsTheThreeTextsAndNoTabs)
{
    ASSERT_TRUE(Step().HasValue());
    ASSERT_TRUE(Draw().HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 3U);
    ExpectQuadAt(quads[0], 0x104, Left, 0.0f, 190.0f, 256.0f);
    ExpectQuadAt(quads[1], 0x105, Left, 256.0f, 190.0f, 177.0f);
    ExpectQuadAt(quads[2], 0x118, Left + 25.0f, 395.0f, 24.0f, 24.0f);
    ASSERT_EQ(m_text.texts.size(), 4U);
    EXPECT_EQ(m_text.texts[0].text, "Gremio");
    EXPECT_EQ(m_text.texts[0].x, WindowX + 0x23);
    EXPECT_EQ(m_text.texts[0].y, 0xc);
    EXPECT_EQ(m_text.texts[0].style.colors.text, 0xffdcdcdcu);
    EXPECT_EQ(m_text.texts[0].style.colors.background, 0U);
    EXPECT_EQ(m_text.texts[0].style.font, Client::Render::Text::FontKind::Bold);
    const std::array<std::string_view, 3> lines{"Sin gremio", "busca uno", "o arma otro"};
    for (std::size_t line = 0; line < lines.size(); ++line)
    {
        SCOPED_TRACE(line);
        const auto& text = m_text.texts[line + 1];
        EXPECT_EQ(text.text, lines[line]);
        EXPECT_EQ(text.x, WindowX + 0x14);
        EXPECT_EQ(text.y, 0x32 + 0xf * static_cast<int>(line));
        EXPECT_EQ(text.style.colors.text, 0xffe6e6e6u);
        EXPECT_EQ(text.style.font, Client::Render::Text::FontKind::Normal);
    }
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(Draws(m_random), 0);
}

// Con gremio: "%s ( Score:%d )" en 0xff64ffc8 bajo el título, las rayas bajo las pestañas 0 y 2 (sin textura), las
// tres pestañas (la 1, la elegida desde 0x64f3e0, blanca y 2 más alta y más arriba; las otras en 0,6) con sus textos
// en y + 0x4b + 4 (la elegida, uno más arriba). La de los miembros (0x6507d0), sin miembros y sin ser maestro, no
// dibuja nada.
TEST_P(GuildWindowTest, WithAGuildItDrawsTheScoreAndTheTabs)
{
    JoinTheGuild("");
    const auto drawn = Draw();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 8U);
    ExpectQuadAt(quads[3], NoTexture, Left + 0xf, 0x5b, 54.0f, 1.0f);
    ExpectQuadAt(quads[4], NoTexture, Left + 0xf + 0x6c, 0x5b, 54.0f, 1.0f);
    EXPECT_FLOAT_EQ(quads[3].first.color.red, 146.0f / 255.0f);
    ExpectQuadAt(quads[5], 0x646, Left + 0xf, 0x4b, 52.0f, 16.0f);
    ExpectQuadAt(quads[6], 0x646, Left + 0xf + 0x36, 0x4b - 2, 52.0f, 18.0f);
    ExpectQuadAt(quads[7], 0x646, Left + 0xf + 0x6c, 0x4b, 52.0f, 16.0f);
    EXPECT_FLOAT_EQ(quads[5].first.color.red, 0.6f);
    EXPECT_FLOAT_EQ(quads[6].first.color.red, 1.0f);
    ASSERT_EQ(m_text.texts.size(), 5U);
    EXPECT_EQ(m_text.texts[1].text, "Talas ( Score:17 )");
    EXPECT_EQ(m_text.texts[1].x, WindowX + 0x23);
    EXPECT_EQ(m_text.texts[1].y, 0x35);
    EXPECT_EQ(m_text.texts[1].style.colors.text, 0xff64ffc8u);
    EXPECT_EQ(m_text.texts[2].text, "Aviso");
    EXPECT_EQ(m_text.texts[2].y, 0x4b + 4);
    EXPECT_EQ(m_text.texts[3].text, "Miembros");
    EXPECT_EQ(m_text.texts[3].y, 0x4b + 3);
    EXPECT_EQ(m_text.texts[4].text, "Alianza");
    EXPECT_EQ(m_text.texts[4].style.font, Client::Render::Text::FontKind::Bold);
    EXPECT_EQ(Draws(m_random), 0);
}

// Con +0x368 puesto y +0x1f2 en −1, el nombre sale del byte 9 del miembro 74 (Q-C1281); fuera de la tabla, corta.
TEST_P(GuildWindowTest, TheHerosGuildNameReadsBeforeTheTable)
{
    JoinTheGuild("");
    Hero().word1f2 = 0xffff;
    Guild().members[74].name[9] = 'x';
    static_cast<void>(Draw());
    ASSERT_GE(m_text.texts.size(), 2U);
    EXPECT_EQ(m_text.texts[1].text, "x ( Score:17 )");
    Hero().word1f2 = 2000;
    const auto cut = Draw();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x651da0"), std::string::npos) << cut.Error();
}

// 0x651895..0x6518e3: el clic en el botón de cerrar lo consume, deja 0x6c8dcc en 6 y cierra (0x6522f0: la pestaña 1 y
// el sonido 0x1c, después del 0x19 del clic).
TEST_P(GuildWindowTest, TheCloseButtonClosesTheWindow)
{
    Guild().window.tab = 2;
    m_mouseX = WindowX + 0x19 + 3;
    m_mouseY = 0x18b + 3;
    Runtime().movement.pendingClick = true;
    ASSERT_TRUE(Step().HasValue());
    EXPECT_FALSE(Runtime().movement.pendingClick);
    EXPECT_EQ(Runtime().movement.mouseUpdateTimeMax, 6);
    EXPECT_EQ(Runtime().panels.delegated.guild, 0);
    EXPECT_EQ(Guild().window.tab, 1);
    EXPECT_EQ(Runtime().lastSoundId, 0x1c);
    EXPECT_EQ(Runtime().ui.mouseOnUi, 1);
}

// 0x651907..0x651bb5: con el izquierdo abajo, la pestaña bajo el mouse. La de la alianza pide la lista (C1 03 E9 en
// claro) solo la primera vez y solo con alianza; después, el paso de la pestaña (0x650c10 y 0x64fc00, que sin ser
// maestro y sin el botón no hacen más que la lista).
TEST_P(GuildWindowTest, TheTabsSwitchAndTheUnionTabAsksForTheListOnce)
{
    JoinTheGuild("Ceibo");
    Runtime().movement.leftHeld = true;
    PointAtTheTab(2);
    auto stepped = Step();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Guild().window.tab, 2);
    EXPECT_EQ(Guild().window.unionRequested, 1);
    ASSERT_EQ(m_sent.size(), 1U);
    const auto bytes = m_sent[0].packet.Bytes();
    ASSERT_EQ(bytes.size(), 3U);
    EXPECT_EQ(bytes[0], std::byte{0xc1});
    EXPECT_EQ(bytes[1], std::byte{0x03});
    EXPECT_EQ(bytes[2], std::byte{0xe9});

    PointAtTheTab(0);
    stepped = Step();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Guild().window.tab, 0);

    PointAtTheTab(2);
    static_cast<void>(Step());
    EXPECT_EQ(Guild().window.tab, 2);
    EXPECT_EQ(m_sent.size(), 1U);

    // Sin el izquierdo abajo, la pestaña no cambia.
    Runtime().movement.leftHeld = false;
    PointAtTheTab(1);
    static_cast<void>(Step());
    EXPECT_EQ(Guild().window.tab, 2);
    EXPECT_EQ(Draws(m_random), 0);
}

// Sin alianza, la pestaña cambia y no sale el E9.
TEST_P(GuildWindowTest, WithoutAnAllianceTheUnionTabSendsNothing)
{
    JoinTheGuild("");
    Runtime().movement.leftHeld = true;
    PointAtTheTab(2);
    static_cast<void>(Step());
    EXPECT_EQ(Guild().window.tab, 2);
    EXPECT_EQ(Guild().window.unionRequested, 0);
    EXPECT_TRUE(m_sent.empty());
}

// 0x6517a0 → 0x46c930: cada miembro entra adelante; con la lista vacía, +0x130 mira si es el héroe. Al final, elegido
// el último del deque (el primero que entró) y el corrimiento hasta el final. 0x6517d0 la vacía, elige 0 y corre a 0.
TEST_P(GuildWindowTest, TheMemberListPutsEachNewMemberFirst)
{
    Hero().name = {std::byte{'S'}, std::byte{'a'}, std::byte{'u'}, std::byte{'c'}, std::byte{'e'}};
    AddMember("Sauce", BattleMasterStatus);
    EXPECT_EQ(Guild().window.heroFirst, 1);
    AddMember("Ombu", MemberStatus);
    AddMember("Tala", MemberStatus);
    ASSERT_EQ(Guild().window.members.size(), 3U);
    EXPECT_EQ(MemberAt(0), "Tala");
    EXPECT_EQ(MemberAt(1), "Ombu");
    EXPECT_EQ(MemberAt(2), "Sauce");
    EXPECT_EQ(Guild().window.memberList.selection, 3);
    EXPECT_EQ(Guild().window.memberList.scroll, 0);
    EXPECT_EQ(Guild().window.heroFirst, 1);

    Client::Scenes::ClearTheGuildMembers(Guild().window);
    EXPECT_TRUE(Guild().window.members.empty());
    EXPECT_EQ(Guild().window.memberList.selection, 0);
    AddMember("Ombu", MemberStatus);
    EXPECT_EQ(Guild().window.heroFirst, 0);
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x650350 con 20 miembros (18 renglones): entraron con el corrimiento al final (2). PgDn corre 18 hacia el principio y
// PgUp vuelve, acotados a [0, 2]. Cada flanco, una vez.
TEST_P(GuildWindowTest, PageUpAndPageDownScrollTheMemberList)
{
    JoinTheGuild("");
    for (int member = 0; member < 20; ++member)
        AddMember(std::format("Socio{}", member), MemberStatus);
    EXPECT_EQ(Guild().window.memberList.scroll, 2);
    EXPECT_EQ(Guild().window.memberList.selection, 20);
    m_edges = {PageDown};
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(Guild().window.memberList.scroll, 0);
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(Guild().window.memberList.scroll, 0);
    m_edges = {PageUp};
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(Guild().window.memberList.scroll, 2);
    EXPECT_EQ(Runtime().ui.capture, 0);
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x46d3a0: el clic sobre un renglón (de 0xd desde su y − 3) lo elige (corrimiento + renglón + 1), invierte +0 de su
// elemento y gasta el clic. El renglón 0 es el de abajo: con tres, en y (3 − 0) · 13 − 0xeb − 10 + 0x159 = 139.
TEST_P(GuildWindowTest, ClickingAMemberRowChoosesAndTogglesIt)
{
    JoinTheGuild("");
    AddMember("Sauce", BattleMasterStatus);
    AddMember("Ombu", MemberStatus);
    AddMember("Tala", MemberStatus);
    m_mouseX = 0x1d2 + 4;
    m_mouseY = 139 - 3 + 2;
    Runtime().movement.pendingClick = true;
    ASSERT_TRUE(Step().HasValue());
    EXPECT_FALSE(Runtime().movement.pendingClick);
    EXPECT_EQ(Guild().window.memberList.selection, 1);
    EXPECT_EQ(Guild().window.members[0].toggled, 1);
    EXPECT_EQ(Guild().window.members[1].toggled, 0);

    // El renglón 1, 13 más arriba.
    m_mouseY -= 13;
    Runtime().movement.pendingClick = true;
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(Guild().window.memberList.selection, 2);
    EXPECT_EQ(Guild().window.members[1].toggled, 1);
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x6504dc..0x650652 y 0x64def0: el maestro quita el cargo al elegido (0x20): el diálogo con Confirmar y Cancelar y el
// nombre en 0x7d535a8; Confirmar manda C1 0F E1 03 00 y el nombre con el XOR32, y C1 03 52 en claro.
TEST_P(GuildWindowTest, TheMasterDismissesTheChosenMember)
{
    JoinTheGuild("");
    Hero().guildStatus = MasterStatus;
    AddMember("Sauce", BattleMasterStatus);
    AddMember("Ombu", MemberStatus);
    ASSERT_EQ(Guild().window.memberList.selection, 2);
    const auto clicked = Click(Client::Scenes::GuildDismissButton);
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    const Client::UI::PanelDialogState& dialog = Runtime().panels.delegated.panelDialog;
    EXPECT_EQ(dialog.owner, Client::UI::PanelDialogOwner::GuildDismiss);
    EXPECT_EQ(dialog.flags, Client::UI::PanelDialogConfirm);
    ASSERT_EQ(dialog.lineCount, 2);
    EXPECT_EQ(dialog.lines[0], "Miembro: Sauce");
    EXPECT_EQ(dialog.lines[1], "pierde el cargo");
    EXPECT_EQ(Guild().window.dialog, Now);
    EXPECT_EQ(Client::Scenes::GuildTextOf(Guild().chosenName), "Sauce");
    EXPECT_EQ(Runtime().lastSoundId, 0x19);
    EXPECT_TRUE(m_sent.empty());

    auto answered =
        Client::Scenes::AnswerThePanelDialog(*m_world, Sender(), dialog.owner, Client::UI::PanelAnswerConfirm);
    ASSERT_TRUE(answered.HasValue());
    EXPECT_TRUE(answered.Value());
    ASSERT_EQ(m_sent.size(), 2U);
    EXPECT_EQ(PlainOf(m_sent[0]), BytesOf({0xc1, 0x0f, 0xe1, 0x03, 0x00, 'S', 'a', 'u', 'c', 'e', 0, 0, 0, 0, 0}));
    const auto list = m_sent[1].packet.Bytes();
    EXPECT_EQ(std::vector<std::byte>(list.begin(), list.end()), BytesOf({0xc1, 0x03, 0x52}));

    // Cancelar no manda nada.
    answered = Client::Scenes::AnswerThePanelDialog(*m_world, Sender(), Client::UI::PanelDialogOwner::GuildDismiss,
                                                    Client::UI::PanelAnswerCancel);
    ASSERT_TRUE(answered.HasValue());
    EXPECT_EQ(m_sent.size(), 2U);
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x650658..0x6507b5 y 0x64e550: echar al elegido busca su lugar en 0x787a5e8 (0x64faa0) y abre el diálogo; Confirmar
// abre el diálogo 0x7d del juego con ese lugar en 0x6d5e2c. A un 0x00 no se le quita el cargo.
TEST_P(GuildWindowTest, TheMasterKicksTheChosenMember)
{
    JoinTheGuild("");
    Hero().guildStatus = MasterStatus;
    AddMember("Sauce", BattleMasterStatus);
    AddMember("Ombu", MemberStatus);
    Client::UI::SelectInTheList(Guild().window.memberList, Guild().window.members.size(), 1);
    ASSERT_TRUE(Click(Client::Scenes::GuildDismissButton).HasValue());
    EXPECT_EQ(Runtime().panels.delegated.panelDialog.opened, 0U);

    const auto clicked = Click(Client::Scenes::GuildKickButton);
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    const Client::UI::PanelDialogState& dialog = Runtime().panels.delegated.panelDialog;
    EXPECT_EQ(dialog.owner, Client::UI::PanelDialogOwner::GuildKick);
    EXPECT_EQ(dialog.lines[0], "Miembro: Ombu");
    EXPECT_EQ(dialog.lines[1], "se va del gremio");
    EXPECT_EQ(Guild().kickIndex, 1);
    EXPECT_EQ(Guild().kickMember, -1);

    ASSERT_TRUE(Client::Scenes::AnswerThePanelDialog(*m_world, Sender(), dialog.owner, Client::UI::PanelAnswerConfirm)
                    .HasValue());
    EXPECT_EQ(Runtime().dialogs.current, 0x7d);
    EXPECT_EQ(Guild().kickMember, 1);
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x6503cf..0x6504d4: el cargo al elegido guarda su puesto y su nombre y abre el diálogo de los paneles vacío (0xd5 ×
// 0x2b, sin botones ni dueño) con los ganchos de la ventana de los cargos (0x6602b0), con lo que da en +0x428. Sin ser
// maestro, los botones no hacen nada. El que se abre después vuelve sin ganchos (0x660090).
TEST_P(GuildWindowTest, TheAssignButtonOpensThePositionsWindow)
{
    JoinTheGuild("");
    AddMember("Sauce", BattleMasterStatus);
    ASSERT_TRUE(Click(Client::Scenes::GuildAssignButton).HasValue());
    ASSERT_TRUE(Click(Client::Scenes::GuildKickButton).HasValue());
    Client::UI::PanelDialogState& dialog = Runtime().panels.delegated.panelDialog;
    EXPECT_EQ(dialog.opened, 0U);

    Hero().guildStatus = MasterStatus;
    const auto clicked = Click(Client::Scenes::GuildAssignButton);
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    EXPECT_EQ(Guild().chosenStatus, BattleMasterStatus);
    EXPECT_EQ(Client::Scenes::GuildTextOf(Guild().chosenName), "Sauce");
    EXPECT_EQ(dialog.opened, Now);
    EXPECT_EQ(Guild().window.dialog, Now);
    EXPECT_EQ(dialog.owner, Client::UI::PanelDialogOwner::None);
    EXPECT_EQ(dialog.hooks, Client::UI::PanelDialogHooks::GuildPositions);
    EXPECT_EQ(dialog.flags, 0U);
    EXPECT_EQ(dialog.lineCount, 0);
    EXPECT_EQ(dialog.width, 0xd5);
    EXPECT_EQ(dialog.height, 0x2b);
    EXPECT_TRUE(m_sent.empty());

    // Cerrar no borra los ganchos; abrir otro, sí.
    ASSERT_TRUE(Client::UI::CloseThePanelDialog(dialog).HasValue());
    EXPECT_EQ(dialog.hooks, Client::UI::PanelDialogHooks::GuildPositions);
    ASSERT_TRUE(Click(Client::Scenes::GuildDismissButton).HasValue());
    EXPECT_EQ(dialog.owner, Client::UI::PanelDialogOwner::GuildDismiss);
    EXPECT_EQ(dialog.hooks, Client::UI::PanelDialogHooks::None);
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x6600f9, el aviso del diálogo ocupado (0x6d69a8 en EUC-KR, los bytes de main.exe).
std::string BusyNotice()
{
    constexpr std::array<std::uint8_t, 26> Binary{0xc0, 0xcc, 0xb9, 0xcc, 0x20, 0xc6, 0xcb, 0xbe, 0xf7,
                                                  0xc3, 0xa2, 0xc0, 0xcc, 0x20, 0xb6, 0xb0, 0x20, 0xc0,
                                                  0xd6, 0xbd, 0xc0, 0xb4, 0xcf, 0xb4, 0xd9, 0x2e};
    std::string text;
    for (const std::uint8_t byte : Binary)
        text.push_back(static_cast<char>(byte));
    return text;
}

// 0x657930 da el paso del diálogo y después el de la ventana: con el de quitar el cargo abierto, el clic en echar llega
// a 0x6507ad, y 0x6600d0 (0x6600f7..0x66011d) da el aviso de 0x6d69a8 en el chat (0x555190 con el tipo 1), deja el
// abierto como estaba y devuelve 0, que va a +0x428. Sin cortar y sin rand().
TEST_P(GuildWindowTest, ASecondMasterButtonWithTheDialogOpenGetsTheBusyNotice)
{
    JoinTheGuild("");
    Hero().guildStatus = MasterStatus;
    AddMember("Sauce", BattleMasterStatus);
    ASSERT_TRUE(Click(Client::Scenes::GuildDismissButton).HasValue());
    const Client::UI::PanelDialogState& dialog = Runtime().panels.delegated.panelDialog;
    ASSERT_EQ(dialog.owner, Client::UI::PanelDialogOwner::GuildDismiss);
    ASSERT_EQ(Guild().window.dialog, Now);
    ASSERT_TRUE(Runtime().chat.log.entries.empty());
    Guild().kickIndex = 7;

    // 0x6507ad: el lugar de 0x64faa0 va a 0x7d536c8 antes de 0x6600d0.
    const auto clicked = Click(Client::Scenes::GuildKickButton);
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    ASSERT_EQ(Runtime().chat.log.entries.size(), 1U);
    EXPECT_EQ(Runtime().chat.log.entries.front().text, BusyNotice());
    EXPECT_EQ(Runtime().chat.log.entries.front().type, Client::UI::SystemMessage);
    EXPECT_EQ(Guild().window.dialog, 0U);
    EXPECT_EQ(Guild().kickIndex, 0);
    EXPECT_EQ(dialog.opened, Now);
    EXPECT_EQ(dialog.owner, Client::UI::PanelDialogOwner::GuildDismiss);
    EXPECT_EQ(dialog.lines[1], "pierde el cargo");
    EXPECT_EQ(dialog.hooks, Client::UI::PanelDialogHooks::None);
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x6504b6..0x6504d4 con otro abierto: 0x6600d0 da el aviso y 0 (a +0x428), y 0x6602b0 le pone igual los ganchos de
// la ventana de los cargos al abierto. Su paso y su dibujo pasan a ser los de los cargos (Aceptar manda el E1 y el 52)
// y su cierre responde a quien lo abrió (0x64def0 con Cancelar, 0x10: nada).
TEST_P(GuildWindowTest, TheAssignButtonHooksThePositionsOntoTheOpenDialog)
{
    JoinTheGuild("");
    Hero().guildStatus = MasterStatus;
    AddMember("Sauce", BattleMasterStatus);
    ASSERT_TRUE(Click(Client::Scenes::GuildDismissButton).HasValue());
    Client::UI::PanelDialogState& dialog = Runtime().panels.delegated.panelDialog;
    ASSERT_EQ(dialog.owner, Client::UI::PanelDialogOwner::GuildDismiss);

    const auto clicked = Click(Client::Scenes::GuildAssignButton);
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    ASSERT_EQ(Runtime().chat.log.entries.size(), 1U);
    EXPECT_EQ(Runtime().chat.log.entries.front().text, BusyNotice());
    EXPECT_EQ(Guild().window.dialog, 0U);
    EXPECT_EQ(Guild().chosenStatus, BattleMasterStatus);
    EXPECT_EQ(dialog.opened, Now);
    EXPECT_EQ(dialog.owner, Client::UI::PanelDialogOwner::GuildDismiss);
    EXPECT_EQ(dialog.flags, Client::UI::PanelDialogConfirm);
    EXPECT_EQ(dialog.hooks, Client::UI::PanelDialogHooks::GuildPositions);

    ASSERT_TRUE(DrawTheDialog().HasValue());
    Guild().positions.choice = Client::Scenes::AssistantChoice;
    const auto accepted = ClickThePositionsButton(Client::Scenes::GuildPositionsOkButton);
    ASSERT_TRUE(accepted.HasValue()) << accepted.Error();
    ASSERT_EQ(m_sent.size(), 2U);
    EXPECT_EQ(PlainOf(m_sent[0]), BytesOf({0xc1, 0x0f, 0xe1, 0x02, 0x40, 'S', 'a', 'u', 'c', 'e', 0, 0, 0, 0, 0}));
    const auto list = m_sent[1].packet.Bytes();
    EXPECT_EQ(std::vector<std::byte>(list.begin(), list.end()), BytesOf({0xc1, 0x03, 0x52}));
    EXPECT_EQ(dialog.opened, 0U);
    EXPECT_EQ(dialog.owner, Client::UI::PanelDialogOwner::None);
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x64f1c0: la caja de 0x96 en (0xd6, 0x37), que deja la esquina para el paso; las dos cajas de 170 × 20 en
// (x + 0x14, y + 0x13) y (x + 0x14, y + 0x27), rellena (0x621aa0) la del cargo elegido; los textos centrados en 170
// desde x + 0x14, con la fuente y los colores que había; y los dos botones en y + 0x75. Sin rand().
TEST_P(GuildWindowTest, ThePositionsWindowDrawsItsBoxesAndTexts)
{
    OpenThePositionsFor("Sauce", MemberStatus);
    Runtime().hud.textColors = Client::Render::Text::TextColors{.text = RowText, .background = 0};
    const auto drawn = DrawTheDialog();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(Guild().positions.x, 0xd6);
    EXPECT_EQ(Guild().positions.y, 0x37);

    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    EXPECT_EQ(CountQuadsAt(quads, 0xfc, 214.0f, 55.0f, 213.0f, 5.0f), 1U);
    EXPECT_EQ(CountQuadsAt(quads, 0xfb, 214.0f, 60.0f, 213.0f, 40.0f), 1U);
    EXPECT_EQ(CountQuadsAt(quads, 0xfb, 214.0f, 180.0f, 213.0f, 20.0f), 1U);
    EXPECT_EQ(CountQuadsAt(quads, 0xfc, 214.0f, 200.0f, 213.0f, 5.0f), 1U);
    // La del ayudante (0x40, el que deja 0x64f3e0), rellena; la del guerrero, no.
    EXPECT_EQ(CountQuadsAt(quads, NoTexture, 234.0f, 74.0f, 170.0f, 20.0f), 1U);
    EXPECT_EQ(CountQuadsAt(quads, 0x117, 234.0f, 74.0f, 170.0f, 2.0f), 1U);
    EXPECT_EQ(CountQuadsAt(quads, 0x104, 234.0f, 74.0f, 2.0f, 20.0f), 1U);
    EXPECT_EQ(CountQuadsAt(quads, NoTexture, 234.0f, 94.0f, 170.0f, 20.0f), 0U);
    EXPECT_EQ(CountQuadsAt(quads, 0x117, 234.0f, 94.0f, 170.0f, 2.0f), 1U);

    struct Line
    {
        std::string_view text;
        int y;
    };
    const std::array<Line, 4> lines{Line{"Hacerlo ayudante", 55 + 0x18}, Line{"Hacerlo guerrero", 55 + 0x2e},
                                    Line{"Sauce sera Ayudante", 55 + 0x4b}, Line{"Elegi el cargo", 55 + 0x5d}};
    std::size_t at = 0;
    for (const Line& line : lines)
    {
        SCOPED_TRACE(line.text);
        at = FindText(m_text.texts, line.text, at);
        ASSERT_LT(at, m_text.texts.size());
        const auto& text = m_text.texts[at];
        const int width = Client::Tests::RecordingTextDrawer::LetterWidth * static_cast<int>(line.text.size());
        EXPECT_EQ(text.x, 234);
        EXPECT_EQ(text.y, line.y);
        EXPECT_TRUE(text.windowed);
        EXPECT_EQ(text.firstColumn, -((170 - width) / 2));
        EXPECT_EQ(text.style.colors.text, RowText);
    }
    EXPECT_LT(FindText(m_text.texts, "Aceptar", at), m_text.texts.size());
    EXPECT_LT(FindText(m_text.texts, "Cancelar", at), m_text.texts.size());
    EXPECT_EQ(Guild().positions.buttons[Client::Scenes::GuildPositionsOkButton].base.x, 214 + 0x37);
    EXPECT_EQ(Guild().positions.buttons[Client::Scenes::GuildPositionsCancelButton].base.x, 214 + 0x6e);
    EXPECT_EQ(Guild().positions.buttons[Client::Scenes::GuildPositionsOkButton].base.y, 55 + 0x75);

    // Con el guerrero elegido, la rellena es la de abajo y el texto dice el 1302.
    Guild().positions.choice = Client::Scenes::BattleMasterChoice;
    m_text.texts.clear();
    m_sprites.Begin(Screen640);
    ASSERT_TRUE(DrawTheDialog().HasValue());
    const std::vector<DrawnQuad> again = QuadsOf(m_sprites);
    EXPECT_EQ(CountQuadsAt(again, NoTexture, 234.0f, 74.0f, 170.0f, 20.0f), 0U);
    EXPECT_EQ(CountQuadsAt(again, NoTexture, 234.0f, 94.0f, 170.0f, 20.0f), 1U);
    EXPECT_LT(FindText(m_text.texts, "Sauce sera Guerrero"), m_text.texts.size());
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x64e600: con el flanco del clic, las cajas eligen el cargo (0x40 o 0x20) desde la esquina del último dibujo;
// Aceptar manda C1 0F E1 01 (el elegido sin cargo), el cargo y los 10 bytes de 0x7d535a8, y C1 03 52, y cierra el
// diálogo. Sin rand().
TEST_P(GuildWindowTest, ThePositionsWindowAppointsAMember)
{
    OpenThePositionsFor("Sauce", MemberStatus);
    // Antes del primer dibujo la esquina es la de 0x64f3e0, (0, 0).
    ASSERT_TRUE(ClickTheDialogAt(0x14 + 2, 0x29 + 2).HasValue());
    EXPECT_EQ(Guild().positions.choice, Client::Scenes::BattleMasterChoice);
    ASSERT_TRUE(DrawTheDialog().HasValue());
    Runtime().ui.mouseOnUi = 0;
    ASSERT_TRUE(ClickTheDialogAt(234 + 2, 74 + 2).HasValue());
    EXPECT_EQ(Guild().positions.choice, Client::Scenes::AssistantChoice);
    EXPECT_EQ(Runtime().ui.mouseOnUi, 1);
    ASSERT_TRUE(ClickTheDialogAt(234 + 2, 55 + 0x29 + 2).HasValue());
    EXPECT_EQ(Guild().positions.choice, Client::Scenes::BattleMasterChoice);
    EXPECT_TRUE(m_sent.empty());
    ASSERT_NE(Runtime().panels.delegated.panelDialog.opened, 0U);

    const auto clicked = ClickThePositionsButton(Client::Scenes::GuildPositionsOkButton);
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    ASSERT_EQ(m_sent.size(), 2U);
    EXPECT_EQ(PlainOf(m_sent[0]), BytesOf({0xc1, 0x0f, 0xe1, 0x01, 0x20, 'S', 'a', 'u', 'c', 'e', 0, 0, 0, 0, 0}));
    const auto list = m_sent[1].packet.Bytes();
    EXPECT_EQ(std::vector<std::byte>(list.begin(), list.end()), BytesOf({0xc1, 0x03, 0x52}));
    EXPECT_EQ(Runtime().panels.delegated.panelDialog.opened, 0U);
    EXPECT_EQ(Runtime().lastSoundId, 0x19);
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x64eb53: con el elegido con cargo (0x7d535b4 distinto de 0), el E1 va con 2; los 10 bytes son los de 0x7d535a8
// después del strcpy (lo que sigue al 0 queda del nombre de antes, Q-C1292). Cancelar (0x7d53640) cierra sin mandar.
TEST_P(GuildWindowTest, ThePositionsWindowChangesAPositionOrCancels)
{
    Fill(Guild().chosenName, "Algarrobo");
    OpenThePositionsFor("Tala", BattleMasterStatus);
    ASSERT_TRUE(DrawTheDialog().HasValue());
    Guild().positions.choice = Client::Scenes::AssistantChoice;
    ASSERT_TRUE(ClickThePositionsButton(Client::Scenes::GuildPositionsOkButton).HasValue());
    ASSERT_EQ(m_sent.size(), 2U);
    EXPECT_EQ(PlainOf(m_sent[0]),
              BytesOf({0xc1, 0x0f, 0xe1, 0x02, 0x40, 'T', 'a', 'l', 'a', 0, 'r', 'o', 'b', 'o', 0}));

    m_sent.clear();
    ASSERT_TRUE(Click(Client::Scenes::GuildAssignButton).HasValue());
    ASSERT_NE(Runtime().panels.delegated.panelDialog.opened, 0U);
    const auto cancelled = ClickThePositionsButton(Client::Scenes::GuildPositionsCancelButton);
    ASSERT_TRUE(cancelled.HasValue()) << cancelled.Error();
    EXPECT_EQ(Runtime().panels.delegated.panelDialog.opened, 0U);
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x64fdf0 sin ser maestro: el marco de 53 con la marca de 48 del gremio del héroe (0x5f16a0(3, 1)), el marco de 140 ×
// 62 con el gremio, el puntaje, los miembros (el 1310) y quién escribió el aviso en cajas de 140 desde x + 0x1e, de a
// 0xd desde y + 0xb2; el título del aviso en y + 0xfa en una de 136; la lista del aviso de 0x88 × 0x50 colgada de
// y + 0x154, su renglón (el elegido, en negro) y el marco de 140 × 95; el botón +0x42c con el 189. Sin rand().
TEST_P(GuildWindowTest, TheNoticeTabDrawsTheGuildAndItsNotice)
{
    JoinTheGuild("");
    Guild().window.tab = 0;
    Fill(Guild().window.name88, "Ceibo");
    AddMember("Sauce", MemberStatus);
    AddMember("Ombu", MemberStatus);
    AddNotice("Hola socios");
    const auto drawn = Draw();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    EXPECT_EQ(CountQuadsAt(quads, 0x117, Left + 0x45, 0x68, 53.0f, 2.0f), 1U);
    EXPECT_EQ(CountQuadsAt(quads, GuildMarkTextures::SlotOf(InTheGuild, true).value, Left + 0x48, 0x6b, 48.0f, 48.0f),
              1U);
    EXPECT_EQ(CountQuadsAt(quads, 0x117, Left + 0x19, 0xaa, 140.0f, 2.0f), 1U);
    EXPECT_EQ(CountQuadsAt(quads, 0x104, Left + 0x19, 0xaa, 2.0f, 62.0f), 1U);
    EXPECT_EQ(CountQuadsAt(quads, 0x117, Left + 0x19, 0xf6, 140.0f, 2.0f), 1U);
    EXPECT_EQ(CountQuadsAt(quads, 0x104, Left + 0x19, 0xf6, 2.0f, 95.0f), 1U);

    const Client::UI::OldList& list = Guild().window.noticeList;
    EXPECT_EQ(list.base.x, WindowX + 0x1c);
    EXPECT_EQ(list.base.y, 0x104 + 0x50);
    EXPECT_EQ(list.base.width, 0x88);
    EXPECT_EQ(list.base.height, 0x50);

    ASSERT_EQ(m_text.texts.size(), 12U);
    const std::array<std::string_view, 4> lines{"Clan :", "Puntos: 17", "Socios: 2", "Aviso de : Ceibo"};
    for (std::size_t line = 0; line < lines.size(); ++line)
    {
        SCOPED_TRACE(line);
        const auto& text = m_text.texts[5 + line];
        EXPECT_EQ(text.text, lines[line]);
        EXPECT_EQ(text.x, WindowX + 0x1e);
        EXPECT_EQ(text.y, 0xb2 + 0xd * static_cast<int>(line));
        EXPECT_TRUE(text.windowed);
        EXPECT_EQ(text.firstColumn, 0);
        EXPECT_EQ(text.columns, 140);
    }
    EXPECT_EQ(m_text.texts[9].text, "El aviso");
    EXPECT_EQ(m_text.texts[9].y, 0xfa);
    EXPECT_EQ(m_text.texts[9].columns, 136);
    EXPECT_EQ(m_text.texts[10].text, "Hola socios");
    EXPECT_EQ(m_text.texts[10].x, WindowX + 0x1c + 4);
    EXPECT_EQ(m_text.texts[10].y, Client::UI::ListRowY(list, 1, 0));
    EXPECT_EQ(m_text.texts[10].style.colors.text, ChosenText);
    EXPECT_EQ(m_text.texts[11].text, "Dejar");
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x6500e8: el maestro abre su registro (0x57630c8) para el tope de miembros y al cerrarlo saca sus rand(): el nivel /
// 10, o con la clase 4 (+0xb & 7) el liderazgo / 10 más el nivel / 10, hasta 0x50. Sin quien escribió el aviso, el
// 1361; el botón, con el 188.
TEST_P(GuildWindowTest, TheMastersNoticeTabShowsTheMemberCapAndClosesTheRecord)
{
    JoinTheGuild("");
    Hero().guildStatus = MasterStatus;
    Guild().window.tab = 0;
    AddMember("Sauce", MasterStatus);
    AddMember("Ombu", MemberStatus);
    Client::Scenes::OwnCharacterRecord& record = m_world->OwnCharacter();
    record.characterClass = 1;
    record.level = 123;
    record.leadership = 700;
    ASSERT_TRUE(Draw().HasValue());
    ASSERT_EQ(m_text.texts.size(), 11U);
    EXPECT_EQ(m_text.texts[7].text, "Socios: 2/12");
    EXPECT_EQ(m_text.texts[8].text, "Aviso de : Nadie");
    EXPECT_EQ(m_text.texts.back().text, "Disolver");
    EXPECT_EQ(Draws(m_random), Client::Render::Models::OwnRecordCloseRandoms);

    m_text.texts.clear();
    record.characterClass = 0x14;
    record.level = 400;
    ASSERT_TRUE(Draw().HasValue());
    ASSERT_GE(m_text.texts.size(), 8U);
    EXPECT_EQ(m_text.texts[7].text, "Socios: 2/80");
    EXPECT_EQ(Draws(m_random), 2 * Client::Render::Models::OwnRecordCloseRandoms);
}

// 0x6507d0 con el maestro: la lista de los miembros (0x47a810 con 0x46cb20 y 0x46d050): los encabezados 1389, 1307 y
// 1022 en y − alto − 0xc; cada renglón con su nombre en x + 8, el puesto en su color (el 0x20, 0xc8 y 0x64 con alfa
// 0x7f) y su texto centrado en 0x46 desde x + 8 + 0x2d, y el servidor + 1 en 0xff00c4ff; el elegido en negro. Después,
// los tres botones.
TEST_P(GuildWindowTest, TheMembersTabDrawsTheRowsAndTheMasterButtons)
{
    JoinTheGuild("");
    Hero().guildStatus = MasterStatus;
    AddMember("Sauce", BattleMasterStatus, 0);
    AddMember("Ombu", MemberStatus, 0xff);
    const auto drawn = Draw();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    const Client::UI::OldList& list = Guild().window.memberList;
    const auto& texts = m_text.texts;
    ASSERT_GE(texts.size(), 8U);
    const std::array<std::string_view, 3> headers{"Nombre", "Cargo", "Server"};
    const std::array<int, 3> lefts{0xe, 0x41, 0x6a};
    for (std::size_t header = 0; header < headers.size(); ++header)
    {
        SCOPED_TRACE(header);
        EXPECT_EQ(texts[5 + header].text, headers[header]);
        EXPECT_EQ(texts[5 + header].x, 0x1d2 + lefts[header]);
        EXPECT_EQ(texts[5 + header].y, 0x159 - 0xeb - 0xc);
    }

    const std::size_t sauce = FindText(texts, "Sauce");
    ASSERT_LT(sauce + 2, texts.size());
    const int sauceY = Client::UI::ListRowY(list, 2, 1);
    EXPECT_EQ(texts[sauce].x, 0x1d2 + 8);
    EXPECT_EQ(texts[sauce].y, sauceY);
    EXPECT_EQ(texts[sauce].style.colors.text, ChosenText);
    EXPECT_EQ(texts[sauce + 1].text, "Guerrero");
    EXPECT_EQ(texts[sauce + 1].x, 0x1d2 + 8 + 0x2d);
    EXPECT_TRUE(texts[sauce + 1].windowed);
    EXPECT_EQ(texts[sauce + 1].firstColumn, -11);
    EXPECT_EQ(texts[sauce + 1].columns, 70);
    EXPECT_EQ(texts[sauce + 2].text, "1");
    EXPECT_EQ(texts[sauce + 2].x, 0x1d2 + 0x9d - 0x1e);
    EXPECT_EQ(texts[sauce + 2].style.colors.text, 0xff00c4ffu);

    const std::size_t ombu = FindText(texts, "Ombu");
    ASSERT_LT(ombu + 1, texts.size());
    EXPECT_EQ(texts[ombu].y, Client::UI::ListRowY(list, 2, 0));
    EXPECT_EQ(texts[ombu].style.colors.text, RowText);
    EXPECT_NE(texts[ombu + 1].y, texts[ombu].y);

    ASSERT_GE(texts.size(), 3U);
    EXPECT_EQ(texts[texts.size() - 3].text, "Cargo");
    EXPECT_EQ(texts[texts.size() - 2].text, "Quitar");
    EXPECT_EQ(texts[texts.size() - 1].text, "Echar");

    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    const auto band = std::find_if(quads.begin(), quads.end(),
                                   [](const DrawnQuad& quad)
                                   {
                                       return quad.slot == NoTexture &&
                                              std::abs(quad.first.color.green - 200.0f / 255.0f) < 1e-4f &&
                                              std::abs(quad.first.color.blue - 100.0f / 255.0f) < 1e-4f;
                                   });
    ASSERT_NE(band, quads.end());
    EXPECT_NEAR(band->first.x, 466.0f, 1e-3f);
    EXPECT_NEAR(band->first.y, 480.0f - static_cast<float>(sauceY - 3), 1e-3f);
    EXPECT_NEAR(band->first.color.alpha, 127.0f / 255.0f, 1e-4f);
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x651400 sin alianza (+4 de la entrada del héroe vacía): los textos 1257 a 1267 en x + 0x19 desde y + 0x6a; desde el
// sexto, en blanco sobre 0xff0000cc, que queda.
TEST_P(GuildWindowTest, WithoutAnAllianceTheUnionTabExplainsIt)
{
    JoinTheGuild("");
    Guild().window.tab = 2;
    ASSERT_TRUE(Draw().HasValue());
    ASSERT_EQ(m_text.texts.size(), 16U);
    const std::array<int, 11> tops{0x6a, 0x79, 0x88, 0x97, 0xa6, 0xbf, 0xce, 0xdd, 0xf1, 0x100, 0x10f};
    for (std::size_t line = 0; line < tops.size(); ++line)
    {
        SCOPED_TRACE(line);
        const auto& text = m_text.texts[5 + line];
        EXPECT_EQ(text.text, std::format("Sin alianza {}", line));
        EXPECT_EQ(text.x, WindowX + 0x19);
        EXPECT_EQ(text.y, tops[line]);
        if (line >= 5)
        {
            EXPECT_EQ(text.style.colors.text, 0xffffffffu);
            EXPECT_EQ(text.style.colors.background, 0xff0000ccu);
        }
    }
    EXPECT_NE(m_text.texts[9].style.colors.background, 0xff0000ccu);
    EXPECT_EQ(Runtime().hud.textColors.background, 0xff0000ccu);
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x651400 con alianza: la lista +0x2f8 de 0xa0 × 0x50 en x + 0xf, colgada de y + 0x6e más el alto de la lista del
// aviso, que es el 100 de 0x45a210 hasta el primer dibujo de la pestaña 0 (Q-C1293); cada renglón (0x46dfa0) con la
// marca de 8 × 8 por la entrada 1999 (que después vuelve a la del gremio del héroe, Q-C1294), el nombre en x + 0x10 y
// el número a la derecha de x + 0x8e. El maestro de la alianza ve +0x6d4 (1422) y +0x64c con el 1324; el otro maestro,
// +0x64c con el 1325 y +0x6d4 apagado.
TEST_P(GuildWindowTest, TheUnionTabListsTheGuildsAndTheMasterButtons)
{
    JoinTheGuild("Talas");
    Hero().guildStatus = MasterStatus;
    Guild().table[InTheGuild].mark.fill(5);
    Guild().window.tab = 2;
    AddUnion("Talas", 3, 1);
    AddUnion("Ombues", 12, 2);
    auto drawn = Draw();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    const Client::UI::OldList& list = Guild().window.unionList;
    EXPECT_EQ(list.base.x, WindowX + 0xf);
    EXPECT_EQ(list.base.y, 0x6e + 100);
    EXPECT_EQ(list.base.width, 0xa0);
    EXPECT_EQ(list.base.height, 0x50);

    const auto& texts = m_text.texts;
    ASSERT_GE(texts.size(), 7U);
    EXPECT_EQ(texts[5].text, "Gremios");
    EXPECT_EQ(texts[5].x, WindowX + 0xf + 0xf);
    EXPECT_EQ(texts[5].y, 0x6e + 100 - 0x50 - 0xc);
    EXPECT_EQ(texts[6].text, "Miembros");
    EXPECT_EQ(texts[6].x, WindowX + 0xf + 0x71);

    const std::size_t talas = FindText(texts, "Talas");
    ASSERT_LT(talas + 1, texts.size());
    const int talasY = Client::UI::ListRowY(list, 2, 1);
    EXPECT_EQ(texts[talas].x, WindowX + 0xf + 4 + 0xc);
    EXPECT_EQ(texts[talas].y, talasY);
    EXPECT_EQ(texts[talas].style.colors.text, ChosenText);
    EXPECT_EQ(texts[talas + 1].text, "3");
    EXPECT_EQ(texts[talas + 1].x, WindowX + 0xf + 4 + 0x8a - 6);
    const std::size_t ombues = FindText(texts, "Ombues");
    ASSERT_LT(ombues + 1, texts.size());
    EXPECT_EQ(texts[ombues].style.colors.text, RowText);
    EXPECT_EQ(texts[ombues + 1].text, "12");
    EXPECT_EQ(texts[ombues + 1].x, WindowX + 0xf + 4 + 0x8a - 12);

    // P-C289: cada renglón con su slot y su marca (Ombues, el 2 gris, en el 0; Talas, el 1 negro, en el 1); nada en el
    // de (1999, 1).
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    const int firstRow = Client::Render::FirstGuildUnionMarkTextureSlot.value;
    EXPECT_EQ(CountQuadsAt(quads, firstRow + 1, static_cast<float>(WindowX + 0xf + 4), static_cast<float>(talasY), 8.0f,
                           8.0f),
              1U);
    EXPECT_EQ(CountQuadsAt(quads, firstRow, static_cast<float>(WindowX + 0xf + 4),
                           static_cast<float>(Client::UI::ListRowY(list, 2, 0)), 8.0f, 8.0f),
              1U);
    EXPECT_FALSE(m_store.Find(GuildMarkTextures::SlotOf(Client::Scenes::CreatingGuildIndex, true)).has_value());
    const auto ombuesMark = m_store.Find(Client::Render::TextureSlot{static_cast<std::uint16_t>(firstRow)});
    const auto talasMark = m_store.Find(Client::Render::TextureSlot{static_cast<std::uint16_t>(firstRow + 1)});
    ASSERT_TRUE(ombuesMark.has_value());
    ASSERT_TRUE(talasMark.has_value());
    EXPECT_EQ(ombuesMark->texture.pixels[0], 0x80);
    EXPECT_EQ(talasMark->texture.pixels[0], 0);
    EXPECT_EQ(talasMark->texture.pixels[3], 0xff);
    EXPECT_EQ(Guild().table[static_cast<std::size_t>(Client::Scenes::CreatingGuildIndex)].mark,
              Guild().table[InTheGuild].mark);

    const Client::Scenes::GuildWindowState& window = Guild().window;
    EXPECT_EQ(texts[texts.size() - 2].text, "Echarlo");
    EXPECT_EQ(texts[texts.size() - 1].text, "Romperla");
    EXPECT_EQ(window.tabButtons[Client::Scenes::GuildUnionKickButton].base.state, Client::UI::ShownWindow);

    EXPECT_EQ(Draws(m_random), 0);

    // Después de la pestaña 0 (que, con el maestro, abre y cierra el registro propio), la lista cuelga del alto 0x50.
    Guild().window.tab = 0;
    ASSERT_TRUE(Draw().HasValue());
    Guild().window.tab = 2;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(list.base.y, 0x6e + 0x50);

    // Con otra alianza: +0x64c con el 1325 y +0x6d4 apagado, sin dibujarlo.
    Fill(Guild().table[InTheGuild].unionName, "Ceibo");
    m_text.texts.clear();
    ASSERT_TRUE(Draw().HasValue());
    ASSERT_FALSE(m_text.texts.empty());
    EXPECT_EQ(m_text.texts.back().text, "Dejarla");
    EXPECT_EQ(FindText(m_text.texts, "Echarlo"), m_text.texts.size());
    EXPECT_EQ(window.tabButtons[Client::Scenes::GuildUnionKickButton].base.state, Client::UI::DisabledWindow);
    EXPECT_EQ(Draws(m_random), Client::Render::Models::OwnRecordCloseRandoms);
}

// 0x64fc00: el maestro del gremio que es el de la alianza (+0xd igual a +4) recibe el aviso 1270 con OK y sin función.
TEST_P(GuildWindowTest, TheAllianceMasterCannotLeaveTheGuild)
{
    JoinTheGuild("Talas");
    Hero().guildStatus = MasterStatus;
    Guild().window.tab = 0;
    const auto clicked = ClickTheTabButton(Client::Scenes::GuildLeaveButton);
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    const Client::UI::PanelDialogState& dialog = Runtime().panels.delegated.panelDialog;
    EXPECT_EQ(dialog.owner, Client::UI::PanelDialogOwner::None);
    EXPECT_EQ(dialog.flags, Client::UI::PanelDialogOk);
    ASSERT_EQ(dialog.lineCount, 1);
    EXPECT_EQ(dialog.lines[0], "No deja el gremio");
    EXPECT_EQ(Guild().window.dialog, Now);
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_TRUE(m_sent.empty());
}

// 0x64fce7 y 0x64de90: el otro maestro abre el diálogo de disolver (1363 a 1366, Confirmar); Confirmar abre el diálogo
// 0x7d del juego con el lugar 0 en 0x6d5e2c, el campo del chat cerrado y un solo campo para Tab. Cancelar, nada.
TEST_P(GuildWindowTest, TheMasterDisbandsTheGuildThroughTheNumberDialog)
{
    JoinTheGuild("");
    Hero().guildStatus = MasterStatus;
    Guild().window.tab = 0;
    ASSERT_TRUE(ClickTheTabButton(Client::Scenes::GuildLeaveButton).HasValue());
    const Client::UI::PanelDialogState& dialog = Runtime().panels.delegated.panelDialog;
    EXPECT_EQ(dialog.owner, Client::UI::PanelDialogOwner::GuildDisband);
    EXPECT_EQ(dialog.flags, Client::UI::PanelDialogConfirm);
    ASSERT_EQ(dialog.lineCount, 4);
    for (std::size_t line = 0; line < 4; ++line)
        EXPECT_EQ(dialog.lines[line], std::format("Disolver {}", line + 1));

    auto answered =
        Client::Scenes::AnswerThePanelDialog(*m_world, Sender(), dialog.owner, Client::UI::PanelAnswerCancel);
    ASSERT_TRUE(answered.HasValue());
    EXPECT_EQ(Runtime().dialogs.current, 0);

    Runtime().ui.chatOpen = true;
    answered = Client::Scenes::AnswerThePanelDialog(*m_world, Sender(), dialog.owner, Client::UI::PanelAnswerConfirm);
    ASSERT_TRUE(answered.HasValue());
    EXPECT_TRUE(answered.Value());
    EXPECT_EQ(Runtime().dialogs.current, 0x7d);
    EXPECT_EQ(Guild().kickMember, 0);
    EXPECT_FALSE(Runtime().ui.chatOpen);
    EXPECT_EQ(Runtime().chat.input.fieldCount, 1);
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x64fd9c: el que no es maestro sale: su lugar en 0x787a5e8 (0x64faa0 con su nombre) en 0x6d5e2c y el diálogo 0x7d.
TEST_P(GuildWindowTest, AMemberLeavesTheGuildThroughTheNumberDialog)
{
    JoinTheGuild("");
    Hero().name = {std::byte{'O'}, std::byte{'m'}, std::byte{'b'}, std::byte{'u'}};
    Guild().window.tab = 0;
    AddMember("Sauce", MasterStatus);
    AddMember("Ombu", MemberStatus);
    ASSERT_TRUE(ClickTheTabButton(Client::Scenes::GuildLeaveButton).HasValue());
    EXPECT_EQ(Guild().kickMember, 1);
    EXPECT_EQ(Runtime().dialogs.current, 0x7d);
    EXPECT_EQ(Runtime().chat.input.fieldCount, 1);
    EXPECT_EQ(Runtime().panels.delegated.panelDialog.opened, 0U);
    EXPECT_TRUE(m_sent.empty());
}

// 0x650d22: el maestro de la alianza no la deja (1271, OK).
TEST_P(GuildWindowTest, TheAllianceMasterCannotLeaveTheAlliance)
{
    JoinTheGuild("Talas");
    Hero().guildStatus = MasterStatus;
    Guild().window.tab = 2;
    ASSERT_TRUE(ClickTheTabButton(Client::Scenes::GuildUnionLeaveButton).HasValue());
    const Client::UI::PanelDialogState& dialog = Runtime().panels.delegated.panelDialog;
    EXPECT_EQ(dialog.flags, Client::UI::PanelDialogOk);
    ASSERT_EQ(dialog.lineCount, 1);
    EXPECT_EQ(dialog.lines[0], "No deja la alianza");
    EXPECT_TRUE(m_sent.empty());
}

// 0x650d35..0x651290: el otro maestro manda C1 07 E5 01 02 con su clave (+0x1f5 y +0x1f4) con el XOR32 y avisa el 1387
// en el chat (0x555190 con el tipo 1).
TEST_P(GuildWindowTest, AMasterLeavesTheAlliance)
{
    JoinTheGuild("Ceibo");
    Hero().guildStatus = MasterStatus;
    Hero().key = 0x1234;
    Guild().window.tab = 2;
    const auto clicked = ClickTheTabButton(Client::Scenes::GuildUnionLeaveButton);
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    ASSERT_EQ(m_sent.size(), 1U);
    EXPECT_EQ(PlainOf(m_sent[0]), BytesOf({0xc1, 0x07, 0xe5, 0x01, 0x02, 0x12, 0x34}));
    ASSERT_FALSE(Runtime().chat.log.entries.empty());
    EXPECT_EQ(Runtime().chat.log.entries.back().text, "Dejaste la alianza");
    EXPECT_EQ(Runtime().chat.log.entries.back().type, Client::UI::SystemMessage);
    EXPECT_EQ(Runtime().panels.delegated.panelDialog.opened, 0U);
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x6512a2..0x6513e0 y 0x650830: +0x6d4 sobre otro gremio de la alianza copia su nombre a 0x7d535a8 (strcpy: lo de
// después del 0 queda del nombre de antes, Q-C1292) y abre el diálogo del 1423 y el 1369; Confirmar manda C1 0C EB 01 y
// los 8 bytes de 0x7d535a8 con el XOR32. Sobre el gremio propio, nada.
TEST_P(GuildWindowTest, TheAllianceMasterKicksAGuildOutOfIt)
{
    JoinTheGuild("Talas");
    Hero().guildStatus = MasterStatus;
    Guild().window.tab = 2;
    Fill(Guild().chosenName, "Sauceda1234");
    AddUnion("Talas", 3, 1);
    AddUnion("Ombues", 12, 2);
    Client::UI::SelectInTheList(Guild().window.unionList, Guild().window.unions.size(), 1);
    const auto clicked = ClickTheTabButton(Client::Scenes::GuildUnionKickButton);
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    const Client::UI::PanelDialogState& dialog = Runtime().panels.delegated.panelDialog;
    EXPECT_EQ(dialog.owner, Client::UI::PanelDialogOwner::UnionKick);
    EXPECT_EQ(dialog.flags, Client::UI::PanelDialogConfirm);
    ASSERT_EQ(dialog.lineCount, 2);
    EXPECT_EQ(dialog.lines[0], "Gremio: Ombues");
    EXPECT_EQ(dialog.lines[1], "se va del gremio");
    const std::array<char, 12> chosen{'O', 'm', 'b', 'u', 'e', 's', '\0', '1', '2', '3', '4', '\0'};
    EXPECT_EQ(Guild().chosenName, chosen);
    EXPECT_TRUE(m_sent.empty());

    auto answered =
        Client::Scenes::AnswerThePanelDialog(*m_world, Sender(), dialog.owner, Client::UI::PanelAnswerCancel);
    ASSERT_TRUE(answered.HasValue());
    EXPECT_TRUE(m_sent.empty());
    answered = Client::Scenes::AnswerThePanelDialog(*m_world, Sender(), dialog.owner, Client::UI::PanelAnswerConfirm);
    ASSERT_TRUE(answered.HasValue());
    EXPECT_TRUE(answered.Value());
    ASSERT_EQ(m_sent.size(), 1U);
    EXPECT_EQ(PlainOf(m_sent[0]), BytesOf({0xc1, 0x0c, 0xeb, 0x01, 'O', 'm', 'b', 'u', 'e', 's', 0, '1'}));

    Runtime().panels.delegated.panelDialog = Client::UI::PanelDialogState{};
    Client::UI::SelectInTheList(Guild().window.unionList, Guild().window.unions.size(), 2);
    ASSERT_TRUE(ClickTheTabButton(Client::Scenes::GuildUnionKickButton).HasValue());
    EXPECT_EQ(Runtime().panels.delegated.panelDialog.opened, 0U);
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x650c5a y 0x650c7b: sin ser maestro, o sin alianza, los botones de la pestaña no hacen nada.
TEST_P(GuildWindowTest, OnlyAMasterWithAnAllianceHasTheUnionButtons)
{
    JoinTheGuild("Talas");
    Guild().window.tab = 2;
    AddUnion("Ombues", 12, 2);
    ASSERT_TRUE(ClickTheTabButton(Client::Scenes::GuildUnionLeaveButton).HasValue());
    ASSERT_TRUE(ClickTheTabButton(Client::Scenes::GuildUnionKickButton).HasValue());
    Hero().guildStatus = MasterStatus;
    Fill(Guild().table[InTheGuild].unionName, "");
    ASSERT_TRUE(ClickTheTabButton(Client::Scenes::GuildUnionLeaveButton).HasValue());
    ASSERT_TRUE(ClickTheTabButton(Client::Scenes::GuildUnionKickButton).HasValue());
    EXPECT_EQ(Runtime().panels.delegated.panelDialog.opened, 0U);
    EXPECT_TRUE(m_sent.empty());
}

// 0x651680: el aviso cortado en renglones de 0x6e (0x459c70), cada uno adelante (0x46bca0), que lo corre hasta el
// final (la cuenta menos los renglones), y al final corrido otro tanto para el otro lado (+0x34 con la cuenta menos los
// renglones): el corrimiento 0, con lo último que entró en el renglón de abajo. 0x651790 la vacía.
TEST_P(GuildWindowTest, TheNoticeLinesGoFirstAndTheLastOnesStayInView)
{
    for (int notice = 1; notice <= 8; ++notice)
        AddNotice(std::format("Aviso {}", notice));
    const Client::Scenes::GuildWindowState& window = Guild().window;
    ASSERT_EQ(window.notice.size(), 8U);
    EXPECT_EQ(window.notice[0].text, "Aviso 8");
    EXPECT_EQ(window.notice[7].text, "Aviso 1");
    EXPECT_EQ(window.noticeList.selection, 8);
    EXPECT_EQ(window.noticeList.scroll, 0);

    const std::string_view text = "uno dos tres cuatro cinco seis siete";
    const auto wrapped = Client::UI::WrapByWidth(text, m_text, Runtime().hud.dcFont, Screen640, 0x6e, 5, 0);
    ASSERT_TRUE(wrapped.HasValue());
    std::vector<std::string> lines;
    for (const std::string& line : wrapped.Value().lines)
    {
        if (!line.empty())
            lines.push_back(line);
    }
    ASSERT_GE(lines.size(), 2U);
    AddNotice(text);
    ASSERT_EQ(window.notice.size(), 8U + lines.size());
    for (std::size_t line = 0; line < lines.size(); ++line)
        EXPECT_EQ(window.notice[line].text, lines[lines.size() - 1 - line]);

    Client::Scenes::ClearTheGuildNotice(Guild().window);
    EXPECT_TRUE(window.notice.empty());
    EXPECT_EQ(window.noticeList.selection, 0);
    EXPECT_EQ(window.noticeList.scroll, 0);
}

// 0x6517e0 → 0x46d900: sin nombre, nada; el nombre con strncpy de 8 y el número, adelante. 0x651830 la vacía.
TEST_P(GuildWindowTest, TheUnionListPutsEachGuildFirst)
{
    AddUnion("", 1, 1);
    EXPECT_TRUE(Guild().window.unions.empty());
    AddUnion("Talas", 3, 1);
    AddUnion("Ombues", 12, 2);
    const Client::Scenes::GuildWindowState& window = Guild().window;
    ASSERT_EQ(window.unions.size(), 2U);
    EXPECT_EQ(Client::Scenes::GuildTextOf(window.unions[0].name), "Ombues");
    EXPECT_EQ(window.unions[0].byte0, 12);
    EXPECT_EQ(window.unions[0].mark[0], 2);
    EXPECT_EQ(Client::Scenes::GuildTextOf(window.unions[1].name), "Talas");
    EXPECT_EQ(window.unionList.selection, 2);
    Client::Scenes::ClearTheUnions(Guild().window);
    EXPECT_TRUE(window.unions.empty());
    EXPECT_EQ(window.unionList.selection, 0);
}

INSTANTIATE_TEST_SUITE_P(Maps, GuildWindowTest, ::testing::Values(0, 2));
} // namespace
