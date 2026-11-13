#include "Scenes/GuildReceive.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Text/TextTable.h"
#include "Network/Crypto/Xor32.h"
#include "Protocol/Guild.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GuildPanels.h"
#include "Scenes/GuildState.h"
#include "Scenes/SocialState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/MessageArea.h"
#include "UI/PanelDialog.h"
#include "UI/PanelRegistry.h"

#include "support/PlayerActors.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Los paquetes del gremio, de la alianza y de la guerra que llegan (CL9 hito 8; 06 § El gremio): 0x4e2b10 con el 50,
// 51, 52, 53, 54, 56, 5D, 60..64, 66, E1, E5, E6, E9 y EB, las dos ventanas al cerrarse (0x6522f0, 0x6570b0) y las
// respuestas del diálogo de los paneles (0x4d6b60, 0x653c00). El gesto de la guerra es el emote del hito 1
// (Scenes::PlayTheEmote). Los nombres de gremio, de personaje y los textos son inventados.
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

// Player.bmd con sus 205 acciones (las que mira 0x4ea030 en el gesto); ningún otro modelo.
class GestureModels final : public Client::Scenes::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(std::uint16_t /*model*/) const override
    {
        return nullptr;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return model == Client::Data::Models::PlayerSkeletonModel ? &player : nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t /*model*/) const override
    {
        return {};
    }
    Client::Render::Models::Skeleton player = Client::Tests::PlayerActionsSkeleton();
};

// 0x576a84: las acciones de parado (1..12 y 0x8a), las únicas con las que 0x576a60 anima y manda.
constexpr std::uint8_t Standing = 1;
constexpr std::uint8_t Walking = 0x0d;
constexpr std::uint8_t ElfClass = 2;

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
        hero.type = Client::Data::Models::PlayerSkeletonModel;
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

    [[nodiscard]] GuildReceiveContext Context(bool withModels = true)
    {
        return GuildReceiveContext{.world = *m_world,
                                   .random = m_random,
                                   .measure = &m_letters,
                                   .screen = Client::App::PixelSize{640, 480},
                                   .texts = &m_texts,
                                   .playerKey = PlayerKey,
                                   .now = Now,
                                   .send = [this](Client::Protocol::Outgoing packet)
                                   { m_sent.push_back(Plain(packet)); },
                                   .models = withModels ? &m_models : nullptr};
    }

    Client::Core::Expected<void, std::string> Apply(const Guild::GuildPacket& packet, bool withModels = true)
    {
        return Client::Scenes::ApplyGuildPacket(packet, Context(withModels));
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
    GestureModels m_models;
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

// 0x4d74f8: sin el personaje de la clave, el original abre el diálogo con el nombre que lee después de los 400
// (Q-C1297); MuRet descarta el 50 sin cortar (la sesión sigue): ni la clave, ni el diálogo, ni el no. Con el diálogo
// ocupado, el no sale igual, que no busca a nadie. En Lorencia y en Devias.
TEST_F(GuildReceiveTest, AJoinRequestFromSomeoneOutOfSightIsDiscarded)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Reset(map);
        GuildOf().requester = 0x0777;
        const auto applied = Apply(Guild::JoinRequest{0x0999});
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(GuildOf().requester, 0x0777);
        EXPECT_EQ(Dialog().opened, 0u);
        EXPECT_EQ(Dialog().owner, Client::UI::PanelDialogOwner::None);
        EXPECT_TRUE(m_sent.empty());
        EXPECT_TRUE(Messages().empty());
        EXPECT_EQ(DrawsSince(), 0);

        Dialog().opened = Now;
        ASSERT_TRUE(Apply(Guild::JoinRequest{0x0999}).HasValue());
        EXPECT_EQ(GuildOf().requester, 0x0999);
        EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{{0xc1, 0x06, 0x51, 0x00, 0x09, 0x99}}));
    }
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

