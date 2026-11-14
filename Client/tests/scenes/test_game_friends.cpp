#include "Scenes/GameFriends.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Protocol/Friends.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/GameUi.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatLog.h"
#include "UI/FriendModel.h"
#include "UI/MessageArea.h"
#include "UI/OldWindows.h"
#include "UI/SlideBanner.h"

#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// Los amigos del juego (CL9 hito 9; 04 § Los amigos): 0x491080 con el registro propio y los paquetes C0..C4
// (0x4dc010..0x4dc720) sobre el administrador de las ventanas viejas y [0x5763518]. Los nombres y los textos son
// inventados.
namespace
{
using Client::Protocol::Friends::FriendAddResult;
using Client::Protocol::Friends::FriendDeleteResult;
using Client::Protocol::Friends::FriendList;
using Client::Protocol::Friends::FriendRequest;
using Client::Protocol::Friends::FriendRow;
using Client::Protocol::Friends::FriendState;
using Client::Render::Models::CrtRandom;
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;
using Client::Scenes::ChatReceiveContext;
using Client::Scenes::FriendsOpenContext;
using Client::Scenes::WorldState;
using Client::UI::FriendSend;
using Client::UI::FriendSendKind;
using Client::UI::OldWindowKind;

constexpr std::uint32_t Seed = 4242;
constexpr std::uint8_t EnterKey = 0x0d;

class LetterMeasure final : public ITextMeasure
{
public:
    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view text) const override
    {
        // De a 2 por letra: ninguna caja corta su texto en dos renglones (0x7d).
        return {static_cast<int>(text.size()) * 2, 13};
    }
};

std::map<std::size_t, std::string> FriendTexts()
{
    return {
        {228, "Aviso"},
        {991, "Pregunta"},
        {1037, "Si"},
        {1038, "No"},
        {990, "Amigos"},
        {1066, "Amigos sin servidor"},
        {1067, "falta nivel"},
        {1072, "cartas %d de %d"},
        {1075, "agregado"},
        {1047, " no existe"},
        {1048, "lista llena"},
        {1049, " ya esta"},
        {1050, "no a uno mismo"},
        {1068, "el otro sin nivel"},
        {1051, " quiere ser amigo"},
        {1052, "no se borra"},
        {1244, "ayuda de amigos"},
    };
}

class GameFriendsTest : public ::testing::Test
{
protected:
    GameFriendsTest()
        : m_world(std::make_unique<WorldState>()), m_random(Seed), m_texts(Client::Tests::MakeTextTable(FriendTexts()))
    {
    }

    [[nodiscard]] ChatReceiveContext Context()
    {
        return ChatReceiveContext{.world = *m_world,
                                  .random = m_random,
                                  .measure = &m_letters,
                                  .screen = Client::App::PixelSize{640, 480},
                                  .scene = Client::Scenes::SceneId::Game,
                                  .texts = &m_texts,
                                  .chosenSlot = std::nullopt};
    }

    [[nodiscard]] Client::UI::OldWindowStepView View()
    {
        return Client::UI::OldWindowStepView{.mouseX = 639,
                                             .mouseY = 479,
                                             .keyEdge = [this](std::uint8_t key) { return m_keys.erase(key) > 0; },
                                             .keyDown = {},
                                             .chatOpen = false,
                                             .texts = &m_oldTexts,
                                             .measure = &m_oldMeasure,
                                             .send = [this](const FriendSend& send) { m_sent.push_back(send); },
                                             .whisperTo = {},
                                             .openFriends = {}};
    }

    Client::Core::Expected<void, std::string> Open()
    {
        return Client::Scenes::OpenTheFriends(FriendsOpenContext{.world = *m_world,
                                                                 .random = m_random,
                                                                 .measure = &m_letters,
                                                                 .screen = Client::App::PixelSize{640, 480},
                                                                 .texts = &m_texts},
                                              Client::Scenes::OldWindowGlobalsOf(Runtime()), View());
    }

