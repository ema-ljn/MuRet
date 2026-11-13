#include "Scenes/GameLetters.h"

#include "App/IChatRoomLinks.h"
#include "App/WindowState.h"
#include "App/WorldEvent.h"
#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Network/Delivery.h"
#include "Protocol/FriendChat.h"
#include "Protocol/Friends.h"
#include "Protocol/Letters.h"
#include "Protocol/OptionData.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/GameFriends.h"
#include "Scenes/GameUi.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/FriendModel.h"
#include "UI/FriendWindows.h"
#include "UI/LetterModel.h"
#include "UI/LetterWindows.h"
#include "UI/OldControls.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindows.h"

#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// El chat con un amigo y las cartas del juego (CL9 hito 10; 04 § Las cartas y el chat con un amigo): los botones 3 y 4
// de la pestaña de amigos (0x49d0b0), el chat (0x4944c0), la de escribir (0x498820), la pestaña de las cartas
// (0x4a11b0), la de leer (0x49a1f0) y los paquetes C5..C8, CA y CB (0x4dc840..0x4dd020), byte a byte. Los nombres y los
// textos son inventados.
namespace
{
using Client::Protocol::Outgoing;
using Client::Render::Models::CrtRandom;
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;
using Client::Scenes::ChatReceiveContext;
using Client::Scenes::WorldState;
using Client::UI::FriendSend;
using Client::UI::FriendSendKind;
using Client::UI::OldPortraitPart;
using Client::UI::OldWindow;
using Client::UI::OldWindowKind;
using Client::UI::PortraitCallKind;

constexpr std::uint32_t Seed = 9191;
constexpr std::string_view Hero = "Yaguarete";

// La clave del XOR32 (0x6b8648), escrita de nuevo para no comparar el código consigo mismo.
constexpr std::array<int, 32> Xor32Key{0xe7, 0x6d, 0x3a, 0x89, 0xbc, 0xb2, 0x9f, 0x73, 0x23, 0xa8, 0xfe,
                                       0xb6, 0x49, 0x5d, 0x39, 0x5d, 0x8a, 0xcb, 0x63, 0x8d, 0xea, 0x7d,
                                       0x2b, 0x5f, 0xc3, 0xb1, 0xe9, 0x83, 0x29, 0x51, 0xe8, 0x56};

std::vector<std::byte> Bytes(std::initializer_list<int> values)
{
    std::vector<std::byte> bytes;
    for (const int value : values)
        bytes.push_back(static_cast<std::byte>(value));
    return bytes;
}

std::vector<std::byte> Xor32Of(std::vector<std::byte> plain, std::size_t first = 3)
{
    for (std::size_t at = first; at < plain.size(); ++at)
        plain[at] ^= static_cast<std::byte>(Xor32Key[at % Xor32Key.size()]) ^ plain[at - 1];
    return plain;
}

void Append(std::vector<std::byte>& bytes, std::string_view text, std::size_t size)
{
    for (std::size_t at = 0; at < size; ++at)
        bytes.push_back(at < text.size() ? static_cast<std::byte>(text[at]) : std::byte{0});
}

std::vector<std::byte> BytesOf(const Outgoing& sent)
{
    return {sent.packet.Bytes().begin(), sent.packet.Bytes().end()};
}

// El XOR de 3 bytes del chat (0x6c82e4), por el lugar.
std::vector<std::byte> ChatTextOf(std::string_view text, std::size_t size)
{
    constexpr std::array<int, 3> Key{0xfc, 0xcf, 0xab};
    std::vector<std::byte> bytes;
    Append(bytes, text, size);
    for (std::size_t at = 0; at < bytes.size(); ++at)
        bytes[at] ^= static_cast<std::byte>(Key[at % Key.size()]);
    return bytes;
}

std::vector<std::byte> Joined(std::vector<std::byte> head, const std::vector<std::byte>& tail)
{
    head.insert(head.end(), tail.begin(), tail.end());
    return head;
}

// Las conexiones de las salas (App::IChatRoomLinks), anotadas en orden.
class FakeRooms final : public Client::App::IChatRoomLinks
{
public:
    struct Call
    {
        std::string kind;
        std::uint32_t room = 0;
        std::int32_t window = 0;
        std::string address;
        std::vector<std::byte> bytes;
    };

    void OpenChatRoom(std::uint32_t room, std::int32_t window, std::string_view address) override
    {
        calls.push_back(
            Call{.kind = "open", .room = room, .window = window, .address = std::string(address), .bytes = {}});
    }
    void SendToChatRoom(std::uint32_t room, Outgoing packet) override
    {
        calls.push_back(Call{.kind = "send", .room = room, .window = 0, .address = {}, .bytes = BytesOf(packet)});
    }
    void CloseChatRoom(std::uint32_t room) override
    {
        calls.push_back(Call{.kind = "close", .room = room, .window = 0, .address = {}, .bytes = {}});
    }
    std::vector<Call> calls;
};

class LetterMeasure final : public ITextMeasure
{
public:
    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 2, 13};
    }
};