// 0x4d7bc2..0x4d7c1f: el miembro 80 en adelante cae sobre la tabla de los gremios (Q-C1303); MuRet descarta el 52
// entero sin cortar: la cuenta, el puntaje, el nombre, los miembros y las dos listas de la ventana quedan como estaban.
TEST_F(GuildReceiveTest, AMemberListOfMoreThanEightyIsDiscarded)
{
    GuildOf().memberCount = 3;
    GuildOf().score = 9;
    Fill(GuildOf().window.name88, "Talas");
    Fill(GuildOf().members[0].name, "Ombu");
    GuildOf().window.unions.push_back({});
    NameTheGuild(0, 70, "Horneros");
    Guild::MemberList list{.count = 81, .score = 5, .name = GuildNameOf("Ceibos"), .members = {}};
    Guild::MemberEntry entry;
    entry.name[0] = std::byte{'Z'};
    entry.server = 0x81;
    list.members.assign(81, entry);
    const auto applied = Apply(list);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(GuildOf().memberCount, 3);
    EXPECT_EQ(GuildOf().score, 9);
    EXPECT_EQ(TextOf(GuildOf().window.name88), "Talas");
    EXPECT_EQ(TextOf(GuildOf().members[0].name), "Ombu");
    EXPECT_EQ(TextOf(GuildOf().members[79].name), "");
    EXPECT_TRUE(GuildOf().window.members.empty());
    EXPECT_EQ(GuildOf().window.unions.size(), 1u);
    EXPECT_EQ(TextOf(GuildOf().table[0].guildName), "Horneros");
    EXPECT_TRUE(m_sent.empty());
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
}

// 0x4e4306: sin el personaje, el original escribe después de los 400 (Q-C1298); MuRet descarta el 5D entero sin cortar:
// ni la cuenta, ni el personaje, ni el gremio cerrado (ni sus rand()). En Lorencia y en Devias.
TEST_F(GuildReceiveTest, AViewportGuildDeleteOfSomeoneOutOfSightIsDiscarded)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Reset(map);
        ActorState& other = Actor(9, "Zorzal", 0x0077);
        other.word1f2 = 4;
        other.guildStatus = 0x20;
        GuildOf().memberCount = 3;
        Runtime().panels.delegated.guild = 1;
        const auto applied = Apply(Guild::ViewportGuildDelete{0x0078});
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(GuildOf().memberCount, 3);
        EXPECT_EQ(other.word1f2, 4);
        EXPECT_EQ(other.guildStatus, 0x20);
        EXPECT_EQ(Runtime().panels.delegated.guild, 1);
        EXPECT_FALSE(Runtime().lastSoundId.has_value());
        EXPECT_EQ(DrawsSince(), 0);
        EXPECT_TRUE(m_sent.empty());
    }
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

