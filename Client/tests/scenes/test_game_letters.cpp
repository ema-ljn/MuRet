#include "Messenger/GameLetters.h"

#include "App/IChatRoomLinks.h"
#include "Session/WorldEvent.h"
#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/TextTable.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Messenger/FriendChat.h"
#include "Protocol/Messenger/Friends.h"
#include "Protocol/Messenger/Letters.h"
#include "Protocol/Player/OptionData.h"
#include "Protocol/Outgoing.h"
#include "Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"
#include "Chat/ChatReceive.h"
#include "Messenger/GameFriends.h"
#include "Hud/GameUi.h"
#include "Kernel/SceneId.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatInput.h"
#include "Messenger/FriendModel.h"
#include "Messenger/FriendWindows.h"
#include "Messenger/LetterModel.h"
#include "Messenger/LetterWindows.h"
#include "Messenger/LetterWindowsDraw.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindowDraw.h"
#include "Ui/OldWindows.h"
#include "Audio/SoundId.h"

#include "support/RecordingTextDrawer.h"
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
#include <utility>
#include <vector>

// The chat with a friend and the game's letters (CL9 milestone 10; 04 § Las cartas y el chat con un amigo): buttons 3
// and 4 of the friends tab (0x49d0b0), the chat (0x4944c0), the write window (0x498820), the letters tab (0x4a11b0),
// the read window (0x49a1f0) and the C5..C8, CA and CB packets (0x4dc840..0x4dd020), byte by byte. The names and texts
// are made up.
namespace
{
using Client::Chat::ChatReceiveContext;
using Client::Crt::CrtRandom;
using Client::Kernel::WorldState;
using Client::Protocol::Outgoing;
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;
using Client::Ui::FriendSend;
using Client::Ui::FriendSendKind;
using Client::Ui::OldPortraitPart;
using Client::Ui::OldWindow;
using Client::Ui::OldWindowKind;
using Client::Ui::PortraitCallKind;

constexpr std::uint32_t Seed = 9191;
constexpr std::string_view Hero = "Yaguarete";

// The XOR32 key (0x6b8648), written again so the code is not compared with itself.
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

// The chat's 3-byte XOR (0x6c82e4), by position.
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
        {1059, " entro"},
        {1061, "no existe"},
        {1062, "carta nueva"},
        {1069, "no esta"},
        {1070, "fuera de linea"},
        {1071, "Carta nueva (%d)"},
        {1073, "buzon lleno"},
    };
}

class FakeRequests final : public Client::Hud::IGameUiRequests
{
public:
    void SendOptionData(const Client::Protocol::Player::OptionBody& /*body*/) override {}
    void RequestLogout(std::uint8_t /*kind*/) override {}
    void RequestPartyList() override {}
    void SendGuildPacket(Outgoing /*packet*/) override {}
    void SendGamePacket(Outgoing /*outgoing*/) override {}
    void SendWindowClose() override {}
    void SendSetQuest(std::uint8_t /*quest*/, std::uint8_t /*state*/) override {}
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
        auto& name = m_world->Hero().name;
        for (std::size_t at = 0; at < Hero.size(); ++at)
            name[at] = static_cast<std::byte>(Hero[at]);
        Manager().letterLimit = 50;
    }

    [[nodiscard]] ChatReceiveContext Context()
    {
        return ChatReceiveContext{.world = *m_world,
                                  .random = m_random,
                                  .measure = &m_letters,
                                  .screen = Client::Core::PixelSize{640, 480},
                                  .scene = Client::Kernel::SceneId::Game,
                                  .texts = &m_texts,
                                  .chosenSlot = std::nullopt,
                                  .rooms = &m_rooms};
    }

    // What the game gives the step (Hud/GameUi), with a test portrait: it builds the character (+0x68c at 1) with zoom
    // 0.8 and draws no rand().
    [[nodiscard]] Client::Ui::OldWindowStepView View(std::int32_t x = 639, std::int32_t y = 479)
    {
        Client::Ui::OldWindowStepView view{.mouseX = x,
                                           .mouseY = y,
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
        // Only records the copy. The game's (Hud::LendTheGameHooks) reads it with ReadLetterCopy, which also reads a
        // 1000-letter letter's (LettersTest.TheCopyOfAThousandLetterLetterReadsAgain), and gives it to ApplyLetterRead.
        view.replayLetter = [this](const std::vector<std::byte>& copy) -> Client::Core::Expected<void, std::string>
        {
            m_replayed.push_back(copy);
            return {};
        };
        view.heroName = std::string(Hero);
        return view;
    }

    [[nodiscard]] Client::Ui::OldWindowGlobals Globals()
    {
        return Client::Hud::OldWindowGlobalsOf(Runtime());
    }

    Client::Core::Expected<void, std::string> Step(std::int32_t x = 639, std::int32_t y = 479)
    {
        return Client::Ui::StepTheOldWindows(Manager(), Globals(), View(x, y));
    }

    // Three friends: two online and one offline.
    void OpenTheFriendsWithThree()
    {
        auto opened = Client::Messenger::OpenTheFriendsWindow(Manager(), Client::Messenger::FriendsWindowX,
                                                              Client::Messenger::FriendsWindowY, Globals(), View());
        ASSERT_TRUE(opened.HasValue()) << opened.Error();
        Client::Messenger::AddAFriend(Manager().friendModel, "Tordo", 0, 2);
        Client::Messenger::AddAFriend(Manager().friendModel, "Calandria", 0, 1);
        Client::Messenger::AddAFriend(Manager().friendModel, "Benteveo", 0, 0xfd);
        Client::Messenger::RefreshTheFriendTab(Manager());
    }

    void ChooseTheFriend(std::string_view name)
    {
        OldWindow& tab = Friends().tabs[Client::Messenger::FriendTabNumber];
        for (std::size_t at = 0; at < tab.tab.friends.size(); ++at)
        {
            if (tab.tab.friends[at].name == name)
                Client::Ui::SelectInTheList(tab.tab.list, tab.tab.friends.size(), static_cast<std::int32_t>(at + 1));
        }
    }

    // The chat with Tordo in room 0x0304 (button 3 and the CA with 1), with the hero and Tordo as members. The calls to
    // the rooms are cleared. Returns the chat id, or 0.
    [[nodiscard]] std::int32_t JoinARoomWithTordo()
    {
        OpenTheFriendsWithThree();
        ChooseTheFriend("Tordo");
        Client::Ui::PostToTheWindow(Manager(), Friends().tabs[Client::Messenger::FriendTabNumber].base.id,
                                    Client::Ui::CommandMessage, 3, 0);
        if (!Step().HasValue())
            return 0;
        const auto joined = Client::Messenger::ApplyRoomAnswer(
            Client::Protocol::Messenger::RoomAnswer{
                .address = "10.0.0.9", .room = 0x0304, .ticket = 77, .type = 0, .name = "Tordo", .result = 1},
            Context());
        OldWindow* chat = FirstOfKind(OldWindowKind::ChatWindow);
        if (!joined.HasValue() || chat == nullptr)
            return 0;
        if (!Client::Messenger::AddAChatMember(Manager(), *chat, Hero, 0, 0, m_oldTexts, Hero, &m_oldMeasure)
                 .HasValue() ||
            !Client::Messenger::AddAChatMember(Manager(), *chat, "Tordo", 1, 0, m_oldTexts, Hero, &m_oldMeasure)
                 .HasValue())
            return 0;
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
            if (message.type == Client::Ui::CloseMessage && message.a == id)
                return true;
        }
        return false;
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

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random;
    LetterMeasure m_letters;
    Client::Tables::TextTable m_texts;
    Client::Ui::OldWindowTexts m_oldTexts = Client::Messenger::OldWindowTextsOf(&m_texts);
    Client::Ui::OldWindowMeasure m_oldMeasure{
        .measure = m_letters, .dcFont = FontKind::Normal, .screen = Client::Core::PixelSize{640, 480}};
    std::vector<FriendSend> m_sent;
    std::vector<PortraitCallKind> m_portraitCalls;
    std::vector<std::vector<std::byte>> m_replayed;
    FakeRooms m_rooms;
    std::int32_t m_pressedButton = 0;
};