    Client::Core::Expected<void, std::string> StepTheWindows()
    {
        return Client::UI::StepTheOldWindows(Manager(), Client::Scenes::OldWindowGlobalsOf(Runtime()), View());
    }

    [[nodiscard]] Client::Scenes::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    [[nodiscard]] Client::UI::OldWindowManager& Manager()
    {
        return Runtime().oldWindows;
    }

    [[nodiscard]] static std::uint32_t After(std::uint32_t count)
    {
        CrtRandom expected(Seed);
        expected.Discard(count);
        return expected.State();
    }

    [[nodiscard]] std::vector<std::string> ModelNames()
    {
        std::vector<std::string> names;
        for (const auto& entry : Manager().friendModel.entries)
            names.push_back(entry.name);
        return names;
    }

    // Las cajas de mensaje abiertas, con su primer renglón.
    [[nodiscard]] std::vector<std::string> BoxLines()
    {
        std::vector<std::string> lines;
        for (const auto& [id, window] : Manager().windows)
        {
            if (window.kind == OldWindowKind::MessageBox)
                lines.push_back(window.box.lines[0]);
        }
        return lines;
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random;
    LetterMeasure m_letters;
    Client::Data::Text::TextTable m_texts;
    Client::UI::OldWindowTexts m_oldTexts = Client::Scenes::OldWindowTextsOf(&m_texts);
    Client::UI::OldWindowMeasure m_oldMeasure{
        .measure = m_letters, .dcFont = FontKind::Normal, .screen = Client::App::PixelSize{640, 480}};
    std::set<std::uint8_t> m_keys;
    std::vector<FriendSend> m_sent;
};

// 0x491080 con menos de nivel 6: el registro propio se cierra una vez (3.848 rand()) y el 1067 va al chat como mensaje
// del sistema, salvo que esté entre los últimos (0x45db70 con 1).
TEST_F(GameFriendsTest, BelowLevelSixOpeningSaysTheLevelText)
{
    m_world->OwnCharacter().level = 5;
    auto opened = Open();
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    EXPECT_EQ(m_random.State(), After(Client::Render::Models::OwnRecordCloseRandoms));
    EXPECT_EQ(Manager().friends, 0);
    ASSERT_EQ(Runtime().chat.log.entries.size(), 1u);
    EXPECT_EQ(Runtime().chat.log.entries[0].text, "falta nivel");
    EXPECT_EQ(Runtime().chat.log.entries[0].type, Client::UI::SystemMessage);

    opened = Open();
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    EXPECT_EQ(m_random.State(), After(2 * Client::Render::Models::OwnRecordCloseRandoms));
    EXPECT_EQ(Runtime().chat.log.entries.size(), 1u);
}

// Con nivel 6, la ventana, los mismos 3.848 rand() y, con las dos colas del cartel quietas, el 1244 en la normal (el
// tipo 2).
TEST_F(GameFriendsTest, AtLevelSixOpeningBuildsTheWindowAndTheHelpBanner)
{
    m_world->OwnCharacter().level = 6;
    const auto opened = Open();
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    EXPECT_EQ(m_random.State(), After(Client::Render::Models::OwnRecordCloseRandoms));
    EXPECT_NE(Manager().friends, 0);
    EXPECT_EQ(Manager().windows.at(Manager().friends).frame.title, std::optional<std::string>("Amigos"));
    ASSERT_EQ(Runtime().hud.slides.normal.entries.size(), 1u);
    EXPECT_EQ(Runtime().hud.slides.normal.entries.begin()->second.text, "ayuda de amigos");
    EXPECT_EQ(Runtime().hud.slides.normal.entries.begin()->second.kind, 1);
    EXPECT_TRUE(Runtime().hud.slides.bold.entries.empty());
    EXPECT_TRUE(Runtime().chat.log.entries.empty());
}

// Con el registro propio ya abierto, el cierre no saca nada; la ventana a la vista se suelta.
TEST_F(GameFriendsTest, OpeningAgainWithTheRecordOpenDropsTheWindowWithoutRandoms)
{
    m_world->OwnCharacter().level = 40;
    ASSERT_TRUE(Open().HasValue());
    Runtime().ownRecordDepth = 1;
    const std::uint32_t before = m_random.State();
    ASSERT_TRUE(Open().HasValue());
    EXPECT_EQ(m_random.State(), before);
    EXPECT_EQ(Manager().friends, 0);
}

// 0x4dc010: la lista entera por nombre y después por estado, la pestaña, +0xe8 en 1 con el título 990, el tope de las
// cartas y el 1072 con cuántas y el tope.
TEST_F(GameFriendsTest, TheListFillsTheModelSortedAndSaysTheLetters)
{
    m_world->OwnCharacter().level = 6;
    ASSERT_TRUE(Open().HasValue());
    Manager().listReceived = 0;
    Manager().titleText = 1066;
    Client::UI::AddAFriend(Manager().friendModel, "Viejo", 0, 1);
    const FriendList list{.letters = 2,
                          .maxLetters = 50,
                          .friends = {FriendRow{"Benteveo", 0xff}, FriendRow{"Zorzal", 3}, FriendRow{"Hornero", 3},
                                      FriendRow{"Calandria", 0xff}}};
    const auto applied = Client::Scenes::ApplyFriendList(list, Context());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(ModelNames(), (std::vector<std::string>{"Calandria", "Benteveo", "Zorzal", "Hornero"}));
    EXPECT_EQ(Manager().friendModel.sortMode, Client::UI::ByStateSort);
    const auto& tab = Manager().windows.at(Manager().friends).tabs[0];
    ASSERT_EQ(tab.tab.friends.size(), 4u);
    EXPECT_EQ(tab.tab.friends[0].name, "Calandria");
    EXPECT_EQ(Manager().listReceived, 1);
    EXPECT_EQ(Manager().titleText, 990);
    EXPECT_EQ(Manager().windows.at(Manager().friends).frame.title, std::optional<std::string>("Amigos"));
    EXPECT_EQ(Manager().letterLimit, 50);
    ASSERT_EQ(Runtime().chat.log.entries.size(), 1u);
    EXPECT_EQ(Runtime().chat.log.entries[0].text, "cartas 2 de 50");
}

// 0x4dc38a: con 0 cartas, nada; las cuentas van sin signo.
TEST_F(GameFriendsTest, AListWithoutLettersSaysNothingAndTheLettersHaveNoSign)
{
    ASSERT_TRUE(Client::Scenes::ApplyFriendList(FriendList{.letters = 0, .maxLetters = 10, .friends = {}}, Context())
                    .HasValue());
    EXPECT_TRUE(Runtime().chat.log.entries.empty());
    EXPECT_TRUE(Manager().friendModel.entries.empty());
    ASSERT_TRUE(Client::Scenes::ApplyFriendList(FriendList{.letters = 0xff, .maxLetters = 10, .friends = {}}, Context())
                    .HasValue());
    ASSERT_EQ(Runtime().chat.log.entries.size(), 1u);
    EXPECT_EQ(Runtime().chat.log.entries[0].text, "cartas 255 de 10");
}

// 0x4dc3d0 con 1: el 1075 al chat y el amigo al final, ordenado con el orden de antes.
TEST_F(GameFriendsTest, AnAddedFriendJoinsTheListInTheSameOrder)
{
    ASSERT_TRUE(
        Client::Scenes::ApplyFriendList(
            FriendList{.letters = 0, .maxLetters = 0, .friends = {FriendRow{"Zorzal", 2}, FriendRow{"Tero", 1}}},
            Context())
            .HasValue());
    const auto applied =
        Client::Scenes::ApplyFriendAddResult(FriendAddResult{.result = 1, .name = "Hornero", .state = 5}, Context());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(ModelNames(), (std::vector<std::string>{"Hornero", "Zorzal", "Tero"}));
    ASSERT_EQ(Runtime().chat.log.entries.size(), 1u);
    EXPECT_EQ(Runtime().chat.log.entries[0].text, "agregado");
    EXPECT_TRUE(BoxLines().empty());
}

// 0x4dc3d0 con 0, 3, 4, 5 y 6: una caja de un botón con el 1047 (con el nombre), 1048, 1049 (con el nombre), 1050 o
// 1068; con otro, nada.
TEST_F(GameFriendsTest, TheAddFailuresOpenANoticeBox)
{
    struct Case
    {
        std::uint8_t result;
        std::string line;
    };
    for (const Case& failure : {Case{0, "Hornero no existe"}, Case{3, "lista llena"}, Case{4, "Hornero ya esta"},
                                Case{5, "no a uno mismo"}, Case{6, "el otro sin nivel"}, Case{2, ""}})
    {
        SCOPED_TRACE(failure.result);
        Client::UI::ResetTheOldWindows(Manager());
        const auto applied = Client::Scenes::ApplyFriendAddResult(
            FriendAddResult{.result = failure.result, .name = "Hornero", .state = 0}, Context());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_TRUE(Manager().friendModel.entries.empty());
        if (failure.line.empty())
        {
            EXPECT_TRUE(BoxLines().empty());
            continue;
        }
        EXPECT_EQ(BoxLines(), std::vector<std::string>{failure.line});
        EXPECT_EQ(Manager().boxes.size(), 1u);
    }
}

// 0x4dc5b0: "<nombre><1051>" en una caja de dos botones con dueño −1 que guarda el nombre, y el sonido 0x2a; el sí
// (Enter sobre la de arriba) manda C1 0E C2 01 con el nombre.
TEST_F(GameFriendsTest, ARequestAsksAndTheYesGoesBackWithTheName)
{
    const auto applied = Client::Scenes::ApplyFriendRequest(FriendRequest{.name = "Calandria"}, Context());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(BoxLines(), std::vector<std::string>{"Calandria quiere ser amigo"});
    EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>(0x2a));
    const std::int32_t box = Manager().order.back();
    EXPECT_EQ(Manager().windows.at(box).box.owner, -1);
    EXPECT_EQ(Manager().windows.at(box).box.friendName, "Calandria");