// 0x4d9260: cada entrada del 65 pone el puesto, +0x369 y la relación; con el gremio en la tabla, su índice; sin él,
// el C1 08 66 que lo pide y 0x447b50, que lo agrega al final. +0x374: 1 con el gremio del héroe. Con la entrada del
// héroe, 0x412120 copia su gremio y su puesto. En Lorencia, en Devias y en Arena (6, la del fútbol de la guerra).
TEST_F(GuildReceiveTest, TheViewportGuildGivesEachActorItsIndexAndAsksForTheUnknownOnes)
{
    for (const std::int32_t map : {0, 2, 6})
    {
        SCOPED_TRACE(map);
        Reset(map);
        GuildOf().tableCount = 2;
        NameTheGuild(0, 70, "Horneros");
        NameTheGuild(1, 71, "Teros");
        ActorState& other = Actor(9, "Zorzal", 0x0077);
        Guild::ViewportGuild viewport;
        viewport.entries.push_back({.guildKey = 71, .status = 0x80, .byte369 = 1, .relation = 2, .key = 0x0042});
        viewport.entries.push_back({.guildKey = 71, .status = 0x20, .byte369 = 0, .relation = 0, .key = 0x0077});
        viewport.entries.push_back({.guildKey = 99, .status = 0, .byte369 = 0, .relation = 1, .key = 0x0077});
        ASSERT_TRUE(Apply(viewport).HasValue());
        const ActorState& hero = m_world->Actors()[0];
        EXPECT_EQ(hero.word1f2, 1);
        EXPECT_EQ(hero.guildStatus, 0x80);
        EXPECT_EQ(hero.byte369, 1);
        EXPECT_EQ(hero.byte370, 2);
        EXPECT_EQ(hero.byte374, 1);
        EXPECT_EQ(GuildOf().heroGuildIndex, 1);
        EXPECT_EQ(GuildOf().heroGuildStatus, 0x80);
        // La tercera entrada pisa a la segunda: el gremio 99 no estaba, se pide y queda en la entrada 2.
        EXPECT_EQ(other.word1f2, 2);
        EXPECT_EQ(other.byte370, 1);
        EXPECT_EQ(other.byte374, 0);
        EXPECT_EQ(GuildOf().tableCount, 3u);
        EXPECT_EQ(GuildOf().table[2].key, 99);
        ASSERT_EQ(m_sent.size(), 1u);
        EXPECT_EQ(m_sent[0], (std::vector<std::uint8_t>{0xc1, 0x08, 0x66, 0x00, 99, 0x00, 0x00, 0x00}));
        EXPECT_EQ(DrawsSince(), 0);
    }
}

// 0x4d96eb..0x4d976b: en guerra y sin el índice del rival, el 65 lo busca por el nombre y deja −1 si no está; con el
// rival, +0x374 en 2. La tabla llena da −1 sin pedir nada más que el 66. Sin el personaje, corta.
TEST_F(GuildReceiveTest, TheViewportGuildMarksTheRivalInAWar)
{
    GuildOf().tableCount = 2;
    NameTheGuild(0, 70, "Horneros");
    NameTheGuild(1, 71, "Teros");
    GuildOf().warOn = 1;
    Fill(GuildOf().rival, "Teros");
    ActorState& other = Actor(9, "Zorzal", 0x0077);
    Guild::ViewportGuild viewport;
    viewport.entries.push_back({.guildKey = 71, .status = 0, .byte369 = 0, .relation = 0, .key = 0x0077});
    ASSERT_TRUE(Apply(viewport).HasValue());
    EXPECT_EQ(GuildOf().rivalIndex, 1);
    EXPECT_EQ(other.byte374, 2);

    Fill(GuildOf().rival, "Benteveos");
    GuildOf().rivalIndex = -1;
    ASSERT_TRUE(Apply(viewport).HasValue());
    EXPECT_EQ(GuildOf().rivalIndex, -1);
    EXPECT_EQ(other.byte374, 0);

    // Con la tabla llena, 0x447b00 leería la entrada 2.000 (Q-C1280): corta antes de 0x447b50.
    GuildOf().tableCount = 2000;
    viewport.entries[0].guildKey = 5000;
    EXPECT_FALSE(Apply(viewport).HasValue());
    EXPECT_TRUE(m_sent.empty());
}

// 0x4d92b2: una entrada sin su personaje, que el original escribe después de los 400 (Q-C1299), se descarta entera
// (sin el 66 que pediría su gremio ni 0x447b50) y las demás siguen; la sesión sigue. En Lorencia y en Devias.
TEST_F(GuildReceiveTest, AViewportGuildEntryOfSomeoneOutOfSightIsDiscarded)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Reset(map);
        GuildOf().tableCount = 1;
        NameTheGuild(0, 70, "Horneros");
        ActorState& other = Actor(9, "Zorzal", 0x0077);
        Guild::ViewportGuild viewport;
        viewport.entries.push_back({.guildKey = 99, .status = 0x20, .byte369 = 1, .relation = 2, .key = 0x0078});
        viewport.entries.push_back({.guildKey = 70, .status = 0x40, .byte369 = 0, .relation = 1, .key = 0x0077});
        const auto applied = Apply(viewport);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(other.word1f2, 0);
        EXPECT_EQ(other.guildStatus, 0x40);
        EXPECT_EQ(other.byte370, 1);
        EXPECT_EQ(GuildOf().tableCount, 1u);
        EXPECT_TRUE(m_sent.empty());
        EXPECT_EQ(DrawsSince(), 0);
    }
}

