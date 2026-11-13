#include "UI/NewUi.h"

#include "App/WindowState.h"
#include "Input/InputState.h"
#include "UI/Widget.h"

#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <vector>

// Client::UI::NewUi: el administrador de la UI nueva (CL8, hito 8): el orden de 0x644b60, 0x65f610 y
// 0x65f6c0, el foco y el mouse de 0x65fc30, el arrastre de 0x669f40 y el pegado de 0x65f720.
namespace
{
using Client::App::PixelSize;
using Client::Input::InputState;
using Client::UI::NewUi;
using Client::UI::NewUiMode;
using Client::UI::NewUiTurn;
using Client::UI::NewUiWindow;
using Client::UI::Point;
using Client::UI::Rect;
using Client::UI::WindowHit;

constexpr PixelSize Screen640{640, 480};

// Una ventana con su rectángulo; `draggable` contesta al título como 0x669df0, y las demás no,
// como las del login y la selección.
class FakeWindow final : public NewUiWindow
{
public:
    explicit FakeWindow(Rect area, bool draggable = false) : m_area(area), m_draggable(draggable) {}

    void Open()
    {
        m_open = true;
    }
    [[nodiscard]] bool IsOpen() const override
    {
        return m_open;
    }
    void Close() override
    {
        m_open = false;
    }
    [[nodiscard]] Rect Bounds(PixelSize /*screen*/) const override
    {
        return m_area;
    }
    [[nodiscard]] bool Hit(WindowHit hit, Point mouse, PixelSize screen) const override
    {
        return m_draggable ? Client::UI::HitTheBounds(*this, hit, mouse, screen) : NewUiWindow::Hit(hit, mouse, screen);
    }
    void PlaceAt(Point topLeft, PixelSize /*screen*/) override
    {
        m_area.x = topLeft.x;
        m_area.y = topLeft.y;
    }

private:
    Rect m_area;
    bool m_draggable = false;
    bool m_open = false;
};

struct Turn
{
    const NewUiWindow* window = nullptr;
    NewUiTurn turn;
};

// Un paso de 0x65fc30 que anota a quién atendió y con qué.
std::vector<Turn> StepAndRecord(NewUi& ui, const InputState& input)
{
    std::vector<Turn> turns;
    ui.Step(input, Screen640, [&](NewUiWindow& window, NewUiTurn turn) { turns.push_back({&window, turn}); });
    return turns;
}

void OpenAndRaise(NewUi& ui, FakeWindow& window)
{
    window.Open();
    ui.Raise(window);
}

TEST(NewUiTest, BuildPutsTheLastOneOnTop)
{
    FakeWindow a{Rect{0, 0, 10, 10}};
    FakeWindow b{Rect{0, 0, 10, 10}};
    FakeWindow c{Rect{0, 0, 10, 10}};
    NewUi ui;
    const std::array<NewUiWindow*, 3> windows{&a, &b, &c};
    ui.Build(NewUiMode::Login, windows);

    EXPECT_EQ(ui.Mode(), NewUiMode::Login);
    EXPECT_EQ(ui.Order(), (std::vector<NewUiWindow*>{&c, &b, &a}));
    EXPECT_FALSE(ui.HasTheFocus(c));
    EXPECT_TRUE(ui.DrawOrder().empty());
}

TEST(NewUiTest, TheLastOneOpenedGoesOnTopWithTheFocus)
{
    FakeWindow a{Rect{0, 0, 10, 10}};
    FakeWindow b{Rect{0, 0, 10, 10}};
    FakeWindow c{Rect{0, 0, 10, 10}};
    NewUi ui;
    const std::array<NewUiWindow*, 3> windows{&a, &b, &c};
    ui.Build(NewUiMode::Login, windows);

    OpenAndRaise(ui, a);
    OpenAndRaise(ui, b);

    EXPECT_EQ(ui.Order(), (std::vector<NewUiWindow*>{&b, &a, &c}));
    EXPECT_TRUE(ui.HasTheFocus(b));
    EXPECT_FALSE(ui.HasTheFocus(a));
    // 0x65fe00: de la cola a la cabeza, la cabeza al final (arriba).
    EXPECT_EQ(ui.DrawOrder(), (std::vector<NewUiWindow*>{&a, &b}));
}

TEST(NewUiTest, RaisingAClosedWindowOnlyTakesTheFocusAway)
{
    FakeWindow a{Rect{0, 0, 10, 10}};
    FakeWindow b{Rect{0, 0, 10, 10}};
    NewUi ui;
    const std::array<NewUiWindow*, 2> windows{&a, &b};
    ui.Build(NewUiMode::Login, windows);
    OpenAndRaise(ui, a);

    ui.Raise(b);

    EXPECT_EQ(ui.Order(), (std::vector<NewUiWindow*>{&a, &b}));
    EXPECT_FALSE(ui.HasTheFocus(a));
    EXPECT_FALSE(ui.HasTheFocus(b));
}

TEST(NewUiTest, ClosingSendsItToTheTailAndFocusesTheNewHead)
{
    FakeWindow a{Rect{0, 0, 10, 10}};
    FakeWindow b{Rect{0, 0, 10, 10}};
    FakeWindow c{Rect{0, 0, 10, 10}};
    NewUi ui;
    const std::array<NewUiWindow*, 3> windows{&a, &b, &c};
    ui.Build(NewUiMode::Selection, windows);
    OpenAndRaise(ui, a);
    OpenAndRaise(ui, b);

    ui.Close(b);

    EXPECT_FALSE(b.IsOpen());
    EXPECT_EQ(ui.Order(), (std::vector<NewUiWindow*>{&a, &c, &b}));
    EXPECT_TRUE(ui.HasTheFocus(a));
}

TEST(NewUiTest, NothingIsSteppedWithoutAMode)
{
    FakeWindow a{Rect{0, 0, 10, 10}};
    a.Open();
    NewUi ui;
    InputState input;

    EXPECT_TRUE(StepAndRecord(ui, input).empty());
}

TEST(NewUiTest, TheFirstOneUnderTheMouseHasItAndOnlyTheFocusedOneThinks)
{
    FakeWindow below{Rect{0, 0, 100, 100}};
    FakeWindow above{Rect{50, 50, 100, 100}};
    NewUi ui;
    const std::array<NewUiWindow*, 2> windows{&below, &above};
    ui.Build(NewUiMode::Login, windows);
    OpenAndRaise(ui, below);
    OpenAndRaise(ui, above);
    InputState input;
    Client::Tests::MoveMouse(input, 60, 60);

    const std::vector<Turn> turns = StepAndRecord(ui, input);

    ASSERT_EQ(turns.size(), 2U);
    EXPECT_EQ(turns[0].window, &above);
    EXPECT_TRUE(turns[0].turn.mouse);
    EXPECT_TRUE(turns[0].turn.logic);
    EXPECT_EQ(turns[1].window, &below);
    EXPECT_FALSE(turns[1].turn.mouse);
    EXPECT_FALSE(turns[1].turn.logic);
    EXPECT_TRUE(ui.HasTheMouse());
}

TEST(NewUiTest, PressingOnAWindowRaisesItBeforeItsTurn)
{
    FakeWindow below{Rect{0, 0, 100, 100}};
    FakeWindow above{Rect{50, 50, 100, 100}};
    NewUi ui;
    const std::array<NewUiWindow*, 2> windows{&below, &above};
    ui.Build(NewUiMode::Login, windows);
    OpenAndRaise(ui, below);
    OpenAndRaise(ui, above);
    InputState input;
    Client::Tests::MoveMouse(input, 10, 10);
    Client::Tests::PressLeft(input);

    const std::vector<Turn> turns = StepAndRecord(ui, input);

    ASSERT_EQ(turns.size(), 2U);
    EXPECT_EQ(turns[0].window, &below);
    EXPECT_TRUE(turns[0].turn.mouse);
    EXPECT_TRUE(turns[0].turn.logic);
    EXPECT_FALSE(turns[1].turn.logic);
    EXPECT_EQ(ui.DrawOrder(), (std::vector<NewUiWindow*>{&above, &below}));
}

TEST(NewUiTest, PressingOutsideEveryWindowTakesTheFocusAway)
{
    FakeWindow window{Rect{0, 0, 100, 100}};
    NewUi ui;
    const std::array<NewUiWindow*, 1> windows{&window};
    ui.Build(NewUiMode::Login, windows);
    OpenAndRaise(ui, window);
    InputState input;
    Client::Tests::MoveMouse(input, 300, 300);
    Client::Tests::PressLeft(input);

    const std::vector<Turn> turns = StepAndRecord(ui, input);

    ASSERT_EQ(turns.size(), 1U);
    EXPECT_FALSE(turns[0].turn.mouse);
    EXPECT_FALSE(turns[0].turn.logic);
    EXPECT_FALSE(ui.HasTheFocus(window));
    EXPECT_FALSE(ui.HasTheMouse());
}

TEST(NewUiTest, AWindowThatClosesItselfLeavesTheFocusToTheNext)
{
    FakeWindow below{Rect{0, 0, 100, 100}};
    FakeWindow above{Rect{50, 50, 100, 100}};
    NewUi ui;
    const std::array<NewUiWindow*, 2> windows{&below, &above};
    ui.Build(NewUiMode::Login, windows);
    OpenAndRaise(ui, below);
    OpenAndRaise(ui, above);
    InputState input;

    ui.Step(input, Screen640,
            [&](NewUiWindow& window, NewUiTurn turn)
            {
                if (&window == &above && turn.logic)
                {
                    window.Close();
                }
            });

    EXPECT_EQ(ui.Order(), (std::vector<NewUiWindow*>{&below, &above}));
    EXPECT_TRUE(ui.HasTheFocus(below));
}

// 0x65fccb: atiende una copia de la lista hecha antes del primer turno. La que otra abre en su turno y
// estaba más abajo se atiende en el mismo paso, ya arriba y con el foco: el clic en crear de la barra de la
// selección abre la ventana de crear, que corre su lógica en ese paso.
TEST(NewUiTest, AWindowOpenedInAnotherOnesTurnIsSteppedInTheSameStep)
{
    FakeWindow bar{Rect{0, 400, 640, 30}};
    FakeWindow create{Rect{0, 0, 640, 480}};
    NewUi ui;
    const std::array<NewUiWindow*, 2> windows{&bar, &create};
    ui.Build(NewUiMode::Selection, windows);
    OpenAndRaise(ui, bar);
    InputState input;
    std::vector<Turn> turns;

    ui.Step(input, Screen640,
            [&](NewUiWindow& window, NewUiTurn turn)
            {
                turns.push_back({&window, turn});
                if (&window == &bar && turn.logic)
                {
                    OpenAndRaise(ui, create);
                }
            });

    ASSERT_EQ(turns.size(), 2U);
    EXPECT_EQ(turns[0].window, &bar);
    EXPECT_EQ(turns[1].window, &create);
    EXPECT_TRUE(turns[1].turn.logic);
    EXPECT_EQ(ui.Order(), (std::vector<NewUiWindow*>{&create, &bar}));
}

TEST(NewUiTest, EnteringTheGameEmptiesTheList)
{
    FakeWindow window{Rect{0, 0, 10, 10}};
    NewUi ui;
    const std::array<NewUiWindow*, 1> windows{&window};
    ui.Build(NewUiMode::Selection, windows);

    ui.EnterTheGame();

    EXPECT_EQ(ui.Mode(), NewUiMode::Game);
    EXPECT_TRUE(ui.Order().empty());
}

TEST(NewUiTest, TheTitleDragsAWindowWithoutItsButtons)
{
    FakeWindow window{Rect{100, 100, 200, 150}, true};
    NewUi ui;
    const std::array<NewUiWindow*, 1> windows{&window};
    ui.Build(NewUiMode::Login, windows);
    OpenAndRaise(ui, window);
    InputState input;
    Client::Tests::MoveMouse(input, 110, 105);
    Client::Tests::PressLeft(input);

    // El paso en que se aprieta atiende los botones y empieza el arrastre.
    std::vector<Turn> turns = StepAndRecord(ui, input);
    ASSERT_EQ(turns.size(), 1U);
    EXPECT_TRUE(turns[0].turn.buttons);
    EXPECT_TRUE(ui.IsDragged(window));
    input.ConsumeEdges();

    Client::Tests::MoveMouse(input, 160, 145);
    turns = StepAndRecord(ui, input);
    ASSERT_EQ(turns.size(), 1U);
    EXPECT_FALSE(turns[0].turn.buttons);
    EXPECT_EQ(window.Bounds(Screen640), (Rect{150, 140, 200, 150}));
    EXPECT_TRUE(ui.HasTheMouse());
    input.ConsumeEdges();

    Client::Tests::ReleaseLeft(input);
    turns = StepAndRecord(ui, input);
    ASSERT_EQ(turns.size(), 1U);
    EXPECT_TRUE(turns[0].turn.buttons);
    EXPECT_FALSE(ui.IsDragged(window));
}

TEST(NewUiTest, TheLoginWindowsDoNotDrag)
{
    FakeWindow window{Rect{100, 100, 200, 150}};
    NewUi ui;
    const std::array<NewUiWindow*, 1> windows{&window};
    ui.Build(NewUiMode::Login, windows);
    OpenAndRaise(ui, window);
    InputState input;
    Client::Tests::MoveMouse(input, 110, 105);
    Client::Tests::PressLeft(input);

    static_cast<void>(StepAndRecord(ui, input));

    EXPECT_FALSE(ui.IsDragged(window));
}

// Arrastra `window`, que está en (100, 100), con el título, hasta que su esquina quede en `to`.
void DragTo(NewUi& ui, InputState& input, Point to)
{
    Client::Tests::MoveMouse(input, 110, 105);
    Client::Tests::PressLeft(input);
    static_cast<void>(StepAndRecord(ui, input));
    input.ConsumeEdges();
    Client::Tests::MoveMouse(input, to.x + 10, to.y + 5);
    static_cast<void>(StepAndRecord(ui, input));
}

TEST(NewUiTest, NearTheTopOfTheScreenItSticksToIt)
{
    FakeWindow window{Rect{100, 100, 100, 100}, true};
    NewUi ui;
    const std::array<NewUiWindow*, 1> windows{&window};
    ui.Build(NewUiMode::Login, windows);
    OpenAndRaise(ui, window);
    InputState input;

    DragTo(ui, input, Point{100, 7});

    EXPECT_EQ(window.Bounds(Screen640), (Rect{100, 0, 100, 100}));
    EXPECT_TRUE(ui.IsStuck(window));
}

TEST(NewUiTest, NearACornerOfAnotherWindowItSticksToThatCorner)
{
    FakeWindow dragged{Rect{100, 100, 100, 100}, true};
    FakeWindow other{Rect{300, 300, 100, 100}};
    NewUi ui;
    const std::array<NewUiWindow*, 2> windows{&other, &dragged};
    ui.Build(NewUiMode::Login, windows);
    OpenAndRaise(ui, other);
    OpenAndRaise(ui, dragged);
    InputState input;

    // La esquina de arriba a la izquierda, cerca de la de abajo a la derecha de la otra.
    DragTo(ui, input, Point{395, 397});

    EXPECT_EQ(dragged.Bounds(Screen640), (Rect{400, 400, 100, 100}));
    EXPECT_TRUE(ui.IsStuck(dragged));
}

TEST(NewUiTest, NearTheBottomOfAnotherWindowItSticksUnderIt)
{
    FakeWindow dragged{Rect{100, 100, 100, 100}, true};
    FakeWindow other{Rect{100, 100, 200, 50}};
    NewUi ui;
    const std::array<NewUiWindow*, 2> windows{&other, &dragged};
    ui.Build(NewUiMode::Login, windows);
    OpenAndRaise(ui, other);
    OpenAndRaise(ui, dragged);
    InputState input;

    DragTo(ui, input, Point{150, 155});

    EXPECT_EQ(dragged.Bounds(Screen640), (Rect{150, 150, 100, 100}));
}

TEST(NewUiTest, AWindowWithoutTheMouseSeesNone)
{
    InputState input;
    Client::Tests::MoveMouse(input, 20, 30);

    EXPECT_EQ(Client::UI::MouseOfTheTurn(NewUiTurn{.mouse = false}, input, Screen640), std::nullopt);
    EXPECT_EQ(Client::UI::MouseOfTheTurn(NewUiTurn{}, input, Screen640), (Point{20, 30}));
}
} // namespace
