#include "Scenes/GuildReceive.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Network/Crypto/Xor32.h"
#include "Protocol/Guild.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/ActorState.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GuildPanels.h"
#include "Scenes/GuildState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/MessageArea.h"
#include "UI/PanelDialog.h"
#include "UI/PanelRegistry.h"

#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Los paquetes del gremio que llegan (CL9 hito 8; 06 § El gremio): 0x4e2b10 con el 50, 51, 52, 53, 54, 56, 5D y 66, las
// dos ventanas al cerrarse (0x6522f0, 0x6570b0) y la respuesta del diálogo de los paneles (0x4d6b60). Los nombres de
// gremio, de personaje y los textos son inventados.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;
using Client::Scenes::ActorState;
using Client::Scenes::GuildReceiveContext;
using Client::Scenes::WorldState;
namespace Guild = Client::Protocol::Guild;

class LetterMeasure final : public ITextMeasure
{
public:
    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

constexpr std::uint32_t Seed = 4242;
constexpr std::uint32_t Now = 0x00abcdef;
constexpr std::uint16_t PlayerKey = 0x0042;

std::map<std::size_t, std::string> GuildTexts()
{
    std::map<std::size_t, std::string> texts{
        {418, "%s quiere entrar"},
        {429, "Acepta?"},
        {516, "crear 0"},
        {517, "crear 2"},
        {515, "echado"},
        {568, "lista otra vez"},
        {526, "guerra con %s"},
        {533, "partido con %s"},
        {480, "empate"},
        {1280, "%s propone alianza"},
        {1281, "para unirse"},
        {1282, "para otra cosa"},
        {1283, "Acepta la alianza?"},
        {1284, "%s propone rivalidad"},
        {1285, "rival otra"},
        {1286, "rival unirse"},
        {1328, "sin alianza"},
        {1375, "asignado 2"},
        {1382, "alianza rota"},
        {1268, "resultado 20"},
    };
    for (std::size_t index = 503; index <= 512; ++index)
        texts.emplace(index, "texto " + std::to_string(index));
    for (std::size_t index = 519; index <= 525; ++index)
        texts.emplace(index, "guerra " + std::to_string(index));
    for (std::size_t index = 527; index <= 532; ++index)
        texts.emplace(index, "fin " + std::to_string(index));
    return texts;
}

void Name(ActorState& actor, std::string_view name)
{
    actor.name.fill(std::byte{0});
    for (std::size_t i = 0; i < name.size(); ++i)
        actor.name[i] = static_cast<std::byte>(name[i]);
}

template <std::size_t Size> void Fill(std::array<char, Size>& target, std::string_view text)
{
    target.fill('\0');
    for (std::size_t i = 0; i < text.size() && i + 1 < Size; ++i)
        target[i] = text[i];
}

template <std::size_t Size> std::string TextOf(const std::array<char, Size>& text)
{
    return std::string(text.data());
}

Guild::GuildName GuildNameOf(std::string_view text)
{
    Guild::GuildName name{};
    for (std::size_t i = 0; i < text.size() && i < name.size(); ++i)
        name[i] = static_cast<std::byte>(text[i]);
    return name;
}

std::vector<std::uint8_t> Plain(const Client::Protocol::Outgoing& sent)
{
    const auto plain = Client::Network::Crypto::Xor32Decode(sent.packet);
    std::vector<std::uint8_t> bytes;
    for (const std::byte value : plain.Bytes())
        bytes.push_back(std::to_integer<std::uint8_t>(value));
    return bytes;
}

class GuildReceiveTest : public ::testing::Test
{
protected:
    GuildReceiveTest() : m_texts(LoadTexts())
    {
        Reset(0);
    }

    static Client::Data::Text::TextTable LoadTexts()
    {
        auto table = Client::Data::Text::LoadTextTable(Client::Tests::TextTableBytes(GuildTexts()));
        EXPECT_TRUE(table.HasValue());
        return std::move(table).Value();
    }

    // Un mundo nuevo en `map`, con el héroe en el lugar 0.
    void Reset(std::int32_t map)
    {
        m_world = std::make_unique<WorldState>();
        m_random = CrtRandom(Seed);
        m_sent.clear();
        Runtime().map = map;
        Runtime().hero = 0;
        ActorState& hero = Actor(0, "Hornero", 0x0042);
        hero.word1f2 = 0xffff;
    }