// 0x4e44aa: el 67 pone el nombre de la alianza en la entrada del gremio y la relación en el personaje; un gremio que
// la tabla no tiene escribe en el miembro 74 (Q-C1284). La clave no pierde el bit 7, como en el 65: con él, nadie.
TEST_F(GuildReceiveTest, TheViewportUnionNamesTheAllianceAndTheRelation)
{
    GuildOf().tableCount = 1;
    NameTheGuild(1, 71, "Teros", "Viejos");
    ActorState& other = Actor(9, "Zorzal", 0x0077);
    Guild::ViewportUnion viewport;
    viewport.entries.push_back({.key = 0x0077, .guildKey = 71, .relation = 1, .unionName = GuildNameOf("Ceibos")});
    ASSERT_TRUE(Apply(viewport).HasValue());
    EXPECT_EQ(TextOf(GuildOf().table[1].unionName), "Ceibos");
    EXPECT_EQ(other.byte370, 1);

    viewport.entries[0].guildKey = 72;
    viewport.entries[0].unionName = GuildNameOf("Talas");
    ASSERT_TRUE(Apply(viewport).HasValue());
    EXPECT_EQ(std::string(GuildOf().members[74].name.data()), "Talas");

    // 0x4e44f5: con la clave 0x8077 no hay nadie y el original escribe la relación después de los 400 (Q-C1300): MuRet
    // descarta la entrada entera, también el nombre de la alianza, y la sesión sigue.
    viewport.entries[0].key = 0x8077;
    viewport.entries[0].guildKey = 71;
    viewport.entries[0].relation = 2;
    viewport.entries[0].unionName = GuildNameOf("Espinillo");
    const auto applied = Apply(viewport);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(TextOf(GuildOf().table[1].unionName), "Ceibos");
    EXPECT_EQ(other.byte370, 1);
}

// 0x6565e0: la alianza pide con el nombre de quien la propone; Confirmar (0x653c00 con 8) manda C1 08 E6 con 1. Con el
// diálogo ocupado, el 0 sin preguntar.
TEST_F(GuildReceiveTest, AnAllianceRequestAsksAndTheAnswerGoesBack)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Reset(map);
        Actor(5, "Benteveo", 0x0123);
        ASSERT_TRUE(
            Apply(Guild::RelationshipRequest{.relation = 1, .request = 1, .keyHigh = 0x01, .keyLow = 0x23}).HasValue());
        ASSERT_EQ(Dialog().opened, Now);
        EXPECT_EQ(Dialog().owner, Client::UI::PanelDialogOwner::Relationship);
        EXPECT_EQ(Dialog().lines[0], "Benteveo propone alianza");
        EXPECT_EQ(Dialog().lines[1], "para unirse");
        EXPECT_EQ(Dialog().lines[2], "Acepta la alianza?");
        EXPECT_EQ(GuildOf().secondWindow.dialog2a4, Now);

        ASSERT_TRUE(
            Apply(Guild::RelationshipRequest{.relation = 2, .request = 2, .keyHigh = 0x01, .keyLow = 0x23}).HasValue());
        EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{{0xc1, 0x08, 0xe6, 0x02, 0x02, 0x00, 0x01, 0x23}}));
        m_sent.clear();

        const auto answered = Client::Scenes::AnswerThePanelDialog(*m_world, Context().send,
                                                                   Client::UI::PanelDialogOwner::Relationship, 8);
        ASSERT_TRUE(answered.HasValue()) << answered.Error();
        EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{{0xc1, 0x08, 0xe6, 0x01, 0x01, 0x01, 0x01, 0x23}}));
        m_sent.clear();
        // 0x654289: lo que no es Confirmar ni Cancelar no manda nada.
        ASSERT_TRUE(Client::Scenes::AnswerThePanelDialog(*m_world, Context().send,
                                                         Client::UI::PanelDialogOwner::Relationship, 0x50)
                        .HasValue());
        EXPECT_TRUE(m_sent.empty());
    }
}