std::map<std::size_t, std::string> LetterTexts()
{
    return {
        {228, "Aviso"},
        {991, "Pregunta"},
        {1037, "Si"},
        {1038, "No"},
        {990, "Amigos"},
        {994, "Chat: "},
        {995, "[cerrado] "},
        {402, "se fue todo el mundo"},
        {993, "Invitar"},
        {996, "Ocultar"},
        {1000, "Para"},
        {1016, "Re: %s"},
        {1017, "Borrar la carta?"},
        {1046, "Carta mandada, costo %d"},
        {1053, "no salio"},
        {1055, "no se borro"},
        {1057, " entro invitado"},
        {1061, "no existe"},
        {1062, "carta nueva"},
        {1069, "no esta"},
        {1070, "fuera de linea"},
        {1071, "Carta nueva (%d)"},
        {1073, "buzon lleno"},
    };
}

class FakeRequests final : public Client::Scenes::IGameUiRequests
{
public:
    void SendOptionData(const Client::Protocol::OptionData::OptionBody& /*body*/) override {}
    void RequestLogout(std::uint8_t /*kind*/) override {}
    void RequestPartyList() override {}
    void SendFriendPacket(Outgoing packet) override
    {
        sent.push_back(BytesOf(packet));
    }
    [[nodiscard]] Client::Core::Expected<void, std::string> SaveMacros(std::string_view /*content*/) override
    {
        return {};
    }
    void Log(std::string_view /*line*/) override {}
    std::vector<std::vector<std::byte>> sent;
};

class GameLettersTest : public ::testing::Test
{
protected:
    GameLettersTest()
        : m_world(std::make_unique<WorldState>()), m_random(Seed), m_texts(Client::Tests::MakeTextTable(LetterTexts()))
    {
        auto& name = m_world->Actors()[Runtime().hero].name;
        for (std::size_t at = 0; at < Hero.size(); ++at)
            name[at] = static_cast<std::byte>(Hero[at]);
        Manager().letterLimit = 50;
    }

    [[nodiscard]] ChatReceiveContext Context()
    {
        return ChatReceiveContext{.world = *m_world,
                                  .random = m_random,
                                  .measure = &m_letters,
                                  .screen = Client::App::PixelSize{640, 480},
                                  .scene = Client::Scenes::SceneId::Game,
                                  .texts = &m_texts,
                                  .chosenSlot = std::nullopt,
                                  .rooms = &m_rooms};
    }

    // Lo que el juego le da al paso (Scenes/GameUi), con un retrato de prueba: arma el personaje (+0x68c en 1) con el
    // zoom de 0,8 y no saca rand().
    [[nodiscard]] Client::UI::OldWindowStepView View()
    {
        Client::UI::OldWindowStepView view{.mouseX = 639,
                                           .mouseY = 479,
                                           .keyEdge = [](std::uint8_t /*key*/) { return false; },
                                           .keyDown = {},
                                           .chatOpen = false,
                                           .texts = &m_oldTexts,
                                           .measure = &m_oldMeasure,
                                           .send = [this](const FriendSend& send) { m_sent.push_back(send); },
                                           .whisperTo = {},
                                           .openFriends = {}};
        view.portrait = [this](OldPortraitPart& portrait, PortraitCallKind kind,
                               std::int32_t /*arg*/) -> Client::Core::Expected<void, std::string>
        {
            m_portraitCalls.push_back(kind);
            if (kind == PortraitCallKind::Build)
            {
                portrait.built = 1;
                portrait.zoom = 0.8f;
            }
            return {};
        };
        view.replayLetter = [this](const std::vector<std::byte>& copy) -> Client::Core::Expected<void, std::string>
        {
            m_replayed.push_back(copy);
            return {};
        };
        view.heroName = std::string(Hero);
        return view;
    }

    [[nodiscard]] Client::UI::OldWindowGlobals Globals()
    {
        return Client::Scenes::OldWindowGlobalsOf(Runtime());
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::UI::StepTheOldWindows(Manager(), Globals(), View());
    }

    // La de amigos con tres: dos en línea y uno fuera.
    void OpenTheFriendsWithThree()
    {
        auto opened = Client::UI::OpenTheFriendsWindow(Manager(), Client::UI::FriendsWindowX,
                                                       Client::UI::FriendsWindowY, Globals(), View());
        ASSERT_TRUE(opened.HasValue()) << opened.Error();
        Client::UI::AddAFriend(Manager().friendModel, "Tordo", 0, 2);
        Client::UI::AddAFriend(Manager().friendModel, "Calandria", 0, 1);
        Client::UI::AddAFriend(Manager().friendModel, "Benteveo", 0, 0xfd);
        Client::UI::RefreshTheFriendTab(Manager());
    }

    void ChooseTheFriend(std::string_view name)
    {
        OldWindow& tab = Friends().tabs[Client::UI::FriendTabNumber];
        for (std::size_t at = 0; at < tab.tab.friends.size(); ++at)
        {
            if (tab.tab.friends[at].name == name)
                Client::UI::SelectInTheList(tab.tab.list, tab.tab.friends.size(), static_cast<std::int32_t>(at + 1));
        }
    }