    // El paso que lee Enter le encola el 10 con 1 (0x4a43c0); el que sigue lo atiende (0x4a3b80) y la cierra.
    m_keys.insert(EnterKey);
    auto stepped = StepTheWindows();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(m_sent.empty());
    stepped = StepTheWindows();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(m_sent[0].kind, FriendSendKind::Answer);
    EXPECT_EQ(m_sent[0].value, 1);
    EXPECT_EQ(m_sent[0].name, "Calandria");
    EXPECT_FALSE(Manager().windows.contains(box));
}

// 0x4dc690: con 0, la caja con el 1052; con 1, fuera de la lista; con otro, nada.
TEST_F(GameFriendsTest, TheDeleteResultRemovesOrSaysItCouldNot)
{
    ASSERT_TRUE(
        Client::Scenes::ApplyFriendList(
            FriendList{.letters = 0, .maxLetters = 0, .friends = {FriendRow{"Zorzal", 2}, FriendRow{"Tero", 1}}},
            Context())
            .HasValue());
    ASSERT_TRUE(
        Client::Scenes::ApplyFriendDeleteResult(FriendDeleteResult{.result = 0, .name = "Tero"}, Context()).HasValue());
    EXPECT_EQ(BoxLines(), std::vector<std::string>{"no se borra"});
    EXPECT_EQ(ModelNames(), (std::vector<std::string>{"Zorzal", "Tero"}));
    ASSERT_TRUE(
        Client::Scenes::ApplyFriendDeleteResult(FriendDeleteResult{.result = 2, .name = "Tero"}, Context()).HasValue());
    EXPECT_EQ(ModelNames(), (std::vector<std::string>{"Zorzal", "Tero"}));
    ASSERT_TRUE(
        Client::Scenes::ApplyFriendDeleteResult(FriendDeleteResult{.result = 1, .name = "Tero"}, Context()).HasValue());
    EXPECT_EQ(ModelNames(), (std::vector<std::string>{"Zorzal"}));
}