// Button 3 with a friend online: C1 0D CA with their name, and it stays requested (+0x158): another 3 does not send
// again. The CA with 1 and type 0 builds the chat (0x48ee60(1)), adds it to +0xb4, takes a slot (0x49e280), opens the
// room's connection (0x49de50) and sends through it the C1 10 00 with the room and the ticket (0x493530). With the
// members, button 1 shows the invite list (without the friend or the offline ones) and the 2, with the EDIT focused
// and the chat open, sends the C1 14 CB with the room and the window. None of this draws rand().
TEST_F(GameLettersTest, AChatWithAFriendInvitesAnother)
{
    OpenTheFriendsWithThree();
    ChooseTheFriend("Tordo");
    const std::int32_t friendTab = Friends().tabs[Client::Messenger::FriendTabNumber].base.id;
    Client::Ui::PostToTheWindow(Manager(), friendTab, Client::Ui::CommandMessage, 3, 0);
    ASSERT_TRUE(Step().HasValue());
    Client::Ui::PostToTheWindow(Manager(), friendTab, Client::Ui::CommandMessage, 3, 0);
    ASSERT_TRUE(Step().HasValue());
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(m_sent[0].kind, FriendSendKind::RoomRequest);
    auto request = Bytes({0xc1, 0x0d, 0xca});
    Append(request, "Tordo", 10);
    EXPECT_EQ(BytesOf(Client::Messenger::FriendPacketOf(m_sent[0])), Xor32Of(request));
    EXPECT_TRUE(Client::Messenger::IsAPendingChat(Manager(), "Tordo"));

    const auto joined = Client::Messenger::ApplyRoomAnswer(
        Client::Protocol::Messenger::RoomAnswer{
            .address = "10.0.0.9", .room = 0x0304, .ticket = 77, .type = 0, .name = "Tordo", .result = 1},
        Context());
    ASSERT_TRUE(joined.HasValue()) << joined.Error();
    EXPECT_FALSE(Client::Messenger::IsAPendingChat(Manager(), "Tordo"));
    OldWindow* chat = FirstOfKind(OldWindowKind::ChatWindow);
    ASSERT_NE(chat, nullptr);
    const std::int32_t chatId = chat->base.id;
    EXPECT_EQ(chat->chat.room, 0x0304u);
    EXPECT_EQ(Manager().windowList.list.chats, std::deque<std::int32_t>{chatId});
    EXPECT_EQ(Manager().chatRooms.used[0], 1);
    EXPECT_EQ(Manager().chatRooms.slots.at(0), 0x0304u);
    const Client::Messenger::ChatRoom* room = Client::Messenger::FindTheChatRoom(Manager().chatRooms, 0x0304);
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

    ASSERT_TRUE(
        Client::Messenger::AddAChatMember(Manager(), *chat, Hero, 0, 0, m_oldTexts, Hero, &m_oldMeasure).HasValue());
    const auto members =
        Client::Messenger::AddAChatMember(Manager(), *chat, "Tordo", 1, 0, m_oldTexts, Hero, &m_oldMeasure);
    ASSERT_TRUE(members.HasValue()) << members.Error();
    EXPECT_EQ(members.Value(), 2);
    EXPECT_EQ(chat->frame.title, std::optional<std::string>("Chat: Tordo"));
    EXPECT_EQ(Client::Messenger::FindTheChatWith(Manager(), "Tordo", Hero), chatId);

    Client::Ui::PostToTheWindow(Manager(), chatId, Client::Ui::CommandMessage, 1, 0);
    ASSERT_TRUE(Step().HasValue());
    chat = &Manager().windows.at(chatId);
    EXPECT_EQ(chat->chat.mode, 2);
    ASSERT_EQ(chat->chat.invitable.size(), 1u);
    EXPECT_EQ(chat->chat.invitable[0].name, "Calandria");
    // The 6 (raise the chat, 0x49541c) gives the focus to the EDIT, like every click on the window: inviting does not
    // look at the focus but at the EDIT's +0xb4 (0x49455e), which only the closed chat sets.
    Client::Ui::PostToTheWindow(Manager(), chatId, Client::Ui::RaisedMessage, 0, 0);
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(Runtime().chat.input.focus, Client::Chat::ChatFocus::OldWindow);
    EXPECT_EQ(Manager().focusedEdit, chat->chat.edit.base.id);
    // 0x461f00 on Calandria's row (13 high, 3 above its y): the click chooses it and the double click is the 0xd to the
    // chat, which 0x4944d9 turns into the 10 with 2.
    chat = &Manager().windows.at(chatId);
    const std::int32_t rowX = chat->chat.invite.base.x + 4;
    const std::int32_t rowY = Client::Ui::ListRowY(chat->chat.invite, chat->chat.invitable.size(), 0) + 3;
    Runtime().mouse.pendingClick = true;
    ASSERT_TRUE(Step(rowX, rowY).HasValue());
    chat = &Manager().windows.at(chatId);
    EXPECT_EQ(Client::Ui::SelectedInTheList(chat->chat.invite, chat->chat.invitable.size()),
              std::optional<std::size_t>(0));
    EXPECT_EQ(chat->chat.invitable[0].selected, 1);
    EXPECT_EQ(m_sent.size(), 1u);
    Runtime().ui.doubleClick = true;
    ASSERT_TRUE(Step(rowX, rowY).HasValue());
    ASSERT_TRUE(Step().HasValue());
    ASSERT_EQ(m_sent.size(), 2u);
    EXPECT_EQ(m_sent[1].kind, FriendSendKind::Invite);
    auto invite = Bytes({0xc1, 0x14, 0xcb});
    Append(invite, "Calandria", 10);
    const auto window = static_cast<std::uint32_t>(chatId);
    for (const int value : {0x00, 0x04, 0x03, static_cast<int>(window & 0xffU), static_cast<int>((window >> 8) & 0xffU),
                            static_cast<int>((window >> 16) & 0xffU), static_cast<int>(window >> 24)})
        invite.push_back(static_cast<std::byte>(value));
    EXPECT_EQ(BytesOf(Client::Messenger::FriendPacketOf(m_sent[1])), Xor32Of(invite));

    ASSERT_TRUE(Client::Messenger::ApplyInviteResult(
                    Client::Protocol::Messenger::InviteResult{.result = 1, .window = window}, Context())
                    .HasValue());
    ASSERT_FALSE(chat->chat.lineRows.empty());
    EXPECT_EQ(chat->chat.lineRows.back().text, "Calandria entro invitado");
    EXPECT_EQ(m_random.State(), After(0));
}