    ActorState& Actor(std::size_t index, std::string_view name, std::uint16_t key)
    {
        ActorState& actor = m_world->Actors()[index];
        actor.alive = true;
        actor.seen = true;
        actor.byte88 = 1;
        actor.key = key;
        actor.word1f2 = 0xffff;
        Name(actor, name);
        return actor;
    }

    [[nodiscard]] GuildReceiveContext Context()
    {
        return GuildReceiveContext{.world = *m_world,
                                   .random = m_random,
                                   .measure = &m_letters,
                                   .screen = Client::App::PixelSize{640, 480},
                                   .texts = &m_texts,
                                   .playerKey = PlayerKey,
                                   .now = Now,
                                   .send = [this](Client::Protocol::Outgoing packet)
                                   { m_sent.push_back(Plain(packet)); }};
    }

    Client::Core::Expected<void, std::string> Apply(const Guild::GuildPacket& packet)
    {
        return Client::Scenes::ApplyGuildPacket(packet, Context());
    }

    [[nodiscard]] Client::Scenes::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    [[nodiscard]] Client::Scenes::GuildState& GuildOf()
    {
        return Runtime().guild;
    }

    [[nodiscard]] Client::UI::PanelDialogState& Dialog()
    {
        return Runtime().panels.delegated.panelDialog;
    }

    // El registro del chat, del más viejo al más nuevo (0x45bb20 agrega adelante).
    [[nodiscard]] std::vector<std::pair<std::int32_t, std::string>> Messages() const
    {
        std::vector<std::pair<std::int32_t, std::string>> messages;
        const auto& entries = m_world->Runtime().chat.log.entries;
        for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry)
            messages.emplace_back(entry->type, entry->text);
        return messages;
    }

    [[nodiscard]] std::string LastNotice() const
    {
        const auto& area = m_world->Runtime().chat.area;
        if (area.noticeCount == 0)
            return {};
        return area.notices[static_cast<std::size_t>(area.noticeCount - 1)].text;
    }

    [[nodiscard]] int DrawsSince() const
    {
        CrtRandom reference(Seed);
        for (int draws = 0; draws <= 1000; ++draws)
        {
            if (reference.State() == m_random.State())
                return draws;
            static_cast<void>(reference.Next());
        }
        return -1;
    }

    void NameTheGuild(std::size_t index, std::int32_t key, std::string_view guildName, std::string_view unionName = {})
    {
        Client::Scenes::GuildTableEntry& entry = GuildOf().table[index];
        entry.key = key;
        Fill(entry.guildName, guildName);
        Fill(entry.unionName, unionName);
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random{Seed};
    LetterMeasure m_letters;
    Client::Data::Text::TextTable m_texts;
    std::vector<std::vector<std::uint8_t>> m_sent;
};

// 0x4e4279 abre la ventana 2 (0x6599a0 con 0x40000: CanOpen y CloseByMask, cuatro rand()); el 56 con 1 la cierra
// (0x6570b0: el gremio 1999 vuelve a −1, suena el 0x1c y manda C1 04 54 00) y vacía el mensaje. En Lorencia, en Devias
// y en Chaos Castle (18), donde los comandos no abren.
TEST_F(GuildReceiveTest, CreatingAGuildOpensTheSecondWindowAndTheResultClosesIt)
{
    for (const std::int32_t map : {0, 2, 18})
    {
        SCOPED_TRACE(map);
        Reset(map);
        ASSERT_TRUE(Apply(Guild::MasterQuestion{}).HasValue());
        EXPECT_EQ(Runtime().panels.delegated.secondGuild, 1);
        EXPECT_EQ(DrawsSince(), 4);
        EXPECT_TRUE(m_sent.empty());

        // 0x4e34a0 (el 01 del Guild Master, que corta): el héroe sin gremio queda con el 1999.
        m_world->Actors()[0].word1f2 = 1999;
        Runtime().chat.input.messageBuffer[0] = 'x';
        Runtime().chat.input.messageLength = 1;
        GuildOf().createKind = 3;
        ASSERT_TRUE(Apply(Guild::CreateResult{1}).HasValue());
        EXPECT_EQ(Runtime().panels.delegated.secondGuild, 0);
        EXPECT_EQ(m_world->Actors()[0].word1f2, 0xffff);
        EXPECT_EQ(GuildOf().createKind, 0);
        EXPECT_EQ(Runtime().lastSoundId, 0x1c);
        EXPECT_EQ(Runtime().chat.input.messageBuffer[0], '\0');
        EXPECT_EQ(Runtime().chat.input.messageLength, 0);
        EXPECT_EQ(Runtime().movement.mouseUpdateTimeMax, 6);
        EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{{0xc1, 0x04, 0x54, 0x00}}));
        EXPECT_EQ(DrawsSince(), 4) << "cerrar 0x40000 no consulta el comercio ni la tienda";
    }
}

