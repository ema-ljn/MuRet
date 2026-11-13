#include "UI/OldWindows.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "UI/ChatInput.h"
#include "UI/ChatLog.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindowDraw.h"
#include "support/RecordingTextDrawer.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <list>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Las ventanas viejas (CL8 hito 7; 06 § HUD y ventanas, "Las ventanas viejas"): la fábrica 0x48ee60, el paso 0x490490
// con los botones (0x45b200) y el marco (0x492ac0), los mensajes de 0x490b70 y el dibujo de 0x492500. En la CL8 nadie
// abre una: los tests abren la caja de mensaje a mano, con textos inventados.
namespace
{
using Client::App::PixelSize;
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::DrawCommand;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::TextureSlot;
using Client::Render::Text::FontKind;
using Client::Render::Text::TextColors;
using Client::Tests::RecordingTextDrawer;
using Client::Tests::TextMarkerSlot;
using Client::UI::ChatFocus;
using Client::UI::OldMessage;
using Client::UI::OldWindow;
using Client::UI::OldWindowDrawContext;
using Client::UI::OldWindowDrawView;
using Client::UI::OldWindowGlobals;
using Client::UI::OldWindowManager;
using Client::UI::OldWindowMeasure;
using Client::UI::OldWindowRequest;
using Client::UI::OldWindowStepView;
using Client::UI::OldWindowTexts;

constexpr PixelSize Screen640{640, 480};
constexpr std::uint8_t EscapeKey = 0x1b;
constexpr std::uint8_t F6Key = 0x75;
constexpr std::uint8_t EnterKey = 0x0d;
// La caja con sus valores por omisión (0x48ee60 con −1 y −1): en (0, 0x14c), de 0x96 × 100.
constexpr std::int32_t BoxY = 0x14c;
// 0x41ed10(0x19): el sonido de los botones.
constexpr std::int32_t ButtonSound = 0x19;

const OldWindowTexts Texts{.notice = "Aviso", .question = "Pregunta", .yes = "Si", .no = "No"};

class OldWindowsTest : public ::testing::Test
{
protected:
    OldWindowsTest() : m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
    }

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

    std::int32_t Open(std::int32_t kind, std::string_view text, std::int32_t x = -1, std::int32_t y = -1,
                      std::int32_t aux = 0, std::int32_t flags = 0)
    {
        const auto opened = Client::UI::CreateTheOldWindow(
            m_manager, OldWindowRequest{.kind = kind, .x = x, .y = y, .data = text, .aux = aux, .flags = flags}, Texts,
            OldWindowMeasure{.measure = m_text, .dcFont = FontKind::Normal, .screen = Screen640});
        EXPECT_TRUE(opened.HasValue()) << opened.Error();
        return opened.HasValue() ? opened.Value() : -1;
    }

    // Un paso como el del juego: 0x636e2f (0x5762eb8 en 0), la lista de ventanas (0x636e3c) y 0x490490.
    Client::Core::Expected<void, std::string> Step(std::int32_t x, std::int32_t y)
    {
        m_hovered = 0;
        m_mouseOnUi = 0;
        const OldWindowStepView view{
            .mouseX = x, .mouseY = y, .keyEdge = [this](std::uint8_t key) { return m_keys.erase(key) > 0; }};
        if (auto list = Client::UI::StepTheWindowList(m_manager, Globals(), view); !list.HasValue())
            return list;
        return Client::UI::StepTheOldWindows(m_manager, Globals(), view);
    }

    // El WndProc: WM_LBUTTONDOWN y WM_LBUTTONUP.
    void Press()
    {
        m_leftHeld = true;
        m_leftClick = true;
        m_released = false;
    }
    void Release()
    {
        m_leftHeld = false;
        m_leftClick = false;
        m_released = true;
    }

    OldWindow& Window(std::int32_t id)
    {
        return m_manager.windows.at(id);
    }

    void Draw(bool friendsOpen = false, std::int32_t x = 639, std::int32_t y = 0)
    {
        const OldWindowDrawContext context{
            .sprites = m_sprites, .text = m_text, .dcFont = m_dcFont, .textColors = m_colors};
        const OldWindowDrawView view{.mouseX = x, .mouseY = y, .leftClick = m_leftClick, .friendsOpen = friendsOpen};
        const auto windows = Client::UI::DrawTheOldWindows(m_manager, context, view);
        ASSERT_TRUE(windows.HasValue()) << windows.Error();
        const auto list = Client::UI::DrawTheWindowList(m_manager, context, view);
        ASSERT_TRUE(list.HasValue()) << list.Error();
    }

    OldWindowManager m_manager;
    SpriteBatch m_sprites;
    RecordingTextDrawer m_text;
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
    std::set<std::uint8_t> m_keys;
    FontKind m_dcFont = FontKind::Bold;
    TextColors m_colors{};
};