// 0x656d44..0x656db1: con el personaje y una relación que no es 1 ni 2, el original abre el diálogo con la pila sin
// inicializar (Q-C1302); MuRet descarta el E5 entero sin cortar. Sin el personaje (0x656ccb), guarda los cuatro bytes y
// nada más, como el original. En Lorencia y en Devias.
TEST_F(GuildReceiveTest, ARelationshipRequestOfAnotherRelationIsDiscarded)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Reset(map);
        Actor(5, "Benteveo", 0x0123);
        GuildOf().relation = 1;
        GuildOf().relationRequest = 1;
        const auto applied =
            Apply(Guild::RelationshipRequest{.relation = 3, .request = 2, .keyHigh = 0x01, .keyLow = 0x23});
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(GuildOf().relation, 1);
        EXPECT_EQ(GuildOf().relationRequest, 1);
        EXPECT_EQ(GuildOf().relationKeyLow, 0);
        EXPECT_EQ(Dialog().opened, 0u);
        EXPECT_EQ(GuildOf().secondWindow.dialog2a4, 0u);
        EXPECT_TRUE(m_sent.empty());

        ASSERT_TRUE(
            Apply(Guild::RelationshipRequest{.relation = 3, .request = 2, .keyHigh = 0x04, .keyLow = 0x56}).HasValue());
        EXPECT_EQ(GuildOf().relation, 3);
        EXPECT_EQ(GuildOf().relationRequest, 2);
        EXPECT_EQ(GuildOf().relationKeyHigh, 0x04);
        EXPECT_EQ(GuildOf().relationKeyLow, 0x56);
        EXPECT_EQ(Dialog().opened, 0u);
        EXPECT_TRUE(m_sent.empty());
    }
}

// 0x6565e0 con la rivalidad (2): sus textos.
TEST_F(GuildReceiveTest, ARivalryRequestHasItsOwnLines)
{
    Actor(5, "Benteveo", 0x0123);
    ASSERT_TRUE(
        Apply(Guild::RelationshipRequest{.relation = 2, .request = 2, .keyHigh = 0x01, .keyLow = 0x23}).HasValue());
    EXPECT_EQ(Dialog().lines[0], "Benteveo propone rivalidad");
    EXPECT_EQ(Dialog().lines[1], "rival otra");
}

// 0x4d9940: el E6 que rompe la alianza del héroe (la clave suya, 1, 2 y 1) borra el nombre de la alianza; los demás
// solo son un mensaje (o "Invalid GuildRelationShipResult").
TEST_F(GuildReceiveTest, BreakingTheAllianceClearsItsName)
{
    NameTheGuild(3, 77, "Horneros", "Ceibos");
    m_world->Actors()[0].word1f2 = 3;
    ASSERT_TRUE(Apply(Guild::RelationshipResult{.relation = 1, .request = 2, .result = 1, .key = 0x0999}).HasValue());
    EXPECT_EQ(TextOf(GuildOf().table[3].unionName), "Ceibos");
    ASSERT_TRUE(
        Apply(Guild::RelationshipResult{.relation = 1, .request = 2, .result = 1, .key = PlayerKey}).HasValue());
    EXPECT_EQ(TextOf(GuildOf().table[3].unionName), "");
    ASSERT_TRUE(Apply(Guild::RelationshipResult{.relation = 1, .request = 1, .result = 0x20, .key = 0}).HasValue());
    ASSERT_TRUE(Apply(Guild::RelationshipResult{.relation = 1, .request = 1, .result = 0x12, .key = 0}).HasValue());
    const std::vector<std::pair<std::int32_t, std::string>> expected{
        {Client::UI::SystemMessage, "alianza rota"},
        {Client::UI::SystemMessage, "alianza rota"},
        {Client::UI::SystemMessage, "resultado 20"},
        // 0x45bb20 corta desde 30 bytes en dos renglones.
        {Client::UI::SystemMessage, "Invalid GuildRelationShipResul"},
        {Client::UI::SystemMessage, "t"}};
    EXPECT_EQ(Messages(), expected);
}