    // El chat con Tordo en la sala 0x0304 (el botón 3 y el CA con 1), con el héroe y Tordo de miembros. Las llamadas a
    // las salas quedan vacías. Da el id del chat, o 0.
    [[nodiscard]] std::int32_t JoinARoomWithTordo()
    {
        OpenTheFriendsWithThree();
        ChooseTheFriend("Tordo");
        Client::UI::PostToTheWindow(Manager(), Friends().tabs[Client::UI::FriendTabNumber].base.id,
                                    Client::UI::CommandMessage, 3, 0);
        if (!Step().HasValue())
            return 0;
        const auto joined = Client::Scenes::ApplyRoomAnswer(
            Client::Protocol::FriendChat::RoomAnswer{
                .address = "10.0.0.9", .room = 0x0304, .ticket = 77, .type = 0, .name = "Tordo", .result = 1},
            Context());
        OldWindow* chat = FirstOfKind(OldWindowKind::ChatWindow);
        if (!joined.HasValue() || chat == nullptr)
            return 0;
        Client::UI::AddAChatMember(Manager(), *chat, Hero, 0, 0, m_oldTexts, Hero);
        Client::UI::AddAChatMember(Manager(), *chat, "Tordo", 1, 0, m_oldTexts, Hero);
        m_rooms.calls.clear();
        return chat->base.id;
    }

    [[nodiscard]] OldWindow& Friends()
    {
        return Manager().windows.at(Manager().friends);
    }

    [[nodiscard]] OldWindow* FirstOfKind(OldWindowKind kind)
    {
        for (auto& [id, window] : Manager().windows)
        {
            if (window.kind == kind)
                return &window;
        }
        return nullptr;
    }