// 0x48ee60 caso 9: el marco (0x491ca0) y los dos botones (0x45ad80) toman los ids que siguen al de la lista de
// ventanas; 0x4a38f0 le da el título 991, el texto cortado en 2 renglones de 0x7d y los botones en (0x12, 0x28) y
// (0x49, 0x28) de su área de adentro (x + 5, y + 0x15), que 0x4a3a50 ubica al final.
TEST_F(OldWindowsTest, AQuestionBoxTakesTheNextIdsAndPlacesItsButtonsInsideIt)
{
    const std::int32_t id = Open(Client::UI::QuestionCase, "Hola");

    EXPECT_EQ(id, Client::UI::WindowListId + 1);
    EXPECT_EQ(m_manager.lastId, Client::UI::WindowListId + 3);
    const OldWindow& box = Window(id);
    EXPECT_EQ(box.kind, Client::UI::OldWindowKind::MessageBox);
    EXPECT_EQ(box.base.x, 0);
    EXPECT_EQ(box.base.y, BoxY);
    EXPECT_EQ(box.base.width, 0x96);
    EXPECT_EQ(box.base.height, 100);
    EXPECT_EQ(box.base.flags, 0xb);
    EXPECT_EQ(box.base.state, Client::UI::ShownWindow);
    EXPECT_EQ(box.frame.title, "Pregunta");
    EXPECT_EQ(box.box.lines[0], "Hola");
    EXPECT_EQ(box.box.lines[1], "");
    const auto& yes = box.box.buttons[0];
    EXPECT_EQ(yes.base.id, id + 1);
    EXPECT_EQ(yes.base.parent, id);
    EXPECT_EQ(yes.base.x, 5 + 0x12);
    EXPECT_EQ(yes.base.y, BoxY + 0x15 + 0x28);
    EXPECT_EQ(yes.base.width, 0x32);
    EXPECT_EQ(yes.base.height, 0x14);
    EXPECT_EQ(yes.command, 1);
    EXPECT_EQ(yes.label, "Si");
    const auto& no = box.box.buttons[1];
    EXPECT_EQ(no.base.id, id + 2);
    EXPECT_EQ(no.base.x, 5 + 0x49);
    EXPECT_EQ(no.base.y, BoxY + 0x15 + 0x28);
    EXPECT_EQ(no.command, 2);
    EXPECT_EQ(no.label, "No");
    EXPECT_EQ(m_manager.order, (std::list<std::int32_t>{id}));
    EXPECT_EQ(m_manager.boxes, (std::list<std::int32_t>{id}));
    EXPECT_EQ(m_manager.modal, 0);
}

// El texto se corta con 0x459c70 a 0x7d en la fuente del DC, como el registro del chat.
TEST_F(OldWindowsTest, TheBoxTextIsWrappedInTwoLines)
{
    constexpr std::string_view Text = "una pregunta inventada que no entra en un solo renglon de la caja";
    const auto expected = Client::UI::WrapByWidth(Text, m_text, FontKind::Normal, Screen640, 0x7d, 2, 0);
    ASSERT_TRUE(expected.HasValue()) << expected.Error();
    ASSERT_EQ(expected.Value().lines.size(), 2u);

    const std::int32_t id = Open(Client::UI::NoticeCase, Text);

    EXPECT_EQ(Window(id).box.lines[0], expected.Value().lines[0]);
    EXPECT_EQ(Window(id).box.lines[1], expected.Value().lines[1]);
}

// Con un solo botón (0x48ee60 caso 10), el primero lleva el título 228 en (0x2d, 0x28); el segundo no lo arma 0x4a38f0
// y queda como lo dejó 0x45ad80: sin padre, en (0, 0) de 0x46 × 0x14 (Q-C570).
TEST_F(OldWindowsTest, ANoticeBoxHasOneButtonAndAGhostAtTheOrigin)
{
    const std::int32_t id = Open(Client::UI::NoticeCase, "Listo", 100, 50);

    const OldWindow& box = Window(id);
    EXPECT_EQ(box.base.x, 100);
    EXPECT_EQ(box.base.y, 50);
    EXPECT_EQ(box.frame.title, "Aviso");
    EXPECT_EQ(box.box.oneButton, 1);
    EXPECT_EQ(box.box.buttons[0].base.x, 100 + 5 + 0x2d);
    EXPECT_EQ(box.box.buttons[0].base.y, 50 + 0x15 + 0x28);
    EXPECT_EQ(box.box.buttons[0].label, "Aviso");
    const auto& ghost = box.box.buttons[1];
    EXPECT_EQ(ghost.base.parent, 0);
    EXPECT_EQ(ghost.base.x, 0);
    EXPECT_EQ(ghost.base.y, 0);
    EXPECT_EQ(ghost.base.width, 0x46);
    EXPECT_EQ(ghost.base.height, 0x14);
    EXPECT_FALSE(ghost.label.has_value());
}

// 0x4a43c0 corre el paso de los dos botones: el que no se usa toma el mouse arriba a la izquierda, se aprieta y suena
// al soltarlo, con su comando a nadie (Q-C570).
TEST_F(OldWindowsTest, TheGhostButtonTakesTheMouseAtTheTopLeft)
{
    const std::int32_t id = Open(Client::UI::NoticeCase, "Listo", 100, 50);
    const std::int32_t ghost = id + 2;

    ASSERT_TRUE(Step(10, 10).HasValue());
    EXPECT_EQ(m_hovered, ghost);
    EXPECT_EQ(m_mouseOnUi, 1);

    Press();
    ASSERT_TRUE(Step(10, 10).HasValue());
    EXPECT_EQ(m_pressedButton, ghost);
    EXPECT_EQ(Window(id).box.buttons[1].pressed, 1);

    Release();
    ASSERT_TRUE(Step(10, 10).HasValue());
    EXPECT_EQ(m_sound, ButtonSound);
    EXPECT_EQ(m_pressedButton, 0);
    EXPECT_FALSE(m_released);
    ASSERT_TRUE(Step(10, 10).HasValue());
    EXPECT_TRUE(m_manager.windows.contains(id));
    EXPECT_TRUE(Window(id).base.queue.empty());
}