// 0x4d7ff0: los demás resultados son un mensaje de error (el 1 no tiene; 7 o más, nada).
TEST_F(GuildReceiveTest, TheOtherCreateResultsAreMessages)
{
    ASSERT_TRUE(Apply(Guild::CreateResult{0}).HasValue());
    ASSERT_TRUE(Apply(Guild::CreateResult{2}).HasValue());
    ASSERT_TRUE(Apply(Guild::CreateResult{7}).HasValue());
    const std::vector<std::pair<std::int32_t, std::string>> expected{{Client::UI::ErrorMessage, "crear 0"},
                                                                     {Client::UI::ErrorMessage, "crear 2"}};
    EXPECT_EQ(Messages(), expected);
}

// 0x4d74b0: el 50 guarda la clave y abre el diálogo con el nombre; Confirmar (0x4d6b60 con 8) manda C1 06 51 01 y la
// clave; Esc (0x50) manda el 00. Con el diálogo ocupado, el 00 sin preguntar.
TEST_F(GuildReceiveTest, AJoinRequestAsksAndTheAnswerCarriesTheKey)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Reset(map);
        Actor(5, "Benteveo", 0x0123);
        ASSERT_TRUE(Apply(Guild::JoinRequest{0x0123}).HasValue());
        EXPECT_EQ(GuildOf().requester, 0x0123);
        ASSERT_EQ(Dialog().opened, Now);
        EXPECT_EQ(Dialog().owner, Client::UI::PanelDialogOwner::GuildJoin);
        EXPECT_EQ(Dialog().lines[0], "Benteveo quiere entrar");
        EXPECT_EQ(Dialog().lines[1], "Acepta?");
        EXPECT_EQ(Dialog().flags, Client::UI::PanelDialogConfirm);
        EXPECT_TRUE(m_sent.empty());

        // Otro pedido con el diálogo abierto: no, y la clave nueva queda.
        Actor(6, "Calandria", 0x0456);
        ASSERT_TRUE(Apply(Guild::JoinRequest{0x0456}).HasValue());
        EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{{0xc1, 0x06, 0x51, 0x00, 0x04, 0x56}}));
        EXPECT_EQ(GuildOf().requester, 0x0456);
        m_sent.clear();

        // Confirmar responde con la clave guardada (0x78aba40), la del último.
        const auto answered =
            Client::Scenes::AnswerThePanelDialog(*m_world, Context().send, Client::UI::PanelDialogOwner::GuildJoin, 8);
        ASSERT_TRUE(answered.HasValue()) << answered.Error();
        EXPECT_TRUE(answered.Value());
        EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{{0xc1, 0x06, 0x51, 0x01, 0x04, 0x56}}));
        m_sent.clear();

        // Esc por el registro (0x657880 → 0x660410): 0x50, el no, y el diálogo cerrado.
        Client::Scenes::GamePanels panels(*m_world, m_random);
        panels.LendTheGuild(*m_world, Context().send);
        const auto key = panels.Registry().PanelKey(0x1b);
        ASSERT_TRUE(key.HasValue()) << key.Error();
        EXPECT_TRUE(key.Value());
        EXPECT_EQ(Dialog().opened, 0u);
        EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{{0xc1, 0x06, 0x51, 0x00, 0x04, 0x56}}));
    }
}