// Enter in the chat (0x4944c0 with the 7) leaves the C1 04 for the room, which goes out when Scenes empties the
// orders; the same text again does not go out. The timer 1002 (0x4a6550) leaves the C1 03 05. The room's FD_CLOSE
// (0x49e480) gives the chat the 402 of nobody and the room stays recorded. Releasing the chat (0x493160 → 0x493a70)
// sends the C1 03 01 and closes the connection (0x49dfd0), with the slot free. Nothing draws rand().
TEST_F(GameLettersTest, TheChatRoomCarriesTheMessagesThePingAndTheLeave)
{
    const std::int32_t chatId = JoinARoomWithTordo();
    ASSERT_NE(chatId, 0);
    for (int times = 0; times < 2; ++times)
    {
        Manager().windows.at(chatId).chat.edit.text.SetText("hola");
        Client::Ui::PostToTheWindow(Manager(), chatId, Client::Ui::EnterMessage, 0, 0);
        auto stepped = Step();
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    }
    EXPECT_EQ(Manager().chatRooms.orders.size(), 1u);
    Client::Messenger::PingTheChatRooms(Manager());
    Client::Messenger::FlushTheChatRoomOrders(Manager().chatRooms, &m_rooms);
    EXPECT_TRUE(Manager().chatRooms.orders.empty());
    ASSERT_EQ(m_rooms.calls.size(), 2u);
    EXPECT_EQ(m_rooms.calls[0].kind, "send");
    EXPECT_EQ(m_rooms.calls[0].room, 0x0304u);
    EXPECT_EQ(m_rooms.calls[0].bytes, Xor32Of(Joined(Bytes({0xc1, 0x09, 0x04, 0x00, 0x04}), ChatTextOf("hola", 4))));
    EXPECT_EQ(m_rooms.calls[1].kind, "send");
    EXPECT_EQ(m_rooms.calls[1].bytes, Bytes({0xc1, 0x03, 0x05}));

    ASSERT_TRUE(Client::Messenger::ApplyChatRoomClosed(
                    Client::App::ChatRoomClosed{.room = 0x0304, .window = chatId, .opened = true}, Context())
                    .HasValue());
    const OldWindow& chat = Manager().windows.at(chatId);
    ASSERT_FALSE(chat.chat.lineRows.empty());
    EXPECT_EQ(chat.chat.lineRows.back().text, "se fue todo el mundo");
    EXPECT_NE(Client::Messenger::FindTheChatRoom(Manager().chatRooms, 0x0304), nullptr);

    m_rooms.calls.clear();
    Client::Ui::PostToTheManager(Manager(), Client::Ui::CloseMessage, chatId, 0);
    auto stepped = Step();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FALSE(Manager().windows.contains(chatId));
    EXPECT_EQ(Client::Messenger::FindTheChatRoom(Manager().chatRooms, 0x0304), nullptr);
    EXPECT_EQ(Manager().chatRooms.used[0], 0);
    Client::Messenger::FlushTheChatRoomOrders(Manager().chatRooms, &m_rooms);
    ASSERT_EQ(m_rooms.calls.size(), 2u);
    EXPECT_EQ(m_rooms.calls[0].kind, "send");
    EXPECT_EQ(m_rooms.calls[0].bytes, Bytes({0xc1, 0x03, 0x01}));
    EXPECT_EQ(m_rooms.calls[1].kind, "close");
    EXPECT_EQ(m_rooms.calls[1].room, 0x0304u);
    EXPECT_EQ(m_random.State(), After(0));
}

// 0x4e2a60's handlers read each room packet by position, without looking at its length: one shorter than what its
// handler reads (Q-C1011) does nothing and does not cut, one per opcode. The texts that end inside the packet are read
// even if the packet is shorter than the fixed position: the 01's name and the 0D's text.
TEST_F(GameLettersTest, AShortRoomPacketDoesNothing)
{
    const std::int32_t chatId = JoinARoomWithTordo();
    ASSERT_NE(chatId, 0);
    const auto apply = [&](std::vector<std::byte> bytes)
    {
        bytes[1] = static_cast<std::byte>(bytes.size());
        auto packet = Client::Network::Packet::FromBytes(std::move(bytes));
        EXPECT_TRUE(packet.HasValue());
        if (!packet.HasValue())
            return false;
        const auto applied = Client::Messenger::ApplyRoomPacket(chatId, packet.Value(), Context());
        EXPECT_TRUE(applied.HasValue()) << applied.Error();
        return applied.HasValue();
    };
    const OldWindow& chat = Manager().windows.at(chatId);
    const std::size_t windows = Manager().windows.size();
    const std::size_t members = chat.chat.members.size();
    const std::size_t logged = Runtime().chat.log.entries.size();
    // 00 without pkt[3]; 01 with the name without its '\0'; 02 with two and a half members; 04 with 10 bytes of length
    // and 2 of text; 0D without the '\0' before the end.
    ASSERT_TRUE(apply(Bytes({0xc1, 0, 0x00})));
    ASSERT_TRUE(apply(Joined(Bytes({0xc1, 0, 0x01, 0x00, 0x05}), Bytes({'T', 'e', 'r'}))));
    auto list = Bytes({0xc1, 0, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02});
    Append(list, "Tero", 10);
    list.push_back(std::byte{0x03});
    list.push_back(std::byte{'C'});
    ASSERT_TRUE(apply(list));
    ASSERT_TRUE(apply(Joined(Bytes({0xc1, 0, 0x04, 0x01, 10}), ChatTextOf("ho", 2))));
    ASSERT_TRUE(apply(Joined(Bytes({0xc1, 0, 0x0d, 0x00, 0x00}), Bytes({'a', 'v', 'i', 's', 'o'}))));
    EXPECT_EQ(Manager().windows.size(), windows);
    EXPECT_EQ(chat.chat.members.size(), members);
    EXPECT_EQ(Runtime().chat.log.entries.size(), logged);
    EXPECT_TRUE(chat.chat.lineRows.empty());

    // With the '\0' inside: Tero joins, with the 1059 after his name (3 members), and the 0D goes to the game's chat.
    auto tero = Bytes({0xc1, 0, 0x01, 0x00, 0x05});
    Append(tero, "Tero", 5);
    ASSERT_TRUE(apply(tero));
    EXPECT_EQ(chat.chat.members.size(), members + 1);
    ASSERT_FALSE(chat.chat.lineRows.empty());
    EXPECT_EQ(chat.chat.lineRows.back().text, "Tero entro");
    ASSERT_TRUE(apply(Joined(Bytes({0xc1, 0, 0x0d, 0x00, 0x00}), Bytes({'a', 'v', 'i', 's', 'o', 0}))));
    ASSERT_EQ(Runtime().chat.log.entries.size(), logged + 1);
    EXPECT_EQ(Runtime().chat.log.entries.back().text, "aviso");
    EXPECT_EQ(m_random.State(), After(0));
}