    [[nodiscard]] bool ClosePosted(std::int32_t id)
    {
        for (const auto& message : Manager().queue)
        {
            if (message.type == Client::UI::CloseMessage && message.a == id)
                return true;
        }
        return false;
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

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random;
    LetterMeasure m_letters;
    Client::Data::Text::TextTable m_texts;
    Client::UI::OldWindowTexts m_oldTexts = Client::Scenes::OldWindowTextsOf(&m_texts);
    Client::UI::OldWindowMeasure m_oldMeasure{
        .measure = m_letters, .dcFont = FontKind::Normal, .screen = Client::App::PixelSize{640, 480}};
    std::vector<FriendSend> m_sent;
    std::vector<PortraitCallKind> m_portraitCalls;
    std::vector<std::vector<std::byte>> m_replayed;
    FakeRooms m_rooms;
};

// El botón 3 con un amigo en línea: C1 0D CA con su nombre, y queda pedido (+0x158): otro 3 no vuelve a mandar. El CA
// con 1 y el tipo 0 arma el chat (0x48ee60(1)), lo suma a +0xb4, toma un lugar (0x49e280), abre la conexión de la sala
// (0x49de50) y manda por ella el C1 10 00 con la sala y el ticket (0x493530). Con los miembros, el botón 1 muestra la
// lista de invitar (sin el amigo ni los de fuera de línea) y el 2, con el EDIT sin el foco, manda el C1 14 CB con la
// sala y la ventana. Nada de esto saca rand().
TEST_F(GameLettersTest, AChatWithAFriendInvitesAnother)
{
    OpenTheFriendsWithThree();
    ChooseTheFriend("Tordo");
    const std::int32_t friendTab = Friends().tabs[Client::UI::FriendTabNumber].base.id;
    Client::UI::PostToTheWindow(Manager(), friendTab, Client::UI::CommandMessage, 3, 0);
    ASSERT_TRUE(Step().HasValue());
    Client::UI::PostToTheWindow(Manager(), friendTab, Client::UI::CommandMessage, 3, 0);
    ASSERT_TRUE(Step().HasValue());
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(m_sent[0].kind, FriendSendKind::RoomRequest);
    auto request = Bytes({0xc1, 0x0d, 0xca});
    Append(request, "Tordo", 10);
    EXPECT_EQ(BytesOf(Client::Scenes::FriendPacketOf(m_sent[0])), Xor32Of(request));
    EXPECT_TRUE(Client::UI::IsAPendingChat(Manager(), "Tordo"));

    const auto joined = Client::Scenes::ApplyRoomAnswer(
        Client::Protocol::FriendChat::RoomAnswer{
            .address = "10.0.0.9", .room = 0x0304, .ticket = 77, .type = 0, .name = "Tordo", .result = 1},
        Context());
    ASSERT_TRUE(joined.HasValue()) << joined.Error();
    EXPECT_FALSE(Client::UI::IsAPendingChat(Manager(), "Tordo"));
    OldWindow* chat = FirstOfKind(OldWindowKind::ChatWindow);
    ASSERT_NE(chat, nullptr);
    const std::int32_t chatId = chat->base.id;
    EXPECT_EQ(chat->chat.room, 0x0304u);
    EXPECT_EQ(Manager().windowList.list.chats, std::deque<std::int32_t>{chatId});
    EXPECT_EQ(Manager().chatRooms.used[0], 1);
    EXPECT_EQ(Manager().chatRooms.slots.at(0), 0x0304u);
    const Client::UI::ChatRoom* room = Client::UI::FindTheChatRoom(Manager().chatRooms, 0x0304);
    ASSERT_NE(room, nullptr);
    EXPECT_EQ(room->window, chatId);
    ASSERT_EQ(m_rooms.calls.size(), 2u);
    EXPECT_EQ(m_rooms.calls[0].kind, "open");
    EXPECT_EQ(m_rooms.calls[0].room, 0x0304u);
    EXPECT_EQ(m_rooms.calls[0].window, chatId);
    EXPECT_EQ(m_rooms.calls[0].address, "10.0.0.9");
    EXPECT_EQ(m_rooms.calls[1].kind, "send");
    EXPECT_EQ(m_rooms.calls[1].room, 0x0304u);
    EXPECT_EQ(m_rooms.calls[1].bytes,
              Xor32Of(Joined(Bytes({0xc1, 0x10, 0x00, 0x00, 0x04, 0x03}), ChatTextOf("77", 10))));

    Client::UI::AddAChatMember(Manager(), *chat, Hero, 0, 0, m_oldTexts, Hero);
    EXPECT_EQ(Client::UI::AddAChatMember(Manager(), *chat, "Tordo", 1, 0, m_oldTexts, Hero), 2);
    EXPECT_EQ(chat->frame.title, std::optional<std::string>("Chat: Tordo"));
    EXPECT_EQ(Client::UI::FindTheChatWith(Manager(), "Tordo", Hero), chatId);

    Client::UI::PostToTheWindow(Manager(), chatId, Client::UI::CommandMessage, 1, 0);
    ASSERT_TRUE(Step().HasValue());
    chat = &Manager().windows.at(chatId);
    EXPECT_EQ(chat->chat.mode, 2);
    ASSERT_EQ(chat->chat.invitable.size(), 1u);
    EXPECT_EQ(chat->chat.invitable[0].name, "Calandria");
    Client::UI::SelectInTheList(chat->chat.invite, chat->chat.invitable.size(), 1);
    Runtime().chat.input.focus = Client::UI::ChatFocus::Game;
    Client::UI::PostToTheWindow(Manager(), chatId, Client::UI::CommandMessage, 2, 0);
    ASSERT_TRUE(Step().HasValue());
    ASSERT_EQ(m_sent.size(), 2u);
    EXPECT_EQ(m_sent[1].kind, FriendSendKind::Invite);
    auto invite = Bytes({0xc1, 0x14, 0xcb});
    Append(invite, "Calandria", 10);
    const auto window = static_cast<std::uint32_t>(chatId);
    for (const int value : {0x00, 0x04, 0x03, static_cast<int>(window & 0xffU), static_cast<int>((window >> 8) & 0xffU),
                            static_cast<int>((window >> 16) & 0xffU), static_cast<int>(window >> 24)})
        invite.push_back(static_cast<std::byte>(value));
    EXPECT_EQ(BytesOf(Client::Scenes::FriendPacketOf(m_sent[1])), Xor32Of(invite));

    ASSERT_TRUE(Client::Scenes::ApplyInviteResult(
                    Client::Protocol::FriendChat::InviteResult{.result = 1, .window = window}, Context())
                    .HasValue());
    ASSERT_FALSE(chat->chat.lineRows.empty());
    EXPECT_EQ(chat->chat.lineRows.back().text, "Calandria entro invitado");
    EXPECT_EQ(m_random.State(), After(0));
}

// Enter en el chat (0x4944c0 con el 7) deja el C1 04 para la sala, que sale cuando Scenes vacía las órdenes; el mismo
// texto otra vez no sale. El timer 1002 (0x4a6550) deja el C1 03 05. El FD_CLOSE de la sala (0x49e480) le da al chat
// el 402 de nadie y la sala sigue anotada. Soltar el chat (0x493160 → 0x493a70) manda el C1 03 01 y cierra la conexión
// (0x49dfd0), con el lugar libre. Nada saca rand().
TEST_F(GameLettersTest, TheChatRoomCarriesTheMessagesThePingAndTheLeave)
{
    const std::int32_t chatId = JoinARoomWithTordo();
    ASSERT_NE(chatId, 0);
    for (int times = 0; times < 2; ++times)
    {
        Manager().windows.at(chatId).chat.edit.text.SetText("hola");
        Client::UI::PostToTheWindow(Manager(), chatId, Client::UI::EnterMessage, 0, 0);
        auto stepped = Step();
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    }
    EXPECT_EQ(Manager().chatRooms.orders.size(), 1u);
    Client::UI::PingTheChatRooms(Manager());
    Client::Scenes::FlushTheChatRoomOrders(Manager().chatRooms, &m_rooms);
    EXPECT_TRUE(Manager().chatRooms.orders.empty());
    ASSERT_EQ(m_rooms.calls.size(), 2u);
    EXPECT_EQ(m_rooms.calls[0].kind, "send");
    EXPECT_EQ(m_rooms.calls[0].room, 0x0304u);
    EXPECT_EQ(m_rooms.calls[0].bytes, Xor32Of(Joined(Bytes({0xc1, 0x09, 0x04, 0x00, 0x04}), ChatTextOf("hola", 4))));
    EXPECT_EQ(m_rooms.calls[1].kind, "send");
    EXPECT_EQ(m_rooms.calls[1].bytes, Bytes({0xc1, 0x03, 0x05}));

    ASSERT_TRUE(Client::Scenes::ApplyChatRoomClosed(
                    Client::App::ChatRoomClosed{.room = 0x0304, .window = chatId, .opened = true}, Context())
                    .HasValue());
    const OldWindow& chat = Manager().windows.at(chatId);
    ASSERT_FALSE(chat.chat.lineRows.empty());
    EXPECT_EQ(chat.chat.lineRows.back().text, "se fue todo el mundo");
    EXPECT_NE(Client::UI::FindTheChatRoom(Manager().chatRooms, 0x0304), nullptr);

    m_rooms.calls.clear();
    Client::UI::PostToTheManager(Manager(), Client::UI::CloseMessage, chatId, 0);
    auto stepped = Step();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FALSE(Manager().windows.contains(chatId));
    EXPECT_EQ(Client::UI::FindTheChatRoom(Manager().chatRooms, 0x0304), nullptr);
    EXPECT_EQ(Manager().chatRooms.used[0], 0);
    Client::Scenes::FlushTheChatRoomOrders(Manager().chatRooms, &m_rooms);
    ASSERT_EQ(m_rooms.calls.size(), 2u);
    EXPECT_EQ(m_rooms.calls[0].kind, "send");
    EXPECT_EQ(m_rooms.calls[0].bytes, Bytes({0xc1, 0x03, 0x01}));
    EXPECT_EQ(m_rooms.calls[1].kind, "close");
    EXPECT_EQ(m_rooms.calls[1].room, 0x0304u);
    EXPECT_EQ(m_random.State(), After(0));
}

// El connect que falla (0x49de50 no anota la sala): la sala se olvida, el lugar de 0x49e280 queda tomado (Q-C1005) y
// Enter ya no manda nada. Las ventanas del primer paso del juego (0x48ea50) sueltan los chats, que salen de sus salas.
TEST_F(GameLettersTest, ARoomThatDoesNotConnectIsForgottenAndTheResetLeavesTheRooms)
{
    std::int32_t chatId = JoinARoomWithTordo();
    ASSERT_NE(chatId, 0);
    ASSERT_TRUE(Client::Scenes::ApplyChatRoomClosed(
                    Client::App::ChatRoomClosed{.room = 0x0304, .window = chatId, .opened = false}, Context())
                    .HasValue());
    EXPECT_EQ(Client::UI::FindTheChatRoom(Manager().chatRooms, 0x0304), nullptr);
    EXPECT_EQ(Manager().chatRooms.used[0], 1);
    EXPECT_TRUE(Manager().windows.at(chatId).chat.lineRows.empty());
    Manager().windows.at(chatId).chat.edit.text.SetText("hola");
    Client::UI::PostToTheWindow(Manager(), chatId, Client::UI::EnterMessage, 0, 0);
    ASSERT_TRUE(Step().HasValue());
    EXPECT_TRUE(Manager().chatRooms.orders.empty());

    Client::UI::ResetTheOldWindows(Manager());
    Client::UI::ClearTheFriends(Manager().friendModel);
    m_rooms.calls.clear();
    chatId = JoinARoomWithTordo();
    ASSERT_NE(chatId, 0);
    Client::UI::ResetTheOldWindows(Manager());
    EXPECT_TRUE(Manager().windows.empty());
    EXPECT_EQ(Client::UI::FindTheChatRoom(Manager().chatRooms, 0x0304), nullptr);
    Client::Scenes::FlushTheChatRoomOrders(Manager().chatRooms, &m_rooms);
    ASSERT_EQ(m_rooms.calls.size(), 2u);
    EXPECT_EQ(m_rooms.calls[0].bytes, Bytes({0xc1, 0x03, 0x01}));
    EXPECT_EQ(m_rooms.calls[1].kind, "close");
}

// 0x4dc720: el amigo fuera de línea cierra su chat (el 995 adelante y el EDIT apagado) y de vuelta en línea lo abre;
// con 0xfc todos los chats reciben el 402 y se cierran (0x4a75c0).
TEST_F(GameLettersTest, TheFriendStateClosesAndOpensItsChat)
{
    OpenTheFriendsWithThree();
    auto view = View();
    auto globals = Globals();
    auto created = Client::UI::CreateTheOldWindow(
        Manager(),
        Client::UI::OldWindowRequest{
            .kind = Client::UI::ChatCase, .x = 100, .y = 100, .data = "Chat: ", .aux = 0, .flags = 0},
        m_oldTexts, m_oldMeasure, &globals, &view);
    ASSERT_TRUE(created.HasValue()) << created.Error();
    OldWindow* chat = &Manager().windows.at(created.Value());
    Client::UI::AddAChatMember(Manager(), *chat, Hero, 0, 0, m_oldTexts, Hero);
    Client::UI::AddAChatMember(Manager(), *chat, "Tordo", 1, 0, m_oldTexts, Hero);
    ASSERT_EQ(Manager().windowList.list.chats.size(), 1u);

    ASSERT_TRUE(Client::Scenes::ApplyFriendState(Client::Protocol::Friends::FriendState{.name = "Tordo", .state = 0xfd},
                                                 Context())
                    .HasValue());
    EXPECT_EQ(chat->frame.title, std::optional<std::string>("[cerrado] Chat: Tordo"));
    EXPECT_EQ(chat->chat.edit.base.state, 6);
    ASSERT_TRUE(
        Client::Scenes::ApplyFriendState(Client::Protocol::Friends::FriendState{.name = "Tordo", .state = 3}, Context())
            .HasValue());
    EXPECT_EQ(chat->frame.title, std::optional<std::string>("Chat: Tordo"));
    EXPECT_EQ(chat->chat.edit.base.state, 0);
    ASSERT_TRUE(
        Client::Scenes::ApplyFriendState(Client::Protocol::Friends::FriendState{.name = "", .state = 0xfc}, Context())
            .HasValue());
    ASSERT_FALSE(chat->chat.lineRows.empty());
    EXPECT_EQ(chat->chat.lineRows.back().text, "se fue todo el mundo");
    EXPECT_EQ(chat->frame.title, std::optional<std::string>("[cerrado] Chat: Tordo"));
}

// El botón 4 arma la de escribir (0x48ee60(7) en (100, 100), el retrato armado, copiado del héroe y con la acción
// 0x7a) con el nombre del elegido, y la cola 0x49dc37 le manda el 8 y el 9. El botón 1 manda el C2 C5 con el cuerpo de
// "\r\n" a "\n", la dirección (90 / 6, con el zoom en 0) y la acción (0x7a − 0x78). Al juego, la carta le saca un
// rand() y el serial (Q-C1002); abrir la ventana, ninguno.
TEST_F(GameLettersTest, WritingALetterSendsItByteByByte)
{
    OpenTheFriendsWithThree();
    ChooseTheFriend("Tordo");
    Client::UI::PostToTheWindow(Manager(), Friends().tabs[Client::UI::FriendTabNumber].base.id,
                                Client::UI::CommandMessage, 4, 0);
    auto stepped = Step();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    OldWindow* write = FirstOfKind(OldWindowKind::LetterWriteWindow);
    ASSERT_NE(write, nullptr);
    EXPECT_EQ(m_random.State(), After(0));
    // Los cuatro de 0x497f50 (el ángulo con el personaje armado). Los dos pasos de 0x49dc37 van con 1: 0x45aa80 sale
    // después de la cola, sin 0x497970.
    EXPECT_EQ(m_portraitCalls,
              (std::vector<PortraitCallKind>{PortraitCallKind::Build, PortraitCallKind::CopyTheHero,
                                             PortraitCallKind::SetAction, PortraitCallKind::SetAngle}));
    EXPECT_EQ(write->frame.title, std::optional<std::string>("Carta nueva (1000)"));
    EXPECT_EQ(write->letter.to.text.Value(), "Tordo");
    EXPECT_EQ(write->letter.prepared, 1);
    EXPECT_EQ(write->base.current.type, Client::UI::SizeMessage);
    EXPECT_FLOAT_EQ(write->letter.portrait.angle, 90.0f);
    EXPECT_EQ(write->letter.portrait.action, 0x7a);

    write->letter.subject.text.SetText("Saludos");
    write->letter.text.text.SetText("Linea uno\r\nLinea dos");
    const std::int32_t writeId = write->base.id;
    Client::UI::PostToTheWindow(Manager(), writeId, Client::UI::CommandMessage, 1, 0);
    ASSERT_TRUE(Step().HasValue());
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(m_sent[0].kind, FriendSendKind::LetterSend);
    EXPECT_EQ(Manager().windows.at(writeId).letter.sending, 1);

    const std::string_view body = "Linea uno\nLinea dos";
    auto plain = Bytes({0xc2, 0x00, static_cast<int>(54 + body.size()), 0xc5});
    const auto window = static_cast<std::uint32_t>(writeId);
    for (std::uint32_t shift = 0; shift < 32; shift += 8)
        plain.push_back(static_cast<std::byte>((window >> shift) & 0xffU));
    Append(plain, "Tordo", 10);
    Append(plain, "Saludos", 0x20);
    for (const int value : {0x0f, 0x02, static_cast<int>(body.size()), 0x00})
        plain.push_back(static_cast<std::byte>(value));
    Append(plain, body, body.size());
    const Outgoing letter = Client::Scenes::FriendPacketOf(m_sent[0]);
    EXPECT_EQ(letter.delivery, Client::Network::Delivery::Encrypted);
    EXPECT_EQ(BytesOf(letter), Xor32Of(plain, 4));

    FakeRequests requests;
    Client::Scenes::SendAFriendPacket(requests, m_random, m_sent[0]);
    ASSERT_EQ(requests.sent.size(), 1u);
    EXPECT_EQ(requests.sent[0], BytesOf(letter));
    EXPECT_EQ(m_random.State(), After(1 + Client::Render::Models::SerialCloseRandoms));
    const std::uint32_t before = m_random.State();
    Client::Scenes::SendAFriendPacket(
        requests, m_random, FriendSend{.kind = FriendSendKind::LetterRead, .value = 0, .name = {}, .index = 7});
    EXPECT_EQ(m_random.State(), before);

    // 0x4dc840 con 1: el 4 a la de escribir y el 1046 con el costo al chat.
    ASSERT_TRUE(
        Client::Scenes::ApplyLetterSendResult(
            Client::Protocol::Letters::LetterSendResult{.result = 1, .window = static_cast<std::uint32_t>(writeId)},
            Context())
            .HasValue());
    EXPECT_TRUE(ClosePosted(writeId));
    ASSERT_FALSE(Runtime().chat.log.entries.empty());
    EXPECT_EQ(Runtime().chat.log.entries.back().text, "Carta mandada, costo 1000");
}

// Sin el retrato del juego (P-C212), abrir la de escribir corta.
TEST_F(GameLettersTest, WithoutThePortraitTheWriteWindowIsACut)
{
    OpenTheFriendsWithThree();
    auto view = View();
    view.portrait = {};
    Client::UI::PostToTheWindow(Manager(), Friends().tabs[Client::UI::FriendTabNumber].base.id,
                                Client::UI::CommandMessage, 4, 0);
    const auto stepped = Client::UI::StepTheOldWindows(Manager(), Globals(), view);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("P-C212"), std::string::npos);
}