// 0x4d9940 sin gremio (−1): escribe el primer byte del miembro 74 (Q-C1281).
TEST_F(GuildReceiveTest, BreakingWithoutAGuildWritesTheMemberBeforeTheTable)
{
    Fill(GuildOf().members[74].name, "Chingolo");
    ASSERT_TRUE(
        Apply(Guild::RelationshipResult{.relation = 1, .request = 2, .result = 1, .key = PlayerKey}).HasValue());
    EXPECT_EQ(GuildOf().members[74].name[0], '\0');
    EXPECT_EQ(GuildOf().members[74].name[1], 'h');
}

// 0x4d9e50 agrega las filas con la marca en nibbles, cada una adelante (0x6517e0 → 0x46d900); 0x4d9b80 con 1 pide la
// lista otra vez (con más de 2) y la vacía (0x651830); con 0, el mensaje.
TEST_F(GuildReceiveTest, TheUnionListAndItsRefresh)
{
    Guild::UnionEntry entry{.byte0 = 3, .mark = {}, .name = GuildNameOf("Ceibos")};
    entry.mark[0] = 0x4f;
    const Guild::UnionEntry last{.byte0 = 7, .mark = {}, .name = GuildNameOf("Talas")};
    ASSERT_TRUE(Apply(Guild::UnionList{.count = 3, .result = 1, .entries = {entry, entry, last}}).HasValue());
    ASSERT_EQ(GuildOf().window.unions.size(), 3u);
    EXPECT_EQ(TextOf(GuildOf().window.unions[0].name), "Talas");
    EXPECT_EQ(GuildOf().window.unions[0].byte0, 7);
    EXPECT_EQ(GuildOf().window.unions[1].mark[0], 4);
    EXPECT_EQ(GuildOf().window.unions[1].mark[1], 0xf);
    EXPECT_EQ(TextOf(GuildOf().window.unions[1].name), "Ceibos");
    EXPECT_EQ(GuildOf().window.unions[1].byte0, 3);
    EXPECT_EQ(GuildOf().window.unionList.selection, 3);

    ASSERT_TRUE(Apply(Guild::UnionListRefresh{.kind = 1, .result = 1}).HasValue());
    EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{{0xc1, 0x03, 0xe9}}));
    EXPECT_TRUE(GuildOf().window.unions.empty());

    ASSERT_TRUE(Apply(Guild::UnionListRefresh{.kind = 1, .result = 0}).HasValue());
    const std::vector<std::pair<std::int32_t, std::string>> expected{{Client::UI::SystemMessage, "sin alianza"}};
    EXPECT_EQ(Messages(), expected);
}

// 0x4d9810: el E1 con 1 da el texto del tipo; con otro, el del resultado; si no, "Invalid GuildAssign".
TEST_F(GuildReceiveTest, TheAssignResultIsASystemMessage)
{
    ASSERT_TRUE(Apply(Guild::AssignResult{.type = 2, .result = 1}).HasValue());
    ASSERT_TRUE(Apply(Guild::AssignResult{.type = 9, .result = 1}).HasValue());
    const std::vector<std::pair<std::int32_t, std::string>> expected{
        {Client::UI::SystemMessage, "asignado 2"}, {Client::UI::SystemMessage, "Invalid GuildAssign"}};
    EXPECT_EQ(Messages(), expected);
}

