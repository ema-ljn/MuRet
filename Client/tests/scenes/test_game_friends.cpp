#include "Messenger/GameFriends.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/TextTable.h"
#include "Protocol/Messenger/Friends.h"
#include "Protocol/Outgoing.h"
#include "Models/LogicalAccess.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Chat/ChatReceive.h"
#include "Hud/GameUi.h"
#include "Kernel/SceneId.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatLog.h"
#include "Messenger/FriendModel.h"
#include "Ui/MessageArea.h"
#include "Ui/OldWindows.h"
#include "Ui/SlideBanner.h"
#include "Audio/SoundId.h"

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
using Client::Chat::ChatReceiveContext;
using Client::Crt::CrtRandom;
using Client::Kernel::WorldState;
using Client::Messenger::FriendsOpenContext;
using Client::Protocol::Messenger::FriendAddResult;
using Client::Protocol::Messenger::FriendDeleteResult;
using Client::Protocol::Messenger::FriendList;
using Client::Protocol::Messenger::FriendRequest;
using Client::Protocol::Messenger::FriendRow;
using Client::Protocol::Messenger::FriendState;
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;
using Client::Ui::FriendSend;
using Client::Ui::FriendSendKind;
using Client::Ui::OldWindowKind;

constexpr std::uint32_t Seed = 4242;
constexpr std::uint8_t EnterKey = 0x0d;
// 0x48ee60: ocupación de las diagonales y filas de la cascada para la caja de 150 × 100 que parte de (0, 332).
constexpr std::int32_t CascadeStep = 20;
constexpr std::int32_t DefaultBoxY = 332;
constexpr std::int32_t LastBlockedX = 460;
constexpr std::int32_t BlockedRowOffset = 360;
constexpr std::int32_t FirstCascadeWrap = 3;
constexpr std::int32_t LastCascadeWrap = 9;

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
                                  .screen = Client::Core::PixelSize{640, 480},
                                  .scene = Client::Kernel::SceneId::Game,
                                  .texts = &m_texts,
                                  .chosenSlot = std::nullopt};
    }

    [[nodiscard]] Client::Ui::OldWindowStepView View()
    {
        return Client::Ui::OldWindowStepView{.mouseX = 639,
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
        return Client::Messenger::OpenTheFriends(FriendsOpenContext{.world = *m_world,
                                                                    .random = m_random,
                                                                    .measure = &m_letters,
                                                                    .screen = Client::Core::PixelSize{640, 480},
                                                                    .texts = &m_texts},
                                                 Client::Hud::OldWindowGlobalsOf(Runtime()), View());
    }

    Client::Core::Expected<void, std::string> StepTheWindows()
    {
        return Client::Ui::StepTheOldWindows(Manager(), Client::Hud::OldWindowGlobalsOf(Runtime()), View());
    }

    [[nodiscard]] Client::Kernel::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    [[nodiscard]] Client::Ui::OldWindowManager& Manager()
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

    void BlockTheFactoryCascade()
    {
        // 191 marcos con KeepPlace: ocupación fija, sin simular la lógica de búsqueda de la fábrica.
        const auto block = [this](std::int32_t x, std::int32_t y)
        {
            const auto created =
                Client::Ui::CreateTheOldWindow(Manager(),
                                               Client::Ui::OldWindowRequest{.kind = Client::Ui::FrameCase,
                                                                            .x = x,
                                                                            .y = y,
                                                                            .data = "obstáculo",
                                                                            .flags = Client::Ui::KeepPlaceFlag},
                                               m_oldTexts, m_oldMeasure);
            ASSERT_TRUE(created.HasValue()) << created.Error();
            ASSERT_NE(created.Value(), 0);
        };
        block(CascadeStep, DefaultBoxY + CascadeStep);
        for (std::int32_t x = 2 * CascadeStep; x <= LastBlockedX; x += CascadeStep)
            block(x, DefaultBoxY + 2 * CascadeStep);
        for (std::int32_t rest = FirstCascadeWrap; rest <= LastCascadeWrap; ++rest)
        {
            for (std::int32_t offset = 0; offset < BlockedRowOffset; offset += CascadeStep)
                block(rest + offset, rest + offset);
            for (std::int32_t offset = BlockedRowOffset; offset <= LastBlockedX; offset += CascadeStep)
                block(rest + offset, rest + BlockedRowOffset);
        }
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random;
    LetterMeasure m_letters;
    Client::Tables::TextTable m_texts;
    Client::Ui::OldWindowTexts m_oldTexts = Client::Messenger::OldWindowTextsOf(&m_texts);
    Client::Ui::OldWindowMeasure m_oldMeasure{
        .measure = m_letters, .dcFont = FontKind::Normal, .screen = Client::Core::PixelSize{640, 480}};
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
    EXPECT_EQ(m_random.State(), After(Client::Models::OwnRecordCloseRandoms));
    EXPECT_EQ(Manager().friends, 0);
    ASSERT_EQ(Runtime().chat.log.entries.size(), 1u);
    EXPECT_EQ(Runtime().chat.log.entries[0].text, "falta nivel");
    EXPECT_EQ(Runtime().chat.log.entries[0].type, Client::Ui::SystemMessage);

    opened = Open();
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    EXPECT_EQ(m_random.State(), After(2 * Client::Models::OwnRecordCloseRandoms));
    EXPECT_EQ(Runtime().chat.log.entries.size(), 1u);
}

// Con nivel 6, la ventana, los mismos 3.848 rand() y, con las dos colas del cartel quietas, el 1244 en la normal (el
// tipo 2).
TEST_F(GameFriendsTest, AtLevelSixOpeningBuildsTheWindowAndTheHelpBanner)
{
    m_world->OwnCharacter().level = 6;
    const auto opened = Open();
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    EXPECT_EQ(m_random.State(), After(Client::Models::OwnRecordCloseRandoms));
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
    Client::Messenger::AddAFriend(Manager().friendModel, "Viejo", 0, 1);
    const FriendList list{.letters = 2,
                          .maxLetters = 50,
                          .friends = {FriendRow{"Benteveo", 0xff}, FriendRow{"Zorzal", 3}, FriendRow{"Hornero", 3},
                                      FriendRow{"Calandria", 0xff}}};
    const auto applied = Client::Messenger::ApplyFriendList(list, Context());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(ModelNames(), (std::vector<std::string>{"Calandria", "Benteveo", "Zorzal", "Hornero"}));
    EXPECT_EQ(Manager().friendModel.sortMode, Client::Messenger::ByStateSort);
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
    ASSERT_TRUE(Client::Messenger::ApplyFriendList(FriendList{.letters = 0, .maxLetters = 10, .friends = {}}, Context())
                    .HasValue());
    EXPECT_TRUE(Runtime().chat.log.entries.empty());
    EXPECT_TRUE(Manager().friendModel.entries.empty());
    ASSERT_TRUE(
        Client::Messenger::ApplyFriendList(FriendList{.letters = 0xff, .maxLetters = 10, .friends = {}}, Context())
            .HasValue());
    ASSERT_EQ(Runtime().chat.log.entries.size(), 1u);
    EXPECT_EQ(Runtime().chat.log.entries[0].text, "cartas 255 de 10");
}

// 0x4dc3d0 con 1: el 1075 al chat y el amigo al final, ordenado con el orden de antes.
TEST_F(GameFriendsTest, AnAddedFriendJoinsTheListInTheSameOrder)
{
    ASSERT_TRUE(
        Client::Messenger::ApplyFriendList(
            FriendList{.letters = 0, .maxLetters = 0, .friends = {FriendRow{"Zorzal", 2}, FriendRow{"Tero", 1}}},
            Context())
            .HasValue());
    const auto applied =
        Client::Messenger::ApplyFriendAddResult(FriendAddResult{.result = 1, .name = "Hornero", .state = 5}, Context());
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
        Client::Ui::ResetTheOldWindows(Manager(), m_world->Runtime().ui.pressedButton);
        const auto applied = Client::Messenger::ApplyFriendAddResult(
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

// 0x4dc3d0 → 0x48ee60: el destructor del cuadro recibido que no entró debe escribir el global real del juego.
TEST_F(GameFriendsTest, AReceivedNoticeReleasesTheReusedButtonWhenItsCascadeAborts)
{
    ASSERT_TRUE(
        Client::Messenger::ApplyFriendAddResult(FriendAddResult{.result = 0, .name = "Hornero", .state = 0}, Context())
            .HasValue());
    ASSERT_EQ(Manager().windows.size(), 1u);
    const std::int32_t firstId = Manager().order.back();
    const std::int32_t buttonId = Manager().windows.at(firstId).box.buttons.front().base.id;
    BlockTheFactoryCascade();
    const std::size_t windowCount = Manager().windows.size();
    Runtime().ui.pressedButton = buttonId;
    Manager().lastId = firstId - 1;

    const auto received =
        Client::Messenger::ApplyFriendAddResult(FriendAddResult{.result = 3, .name = "Hornero", .state = 0}, Context());

    ASSERT_TRUE(received.HasValue()) << received.Error();
    EXPECT_EQ(Runtime().ui.pressedButton, 0);
    EXPECT_EQ(Manager().windows.size(), windowCount);
    EXPECT_EQ(Manager().order.size(), windowCount);
    EXPECT_EQ(BoxLines(), std::vector<std::string>{"Hornero no existe"});
    // 0x4917ea → 0x4918a0 deduplica el ID antes del abandono; la fábrica sí consumió los dos IDs de botón.
    EXPECT_EQ(Manager().boxes.size(), 1u);
    EXPECT_EQ(Manager().lastId, firstId + 2);
}

// 0x4dc5b0: "<nombre><1051>" en una caja de dos botones con dueño −1 que guarda el nombre, y el sonido 0x2a; el sí
// (Enter sobre la de arriba) manda C1 0E C2 01 con el nombre.
TEST_F(GameFriendsTest, ARequestAsksAndTheYesGoesBackWithTheName)
{
    const auto applied = Client::Messenger::ApplyFriendRequest(FriendRequest{.name = "Calandria"}, Context());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(BoxLines(), std::vector<std::string>{"Calandria quiere ser amigo"});
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x2a});
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
        Client::Messenger::ApplyFriendList(
            FriendList{.letters = 0, .maxLetters = 0, .friends = {FriendRow{"Zorzal", 2}, FriendRow{"Tero", 1}}},
            Context())
            .HasValue());
    ASSERT_TRUE(Client::Messenger::ApplyFriendDeleteResult(FriendDeleteResult{.result = 0, .name = "Tero"}, Context())
                    .HasValue());
    EXPECT_EQ(BoxLines(), std::vector<std::string>{"no se borra"});
    EXPECT_EQ(ModelNames(), (std::vector<std::string>{"Zorzal", "Tero"}));
    ASSERT_TRUE(Client::Messenger::ApplyFriendDeleteResult(FriendDeleteResult{.result = 2, .name = "Tero"}, Context())
                    .HasValue());
    EXPECT_EQ(ModelNames(), (std::vector<std::string>{"Zorzal", "Tero"}));
    ASSERT_TRUE(Client::Messenger::ApplyFriendDeleteResult(FriendDeleteResult{.result = 1, .name = "Tero"}, Context())
                    .HasValue());
    EXPECT_EQ(ModelNames(), (std::vector<std::string>{"Zorzal"}));
}