// The CA with 1 and type 1 on the chat already open with the friend (0x4a6430): 0x493a70 sends the C1 03 01 and closes
// the old room at once, and then 0x493530 opens the new one and sends the C1 10 00. With the same room number, the new
// one is another connection and stays recorded with the chat.
TEST_F(GameLettersTest, ACallToTheSameRoomLeavesItBeforeJoiningAgain)
{
    const std::int32_t chatId = JoinARoomWithTordo();
    ASSERT_NE(chatId, 0);
    const auto called = Client::Messenger::ApplyRoomAnswer(
        Client::Protocol::Messenger::RoomAnswer{
            .address = "10.0.0.9", .room = 0x0304, .ticket = 88, .type = 1, .name = "Tordo", .result = 1},
        Context());
    ASSERT_TRUE(called.HasValue()) << called.Error();
    ASSERT_EQ(m_rooms.calls.size(), 4u);
    EXPECT_EQ(m_rooms.calls[0].kind, "send");
    EXPECT_EQ(m_rooms.calls[0].room, 0x0304u);
    EXPECT_EQ(m_rooms.calls[0].bytes, Bytes({0xc1, 0x03, 0x01}));
    EXPECT_EQ(m_rooms.calls[1].kind, "close");
    EXPECT_EQ(m_rooms.calls[1].room, 0x0304u);
    EXPECT_EQ(m_rooms.calls[2].kind, "open");
    EXPECT_EQ(m_rooms.calls[2].room, 0x0304u);
    EXPECT_EQ(m_rooms.calls[2].window, chatId);
    EXPECT_EQ(m_rooms.calls[3].kind, "send");
    EXPECT_EQ(m_rooms.calls[3].bytes,
              Xor32Of(Joined(Bytes({0xc1, 0x10, 0x00, 0x00, 0x04, 0x03}), ChatTextOf("88", 10))));
    EXPECT_TRUE(Manager().chatRooms.orders.empty());
    const Client::Messenger::ChatRoom* room = Client::Messenger::FindTheChatRoom(Manager().chatRooms, 0x0304);
    ASSERT_NE(room, nullptr);
    EXPECT_EQ(room->window, chatId);
    EXPECT_EQ(m_random.State(), After(0));
}

// The closed chat (0x495630(1): the EDIT's +0xb4 at 1, with fewer than 2 members after Enter or with the friend
// offline): Enter goes to the single-line Windows EDIT (0x467c83), without the 7 or the C1 04, and the text stays; the
// 10 and 2 does not invite (0x49455e). Opened again (0x495630(0)), both come back.
TEST_F(GameLettersTest, AClosedChatNeitherSaysNorInvites)
{
    const std::int32_t chatId = JoinARoomWithTordo();
    ASSERT_NE(chatId, 0);
    m_sent.clear();
    Client::Ui::PostToTheWindow(Manager(), chatId, Client::Ui::CommandMessage, 1, 0);
    ASSERT_TRUE(Step().HasValue());
    OldWindow* chat = &Manager().windows.at(chatId);
    ASSERT_EQ(chat->chat.invitable.size(), 1u);
    Client::Ui::SelectInTheList(chat->chat.invite, chat->chat.invitable.size(), 1);
    Client::Ui::PostToTheWindow(Manager(), chatId, Client::Ui::RaisedMessage, 0, 0);
    ASSERT_TRUE(Step().HasValue());
    ASSERT_EQ(Manager().focusedEdit, chat->chat.edit.base.id);
    Client::Messenger::CloseTheChat(*chat, 1, m_oldTexts);
    EXPECT_EQ(chat->chat.edit.base.state, 0);

    chat->chat.edit.text.SetText("hola");
    Client::Messenger::PressEnterInTheOldEdit(Manager());
    auto stepped = Step();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    chat = &Manager().windows.at(chatId);
    EXPECT_TRUE(Manager().chatRooms.orders.empty());
    EXPECT_EQ(chat->chat.edit.text.Value(), "hola");
    Client::Ui::PostToTheWindow(Manager(), chatId, Client::Ui::CommandMessage, 2, 0);
    ASSERT_TRUE(Step().HasValue());
    EXPECT_TRUE(m_sent.empty());

    Client::Messenger::CloseTheChat(*chat, 0, m_oldTexts);
    Client::Messenger::PressEnterInTheOldEdit(Manager());
    stepped = Step();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Manager().chatRooms.orders.size(), 1u);
    chat = &Manager().windows.at(chatId);
    Client::Ui::PostToTheWindow(Manager(), chatId, Client::Ui::CommandMessage, 2, 0);
    ASSERT_TRUE(Step().HasValue());
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(m_sent[0].kind, FriendSendKind::Invite);
    EXPECT_EQ(m_random.State(), After(0));
}

// The connect that fails (0x49de50 does not record the room): the room is forgotten, the 0x49e280 slot stays taken
// (Q-C1005) and Enter no longer sends anything. The windows of the game's first step (0x48ea50) release the chats,
// which leave their rooms.
TEST_F(GameLettersTest, ARoomThatDoesNotConnectIsForgottenAndTheResetLeavesTheRooms)
{
    std::int32_t chatId = JoinARoomWithTordo();
    ASSERT_NE(chatId, 0);
    ASSERT_TRUE(Client::Messenger::ApplyChatRoomClosed(
                    Client::App::ChatRoomClosed{.room = 0x0304, .window = chatId, .opened = false}, Context())
                    .HasValue());
    EXPECT_EQ(Client::Messenger::FindTheChatRoom(Manager().chatRooms, 0x0304), nullptr);
    EXPECT_EQ(Manager().chatRooms.used[0], 1);
    EXPECT_TRUE(Manager().windows.at(chatId).chat.lineRows.empty());
    Manager().windows.at(chatId).chat.edit.text.SetText("hola");
    Client::Ui::PostToTheWindow(Manager(), chatId, Client::Ui::EnterMessage, 0, 0);
    ASSERT_TRUE(Step().HasValue());
    EXPECT_TRUE(Manager().chatRooms.orders.empty());

    Client::Ui::ResetTheOldWindows(Manager(), m_pressedButton);
    Client::Messenger::ClearTheFriends(Manager().friendModel);
    m_rooms.calls.clear();
    chatId = JoinARoomWithTordo();
    ASSERT_NE(chatId, 0);
    Client::Ui::ResetTheOldWindows(Manager(), m_pressedButton);
    EXPECT_TRUE(Manager().windows.empty());
    EXPECT_EQ(Client::Messenger::FindTheChatRoom(Manager().chatRooms, 0x0304), nullptr);
    Client::Messenger::FlushTheChatRoomOrders(Manager().chatRooms, &m_rooms);
    ASSERT_EQ(m_rooms.calls.size(), 2u);
    EXPECT_EQ(m_rooms.calls[0].bytes, Bytes({0xc1, 0x03, 0x01}));
    EXPECT_EQ(m_rooms.calls[1].kind, "close");
}