// El clic en "Si": el apretón lo marca (0x5762ec4); el soltado le manda el 10 a la caja y suena; en el paso
// siguiente la caja le avisa al dueño (el 12 con su id y 1) y le pide al administrador que la cierre, que la suelta al
// final del mismo paso, suena otra vez y le da el foco a la de abajo.
TEST_F(OldWindowsTest, ClickingYesAnswersTheOwnerAndClosesTheBoxOneStepLater)
{
    const std::int32_t owner = Open(Client::UI::FrameCase, "Dueño", 300, 20);
    const std::int32_t id = Open(Client::UI::QuestionCase, "Seguro?", -1, -1, owner);
    const auto& yes = Window(id).box.buttons[0].base;
    const std::int32_t x = yes.x + 4;
    const std::int32_t y = yes.y + 4;

    Press();
    ASSERT_TRUE(Step(x, y).HasValue());
    EXPECT_EQ(m_hovered, id + 1);
    EXPECT_EQ(m_pressedButton, id + 1);
    // El apretón sobre el botón sube a su padre (0x45aa80 → 0x490b70 con el 1).
    EXPECT_EQ(m_manager.focus, id);

    Release();
    ASSERT_TRUE(Step(x, y).HasValue());
    EXPECT_EQ(m_sound, ButtonSound);
    EXPECT_EQ(Window(id).base.queue.front(), (OldMessage{.type = 10, .a = 1, .b = 0, .text = {}}));

    m_sound.reset();
    ASSERT_TRUE(Step(x, y).HasValue());
    EXPECT_FALSE(m_manager.windows.contains(id));
    EXPECT_EQ(m_manager.order, (std::list<std::int32_t>{owner}));
    EXPECT_TRUE(m_manager.boxes.empty());
    EXPECT_EQ(m_sound, ButtonSound);
    // El dueño, más abajo en la lista, corre su paso después de la caja: atiende el 12 en el mismo paso.
    EXPECT_EQ(Window(owner).base.current, (OldMessage{.type = 12, .a = id, .b = 1, .text = {}}));
    // 0x490b70 con el 4: la nueva del final de la lista recibe el 6.
    EXPECT_EQ(Window(owner).base.queue.front(), (OldMessage{.type = 6, .a = 0, .b = 0, .text = {}}));
}

// "No" avisa con 0.
TEST_F(OldWindowsTest, ClickingNoAnswersZero)
{
    const std::int32_t owner = Open(Client::UI::FrameCase, "Dueño", 300, 20);
    const std::int32_t id = Open(Client::UI::QuestionCase, "Seguro?", -1, -1, owner);
    const auto& no = Window(id).box.buttons[1].base;

    Press();
    ASSERT_TRUE(Step(no.x + 1, no.y + 1).HasValue());
    Release();
    ASSERT_TRUE(Step(no.x + 1, no.y + 1).HasValue());
    ASSERT_TRUE(Step(no.x + 1, no.y + 1).HasValue());

    EXPECT_FALSE(m_manager.windows.contains(id));
    EXPECT_EQ(Window(owner).base.current, (OldMessage{.type = 12, .a = id, .b = 0, .text = {}}));
}

// Soltado fuera del botón: 0x45b200 suelta todo, el izquierdo y los dos flancos.
TEST_F(OldWindowsTest, ReleasingAwayFromTheButtonDropsThePress)
{
    const std::int32_t id = Open(Client::UI::QuestionCase, "Seguro?");
    const auto& yes = Window(id).box.buttons[0].base;

    Press();
    ASSERT_TRUE(Step(yes.x + 1, yes.y + 1).HasValue());
    ASSERT_EQ(m_pressedButton, id + 1);
    ASSERT_TRUE(Step(yes.x + 1 + yes.width, yes.y + 1).HasValue());

    EXPECT_EQ(m_pressedButton, 0);
    EXPECT_EQ(Window(id).box.buttons[0].pressed, 0);
    EXPECT_FALSE(m_leftHeld);
    EXPECT_FALSE(m_leftClick);
    EXPECT_TRUE(Window(id).base.queue.empty());
}

// Enter (0x553520(0xd)) solo lo lee la caja del final de la lista: se manda el 10 con 1 y se cierra al paso siguiente.
TEST_F(OldWindowsTest, EnterAnswersOnlyTheBoxOnTop)
{
    const std::int32_t below = Open(Client::UI::QuestionCase, "Abajo", 0, 0, 0, Client::UI::KeepPlaceFlag);
    const std::int32_t top = Open(Client::UI::QuestionCase, "Arriba", 300, 0, 0, Client::UI::KeepPlaceFlag);
    m_keys = {EnterKey};

    ASSERT_TRUE(Step(639, 479).HasValue());
    EXPECT_EQ(Window(top).base.queue.front(), (OldMessage{.type = 10, .a = 1, .b = 0, .text = {}}));
    EXPECT_TRUE(Window(below).base.queue.empty());
    EXPECT_TRUE(m_keys.empty());

    ASSERT_TRUE(Step(639, 479).HasValue());
    EXPECT_FALSE(m_manager.windows.contains(top));
    EXPECT_TRUE(m_manager.windows.contains(below));
}