// 0x4dc840: con un error, la caja con su texto y la de escribir sin el envío pendiente (+0x778 = 0).
TEST_F(GameLettersTest, AFailedSendOpensItsBox)
{
    ASSERT_TRUE(Client::Scenes::ApplyLetterSendResult(
                    Client::Protocol::Letters::LetterSendResult{.result = 2, .window = 0}, Context())
                    .HasValue());
    OldWindow* box = FirstOfKind(OldWindowKind::MessageBox);
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->box.lines[0], "no existe");
}

// 0x4dc9e0 con el tipo 2, la pestaña de las cartas (0x4a11b0 con el 10 y 2: C1 06 C7, y 0x491570 la anota), el C7
// (0x4dcb70: la copia, leída, y sin el retrato del juego, el corte P-C212) y el borrado (0x4dcd90).
TEST_F(GameLettersTest, AnArrivingLetterIsReadAndDeleted)
{
    OpenTheFriendsWithThree();
    ASSERT_TRUE(
        Client::Scenes::ApplyLetterRow(
            Client::Protocol::Letters::LetterRow{
                .index = 7, .name = "Tordo", .date = "2026/11/13", .time = "10:20", .subject = "Saludos", .type = 2},
            Context())
            .HasValue());
    EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>(0x29));
    EXPECT_EQ(Manager().windowList.list.newLetter, 1);
    EXPECT_EQ(Runtime().chat.log.entries.back().text, "carta nueva");
    ASSERT_EQ(Manager().letterModel.letters.size(), 1u);
    EXPECT_EQ(Manager().letterModel.letters[0].type, 0);

    Friends().friends.currentTab = Client::UI::LettersTabNumber;
    OldWindow& tab = Friends().tabs[Client::UI::LettersTabNumber];
    ASSERT_EQ(tab.tab.letters.size(), 1u);
    Client::UI::SelectInTheList(tab.tab.list, 1, 1);
    for (int times = 0; times < 2; ++times)
    {
        Client::UI::PostToTheWindow(Manager(), tab.base.id, Client::UI::CommandMessage, 2, 0);
        ASSERT_TRUE(Step().HasValue());
    }
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(m_sent[0].kind, FriendSendKind::LetterRead);
    EXPECT_EQ(BytesOf(Client::Scenes::FriendPacketOf(m_sent[0])), Xor32Of(Bytes({0xc1, 0x06, 0xc7, 0x00, 0x07, 0x00})));
    EXPECT_EQ(Manager().letterWindows.at(7), 0);

    Client::Protocol::Letters::LetterRead read{.index = 7,
                                               .length = 9,
                                               .portraitClass = 0x20,
                                               .equipment = {},
                                               .direction = 0x0f,
                                               .action = 2,
                                               .body = "Hola che",
                                               .copy = Bytes({0xc2, 0x00, 0x24, 0xc7})};
    const auto shown = Client::Scenes::ApplyLetterRead(read, Context());
    ASSERT_FALSE(shown.HasValue());
    EXPECT_NE(shown.Error().find("P-C212"), std::string::npos);
    EXPECT_EQ(Manager().letterModel.letters[0].type, 1);
    ASSERT_NE(Client::UI::KeptLetter(Manager().letterModel, 7), nullptr);

    ASSERT_TRUE(Client::Scenes::ApplyLetterDeleteResult(
                    Client::Protocol::Letters::LetterDeleteResult{.result = 1, .index = 7}, Context())
                    .HasValue());
    EXPECT_TRUE(Manager().letterModel.letters.empty());
    EXPECT_EQ(Client::UI::KeptLetter(Manager().letterModel, 7), nullptr);
    EXPECT_TRUE(Friends().tabs[Client::UI::LettersTabNumber].tab.letters.empty());
    EXPECT_EQ(m_random.State(), After(0));
}