// 0x4dc720: the friend going offline closes their chat (the 995 in front and the EDIT off) and back online opens it;
// with 0xfc every chat gets the 402 and closes (0x4a75c0).
TEST_F(GameLettersTest, TheFriendStateClosesAndOpensItsChat)
{
    OpenTheFriendsWithThree();
    auto view = View();
    auto globals = Globals();
    auto created = Client::Ui::CreateTheOldWindow(
        Manager(),
        Client::Ui::OldWindowRequest{
            .kind = Client::Ui::ChatCase, .x = 100, .y = 100, .data = "Chat: ", .aux = 0, .flags = 0},
        m_oldTexts, m_oldMeasure, &globals, &view);
    ASSERT_TRUE(created.HasValue()) << created.Error();
    OldWindow* chat = &Manager().windows.at(created.Value());
    ASSERT_TRUE(
        Client::Messenger::AddAChatMember(Manager(), *chat, Hero, 0, 0, m_oldTexts, Hero, &m_oldMeasure).HasValue());
    ASSERT_TRUE(
        Client::Messenger::AddAChatMember(Manager(), *chat, "Tordo", 1, 0, m_oldTexts, Hero, &m_oldMeasure).HasValue());
    ASSERT_EQ(Manager().windowList.list.chats.size(), 1u);

    ASSERT_TRUE(Client::Messenger::ApplyFriendState(
                    Client::Protocol::Messenger::FriendState{.name = "Tordo", .state = 0xfd}, Context())
                    .HasValue());
    EXPECT_EQ(chat->frame.title, std::optional<std::string>("[cerrado] Chat: Tordo"));
    EXPECT_EQ(chat->chat.edit.closed, 1);
    EXPECT_EQ(chat->chat.edit.base.state, 0);
    ASSERT_TRUE(Client::Messenger::ApplyFriendState(
                    Client::Protocol::Messenger::FriendState{.name = "Tordo", .state = 3}, Context())
                    .HasValue());
    EXPECT_EQ(chat->frame.title, std::optional<std::string>("Chat: Tordo"));
    EXPECT_EQ(chat->chat.edit.closed, 0);
    EXPECT_EQ(chat->chat.edit.base.state, 0);
    ASSERT_TRUE(Client::Messenger::ApplyFriendState(Client::Protocol::Messenger::FriendState{.name = "", .state = 0xfc},
                                                    Context())
                    .HasValue());
    ASSERT_FALSE(chat->chat.lineRows.empty());
    EXPECT_EQ(chat->chat.lineRows.back().text, "se fue todo el mundo");
    EXPECT_EQ(chat->frame.title, std::optional<std::string>("[cerrado] Chat: Tordo"));
}

// Button 4 builds the write window (0x48ee60(7) at (100, 100), the portrait built, copied from the hero and with action
// 0x7a) with the chosen one's name, and the queue 0x49dc37 sends it the 8 and the 9. Button 1 sends the C2 C5 with the
// body from "\r\n" to "\n", the direction (90 / 6, with the zoom at 0) and the action (0x7a − 0x78). The letter takes
// one rand() and the serial from the game (Q-C1002); opening the window, none.
TEST_F(GameLettersTest, WritingALetterSendsItByteByByte)
{
    OpenTheFriendsWithThree();
    ChooseTheFriend("Tordo");
    Client::Ui::PostToTheWindow(Manager(), Friends().tabs[Client::Messenger::FriendTabNumber].base.id,
                                Client::Ui::CommandMessage, 4, 0);
    auto stepped = Step();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    OldWindow* write = FirstOfKind(OldWindowKind::LetterWriteWindow);
    ASSERT_NE(write, nullptr);
    EXPECT_EQ(m_random.State(), After(0));
    // 0x497f50's four (the angle with the character built). The two steps of 0x49dc37 go with 1: 0x45aa80 goes out
    // after the queue, without 0x497970.
    EXPECT_EQ(m_portraitCalls,
              (std::vector<PortraitCallKind>{PortraitCallKind::Build, PortraitCallKind::CopyTheHero,
                                             PortraitCallKind::SetAction, PortraitCallKind::SetAngle}));
    EXPECT_EQ(write->frame.title, std::optional<std::string>("Carta nueva (1000)"));
    EXPECT_EQ(write->letter.to.text.Value(), "Tordo");
    EXPECT_EQ(write->letter.prepared, 1);
    EXPECT_EQ(write->base.current.type, Client::Ui::SizeMessage);
    EXPECT_FLOAT_EQ(write->letter.portrait.angle, 90.0f);
    EXPECT_EQ(write->letter.portrait.action, 0x7a);

    write->letter.subject.text.SetText("Saludos");
    write->letter.text.text.SetText("Linea uno\r\nLinea dos");
    const std::int32_t writeId = write->base.id;
    Client::Ui::PostToTheWindow(Manager(), writeId, Client::Ui::CommandMessage, 1, 0);
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
    const Outgoing letter = Client::Messenger::FriendPacketOf(m_sent[0]);
    EXPECT_EQ(letter.delivery, Client::Network::Delivery::Encrypted);
    EXPECT_EQ(BytesOf(letter), Xor32Of(plain, 4));

    FakeRequests requests;
    Client::Messenger::SendAFriendPacket(requests, m_random, m_sent[0]);
    ASSERT_EQ(requests.sent.size(), 1u);
    EXPECT_EQ(requests.sent[0], BytesOf(letter));
    EXPECT_EQ(m_random.State(), After(1 + Client::Models::SerialCloseRandoms));
    const std::uint32_t before = m_random.State();
    Client::Messenger::SendAFriendPacket(
        requests, m_random, FriendSend{.kind = FriendSendKind::LetterRead, .value = 0, .name = {}, .index = 7});
    EXPECT_EQ(m_random.State(), before);

    // 0x4dc840 with 1: the 4 to the write window and the 1046 with the cost to the chat.
    ASSERT_TRUE(
        Client::Messenger::ApplyLetterSendResult(
            Client::Protocol::Messenger::LetterSendResult{.result = 1, .window = static_cast<std::uint32_t>(writeId)},
            Context())
            .HasValue());
    EXPECT_TRUE(ClosePosted(writeId));
    ASSERT_FALSE(Runtime().chat.log.entries.empty());
    EXPECT_EQ(Runtime().chat.log.entries.back().text, "Carta mandada, costo 1000");
}