// 0x48f606..0x48f6b8: al correrse, la vuelta sigue desde el primero del mapa, que el ++ saltea; una ventana nueva puede
// quedar justo encima de la de id más chico (Q-C571).
TEST_F(OldWindowsTest, TheCascadeSkipsTheFirstWindowAfterMoving)
{
    const std::int32_t first = Open(Client::UI::FrameCase, "Uno", 20, 20, 0, Client::UI::KeepPlaceFlag);
    const std::int32_t second = Open(Client::UI::FrameCase, "Dos", 0, 0, 0, Client::UI::KeepPlaceFlag);

    const std::int32_t third = Open(Client::UI::FrameCase, "Tres", 0, 0);

    EXPECT_EQ(Window(second).base.x, 0);
    EXPECT_EQ(Window(third).base.x, Window(first).base.x);
    EXPECT_EQ(Window(third).base.y, Window(first).base.y);
}

// Sin el salteo, la cascada corre 20 cada vez que encuentra una en su lugar.
TEST_F(OldWindowsTest, TheCascadeMovesTwentyPerWindowInItsPlace)
{
    static_cast<void>(Open(Client::UI::FrameCase, "Uno", 0, 0, 0, Client::UI::KeepPlaceFlag));
    static_cast<void>(Open(Client::UI::FrameCase, "Dos", 20, 20, 0, Client::UI::KeepPlaceFlag));

    const std::int32_t third = Open(Client::UI::FrameCase, "Tres", 0, 0);

    EXPECT_EQ(Window(third).base.x, 40);
    EXPECT_EQ(Window(third).base.y, 40);
}

// 0x5762ebc: con una caja modal, la fábrica no arma otra (y no gasta ids); se libera con la caja.
TEST_F(OldWindowsTest, AModalBoxBlocksAnotherModalUntilItGoes)
{
    const std::int32_t modal = Open(Client::UI::ModalNoticeCase, "Espera");
    ASSERT_EQ(m_manager.modal, modal);
    EXPECT_TRUE(m_manager.boxes.empty());
    const std::int32_t lastId = m_manager.lastId;

    EXPECT_EQ(Open(Client::UI::ModalQuestionCase, "Otra"), 0);
    EXPECT_EQ(m_manager.lastId, lastId);
    EXPECT_EQ(m_manager.order.size(), 1u);

    ASSERT_TRUE(Client::UI::DestroyTheOldWindow(m_manager, modal).HasValue());
    EXPECT_EQ(m_manager.modal, 0);
    EXPECT_NE(Open(Client::UI::ModalQuestionCase, "Otra"), 0);
}

// Los casos de la CL9.4 cortan con la dirección de la fábrica.
TEST_F(OldWindowsTest, TheFriendsCasesOfTheFactoryAreACut)
{
    const auto opened = Client::UI::CreateTheOldWindow(
        m_manager, OldWindowRequest{.kind = 3, .x = -1, .y = -1, .data = "", .aux = 0, .flags = 0}, Texts,
        OldWindowMeasure{.measure = m_text, .dcFont = FontKind::Normal, .screen = Screen640});
    ASSERT_FALSE(opened.HasValue());
    EXPECT_NE(opened.Error().find("0x48ee60"), std::string::npos);
}

// 0x4904bc: Esc cierra la del final de la lista, a la vista; suena y el foco va a la que queda.
TEST_F(OldWindowsTest, EscapeClosesTheLastWindow)
{
    const std::int32_t below = Open(Client::UI::QuestionCase, "Abajo", 0, 0, 0, Client::UI::KeepPlaceFlag);
    const std::int32_t top = Open(Client::UI::QuestionCase, "Arriba", 300, 0, 0, Client::UI::KeepPlaceFlag);
    m_keys = {EscapeKey};

    ASSERT_TRUE(Step(639, 479).HasValue());

    EXPECT_FALSE(m_manager.windows.contains(top));
    EXPECT_EQ(m_manager.order, (std::list<std::int32_t>{below}));
    EXPECT_EQ(m_sound, ButtonSound);
    EXPECT_EQ(Window(below).base.queue.front(), (OldMessage{.type = 6, .a = 0, .b = 0, .text = {}}));
}

// F6 (0x490561): el primero esconde las que están a la vista y las anota; el segundo las muestra, le pide al
// administrador que suba la de arriba y vacía la lista.
TEST_F(OldWindowsTest, F6HidesTheWindowsAndShowsThemAgain)
{
    const std::int32_t id = Open(Client::UI::QuestionCase, "Hola");
    m_focus = ChatFocus::Message;
    m_manager.focus = id;

    m_keys = {F6Key};
    ASSERT_TRUE(Step(639, 479).HasValue());
    EXPECT_EQ(Window(id).base.state, Client::UI::HiddenWindow);
    EXPECT_EQ(m_manager.hidden, (std::list<std::int32_t>{id}));
    EXPECT_EQ(m_manager.hiddenByF6, 1);
    EXPECT_EQ(m_manager.focus, 0);
    EXPECT_EQ(m_focus, ChatFocus::Game);

    m_keys = {F6Key};
    ASSERT_TRUE(Step(639, 479).HasValue());
    EXPECT_EQ(Window(id).base.state, Client::UI::ShownWindow);
    EXPECT_TRUE(m_manager.hidden.empty());
    EXPECT_EQ(m_manager.hiddenByF6, 0);
    EXPECT_EQ(m_manager.focus, id);
}

