#include "Scenes/GuildWindow.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Network/Crypto/Xor32.h"
#include "Data/Text/TextTable.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GuildPanels.h"
#include "Scenes/GuildState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Dialogs.h"
#include "UI/OldControls.h"
#include "UI/OldWindowBase.h"
#include "UI/PanelDialog.h"

#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
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

// La ventana del gremio (CL9 hito 8, fase 4): 0x64f3e0 con su paso (0x45aa80(0) y 0x651840) y su dibujo (0x651c20): el
// marco, el título, el gremio con el puntaje o los textos sin gremio, el botón de cerrar, las pestañas y el pedido de
// la alianza (E9). Los pasos y los dibujos de las pestañas cortan con su dirección (P-C287). En Lorencia (0) y en
// Devias (2). Los gremios y los textos son inventados.
namespace
{
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::Models::CrtRandom;
using Client::Scenes::GameHudContext;
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

Client::Data::Text::TextTable WindowTexts()
{
    const std::map<std::size_t, std::string> texts{{180, "Gremio"},
                                                   {185, "Sin gremio"},
                                                   {186, "busca uno"},
                                                   {187, "o arma otro"},
                                                   {220, "Salir"},
                                                   {946, "Aviso"},
                                                   {1330, "Miembros"},
                                                   {1352, "Alianza"},
                                                   {1367, "Miembro: %s"},
                                                   {1368, "pierde el cargo"},
                                                   {1369, "se va del gremio"}};
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
    void AddMember(std::string_view name, std::uint8_t status)
    {
        Client::Scenes::GuildMemberSlot& slot = Guild().members[static_cast<std::size_t>(Guild().memberCount)];
        Fill(slot.name, name);
        slot.status = status;
        ++Guild().memberCount;
        Client::Scenes::AddTheGuildMember(*m_world, slot);
    }

    std::string MemberAt(std::size_t at)
    {
        return std::string(Client::Scenes::GuildTextOf(Guild().window.members.at(at).slot.name));
    }

    // 0x45b200: el flanco sobre el botón y, el cuadro que sigue, soltarlo encima.
    Client::Core::Expected<void, std::string> Click(std::size_t button)
    {
        const Client::UI::OldButton& target = Guild().window.memberButtons.at(button);
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
                               .mouse = {m_mouseX, m_mouseY}};
        return Client::Scenes::DrawTheGuildWindow(context);
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
// en y + 0x4b + 4 (la elegida, uno más arriba), y el dibujo de la pestaña corta en 0x6507d0.
TEST_P(GuildWindowTest, WithAGuildItDrawsTheScoreAndTheTabsBeforeTheTabCut)
{
    JoinTheGuild("");
    const auto drawn = Draw();
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x6507d0"), std::string::npos) << drawn.Error();
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
// claro) solo la primera vez y solo con alianza; después, el paso de la pestaña corta con su dirección.
TEST_P(GuildWindowTest, TheTabsSwitchAndTheUnionTabAsksForTheListOnce)
{
    JoinTheGuild("Ceibo");
    Runtime().movement.leftHeld = true;
    PointAtTheTab(2);
    auto stepped = Step();
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x650c10"), std::string::npos) << stepped.Error();
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
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x64fc00"), std::string::npos) << stepped.Error();
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

// 0x6503cf..0x6504d4: el cargo al elegido guarda su puesto y su nombre, y la ventana de los cargos corta (P-C288). Sin
// ser maestro, los botones no hacen nada.
TEST_P(GuildWindowTest, TheAssignButtonCutsAndOnlyTheMasterHasButtons)
{
    JoinTheGuild("");
    AddMember("Sauce", BattleMasterStatus);
    ASSERT_TRUE(Click(Client::Scenes::GuildAssignButton).HasValue());
    ASSERT_TRUE(Click(Client::Scenes::GuildKickButton).HasValue());
    EXPECT_EQ(Runtime().panels.delegated.panelDialog.opened, 0U);

    Hero().guildStatus = MasterStatus;
    const auto clicked = Click(Client::Scenes::GuildAssignButton);
    ASSERT_FALSE(clicked.HasValue());
    EXPECT_NE(clicked.Error().find("0x6504b6"), std::string::npos) << clicked.Error();
    EXPECT_EQ(Guild().chosenStatus, BattleMasterStatus);
    EXPECT_EQ(Client::Scenes::GuildTextOf(Guild().chosenName), "Sauce");
}

INSTANTIATE_TEST_SUITE_P(Maps, GuildWindowTest, ::testing::Values(0, 2));
} // namespace