// 0x49817a..0x498192: Tab moves the focus from To to Subject, to Body and back to To (0x467d07 and 0x4682a0(1): the
// arrows' list in it and the cursor at the end). 0x467710 does not destroy the EDIT: when the write window closes, the
// one that had the focus stays orphaned with its text until another takes it (0x4983e0 in the next write window). And
// 0x48eae6 also empties the map of the letters waiting for the C7.
TEST_F(GameLettersTest, TabGoesAroundTheWriteEditsAndTheClosedOneKeepsItsFocus)
{
    OpenTheFriendsWithThree();
    ChooseTheFriend("Tordo");
    const std::int32_t friendTab = Friends().tabs[Client::Messenger::FriendTabNumber].base.id;
    Client::Ui::PostToTheWindow(Manager(), friendTab, Client::Ui::CommandMessage, 4, 0);
    ASSERT_TRUE(Step().HasValue());
    OldWindow* write = FirstOfKind(OldWindowKind::LetterWriteWindow);
    ASSERT_NE(write, nullptr);
    const std::int32_t writeId = write->base.id;
    const std::int32_t to = write->letter.to.base.id;
    const std::int32_t subject = write->letter.subject.base.id;
    const std::int32_t text = write->letter.text.base.id;
    EXPECT_EQ(write->letter.to.next, subject);
    EXPECT_EQ(write->letter.subject.next, text);
    EXPECT_EQ(write->letter.text.next, to);
    // 0x4983e0: with the name set, the focus on the subject.
    ASSERT_EQ(Manager().focusedEdit, subject);

    write->letter.text.text.SetText("Linea uno");
    write->letter.text.text.SetCursor(0);
    std::int32_t focusedList = 0;
    Client::Messenger::ApplyKeyToTheOldEdit(Manager(), Client::Ui::OldEditKey::Tab, false, false, nullptr, nullptr,
                                            &focusedList);
    EXPECT_EQ(Manager().focusedEdit, text);
    EXPECT_EQ(focusedList, text);
    EXPECT_EQ(write->letter.text.text.Cursor(), 9u);
    Client::Messenger::ApplyKeyToTheOldEdit(Manager(), Client::Ui::OldEditKey::Tab, false, false, nullptr, nullptr,
                                            &focusedList);
    EXPECT_EQ(Manager().focusedEdit, to);
    EXPECT_EQ(write->letter.to.text.Cursor(), 5u);
    Client::Messenger::ApplyKeyToTheOldEdit(Manager(), Client::Ui::OldEditKey::Tab, false, false, nullptr, nullptr,
                                            &focusedList);
    EXPECT_EQ(Manager().focusedEdit, subject);
    EXPECT_EQ(focusedList, subject);

    write->letter.subject.text.SetText("Sal");
    ASSERT_TRUE(Client::Ui::DestroyTheOldWindow(Manager(), writeId, m_pressedButton).HasValue());
    EXPECT_EQ(Manager().windows.count(writeId), 0u);
    EXPECT_TRUE(Manager().focusedEditIsOrphan);
    ASSERT_TRUE(Manager().orphanedEdit.has_value());
    EXPECT_EQ(Manager().orphanedEdit->base.id, subject);
    Client::Messenger::TypeInTheOldEdit(Manager(), "udos");
    EXPECT_EQ(Manager().orphanedEdit->text.Value(), "Saludos");

    // Another write window takes the focus from the orphan.
    ChooseTheFriend("Calandria");
    Client::Ui::PostToTheWindow(Manager(), friendTab, Client::Ui::CommandMessage, 4, 0);
    ASSERT_TRUE(Step().HasValue());
    OldWindow* again = FirstOfKind(OldWindowKind::LetterWriteWindow);
    ASSERT_NE(again, nullptr);
    EXPECT_FALSE(Manager().focusedEditIsOrphan);
    EXPECT_FALSE(Manager().orphanedEdit.has_value());
    EXPECT_EQ(Manager().focusedEdit, again->letter.subject.base.id);

    Manager().letterWindows[7] = 0;
    Manager().letterWindows[9] = again->base.id;
    Client::Ui::ResetTheOldWindows(Manager(), m_pressedButton);
    EXPECT_TRUE(Manager().letterWindows.empty());
}

// 0x4dccbd: _strnicmp(name, "webzen", 10), case-insensitive; another name that starts the same is not.
TEST(GameLettersWebzenTest, OnlyWebzenWithAnyCaseIsTheBlankPortrait)
{
    EXPECT_TRUE(Client::Messenger::LetterFromWebzen("webzen"));
    EXPECT_TRUE(Client::Messenger::LetterFromWebzen("WebZen"));
    EXPECT_TRUE(Client::Messenger::LetterFromWebzen("WEBZEN"));
    EXPECT_FALSE(Client::Messenger::LetterFromWebzen("webzen2"));
    EXPECT_FALSE(Client::Messenger::LetterFromWebzen("webze"));
    EXPECT_FALSE(Client::Messenger::LetterFromWebzen("Tordo"));
    EXPECT_TRUE(Client::Messenger::LetterFromWebzen(std::string_view("Webzen\0xy", 9)));
}

// Without the game's portrait (P-C212), opening the write window stops.
TEST_F(GameLettersTest, WithoutThePortraitTheWriteWindowIsACut)
{
    OpenTheFriendsWithThree();
    auto view = View();
    view.portrait = {};
    Client::Ui::PostToTheWindow(Manager(), Friends().tabs[Client::Messenger::FriendTabNumber].base.id,
                                Client::Ui::CommandMessage, 4, 0);
    const auto stepped = Client::Ui::StepTheOldWindows(Manager(), Globals(), view);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("P-C212"), std::string::npos);
}

// 0x4dc840: with an error, the box with its text and the write window without the pending send (+0x778 = 0).
TEST_F(GameLettersTest, AFailedSendOpensItsBox)
{
    ASSERT_TRUE(Client::Messenger::ApplyLetterSendResult(
                    Client::Protocol::Messenger::LetterSendResult{.result = 2, .window = 0}, Context())
                    .HasValue());
    OldWindow* box = FirstOfKind(OldWindowKind::MessageBox);
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->box.lines[0], "no existe");
}

// 0x4dc9e0 with type 2, the letters tab (0x4a11b0 with the 10 and 2: C1 06 C7, and 0x491570 records it), the C7
// (0x4dcb70: the copy, read, and without the game's portrait, the P-C212 stop) and the deletion (0x4dcd90).
TEST_F(GameLettersTest, AnArrivingLetterIsReadAndDeleted)
{
    OpenTheFriendsWithThree();
    ASSERT_TRUE(
        Client::Messenger::ApplyLetterRow(
            Client::Protocol::Messenger::LetterRow{
                .index = 7, .name = "Tordo", .date = "2026/11/13", .time = "10:20", .subject = "Saludos", .type = 2},
            Context())
            .HasValue());
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x29});
    EXPECT_EQ(Manager().windowList.list.newLetter, 1);
    EXPECT_EQ(Runtime().chat.log.entries.back().text, "carta nueva");
    ASSERT_EQ(Manager().letterModel.letters.size(), 1u);
    EXPECT_EQ(Manager().letterModel.letters[0].type, 0);

    Friends().friends.currentTab = Client::Messenger::LettersTabNumber;
    OldWindow& tab = Friends().tabs[Client::Messenger::LettersTabNumber];
    ASSERT_EQ(tab.tab.letters.size(), 1u);
    Client::Ui::SelectInTheList(tab.tab.list, 1, 1);
    for (int times = 0; times < 2; ++times)
    {
        Client::Ui::PostToTheWindow(Manager(), tab.base.id, Client::Ui::CommandMessage, 2, 0);
        ASSERT_TRUE(Step().HasValue());
    }
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(m_sent[0].kind, FriendSendKind::LetterRead);
    EXPECT_EQ(BytesOf(Client::Messenger::FriendPacketOf(m_sent[0])),
              Xor32Of(Bytes({0xc1, 0x06, 0xc7, 0x00, 0x07, 0x00})));
    EXPECT_EQ(Manager().letterWindows.at(7), 0);

    Client::Protocol::Messenger::LetterRead read{.index = 7,
                                                 .length = 9,
                                                 .portraitClass = 0x20,
                                                 .equipment = {},
                                                 .direction = 0x0f,
                                                 .action = 2,
                                                 .body = "Hola che",
                                                 .copy = Bytes({0xc2, 0x00, 0x24, 0xc7})};
    const auto shown = Client::Messenger::ApplyLetterRead(read, Context());
    ASSERT_FALSE(shown.HasValue());
    EXPECT_NE(shown.Error().find("P-C212"), std::string::npos);
    EXPECT_EQ(Manager().letterModel.letters[0].type, 1);
    ASSERT_NE(Client::Messenger::KeptLetter(Manager().letterModel, 7), nullptr);

    ASSERT_TRUE(Client::Messenger::ApplyLetterDeleteResult(
                    Client::Protocol::Messenger::LetterDeleteResult{.result = 1, .index = 7}, Context())
                    .HasValue());
    EXPECT_TRUE(Manager().letterModel.letters.empty());
    EXPECT_EQ(Client::Messenger::KeptLetter(Manager().letterModel, 7), nullptr);
    EXPECT_TRUE(Friends().tabs[Client::Messenger::LettersTabNumber].tab.letters.empty());
    EXPECT_EQ(m_random.State(), After(0));
}