// F6 sin ventanas a la vista pone +0xa8 en 1 con la lista vacía: desde ahí F6 no hace nada más, y una caja que se abre
// después no se esconde (Q-C573).
TEST_F(OldWindowsTest, AFirstF6WithNothingShownLeavesF6Stuck)
{
    m_keys = {F6Key};
    ASSERT_TRUE(Step(639, 479).HasValue());
    ASSERT_EQ(m_manager.hiddenByF6, 1);

    const std::int32_t id = Open(Client::UI::QuestionCase, "Hola");
    m_keys = {F6Key};
    ASSERT_TRUE(Step(639, 479).HasValue());
    EXPECT_TRUE(m_keys.empty());
    EXPECT_EQ(Window(id).base.state, Client::UI::ShownWindow);
    EXPECT_EQ(m_manager.hiddenByF6, 1);
}

// Una escondida por F6 que se suelta antes del segundo F6 deja la cuenta corta: 0x491080 abre la de amigos (Q-C448).
TEST_F(OldWindowsTest, ASecondF6WithAWindowGoneOpensTheFriendsWindow)
{
    const std::int32_t id = Open(Client::UI::QuestionCase, "Hola");
    m_keys = {F6Key};
    ASSERT_TRUE(Step(639, 479).HasValue());
    ASSERT_TRUE(Client::UI::DestroyTheOldWindow(m_manager, id).HasValue());

    m_keys = {F6Key};
    const auto stepped = Step(639, 479);

    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x491080"), std::string::npos);
}

// Con el juego (CL9 hito 9), el segundo F6 abre la de amigos con 0x491080 en vez de mostrar la que ya no está
// (Q-C448).
TEST_F(OldWindowsTest, ShowingAgainWithAWindowGoneRunsTheFriendsOpening)
{
    int opened = 0;
    OldWindowStepView view{.mouseX = 639, .mouseY = 479};
    view.openFriends = [&opened]() -> Client::Core::Expected<void, std::string>
    {
        ++opened;
        return {};
    };
    const std::int32_t id = Open(Client::UI::QuestionCase, "Hola");
    ASSERT_TRUE(Client::UI::HideTheOldWindows(m_manager, 1, 1, Globals(), &view).HasValue());
    ASSERT_TRUE(Client::UI::DestroyTheOldWindow(m_manager, id).HasValue());
    ASSERT_TRUE(Client::UI::HideTheOldWindows(m_manager, 0, 1, Globals(), &view).HasValue());
    EXPECT_EQ(opened, 1);
    EXPECT_TRUE(m_manager.hidden.empty());
}

// 0x4906a0(1, 0), el de Chaos Castle: las esconde sin anotarlas, y F6 ya no las vuelve a mostrar (Q-C572); la modal
// queda.
TEST_F(OldWindowsTest, HidingWithoutFriendsDoesNotNoteTheWindows)
{
    const std::int32_t box = Open(Client::UI::QuestionCase, "Hola");
    const std::int32_t modal = Open(Client::UI::ModalNoticeCase, "Espera", 300, 0);

    ASSERT_TRUE(Client::UI::HideTheOldWindows(m_manager, 1, 0, Globals()).HasValue());
    EXPECT_EQ(Window(box).base.state, Client::UI::HiddenWindow);
    EXPECT_EQ(Window(modal).base.state, Client::UI::ShownWindow);
    EXPECT_TRUE(m_manager.hidden.empty());

    m_keys = {F6Key};
    ASSERT_TRUE(Step(639, 479).HasValue());
    m_keys = {F6Key};
    ASSERT_TRUE(Step(639, 479).HasValue());
    EXPECT_EQ(Window(box).base.state, Client::UI::HiddenWindow);
}

// De arriba hacia abajo: con dos cajas una sobre otra, el mouse es de la de arriba; el apretón sobre la de abajo, donde
// no la tapa la otra, la sube.
TEST_F(OldWindowsTest, TheMouseGoesToTheTopWindowAndAPressRaisesTheOneBelow)
{
    const std::int32_t below = Open(Client::UI::QuestionCase, "Abajo", 0, 0, 0, Client::UI::KeepPlaceFlag);
    const std::int32_t top = Open(Client::UI::QuestionCase, "Arriba", 100, 0, 0, Client::UI::KeepPlaceFlag);

    ASSERT_TRUE(Step(120, 2).HasValue());
    EXPECT_EQ(m_hovered, top);

    Press();
    ASSERT_TRUE(Step(50, 30).HasValue());
    EXPECT_EQ(m_hovered, below);
    EXPECT_EQ(m_manager.order, (std::list<std::int32_t>{top, below}));
    EXPECT_EQ(m_manager.focus, below);
}