// La guerra: el 61 guarda al rival y abre el diálogo 0x7f; el 62 la empieza (el aviso, +0x374 de cada personaje, el
// gesto 0x576a60 con 0xc0 y 0x94 y su C1 05 18); el 64 trae los puntajes; el 63 la termina con el gesto de ganar (0xa7,
// 0x89) y su C1 18. Con el héroe parado, cada gesto anima y manda dos C1 18 iguales (el de 0x576a60 y el propio). En
// Lorencia, en Devias y en Arena (6).
TEST_F(GuildReceiveTest, AWarStartsScoresAndEndsWithTwoActions)
{
    for (const std::int32_t map : {0, 2, 6})
    {
        SCOPED_TRACE(map);
        Reset(map);
        NameTheGuild(1, 11, "Horneros");
        NameTheGuild(2, 22, "Teros");
        ActorState& hero = m_world->Actors()[0];
        hero.word1f2 = 1;
        hero.angles.z = 90.0f;
        hero.animation.action = Standing;
        Actor(5, "Calandria", 0x0105).word1f2 = 1;
        Actor(6, "Carancho", 0x0106).word1f2 = 2;
        Actor(7, "Chingolo", 0x0107);

        ASSERT_TRUE(Apply(Guild::WarRequest{.name = GuildNameOf("Teros"), .type = 1}).HasValue());
        EXPECT_EQ(TextOf(GuildOf().rival), "Teros");
        EXPECT_EQ(GuildOf().warType, 1);
        EXPECT_EQ(Runtime().dialogs.current, 0x7f);
        GuildOf().warType = 0;

        ASSERT_TRUE(Apply(Guild::WarStart{.name = GuildNameOf("Teros"), .type = 0, .team = 1}).HasValue());
        EXPECT_EQ(GuildOf().warOn, 1);
        EXPECT_EQ(GuildOf().warType, 0);
        EXPECT_EQ(GuildOf().warTeam, 1);
        EXPECT_EQ(GuildOf().rivalIndex, 2);
        EXPECT_EQ(LastNotice(), "guerra con Teros");
        EXPECT_EQ(hero.byte374, 1);
        EXPECT_EQ(m_world->Actors()[5].byte374, 1);
        EXPECT_EQ(m_world->Actors()[6].byte374, 2);
        EXPECT_EQ(m_world->Actors()[7].byte374, 0);
        // (90 + 22.5) / 45 + 1 = 3.5: el sector 3.
        EXPECT_EQ(hero.animation.action, 0xc0);
        const std::vector<std::uint8_t> started{0xc1, 0x05, 0x18, 0x03, 0x94};
        EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{started, started}));

        ASSERT_TRUE(Apply(Guild::WarScore{.ours = 3, .theirs = 1}).HasValue());
        EXPECT_EQ(GuildOf().ourScore, 3);
        EXPECT_EQ(GuildOf().theirScore, 1);

        // La animación del gesto terminó: el héroe está parado otra vez. 0x4d8958 pone en 0 el WORD 0x5767816, el del
        // duelo (0x4e4d60).
        hero.animation.action = Standing;
        Runtime().social.duel.word5767816 = 0x1234;
        ASSERT_TRUE(Apply(Guild::WarEnd{0}).HasValue());
        EXPECT_EQ(Runtime().social.duel.word5767816, 0);
        EXPECT_EQ(LastNotice(), "fin 527");
        EXPECT_EQ(GuildOf().warOn, 0);
        EXPECT_EQ(GuildOf().rivalIndex, -1);
        EXPECT_EQ(TextOf(GuildOf().rival), "");
        EXPECT_EQ(m_world->Actors()[6].byte374, 0);
        EXPECT_EQ(m_world->Actors()[5].byte374, 1);
        EXPECT_EQ(hero.animation.action, 0xa7);
        const std::vector<std::uint8_t> won{0xc1, 0x05, 0x18, 0x03, 0x89};
        EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{started, started, won, won}));
        EXPECT_EQ(DrawsSince(), 0);
    }
}