// The read window (0x48ee60(6) with 0x4999d0 and 0x499cb0): the body split by '\n' without the empty ones; the 2 asks
// with the 1017, the yes sends C1 06 C8 and closes it; the 1 replies with the write window (the sender and "Re: " with
// the subject, without the 8 and the 9); the 5 reads the next one (chooses it in the tab, closes, leaves its slot and
// releases its own).
TEST_F(GameLettersTest, TheReadWindowDeletesRepliesAndGoesOn)
{
    OpenTheFriendsWithThree();
    Client::Messenger::AddALetter(Manager().letterModel, 7, "Tordo", "Saludos", "2026/11/13", "10:20", 1);
    Client::Messenger::AddALetter(Manager().letterModel, 9, "Calandria", "Otra", "2026/11/13", "10:30", 0);
    Client::Messenger::RefreshTheLettersTab(Manager());
    const auto openTheFirst = [this]() -> std::int32_t
    {
        auto view = View();
        auto globals = Globals();
        auto created = Client::Ui::CreateTheOldWindow(
            Manager(),
            Client::Ui::OldWindowRequest{
                .kind = Client::Ui::LetterReadCase, .x = 120, .y = 110, .data = "Leer", .aux = 0, .flags = 0},
            m_oldTexts, m_oldMeasure, &globals, &view);
        EXPECT_TRUE(created.HasValue()) << created.Error();
        if (!created.HasValue())
            return 0;
        OldWindow& read = Manager().windows.at(created.Value());
        const auto set = Client::Messenger::SetTheLetter(Manager(), read, Manager().letterModel.letters[0],
                                                         "Linea uno\n\nLinea dos", &m_oldMeasure);
        EXPECT_TRUE(set.HasValue()) << set.Error();
        Manager().letterWindows[7] = created.Value();
        return created.Value();
    };
    std::int32_t readId = openTheFirst();
    ASSERT_NE(readId, 0);
    EXPECT_EQ(m_portraitCalls, std::vector<PortraitCallKind>{PortraitCallKind::Build});
    EXPECT_EQ(Manager().windows.at(readId).letter.lineRows, (std::deque<std::string>{"Linea uno", "Linea dos"}));

    Client::Ui::PostToTheWindow(Manager(), readId, Client::Ui::CommandMessage, 2, 0);
    ASSERT_TRUE(Step().HasValue());
    OldWindow* question = FirstOfKind(OldWindowKind::MessageBox);
    ASSERT_NE(question, nullptr);
    EXPECT_EQ(question->box.lines[0], "Borrar la carta?");
    EXPECT_EQ(question->box.owner, readId);
    ASSERT_TRUE(Client::Ui::DestroyTheOldWindow(Manager(), question->base.id, m_pressedButton).HasValue());
    Manager().modal = 0;

    // The yes: the C8 and the 4, which the manager handles in the same step (the window releases its letter, 0x4916b0).
    Client::Ui::PostToTheWindow(Manager(), readId, Client::Ui::AnswerMessage, 0, 1);
    ASSERT_TRUE(Step().HasValue());
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(BytesOf(Client::Messenger::FriendPacketOf(m_sent[0])),
              Xor32Of(Bytes({0xc1, 0x06, 0xc8, 0x00, 0x07, 0x00})));
    EXPECT_EQ(Manager().windows.count(readId), 0u);
    EXPECT_EQ(Manager().letterWindows.count(7), 0u);

    readId = openTheFirst();
    ASSERT_NE(readId, 0);
    Client::Ui::PostToTheWindow(Manager(), readId, Client::Ui::CommandMessage, 1, 0);
    ASSERT_TRUE(Step().HasValue());
    OldWindow* reply = FirstOfKind(OldWindowKind::LetterWriteWindow);
    ASSERT_NE(reply, nullptr);
    EXPECT_EQ(reply->letter.to.text.Value(), "Tordo");
    EXPECT_EQ(reply->letter.subject.text.Value(), "Re: Saludos");
    EXPECT_EQ(Manager().windows.count(readId), 1u);

    const std::int32_t readX = Manager().windows.at(readId).base.x;
    Client::Ui::PostToTheWindow(Manager(), readId, Client::Ui::CommandMessage, 5, 0);
    ASSERT_TRUE(Step().HasValue());
    ASSERT_EQ(m_sent.size(), 2u);
    EXPECT_EQ(m_sent[1].kind, FriendSendKind::LetterRead);
    EXPECT_EQ(m_sent[1].index, 9);
    EXPECT_EQ(Manager().windows.count(readId), 0u);
    EXPECT_EQ(Manager().letterWindows.count(7), 0u);
    EXPECT_EQ(Manager().letterWindows.at(9), 0);
    EXPECT_EQ(Manager().nextReadX, readX);
    EXPECT_EQ(Friends().tabs[Client::Messenger::LettersTabNumber].tab.list.selection, 2);
    EXPECT_EQ(m_random.State(), After(0));
}

constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;

class GameLettersMapsTest : public GameLettersTest, public ::testing::WithParamInterface<std::int32_t>
{
};

// Milestone 10's drawings do not read the map: in Lorencia and in Devias, the chat that the CA built and the 0xfc
// closed (0x4a75c0: the 995 in front of the title and the 402 as a row; 0x493fc0) and its row in the F5 list (0x4a5750:
// the friend, without the 995 or the 994) are drawn the same, as the game draws them (0x490390 and the list at
// 0x637680), without drawing rand().
TEST_P(GameLettersMapsTest, TheChatAndItsRowInTheWindowListAreDrawnInEveryMap)
{
    Runtime().map = GetParam();
    const std::int32_t chatId = JoinARoomWithTordo();
    ASSERT_NE(chatId, 0);
    ASSERT_TRUE(Client::Messenger::ApplyFriendState(Client::Protocol::Messenger::FriendState{.name = "", .state = 0xfc},
                                                    Context())
                    .HasValue());
    ASSERT_EQ(Manager().windows.at(chatId).frame.title, std::optional<std::string>("[cerrado] Chat: Tordo"));
    Client::Ui::ShowTheWindowList(Manager(), 1);

    Client::Render::SpriteBatch sprites;
    sprites.Begin(Client::Core::PixelSize{640, 480});
    Client::Tests::RecordingTextDrawer text(sprites);
    FontKind font = FontKind::Bold;
    Client::Render::Text::TextColors colors{};
    const Client::Ui::OldWindowDrawContext context{
        .sprites = sprites, .text = text, .dcFont = font, .textColors = colors};
    const Client::Ui::OldWindowDrawView view{
        .friendsOpen = true, .texts = &m_oldTexts, .chatPlacement = Runtime().ui.chatModes.placement};
    const auto windows = Client::Ui::DrawTheOldWindows(Manager(), context, view);
    ASSERT_TRUE(windows.HasValue()) << windows.Error();
    const auto list = Client::Ui::DrawTheWindowList(Manager(), context, view);
    ASSERT_TRUE(list.HasValue()) << list.Error();

    const auto drawn = [&](std::string_view what)
    {
        for (const Client::Tests::RecordedText& recorded : text.texts)
        {
            if (recorded.text == what)
                return &recorded;
        }
        return static_cast<const Client::Tests::RecordedText*>(nullptr);
    };
    const OldWindow& chat = Manager().windows.at(chatId);
    const Client::Tests::RecordedText* line = drawn("se fue todo el mundo");
    ASSERT_NE(line, nullptr);
    EXPECT_EQ(line->x, chat.chat.lines.base.x + 8);
    // The F5 row is the last text: the friend at the list's x + 2, 3 lower than (row + 4) · 1 above its bottom edge.
    ASSERT_FALSE(text.texts.empty());
    const OldWindow& windowList = Manager().windowList;
    EXPECT_EQ(text.texts.back().text, "Tordo");
    EXPECT_EQ(text.texts.back().x, windowList.base.x + 2);
    EXPECT_EQ(text.texts.back().y, windowList.list.bottom - 17 + 3);
    EXPECT_EQ(m_random.State(), After(0));
}