// 0x492ac0: el apretón en la barra del título agarra el mouse (0x5762eb4) y arrastra la caja, acotada a la pantalla;
// soltado, la deja.
TEST_F(OldWindowsTest, TheTitleBarDragsTheBox)
{
    const std::int32_t id = Open(Client::UI::QuestionCase, "Hola", 100, 100);

    Press();
    ASSERT_TRUE(Step(110, 105).HasValue());
    EXPECT_EQ(m_capture, id);
    EXPECT_EQ(Window(id).base.state, Client::UI::DraggedWindow);

    m_leftClick = false;
    ASSERT_TRUE(Step(130, 95).HasValue());
    EXPECT_EQ(Window(id).base.x, 120);
    EXPECT_EQ(Window(id).base.y, 90);
    ASSERT_TRUE(Step(0, 0).HasValue());
    EXPECT_EQ(Window(id).base.x, 0);
    EXPECT_EQ(Window(id).base.y, 0);

    Release();
    ASSERT_TRUE(Step(0, 0).HasValue());
    EXPECT_EQ(m_capture, 0);
    EXPECT_EQ(Window(id).base.state, Client::UI::ShownWindow);
}

// 0x491960: los colores con ftol(alfa · 255), y el 3 con ftol(alfa · 179).
TEST(OldWindowPaletteTest, TheShadowTakesLessAlpha)
{
    EXPECT_EQ(Client::UI::OldWindowPalette(0, 1.0f),
              (Color{.red = 146.0f / 255.0f, .green = 134.0f / 255.0f, .blue = 121.0f / 255.0f, .alpha = 1.0f}));
    EXPECT_EQ(Client::UI::OldWindowPalette(3, 1.0f),
              (Color{.red = 0.0f, .green = 0.0f, .blue = 0.0f, .alpha = 179.0f / 255.0f}));
    EXPECT_FLOAT_EQ(Client::UI::OldWindowPalette(2, 0.5f)->alpha, 127.0f / 255.0f);
    EXPECT_FALSE(Client::UI::OldWindowPalette(19, 1.0f).has_value());
    EXPECT_FALSE(Client::UI::OldWindowPalette(-1, 1.0f).has_value());
}

struct ExpectedCommand
{
    std::optional<TextureSlot> texture;
    BlendMode blend;
    bool alphaTest;
    std::size_t quads;
};