// 0x4d74f8: sin el personaje de la clave, el original lee el nombre fuera de los 400: corta.
TEST_F(GuildReceiveTest, AJoinRequestFromNobodyIsCut)
{
    const auto applied = Apply(Guild::JoinRequest{0x0999});
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x4d74f8"), std::string::npos);
}

// 0x4d7a20: el resultado de 0 a 7 es un mensaje de error y siempre 0x6236f0(0).
TEST_F(GuildReceiveTest, TheJoinResultIsAMessage)
{
    Runtime().dialogs.current = 0x22;
    ASSERT_TRUE(Apply(Guild::JoinResult{1}).HasValue());
    ASSERT_TRUE(Apply(Guild::JoinResult{8}).HasValue());
    const std::vector<std::pair<std::int32_t, std::string>> expected{{Client::UI::ErrorMessage, "texto 504"}};
    EXPECT_EQ(Messages(), expected);
    EXPECT_EQ(Runtime().dialogs.current, 0);
}

// 0x4d7b60: la cuenta, el puntaje sin negativos, el nombre y cada miembro con su servidor (bit 7) o 0xff.
TEST_F(GuildReceiveTest, TheMemberListFillsTheSlotsAndTheWindow)
{
    GuildOf().window.unions.push_back({});
    Guild::MemberList list{.count = 2, .score = -7, .name = GuildNameOf("Horneros"), .members = {}};
    Guild::MemberEntry first;
    first.name = {std::byte{'H'}, std::byte{'o'}, std::byte{'r'}, std::byte{'n'}, std::byte{'e'},
                  std::byte{'r'}, std::byte{'o'}, std::byte{0},   std::byte{0},   std::byte{0}};
    first.number = 1;
    first.server = 0x83;
    first.status = 0x80;
    Guild::MemberEntry second = first;
    second.name[0] = std::byte{'T'};
    second.server = 0x03;
    second.status = 0;
    list.members = {first, second};
    ASSERT_TRUE(Apply(list).HasValue());
    EXPECT_EQ(GuildOf().memberCount, 2);
    EXPECT_EQ(GuildOf().score, 0);
    EXPECT_EQ(TextOf(GuildOf().window.name88), "Horneros");
    EXPECT_EQ(TextOf(GuildOf().members[0].name), "Hornero");
    EXPECT_EQ(GuildOf().members[0].server, 3);
    EXPECT_EQ(GuildOf().members[1].server, 0xff);
    EXPECT_EQ(GuildOf().members[1].status, 0);
    EXPECT_EQ(GuildOf().window.members.size(), 2u);
    EXPECT_TRUE(GuildOf().window.unions.empty());
}

// 0x4d7c30: el 4 saca al héroe del gremio y borra la entrada; el 1 y el 4 cierran el gremio (0x6522f0); el 5 pide la
// lista (C1 03 52).
TEST_F(GuildReceiveTest, LeavingTheGuildClosesTheWindow)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Reset(map);
        NameTheGuild(3, 77, "Horneros", "Ceibos");
        ActorState& hero = m_world->Actors()[0];
        hero.word1f2 = 3;
        hero.guildStatus = 0x80;
        hero.byte370 = 4;
        GuildOf().warOn = 1;
        GuildOf().window.tab = 3;
        Runtime().panels.delegated.guild = 1;

        ASSERT_TRUE(Apply(Guild::LeaveResult{4}).HasValue());
        EXPECT_EQ(hero.word1f2, 0xffff);
        EXPECT_EQ(GuildOf().table[3].key, -1);
        EXPECT_EQ(TextOf(GuildOf().table[3].guildName), "");
        EXPECT_EQ(TextOf(GuildOf().table[3].unionName), "");
        EXPECT_EQ(hero.guildStatus, 0xff);
        EXPECT_EQ(hero.byte370, 0);
        EXPECT_EQ(GuildOf().warOn, 0);
        EXPECT_EQ(GuildOf().memberCount, -1);
        EXPECT_EQ(Runtime().panels.delegated.guild, 0);
        EXPECT_EQ(GuildOf().window.tab, 1);
        EXPECT_EQ(Runtime().lastSoundId, 0x1c);
        EXPECT_EQ(DrawsSince(), 0);

        ASSERT_TRUE(Apply(Guild::LeaveResult{5}).HasValue());
        EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{{0xc1, 0x03, 0x52}}));
        const std::vector<std::pair<std::int32_t, std::string>> expected{{Client::UI::ErrorMessage, "echado"},
                                                                         {Client::UI::ErrorMessage, "lista otra vez"}};
        EXPECT_EQ(Messages(), expected);
    }
}