// The room's C1 04 with 100 bytes from Tordo (0x4dd3d0 → 0x493f80 → 0x45f6f0): 0x45fea0 splits it at the rows' width
// minus 0x1e (240 − 30, with letters of 2) with the indent of "Tordo" plus 5; the first piece with the name and the
// second without it, ahead (0x460130). The draw (0x45f9c0) shows them from newest to oldest: the second below. A letter
// with a long row (0x464ae0 → 0x465450) splits the same, without indent. Same in Lorencia and in Devias, without
// drawing rand().
TEST_P(GameLettersMapsTest, TheLongLinesOfAChatAndOfALetterAreWrappedInEveryMap)
{
    Runtime().map = GetParam();
    const std::int32_t chatId = JoinARoomWithTordo();
    ASSERT_NE(chatId, 0);
    constexpr std::string_view Said =
        "la vaquita para la parrilla se junta el viernes a la noche y cada uno trae algo para compartir, che!";
    ASSERT_EQ(Said.size(), 100u);
    auto bytes =
        Joined(Bytes({0xc1, 5 + 100, Client::Network::ByteOf(Client::Protocol::Messenger::RoomMessageOpcode), 1, 100}),
               ChatTextOf(Said, 100));
    auto packet = Client::Network::Packet::FromBytes(std::move(bytes));
    ASSERT_TRUE(packet.HasValue());
    const auto applied = Client::Messenger::ApplyRoomPacket(chatId, packet.Value(), Context());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();

    const OldWindow& chat = Manager().windows.at(chatId);
    ASSERT_EQ(chat.chat.lineRows.size(), 1u);
    EXPECT_EQ(chat.chat.lineRows[0].text, Said);
    ASSERT_EQ(chat.chat.wrappedRows.size(), 2u);
    EXPECT_EQ(chat.chat.wrappedRows[0].name, "");
    EXPECT_EQ(chat.chat.wrappedRows[0].text, "he!");
    EXPECT_EQ(chat.chat.wrappedRows[1].name, "Tordo");
    EXPECT_EQ(chat.chat.wrappedRows[1].text, Said.substr(0, 97));
    EXPECT_EQ(chat.chat.lines.fixedCountValue, 2);

    Client::Render::SpriteBatch sprites;
    sprites.Begin(Client::Core::PixelSize{640, 480});
    Client::Tests::RecordingTextDrawer text(sprites);
    FontKind font = FontKind::Bold;
    Client::Render::Text::TextColors colors{};
    const Client::Ui::OldWindowDrawContext context{
        .sprites = sprites, .text = text, .dcFont = font, .textColors = colors};
    const Client::Ui::OldWindowDrawView drawView{
        .friendsOpen = true, .texts = &m_oldTexts, .chatPlacement = Runtime().ui.chatModes.placement};
    const auto windows = Client::Ui::DrawTheOldWindows(Manager(), context, drawView);
    ASSERT_TRUE(windows.HasValue()) << windows.Error();
    const auto drawn = [&](std::string_view what)
    {
        for (const Client::Tests::RecordedText& recorded : text.texts)
        {
            if (recorded.text == what)
                return &recorded;
        }
        return static_cast<const Client::Tests::RecordedText*>(nullptr);
    };
    const Client::Tests::RecordedText* first = drawn(Said.substr(0, 97));
    const Client::Tests::RecordedText* second = drawn("he!");
    const Client::Tests::RecordedText* name = drawn("Tordo: ");
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(second->x, chat.chat.lines.base.x + 8);
    EXPECT_EQ(first->y, second->y - 13);
    EXPECT_EQ(name->y, first->y);
    // The draw measures "Tordo: " with its text (6 per letter), not with the packet's measure.
    EXPECT_EQ(first->x, chat.chat.lines.base.x + 8 + 42);

    auto view = View();
    auto globals = Globals();
    auto created = Client::Ui::CreateTheOldWindow(
        Manager(),
        Client::Ui::OldWindowRequest{
            .kind = Client::Ui::LetterReadCase, .x = 120, .y = 110, .data = "Leer", .aux = 0, .flags = 0},
        m_oldTexts, m_oldMeasure, &globals, &view);
    ASSERT_TRUE(created.HasValue()) << created.Error();
    constexpr std::string_view Long = "el horno de barro ya esta listo para las empanadas de carne del domingo que "
                                      "viene, asi que cada uno avise cuantas va a comer antes";
    const Client::Messenger::LetterEntry entry{.selected = 0,
                                               .index = 7,
                                               .name = "Tordo",
                                               .subject = "Horno",
                                               .date = "2026/11/13",
                                               .time = "10:20",
                                               .type = 1};
    OldWindow& read = Manager().windows.at(created.Value());
    const auto set =
        Client::Messenger::SetTheLetter(Manager(), read, entry, "Hola\n" + std::string(Long), &m_oldMeasure);
    ASSERT_TRUE(set.HasValue()) << set.Error();
    // 360 inside − 0x78 of the portrait: 240, and 210 to split (105 letters of 2).
    ASSERT_EQ(read.letter.lines.base.width, 240);
    EXPECT_EQ(read.letter.wrappedRows,
              (std::deque<std::string>{std::string(Long.substr(105)), std::string(Long.substr(0, 105)), "Hola"}));
    EXPECT_EQ(read.letter.lines.fixedCountValue, 3);

    text.texts.clear();
    ASSERT_TRUE(Client::Messenger::DrawTheReadWindow(Manager(), read, context, drawView).HasValue());
    ASSERT_GE(text.texts.size(), 3u);
    EXPECT_EQ(text.texts[0].text, Long.substr(105));
    EXPECT_EQ(text.texts[1].text, Long.substr(0, 105));
    EXPECT_EQ(text.texts[2].text, "Hola");
    EXPECT_EQ(text.texts[1].y, text.texts[0].y - 13);
    EXPECT_EQ(text.texts[2].y, text.texts[1].y - 13);
    EXPECT_EQ(m_random.State(), After(0));
}

INSTANTIATE_TEST_SUITE_P(TwoMaps, GameLettersMapsTest, ::testing::Values(Lorencia, Devias));
} // namespace