// La caja de 0x48ee60 caso 9 en (0, 0x14c), dibujada por 0x492500 sin el mouse encima y sin la ventana de amigos: la
// sombra, el texto y los botones (0x4a3a90 y 0x45b2e0), todo el marco en gris (0,5; la barra del título, sin mezcla
// porque el botón dejó 0x620980), los filos 10..13, el borde (0x492180), la raya, el título y la cruz. La lista de
// ventanas, oculta, no pone nada.
TEST_F(OldWindowsTest, TheBoxIsDrawnLikeTheOriginal)
{
    static_cast<void>(Open(Client::UI::QuestionCase, "Hola"));

    Draw();

    const std::span<const DrawCommand> commands = m_sprites.Commands();
    const std::vector<ExpectedCommand> expected{
        {std::nullopt, BlendMode::Alpha, false, 1},   // la sombra
        {TextMarkerSlot, BlendMode::Alpha, false, 1}, // "Hola"
        {TextureSlot{Client::UI::WindowButtonSlot}, BlendMode::Alpha, true, 1},
        {TextMarkerSlot, BlendMode::Alpha, true, 1}, // "Si"
        {TextureSlot{Client::UI::WindowButtonSlot}, BlendMode::Alpha, true, 1},
        {TextMarkerSlot, BlendMode::Alpha, true, 1}, // "No"
        {TextureSlot{Client::UI::WindowTitleSlot}, BlendMode::Opaque, false, 1},
        {std::nullopt, BlendMode::Opaque, false, 6 + 16}, // los filos y el borde
        {std::nullopt, BlendMode::Alpha, false, 1},       // la raya
        {TextMarkerSlot, BlendMode::Alpha, false, 1},     // "Pregunta"
        {TextureSlot{Client::UI::WindowTitleButtonsSlot}, BlendMode::Alpha, false, 1},
    };
    ASSERT_EQ(commands.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(commands[i].texture, expected[i].texture);
        EXPECT_EQ(commands[i].blend, expected[i].blend);
        EXPECT_EQ(commands[i].alphaTest, expected[i].alphaTest);
        EXPECT_EQ(commands[i].vertexCount, expected[i].quads * 6);
    }

    const std::span<const SpriteVertex> vertices = m_sprites.Vertices();
    // La sombra: (x, y + 5) de w × (h − 10), negra con 179.
    EXPECT_NEAR(vertices[0].x, 0.0f, 1e-4f);
    EXPECT_NEAR(vertices[0].y, 480.0f - 337.0f, 1e-4f);
    EXPECT_NEAR(vertices[2].x, 150.0f, 1e-4f);
    EXPECT_NEAR(vertices[2].y, 480.0f - 427.0f, 1e-4f);
    EXPECT_FLOAT_EQ(vertices[0].color.alpha, 179.0f / 255.0f);
    // El botón "Si" en (0x17, 0x189) de 0x32 × 0x14, en 0,8.
    const SpriteVertex& yes = vertices[commands[2].firstVertex];
    EXPECT_NEAR(yes.x, 23.0f, 1e-4f);
    EXPECT_NEAR(yes.y, 480.0f - 393.0f, 1e-4f);
    EXPECT_NEAR(vertices[commands[2].firstVertex + 2].x, 73.0f, 1e-4f);
    EXPECT_FLOAT_EQ(yes.color.red, 0.8f);
    // La barra del título en (x + 6, y + 5) de w − 12 × 15, en 0,5.
    const SpriteVertex& bar = vertices[commands[6].firstVertex];
    EXPECT_NEAR(bar.x, 6.0f, 1e-4f);
    EXPECT_NEAR(bar.y, 480.0f - 337.0f, 1e-4f);
    EXPECT_NEAR(vertices[commands[6].firstVertex + 2].x, 144.0f, 1e-4f);
    EXPECT_NEAR(vertices[commands[6].firstVertex + 2].y, 480.0f - 352.0f, 1e-4f);
    EXPECT_FLOAT_EQ(bar.color.red, 0.5f);
    // El primer filo, el 10 en lugar del 8 (no es la de arriba con la de amigos).
    const SpriteVertex& edge = vertices[commands[7].firstVertex];
    EXPECT_NEAR(edge.x, 5.0f, 1e-4f);
    EXPECT_NEAR(edge.y, 480.0f - 337.0f, 1e-4f);
    EXPECT_FLOAT_EQ(edge.color.red, 83.0f / 255.0f);
    // La cruz, 9 × 9 en (x + w − 16, y + 8), en 0,8.
    const SpriteVertex& close = vertices[commands[10].firstVertex];
    EXPECT_NEAR(close.x, 134.0f, 1e-4f);
    EXPECT_NEAR(close.y, 480.0f - 340.0f, 1e-4f);
    EXPECT_FLOAT_EQ(close.color.red, 0.8f);

    ASSERT_EQ(m_text.texts.size(), 4u);
    EXPECT_EQ(m_text.texts[0].text, "Hola");
    EXPECT_EQ(m_text.texts[0].x, 10);
    EXPECT_EQ(m_text.texts[0].y, BoxY + 0x1d);
    EXPECT_EQ(m_text.texts[0].style.font, FontKind::Normal);
    EXPECT_EQ(m_text.texts[0].style.colors, (TextColors{.text = 0xffffffffu, .background = 0}));
    // ((0x32 − 12) + 0,5) · 0,5 + x y ((0x14 − 13) + 0,5) · 0,5 + y + 1.
    EXPECT_EQ(m_text.texts[1].text, "Si");
    EXPECT_EQ(m_text.texts[1].x, 42);
    EXPECT_EQ(m_text.texts[1].y, 397);
    EXPECT_EQ(m_text.texts[1].style.colors, (TextColors{.text = 0xffc8dce6u, .background = 0}));
    EXPECT_EQ(m_text.texts[2].text, "No");
    EXPECT_EQ(m_text.texts[2].x, 97);
    EXPECT_EQ(m_text.texts[3].text, "Pregunta");
    EXPECT_EQ(m_text.texts[3].x, 9);
    EXPECT_EQ(m_text.texts[3].y, BoxY + 8);
    EXPECT_EQ(m_text.texts[3].style.font, FontKind::Bold);
    EXPECT_EQ(m_text.texts[3].style.colors, (TextColors{.text = 0xff646e73u, .background = 0}));
    EXPECT_EQ(m_dcFont, FontKind::Normal);
    EXPECT_EQ(m_sprites.Blend(), BlendMode::Opaque);
    EXPECT_EQ(m_manager.drawn, 1);
    // 0x4a5490: la lista de ventanas mide su renglón con "0" la primera vez.
    EXPECT_FLOAT_EQ(m_manager.windowList.list.rowHeight, 13.0f);
}

// Con el mouse y el clic sobre el botón apretado: 0,6, un píxel adentro y el texto uno más abajo y a la derecha.
TEST_F(OldWindowsTest, ThePressedButtonSinksOnePixel)
{
    const std::int32_t id = Open(Client::UI::QuestionCase, "Hola");
    Window(id).box.buttons[0].pressed = 1;
    m_leftClick = true;

    Draw(false, 30, 400);

    const std::span<const DrawCommand> commands = m_sprites.Commands();
    ASSERT_GE(commands.size(), 3u);
    const SpriteVertex& yes = m_sprites.Vertices()[commands[2].firstVertex];
    EXPECT_NEAR(yes.x, 24.0f, 1e-4f);
    EXPECT_NEAR(yes.y, 480.0f - 394.0f, 1e-4f);
    EXPECT_NEAR(m_sprites.Vertices()[commands[2].firstVertex + 2].x, 73.0f, 1e-4f);
    EXPECT_FLOAT_EQ(yes.color.red, 0.6f);
    EXPECT_EQ(m_text.texts[1].x, 43);
    EXPECT_EQ(m_text.texts[1].y, 398);
}