// 0x4dc720: el estado del amigo y la lista vuelta a ordenar; con 0xfc, todos fuera de línea y el título 1066.
TEST_F(GameFriendsTest, TheStateChangesOneFriendOrAllOfThem)
{
    m_world->OwnCharacter().level = 6;
    ASSERT_TRUE(Open().HasValue());
    ASSERT_TRUE(
        Client::Scenes::ApplyFriendList(
            FriendList{.letters = 0, .maxLetters = 0, .friends = {FriendRow{"Zorzal", 2}, FriendRow{"Tero", 1}}},
            Context())
            .HasValue());
    EXPECT_EQ(ModelNames(), (std::vector<std::string>{"Zorzal", "Tero"}));
    ASSERT_TRUE(Client::Scenes::ApplyFriendState(FriendState{.name = "Tero", .state = 7}, Context()).HasValue());
    EXPECT_EQ(ModelNames(), (std::vector<std::string>{"Tero", "Zorzal"}));
    EXPECT_EQ(Manager().windows.at(Manager().friends).tabs[0].tab.friends[0].state, 7);

    ASSERT_TRUE(Client::Scenes::ApplyFriendState(FriendState{.name = "", .state = 0xfc}, Context()).HasValue());
    for (const auto& entry : Manager().friendModel.entries)
        EXPECT_EQ(entry.state, 0xfc);
    EXPECT_EQ(Manager().listReceived, 0);
    EXPECT_EQ(Manager().titleText, 1066);
    EXPECT_EQ(Manager().windows.at(Manager().friends).frame.title, std::optional<std::string>("Amigos sin servidor"));
}