// 0x4dc720: el estado del amigo y la lista vuelta a ordenar; con 0xfc, todos fuera de línea y el título 1066.
TEST_F(GameFriendsTest, TheStateChangesOneFriendOrAllOfThem)
{
    m_world->OwnCharacter().level = 6;
    ASSERT_TRUE(Open().HasValue());
    ASSERT_TRUE(
        Client::Messenger::ApplyFriendList(
            FriendList{.letters = 0, .maxLetters = 0, .friends = {FriendRow{"Zorzal", 2}, FriendRow{"Tero", 1}}},
            Context())
            .HasValue());
    EXPECT_EQ(ModelNames(), (std::vector<std::string>{"Zorzal", "Tero"}));
    ASSERT_TRUE(Client::Messenger::ApplyFriendState(FriendState{.name = "Tero", .state = 7}, Context()).HasValue());
    EXPECT_EQ(ModelNames(), (std::vector<std::string>{"Tero", "Zorzal"}));
    EXPECT_EQ(Manager().windows.at(Manager().friends).tabs[0].tab.friends[0].state, 7);

    ASSERT_TRUE(Client::Messenger::ApplyFriendState(FriendState{.name = "", .state = 0xfc}, Context()).HasValue());
    for (const auto& entry : Manager().friendModel.entries)
        EXPECT_EQ(entry.state, 0xfc);
    EXPECT_EQ(Manager().listReceived, 0);
    EXPECT_EQ(Manager().titleText, 1066);
    EXPECT_EQ(Manager().windows.at(Manager().friends).frame.title, std::optional<std::string>("Amigos sin servidor"));
}