// La de arriba con la ventana de amigos abierta (0x659290(1)) va sin gris: los filos 8 y 9, el título claro y sin la
// mezcla opaca del final.
TEST_F(OldWindowsTest, TheTopWindowWithTheFriendsWindowOpenIsNotDimmed)
{
    static_cast<void>(Open(Client::UI::QuestionCase, "Hola"));

    Draw(true);

    const std::span<const DrawCommand> commands = m_sprites.Commands();
    ASSERT_EQ(commands.size(), 11u);
    EXPECT_FLOAT_EQ(m_sprites.Vertices()[commands[6].firstVertex].color.red, 1.0f);
    EXPECT_FLOAT_EQ(m_sprites.Vertices()[commands[7].firstVertex].color.red, 153.0f / 255.0f);
    EXPECT_EQ(m_text.texts[3].style.colors.text, 0xffc8dce6u);
}
// P-C215: F5 con dos chats (dos cajas como chats): 0x4a5be0 la muestra y elige el que no es la de arriba, después el
// que sigue con vuelta; Enter sube el elegido (0x4a4e20); el mouse sobre los renglones (0x4a4eb0) los elige de abajo
// hacia arriba y el soltado minimiza la de arriba o sube otra. En el borde de arriba, ninguno (Q-C1008).
TEST_F(OldWindowsTest, F5ShowsTheChatsAndChoosesOne)
{
    constexpr std::uint8_t F5Key = 0x74;
    const std::int32_t first = Open(Client::UI::QuestionCase, "Uno");
    const std::int32_t second = Open(Client::UI::QuestionCase, "Dos");
    OldWindow& list = m_manager.windowList;
    list.list.chats = {first, second};
    list.list.cursor = 2;
    list.list.rowHeight = 14.0f;
    m_keys.insert(F5Key);
    ASSERT_TRUE(Step(0, 0).HasValue());
    EXPECT_EQ(list.base.state, Client::UI::ShownWindow);
    EXPECT_EQ(list.list.armed, 1);
    EXPECT_EQ(list.base.height, 36);
    EXPECT_EQ(list.base.y, 0x1b3 - 36);
    EXPECT_FLOAT_EQ(list.list.blinkStep, 0.25f);
    EXPECT_EQ(list.list.cursor, 0u);
    m_keys.insert(F5Key);
    ASSERT_TRUE(Step(0, 0).HasValue());
    EXPECT_EQ(list.list.cursor, 1u);
    m_keys.insert(F5Key);
    ASSERT_TRUE(Step(0, 0).HasValue());
    EXPECT_EQ(list.list.cursor, 0u);
    m_keys.insert(EnterKey);
    ASSERT_TRUE(Step(0, 0).HasValue());
    EXPECT_EQ(list.base.state, Client::UI::HiddenWindow);
    EXPECT_EQ(list.list.armed, 0);
    EXPECT_EQ(m_manager.order.back(), first);

    // Oculta, F5 saltea la de arriba (ahora la primera). El mouse en el renglón de abajo es el primero: es la de arriba
    // y se minimiza.
    m_keys.insert(F5Key);
    ASSERT_TRUE(Step(0, 0).HasValue());
    EXPECT_EQ(list.list.cursor, 1u);
    Release();
    ASSERT_TRUE(Step(0x250, 0x1b3 - 5).HasValue());
    EXPECT_EQ(m_sound, std::optional<std::int32_t>(ButtonSound));
    EXPECT_EQ(m_mouseOnUi, 1);
    EXPECT_EQ(list.base.state, Client::UI::HiddenWindow);
    EXPECT_EQ(Window(first).base.state, Client::UI::HiddenWindow);
    EXPECT_EQ(m_manager.order.front(), first);

    // La primera, oculta, se sube.
    m_keys.insert(F5Key);
    ASSERT_TRUE(Step(0, 0).HasValue());
    Release();
    m_leftHeld = true;
    ASSERT_TRUE(Step(0x250, 0x1b3 - 5).HasValue());
    EXPECT_EQ(m_manager.order.back(), first);
    EXPECT_EQ(Window(first).base.state, Client::UI::ShownWindow);
    EXPECT_FALSE(m_leftHeld);

    // El borde de arriba da el final: ninguna, y la lista se oculta igual.
    m_keys.insert(F5Key);
    ASSERT_TRUE(Step(0, 0).HasValue());
    Release();
    ASSERT_TRUE(Step(0x250, 0x1b3 - 36).HasValue());
    EXPECT_EQ(list.list.cursor, 2u);
    EXPECT_EQ(list.base.state, Client::UI::HiddenWindow);
    EXPECT_EQ(m_manager.order.back(), first);

    // Fuera de los renglones y sin F5, el elegido va al final; al soltar un chat (0x4a4b30), también.
    m_keys.insert(F5Key);
    ASSERT_TRUE(Step(0, 0).HasValue());
    list.list.armed = 0;
    ASSERT_TRUE(Step(0, 0).HasValue());
    EXPECT_EQ(list.list.cursor, 2u);
    list.list.cursor = 0;
    ASSERT_TRUE(Client::UI::DestroyTheOldWindow(m_manager, first).HasValue());
    EXPECT_EQ(list.list.chats.size(), 1u);
    EXPECT_EQ(list.list.cursor, 1u);

    // Con el foco en la entrada del chat, F5 no se lee; en el EDIT de una ventana vieja, sí (0x467d3b).
    m_focus = ChatFocus::Message;
    m_keys.insert(F5Key);
    ASSERT_TRUE(Step(0, 0).HasValue());
    EXPECT_EQ(list.list.armed, 0);
    Release();
    m_focus = ChatFocus::OldWindow;
    m_keys.insert(F5Key);
    ASSERT_TRUE(Step(0, 0).HasValue());
    EXPECT_EQ(list.list.armed, 1);
}
} // namespace