// 0x6599a0(0x10) por el registro con el gancho del gremio: CanOpen y CloseByMask (cuatro rand()), el C1 03 52 y el
// byte; en Chaos Castle también abre.
TEST_F(GuildReceiveTest, OpeningTheGuildAsksForTheList)
{
    for (const std::int32_t map : {0, 2, 18})
    {
        SCOPED_TRACE(map);
        Reset(map);
        Client::Scenes::GamePanels panels(*m_world, m_random);
        panels.LendTheGuild(*m_world, Context().send);
        const auto opened = panels.Registry().Open(Client::UI::GuildPanel);
        ASSERT_TRUE(opened.HasValue()) << opened.Error();
        EXPECT_TRUE(opened.Value());
        EXPECT_EQ(Runtime().panels.delegated.guild, 1);
        EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{{0xc1, 0x03, 0x52}}));
        EXPECT_EQ(DrawsSince(), 4);

        // Con la ventana 2 abierta, CanOpen no deja abrir nada.
        const auto closed = panels.Registry().Close(Client::UI::GuildPanel);
        ASSERT_TRUE(closed.HasValue()) << closed.Error();
        Runtime().panels.delegated.secondGuild = 1;
        const auto refused = panels.Registry().Open(Client::UI::GuildPanel);
        ASSERT_TRUE(refused.HasValue());
        EXPECT_FALSE(refused.Value());
    }
}

// 0x4e42f0: el 5D saca del gremio al personaje de la clave (sin el bit 7) y cierra la ventana.
TEST_F(GuildReceiveTest, TheViewportGuildDeleteClearsTheActor)
{
    ActorState& other = Actor(9, "Zorzal", 0x0077);
    other.word1f2 = 4;
    other.guildStatus = 0x20;
    other.byte369 = 1;
    other.dword36c = 5;
    other.byte370 = 2;
    ASSERT_TRUE(Apply(Guild::ViewportGuildDelete{0x0077}).HasValue());
    EXPECT_EQ(other.word1f2, 0xffff);
    EXPECT_EQ(other.guildStatus, 0xff);
    EXPECT_EQ(other.byte369, 0);
    EXPECT_EQ(other.dword36c, 0u);
    EXPECT_EQ(other.byte370, 0);
    EXPECT_EQ(GuildOf().memberCount, -1);
    EXPECT_FALSE(Apply(Guild::ViewportGuildDelete{0x0078}).HasValue());
}

// 0x447b90: el 66 escribe la entrada de la clave (0x447b00 mira hasta la cuenta inclusive) con los nombres y la marca
// en nibbles; sin la clave, nada.
TEST_F(GuildReceiveTest, TheGuildInfoWritesTheEntryOfItsKey)
{
    GuildOf().tableCount = 1;
    NameTheGuild(1, 55, "viejo");
    Guild::GuildInfo info{
        .key = 55, .unionName = GuildNameOf("Ceibos"), .guildName = GuildNameOf("Horneros"), .mark = {}};
    info.mark[0] = 0x12;
    info.mark[31] = 0xab;
    ASSERT_TRUE(Apply(info).HasValue());
    const Client::Scenes::GuildTableEntry& entry = GuildOf().table[1];
    EXPECT_EQ(TextOf(entry.guildName), "Horneros");
    EXPECT_EQ(TextOf(entry.unionName), "Ceibos");
    EXPECT_EQ(entry.mark[0], 1);
    EXPECT_EQ(entry.mark[1], 2);
    EXPECT_EQ(entry.mark[62], 0xa);
    EXPECT_EQ(entry.mark[63], 0xb);

    info.key = 56;
    ASSERT_TRUE(Apply(info).HasValue());
    EXPECT_EQ(TextOf(GuildOf().table[2].guildName), "");
}

} // namespace