// El paquete de cada pedido de la ventana (Messenger::FriendPacketOf).
TEST_F(GameFriendsTest, EachSendBecomesItsPacket)
{
    const auto bytesOf = [](const Client::Protocol::Outgoing& sent)
    { return std::vector<std::byte>(sent.packet.Bytes().begin(), sent.packet.Bytes().end()); };
    using Client::Messenger::FriendPacketOf;
    EXPECT_EQ(bytesOf(FriendPacketOf(FriendSend{.kind = FriendSendKind::ListRequest, .value = 0, .name = {}})),
              bytesOf(Client::Protocol::Messenger::WriteFriendListRequest()));
    EXPECT_EQ(bytesOf(FriendPacketOf(FriendSend{.kind = FriendSendKind::Add, .value = 0, .name = "Tero"})),
              bytesOf(Client::Protocol::Messenger::WriteFriendAdd("Tero")));
    EXPECT_EQ(bytesOf(FriendPacketOf(FriendSend{.kind = FriendSendKind::Answer, .value = 1, .name = "Tero"})),
              bytesOf(Client::Protocol::Messenger::WriteFriendAnswer(1, "Tero")));
    EXPECT_EQ(bytesOf(FriendPacketOf(FriendSend{.kind = FriendSendKind::Delete, .value = 0, .name = "Tero"})),
              bytesOf(Client::Protocol::Messenger::WriteFriendDelete("Tero")));
    EXPECT_EQ(bytesOf(FriendPacketOf(FriendSend{.kind = FriendSendKind::State, .value = 1, .name = {}})),
              bytesOf(Client::Protocol::Messenger::WriteFriendState(1)));
}
} // namespace