// La de leer (0x48ee60(6) con 0x4999d0 y 0x499cb0): el cuerpo partido por '\n' sin los vacíos; el 2 pregunta con el
// 1017, el sí manda C1 06 C8 y la cierra; el 1 responde con la de escribir (el remitente y "Re: asunto", sin el 8 y el
// 9); el 5 lee la que sigue (la elige en la pestaña, se cierra, deja su lugar y suelta la suya).
TEST_F(GameLettersTest, TheReadWindowDeletesRepliesAndGoesOn)
{
    OpenTheFriendsWithThree();
    Client::UI::AddALetter(Manager().letterModel, 7, "Tordo", "Saludos", "2026/11/13", "10:20", 1);
    Client::UI::AddALetter(Manager().letterModel, 9, "Calandria", "Otra", "2026/11/13", "10:30", 0);
    Client::UI::RefreshTheLettersTab(Manager());
    const auto openTheFirst = [this]() -> std::int32_t
    {
        auto view = View();
        auto globals = Globals();
        auto created = Client::UI::CreateTheOldWindow(
            Manager(),
            Client::UI::OldWindowRequest{
                .kind = Client::UI::LetterReadCase, .x = 120, .y = 110, .data = "Leer", .aux = 0, .flags = 0},
            m_oldTexts, m_oldMeasure, &globals, &view);
        EXPECT_TRUE(created.HasValue()) << created.Error();
        if (!created.HasValue())
            return 0;
        OldWindow& read = Manager().windows.at(created.Value());
        Client::UI::SetTheLetter(Manager(), read, Manager().letterModel.letters[0], "Linea uno\n\nLinea dos");
        Manager().letterWindows[7] = created.Value();
        return created.Value();
    };
    std::int32_t readId = openTheFirst();
    ASSERT_NE(readId, 0);
    EXPECT_EQ(m_portraitCalls, std::vector<PortraitCallKind>{PortraitCallKind::Build});
    EXPECT_EQ(Manager().windows.at(readId).letter.lineRows, (std::deque<std::string>{"Linea uno", "Linea dos"}));

    Client::UI::PostToTheWindow(Manager(), readId, Client::UI::CommandMessage, 2, 0);
    ASSERT_TRUE(Step().HasValue());
    OldWindow* question = FirstOfKind(OldWindowKind::MessageBox);
    ASSERT_NE(question, nullptr);
    EXPECT_EQ(question->box.lines[0], "Borrar la carta?");
    EXPECT_EQ(question->box.owner, readId);
    ASSERT_TRUE(Client::UI::DestroyTheOldWindow(Manager(), question->base.id).HasValue());
    Manager().modal = 0;

    // El sí: el C8 y el 4, que el administrador atiende en el mismo paso (la ventana suelta su carta, 0x4916b0).
    Client::UI::PostToTheWindow(Manager(), readId, Client::UI::AnswerMessage, 0, 1);
    ASSERT_TRUE(Step().HasValue());
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(BytesOf(Client::Scenes::FriendPacketOf(m_sent[0])), Xor32Of(Bytes({0xc1, 0x06, 0xc8, 0x00, 0x07, 0x00})));
    EXPECT_EQ(Manager().windows.count(readId), 0u);
    EXPECT_EQ(Manager().letterWindows.count(7), 0u);

    readId = openTheFirst();
    ASSERT_NE(readId, 0);
    Client::UI::PostToTheWindow(Manager(), readId, Client::UI::CommandMessage, 1, 0);
    ASSERT_TRUE(Step().HasValue());
    OldWindow* reply = FirstOfKind(OldWindowKind::LetterWriteWindow);
    ASSERT_NE(reply, nullptr);
    EXPECT_EQ(reply->letter.to.text.Value(), "Tordo");
    EXPECT_EQ(reply->letter.subject.text.Value(), "Re: Saludos");
    EXPECT_EQ(Manager().windows.count(readId), 1u);

    const std::int32_t readX = Manager().windows.at(readId).base.x;
    Client::UI::PostToTheWindow(Manager(), readId, Client::UI::CommandMessage, 5, 0);
    ASSERT_TRUE(Step().HasValue());
    ASSERT_EQ(m_sent.size(), 2u);
    EXPECT_EQ(m_sent[1].kind, FriendSendKind::LetterRead);
    EXPECT_EQ(m_sent[1].index, 9);
    EXPECT_EQ(Manager().windows.count(readId), 0u);
    EXPECT_EQ(Manager().letterWindows.count(7), 0u);
    EXPECT_EQ(Manager().letterWindows.at(9), 0);
    EXPECT_EQ(Manager().nextReadX, readX);
    EXPECT_EQ(Friends().tabs[Client::UI::LettersTabNumber].tab.list.selection, 2);
    EXPECT_EQ(m_random.State(), After(0));
}
} // namespace