// 0x4d8870: perder (1, 2 y 4) es el otro gesto (0xad, 0x8d; el elfo anima la siguiente, 0x576a9d); el 6 no tiene;
// más de 6 muestra la pila sin inicializar (Q-C1301): MuRet descarta el 63 sin cortar y la guerra sigue. Caminando,
// 0x576a60 no anima ni manda: queda solo el C1 18 propio.
TEST_F(GuildReceiveTest, TheWarEndGestureDependsOnTheResult)
{
    ActorState& hero = m_world->Actors()[0];
    hero.characterClass = ElfClass;
    hero.animation.action = Standing;
    // (0 + 22.5) / 45 + 1 = 1.5: el sector 1.
    const std::vector<std::uint8_t> lost{0xc1, 0x05, 0x18, 0x01, 0x8d};
    ASSERT_TRUE(Apply(Guild::WarEnd{2}).HasValue());
    EXPECT_EQ(hero.animation.action, 0xae);
    EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{lost, lost}));
    ASSERT_TRUE(Apply(Guild::WarEnd{6}).HasValue());
    EXPECT_EQ(LastNotice(), "empate");
    EXPECT_EQ(m_sent.size(), 2u);

    hero.animation.action = Walking;
    ASSERT_TRUE(Apply(Guild::WarEnd{4}).HasValue());
    EXPECT_EQ(hero.animation.action, Walking);
    EXPECT_EQ(m_sent, (std::vector<std::vector<std::uint8_t>>{lost, lost, lost}));

    GuildOf().warOn = 1;
    GuildOf().rivalIndex = 2;
    Fill(GuildOf().rival, "Teros");
    Runtime().social.duel.word5767816 = 0x1234;
    const std::string notice = LastNotice();
    const auto applied = Apply(Guild::WarEnd{7});
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(GuildOf().warOn, 1);
    EXPECT_EQ(GuildOf().rivalIndex, 2);
    EXPECT_EQ(TextOf(GuildOf().rival), "Teros");
    EXPECT_EQ(Runtime().social.duel.word5767816, 0x1234);
    EXPECT_EQ(LastNotice(), notice);
    EXPECT_EQ(m_sent.size(), 3u);
}

// Sin los modelos de 0x4ea030, el 62 corta después de marcar a los personajes y antes del gesto y de los C1 18.
TEST_F(GuildReceiveTest, WithoutTheModelsTheWarStartIsCut)
{
    NameTheGuild(2, 22, "Teros");
    const auto applied = Apply(Guild::WarStart{.name = GuildNameOf("Teros"), .type = 1, .team = 0}, false);
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x576a60"), std::string::npos);
    EXPECT_EQ(GuildOf().warOn, 1);
    EXPECT_EQ(GuildOf().warType, 1);
    EXPECT_EQ(LastNotice(), "partido con Teros");
    EXPECT_TRUE(m_sent.empty());
}

// 0x4d8150: con un resultado que no es 1 y sin guerra, apaga al rival y desmarca.
TEST_F(GuildReceiveTest, ARefusedWarRequestForgetsTheRival)
{
    Fill(GuildOf().rival, "Teros");
    GuildOf().rivalIndex = 2;
    m_world->Actors()[0].byte374 = 2;
    ASSERT_TRUE(Apply(Guild::WarRequestResult{1}).HasValue());
    EXPECT_EQ(GuildOf().rivalIndex, 2);
    ASSERT_TRUE(Apply(Guild::WarRequestResult{3}).HasValue());
    EXPECT_EQ(GuildOf().rivalIndex, -1);
    EXPECT_EQ(TextOf(GuildOf().rival), "");
    EXPECT_EQ(m_world->Actors()[0].byte374, 0);
    const std::vector<std::pair<std::int32_t, std::string>> expected{{Client::UI::ErrorMessage, "guerra 520"},
                                                                     {Client::UI::ErrorMessage, "guerra 522"}};
    EXPECT_EQ(Messages(), expected);
}
} // namespace