// El paquete de cada pedido de la ventana (Scenes::FriendPacketOf).
TEST_F(GameFriendsTest, EachSendBecomesItsPacket)
{
    const auto bytesOf = [](const Client::Protocol::Outgoing& sent)
    { return std::vector<std::byte>(sent.packet.Bytes().begin(), sent.packet.Bytes().end()); };
    using Client::Scenes::FriendPacketOf;
    EXPECT_EQ(bytesOf(FriendPacketOf(FriendSend{.kind = FriendSendKind::ListRequest, .value = 0, .name = {}})),
              bytesOf(Client::Protocol::Friends::WriteFriendListRequest()));
    EXPECT_EQ(bytesOf(FriendPacketOf(FriendSend{.kind = FriendSendKind::Add, .value = 0, .name = "Tero"})),
              bytesOf(Client::Protocol::Friends::WriteFriendAdd("Tero")));
    EXPECT_EQ(bytesOf(FriendPacketOf(FriendSend{.kind = FriendSendKind::Answer, .value = 1, .name = "Tero"})),
              bytesOf(Client::Protocol::Friends::WriteFriendAnswer(1, "Tero")));
    EXPECT_EQ(bytesOf(FriendPacketOf(FriendSend{.kind = FriendSendKind::Delete, .value = 0, .name = "Tero"})),
              bytesOf(Client::Protocol::Friends::WriteFriendDelete("Tero")));
    EXPECT_EQ(bytesOf(FriendPacketOf(FriendSend{.kind = FriendSendKind::State, .value = 1, .name = {}})),
              bytesOf(Client::Protocol::Friends::WriteFriendState(1)));
}
} // namespace
