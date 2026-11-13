#include "UI/FriendWindows.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "UI/ChatInput.h"
#include "UI/FriendModel.h"
#include "UI/OldControls.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindowSteps.h"
#include "UI/OldWindows.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// La ventana de amigos (CL9 hito 9; 06 § HUD y ventanas, "Los amigos"): 0x491080 desde el nivel, la fábrica
// 0x48ee60(3) y (4), la pestaña de amigos (0x49d0b0), la de las ventanas (0x49ea50), la caja de agregar (0x4a3850) y su
// EDIT. Los nombres y los textos son inventados.
namespace
{
using Client::App::PixelSize;
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;
using Client::UI::ChatFocus;
using Client::UI::FriendSend;
using Client::UI::FriendSendKind;
using Client::UI::OldWindow;
using Client::UI::OldWindowGlobals;
using Client::UI::OldWindowKind;
using Client::UI::OldWindowManager;
using Client::UI::OldWindowMeasure;
using Client::UI::OldWindowRequest;
using Client::UI::OldWindowStepView;
using Client::UI::OldWindowTexts;

constexpr std::uint8_t EnterKey = 0x0d;
constexpr std::uint8_t FriendsKey = 0x46;
constexpr std::int32_t OpenSound = 0x1c;

class LetterMeasure final : public ITextMeasure
{
public:
    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

// Cada texto es "t<índice>".
std::string TextOf(std::size_t index)
{
    return "t" + std::to_string(index);
}

class FriendWindowsTest : public ::testing::Test
{
protected:
    OldWindowGlobals Globals()
    {
        return OldWindowGlobals{.leftHeld = m_leftHeld,
                                .leftClick = m_leftClick,
                                .releasedClick = m_released,
                                .mouseOnUi = m_mouseOnUi,
                                .hovered = m_hovered,
                                .capture = m_capture,
                                .pressedButton = m_pressedButton,
                                .keyboardFocus = m_focus,
                                .sound = m_sound,
                                .focusedList = m_focusedList,
                                .wheel = m_wheel,
                                .rightClick = m_rightClick,
                                .doubleClick = m_doubleClick};
    }

    OldWindowStepView View(std::int32_t x = 639, std::int32_t y = 479)
    {
        return OldWindowStepView{.mouseX = x,
                                 .mouseY = y,
                                 .keyEdge = [this](std::uint8_t key) { return m_keys.erase(key) > 0; },
                                 .keyDown = {},
                                 .chatOpen = m_chatOpen,
                                 .texts = &m_texts,
                                 .measure = &m_measure,
                                 .send = [this](const FriendSend& send) { m_sent.push_back(send); },
                                 .whisperTo = {},
                                 .openFriends = {}};
    }

    Client::Core::Expected<bool, std::string> OpenFriends()
    {
        return Client::UI::OpenTheFriendsWindow(m_manager, Client::UI::FriendsWindowX, Client::UI::FriendsWindowY,
                                                Globals(), View());
    }

    Client::Core::Expected<void, std::string> Step()
    {
        m_hovered = 0;
        m_mouseOnUi = 0;
        return Client::UI::StepTheOldWindows(m_manager, Globals(), View());
    }

    std::int32_t OpenABox(std::int32_t kind, std::string_view text, std::int32_t aux = 0)
    {
        const auto opened = Client::UI::CreateTheOldWindow(
            m_manager, OldWindowRequest{.kind = kind, .x = -1, .y = -1, .data = text, .aux = aux, .flags = 0}, m_texts,
            m_measure);
        EXPECT_TRUE(opened.HasValue()) << opened.Error();
        return opened.HasValue() ? opened.Value() : 0;
    }

    OldWindow& Friends()
    {
        return m_manager.windows.at(m_manager.friends);
    }

    OldWindow& FriendTab()
    {
        return Friends().tabs[0];
    }

    LetterMeasure m_letters;
    const OldWindowTexts m_texts{.notice = "Aviso", .question = "Pregunta", .yes = "Si", .no = "No", .lookup = TextOf};
    const OldWindowMeasure m_measure{.measure = m_letters, .dcFont = FontKind::Normal, .screen = PixelSize{640, 480}};
    OldWindowManager m_manager;
    bool m_leftHeld = false;
    bool m_leftClick = false;
    bool m_released = false;
    std::uint8_t m_mouseOnUi = 0;
    std::int32_t m_hovered = 0;
    std::int32_t m_capture = 0;
    std::int32_t m_pressedButton = 0;
    ChatFocus m_focus = ChatFocus::Game;
    std::optional<std::int32_t> m_sound;
    std::int32_t m_focusedList = 0;
    std::int32_t m_wheel = 0;
    bool m_rightClick = false;
    bool m_doubleClick = false;
    bool m_chatOpen = false;
    std::set<std::uint8_t> m_keys;
    std::vector<FriendSend> m_sent;
};

// 0x491080 la primera vez: la arma en (0x186, 0x106) con el título 990, sus tres pestañas y la de amigos a la vista; la
// pestaña de las ventanas tiene las que ya estaban. Con la lista (+0xe8 en 1) no pide nada.
TEST_F(FriendWindowsTest, TheFirstOpenBuildsTheWindowWithItsTabs)
{
    const std::int32_t box = OpenABox(Client::UI::NoticeCase, "hola");
    const auto opened = OpenFriends();
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    EXPECT_TRUE(opened.Value());
    OldWindow& friends = Friends();
    EXPECT_EQ(friends.kind, OldWindowKind::FriendsWindow);
    EXPECT_EQ(friends.base.x, 0x186);
    EXPECT_EQ(friends.base.y, 0x106);
    EXPECT_EQ(friends.frame.title, std::optional<std::string>("t990"));
    ASSERT_EQ(friends.tabs.size(), 3u);
    EXPECT_EQ(friends.tabs[0].kind, OldWindowKind::FriendTab);
    EXPECT_EQ(friends.tabs[1].kind, OldWindowKind::LettersTab);
    EXPECT_EQ(friends.tabs[2].kind, OldWindowKind::WindowsTab);
    EXPECT_EQ(friends.friends.currentTab, 0);
    ASSERT_EQ(friends.tabs[2].tab.windows.size(), 1u);
    EXPECT_EQ(friends.tabs[2].tab.windows[0].id, box);
    EXPECT_EQ(m_sound, std::optional<std::int32_t>(OpenSound));
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(m_manager.order.back(), m_manager.friends);
}

// 0x48ee60(3) con +0xe8 en 0: el C1 03 C0.
TEST_F(FriendWindowsTest, WithoutTheListOpeningAsksForIt)
{
    m_manager.listReceived = 0;
    ASSERT_TRUE(OpenFriends().HasValue());
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(m_sent[0].kind, FriendSendKind::ListRequest);
}

// 0x491318..0x49137f: oculta, la muestra; a la vista, la suelta (0x4914e0) y guarda su lugar (0x490200), donde la
// vuelve a abrir.
TEST_F(FriendWindowsTest, OpeningShowsTheHiddenDropsTheShownAndReopensWhereItWas)
{
    ASSERT_TRUE(OpenFriends().HasValue());
    Friends().base.state = Client::UI::HiddenWindow;
    m_sound.reset();
    auto again = OpenFriends();
    ASSERT_TRUE(again.HasValue()) << again.Error();
    EXPECT_FALSE(again.Value());
    EXPECT_EQ(Friends().base.state, Client::UI::ShownWindow);
    EXPECT_EQ(m_sound, std::optional<std::int32_t>(OpenSound));

    Friends().base.x = 0x40;
    Friends().base.y = 0x50;
    Friends().friends.currentTab = 2;
    again = OpenFriends();
    ASSERT_TRUE(again.HasValue()) << again.Error();
    EXPECT_FALSE(again.Value());
    EXPECT_EQ(m_manager.friends, 0);
    EXPECT_EQ(m_manager.friendsX, 0x40);
    EXPECT_EQ(m_manager.lastTab, 2);

    again = OpenFriends();
    ASSERT_TRUE(again.HasValue()) << again.Error();
    EXPECT_TRUE(again.Value());
    EXPECT_EQ(Friends().base.x, 0x40);
    EXPECT_EQ(Friends().base.y, 0x50);
    EXPECT_EQ(Friends().friends.currentTab, 2);
}

// 0x49ddd0: copia el modelo y deja elegido al que estaba elegido, por su nombre.
TEST_F(FriendWindowsTest, TheTabCopiesTheModelAndKeepsTheChosenByName)
{
    ASSERT_TRUE(OpenFriends().HasValue());
    Client::UI::AddAFriend(m_manager.friendModel, "Zorzal", 0, 2);
    Client::UI::AddAFriend(m_manager.friendModel, "Hornero", 0, 0xff);
    Client::UI::RefreshTheFriendTab(m_manager);
    ASSERT_EQ(FriendTab().tab.friends.size(), 2u);
    Client::UI::SelectInTheList(FriendTab().tab.list, 2, 2);
    Client::UI::AddAFriend(m_manager.friendModel, "Benteveo", 0, 1);
    Client::UI::SortTheFriends(m_manager.friendModel, Client::UI::ByNameSort);
    Client::UI::RefreshTheFriendTab(m_manager);
    ASSERT_EQ(FriendTab().tab.friends.size(), 3u);
    EXPECT_EQ(FriendTab().tab.friends[0].name, "Zorzal");
    EXPECT_EQ(FriendTab().tab.list.selection, 2);
    EXPECT_EQ(FriendTab().tab.friends[1].name, "Hornero");
}

// 0x49bd60 da el lugar del último con el nombre del elegido: con dos iguales, el elegido pasa al de abajo (Q-C885).
TEST_F(FriendWindowsTest, WithTwoEqualNamesTheRefreshChoosesTheLast)
{
    ASSERT_TRUE(OpenFriends().HasValue());
    Client::UI::AddAFriend(m_manager.friendModel, "Hornero", 0, 3);
    Client::UI::AddAFriend(m_manager.friendModel, "Tordo", 0, 2);
    Client::UI::AddAFriend(m_manager.friendModel, "Hornero", 0, 1);
    Client::UI::RefreshTheFriendTab(m_manager);
    Client::UI::SelectInTheList(FriendTab().tab.list, 3, 1);
    Client::UI::RefreshTheFriendTab(m_manager);
    EXPECT_EQ(FriendTab().tab.list.selection, 3);
}

// Agregar: el botón 1 arma la caja (0x48ee60(4)), modal y con el foco en su EDIT, y le manda el 8 y el 9 con su paso
// (0x49dc37); lo escrito y Enter (el 7 de la subclase) le mandan el 11 a la pestaña, que manda el C1 0D C1, y la caja
// se cierra.
TEST_F(FriendWindowsTest, TheAddBoxSendsTheTypedName)
{
    ASSERT_TRUE(OpenFriends().HasValue());
    // El 1 al administrador que dejó 0x491080 se atiende en el paso que sigue.
    ASSERT_TRUE(Step().HasValue());
    Client::UI::PostToTheWindow(m_manager, FriendTab().base.id, Client::UI::CommandMessage, 1, 0);
    auto stepped = Step();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const std::int32_t box = m_manager.letter;
    ASSERT_NE(box, 0);
    EXPECT_EQ(m_manager.modal, box);
    EXPECT_EQ(m_manager.windows.at(box).kind, OldWindowKind::AddFriendBox);
    EXPECT_EQ(m_manager.windows.at(box).frame.title, std::optional<std::string>("t1023"));
    EXPECT_EQ(m_manager.windows.at(box).base.current.type, Client::UI::SizeMessage);
    EXPECT_TRUE(m_manager.windows.at(box).base.queue.empty());
    EXPECT_EQ(m_focus, ChatFocus::OldWindow);
    ASSERT_TRUE(Client::UI::HasAnOldEditFocused(m_manager, m_focus));

    Client::UI::TypeInTheOldEdit(m_manager, "Calandrias");
    Client::UI::BackspaceInTheOldEdit(m_manager);
    Client::UI::PressEnterInTheOldEdit(m_manager);
    stepped = Step();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    stepped = Step();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(m_sent[0].kind, FriendSendKind::Add);
    EXPECT_EQ(m_sent[0].name, "Calandria");
    EXPECT_FALSE(m_manager.windows.contains(box));
}

// Con la lista de ventanas armada (F5, +0x150), Enter no es del EDIT (0x467b10).
TEST_F(FriendWindowsTest, WithTheWindowListArmedEnterIsNotTheEdits)
{
    ASSERT_TRUE(OpenFriends().HasValue());
    ASSERT_TRUE(Step().HasValue());
    Client::UI::PostToTheWindow(m_manager, FriendTab().base.id, Client::UI::CommandMessage, 1, 0);
    ASSERT_TRUE(Step().HasValue());
    const std::int32_t box = m_manager.letter;
    Client::UI::TypeInTheOldEdit(m_manager, "Tero");
    m_manager.windowList.list.armed = 1;
    Client::UI::PressEnterInTheOldEdit(m_manager);
    EXPECT_TRUE(m_manager.windows.at(box).base.queue.empty());
}

// Borrar: el botón 2 pregunta "<nombre><1024>" en una caja modal que le responde a la pestaña; con sí, C1 0D C3.
TEST_F(FriendWindowsTest, DeletingAsksAndSendsTheChosenName)
{
    ASSERT_TRUE(OpenFriends().HasValue());
    ASSERT_TRUE(Step().HasValue());
    Client::UI::AddAFriend(m_manager.friendModel, "Tordo", 0, 3);
    Client::UI::AddAFriend(m_manager.friendModel, "Hornero", 0, 1);
    Client::UI::RefreshTheFriendTab(m_manager);
    Client::UI::SelectInTheList(FriendTab().tab.list, 2, 2);
    Client::UI::PostToTheWindow(m_manager, FriendTab().base.id, Client::UI::CommandMessage, 2, 0);
    ASSERT_TRUE(Step().HasValue());
    const std::int32_t question = m_manager.modal;
    ASSERT_NE(question, 0);
    EXPECT_EQ(m_manager.windows.at(question).box.lines[0], "Hornerot1024");
    EXPECT_EQ(m_manager.windows.at(question).box.owner, FriendTab().base.id);
    // 0x49dc37: también a la pregunta, el 8 y el 9 con su paso.
    EXPECT_EQ(m_manager.windows.at(question).base.current.type, Client::UI::SizeMessage);
    m_keys.insert(EnterKey);
    auto stepped = Step();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    stepped = Step();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(m_sent[0].kind, FriendSendKind::Delete);
    EXPECT_EQ(m_sent[0].name, "Hornero");
}

// El chat con el elegido en línea (0x49d0b0 con el 3) pide la sala con el C1 CA (hito 10); con uno fuera de línea (más
// de 0xfc), nada. La carta (el 4) arma la de escribir, que sin el retrato del juego corta (P-C212).
TEST_F(FriendWindowsTest, TheChatAsksForARoomAndTheLetterNeedsThePortrait)
{
    ASSERT_TRUE(OpenFriends().HasValue());
    Client::UI::AddAFriend(m_manager.friendModel, "Tordo", 0, 0xff);
    Client::UI::AddAFriend(m_manager.friendModel, "Hornero", 0, 3);
    Client::UI::RefreshTheFriendTab(m_manager);
    const auto choose = [this](std::string_view name)
    {
        for (std::size_t at = 0; at < FriendTab().tab.friends.size(); ++at)
        {
            if (FriendTab().tab.friends[at].name == name)
                Client::UI::SelectInTheList(FriendTab().tab.list, FriendTab().tab.friends.size(),
                                            static_cast<std::int32_t>(at + 1));
        }
    };
    choose("Tordo");
    Client::UI::PostToTheWindow(m_manager, FriendTab().base.id, Client::UI::CommandMessage, 3, 0);
    ASSERT_TRUE(Step().HasValue());
    EXPECT_TRUE(m_sent.empty());
    choose("Hornero");
    Client::UI::PostToTheWindow(m_manager, FriendTab().base.id, Client::UI::CommandMessage, 3, 0);
    ASSERT_TRUE(Step().HasValue());
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(m_sent[0].kind, FriendSendKind::RoomRequest);
    EXPECT_EQ(m_sent[0].name, "Hornero");
    Client::UI::PostToTheWindow(m_manager, FriendTab().base.id, Client::UI::CommandMessage, 4, 0);
    const auto cut = Step();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("P-C212"), std::string::npos);
}

// 0x4a2be0: el sí a "¿salir de línea?" manda C4 00 una vez (+0xe0).
TEST_F(FriendWindowsTest, GoingOfflineSendsTheStateOnce)
{
    ASSERT_TRUE(OpenFriends().HasValue());
    for (int times = 0; times < 2; ++times)
    {
        Client::UI::PostToTheWindow(m_manager, m_manager.friends, Client::UI::AnswerMessage, 99, 1);
        ASSERT_TRUE(Step().HasValue());
    }
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(m_sent[0].kind, FriendSendKind::State);
    EXPECT_EQ(m_sent[0].value, 0);
    EXPECT_EQ(m_manager.offlineSent, 1);
}

// 0x49e6e0 y 0x49e710: una caja nueva entra a la pestaña de las ventanas y sale al cerrarse.
TEST_F(FriendWindowsTest, TheWindowsTabFollowsTheBoxes)
{
    ASSERT_TRUE(OpenFriends().HasValue());
    const std::int32_t box = OpenABox(Client::UI::NoticeCase, "aviso uno");
    ASSERT_EQ(Friends().tabs[2].tab.windows.size(), 1u);
    EXPECT_EQ(Friends().tabs[2].tab.windows[0].id, box);
    EXPECT_EQ(Friends().tabs[2].tab.windows[0].title, "aviso uno");
    ASSERT_TRUE(Client::UI::DestroyTheOldWindow(m_manager, box).HasValue());
    EXPECT_TRUE(Friends().tabs[2].tab.windows.empty());
}

// 0x49e6e0: el renglón nuevo va adelante y, haya lo que haya corrido la lista, queda elegido el primero con el
// corrimiento en 0 (Q-C887).
TEST_F(FriendWindowsTest, ANewWindowRowIsChosenWithoutScroll)
{
    ASSERT_TRUE(OpenFriends().HasValue());
    OpenABox(Client::UI::NoticeCase, "aviso uno");
    OpenABox(Client::UI::NoticeCase, "aviso dos");
    Friends().tabs[2].tab.list.scroll = 1;
    OpenABox(Client::UI::NoticeCase, "aviso tres");
    const OldWindow& tab = Friends().tabs[2];
    ASSERT_EQ(tab.tab.windows.size(), 3u);
    EXPECT_EQ(tab.tab.windows[0].title, "aviso tres");
    EXPECT_EQ(tab.tab.list.selection, 1);
    EXPECT_EQ(tab.tab.list.scroll, 0);
}

// 0x4914f0 vacía la lista con su vtable + 0x2c (0x47d320): sin renglones queda sin elegido (+0x58(0), 0x47de10) y sin
// corrimiento; con renglones, el último que entra deja elegido el primero.
TEST_F(FriendWindowsTest, RefillingTheWindowsTabResetsTheChoiceAndTheScroll)
{
    ASSERT_TRUE(OpenFriends().HasValue());
    Client::UI::OldList& list = Friends().tabs[2].tab.list;
    list.selection = 2;
    list.scroll = 1;
    Client::UI::Steps::FillTheWindowsTab(m_manager);
    EXPECT_TRUE(Friends().tabs[2].tab.windows.empty());
    EXPECT_EQ(list.selection, 0);
    EXPECT_EQ(list.scroll, 0);

    OpenABox(Client::UI::NoticeCase, "aviso uno");
    OpenABox(Client::UI::NoticeCase, "aviso dos");
    list.selection = 2;
    list.scroll = 1;
    Client::UI::Steps::FillTheWindowsTab(m_manager);
    ASSERT_EQ(Friends().tabs[2].tab.windows.size(), 2u);
    EXPECT_EQ(list.selection, 1);
    EXPECT_EQ(list.scroll, 0);
}

// 0x4a2f70: la F la cierra si está arriba y el chat cerrado.
TEST_F(FriendWindowsTest, TheFKeyClosesItOnTop)
{
    ASSERT_TRUE(OpenFriends().HasValue());
    m_chatOpen = true;
    m_keys.insert(FriendsKey);
    ASSERT_TRUE(Step().HasValue());
    EXPECT_NE(m_manager.friends, 0);
    m_chatOpen = false;
    m_keys.insert(FriendsKey);
    ASSERT_TRUE(Step().HasValue());
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(m_manager.friends, 0);
}

// 0x4918d0: el título 1066 sin la lista y 990 con ella, también en la ventana.
TEST_F(FriendWindowsTest, MarkingTheListChangesTheTitle)
{
    ASSERT_TRUE(OpenFriends().HasValue());
    Client::UI::MarkTheFriendList(m_manager, 0, m_texts);
    EXPECT_EQ(m_manager.listReceived, 0);
    EXPECT_EQ(m_manager.titleText, 1066);
    EXPECT_EQ(Friends().frame.title, std::optional<std::string>("t1066"));
    Client::UI::MarkTheFriendList(m_manager, 1, m_texts);
    EXPECT_EQ(Friends().frame.title, std::optional<std::string>("t990"));
}

// 0x48ea50: todo suelto, la modal en 0 y los valores del principio.
TEST_F(FriendWindowsTest, TheResetDropsEverything)
{
    ASSERT_TRUE(OpenFriends().HasValue());
    static_cast<void>(OpenABox(Client::UI::ModalNoticeCase, "aviso"));
    m_manager.listReceived = 0;
    m_manager.titleText = 1066;
    m_manager.offlineSent = 1;
    Client::UI::ResetTheOldWindows(m_manager);
    EXPECT_TRUE(m_manager.windows.empty());
    EXPECT_TRUE(m_manager.order.empty());
    EXPECT_EQ(m_manager.friends, 0);
    EXPECT_EQ(m_manager.modal, 0);
    EXPECT_EQ(m_manager.listReceived, 1);
    EXPECT_EQ(m_manager.titleText, 990);
    EXPECT_EQ(m_manager.offlineSent, 0);
}
} // namespace
