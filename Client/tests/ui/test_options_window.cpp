#include "UI/OptionsWindow.h"

#include "App/WindowState.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/SpriteBatch.h"
#include "UI/NewUi.h"
#include "UI/Widget.h"

#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <optional>

// Client::UI::OptionsWindow: las opciones del login y de la selección (+0x7e8: 0x644120, 0x644520,
// 0x644640, 0x644730 y 0x644860; CL8, hito 8). Todo en píxeles.
namespace
{
using Client::App::PixelSize;
using Client::Input::InputState;
using Client::Input::Key;
using Client::UI::LayoutOptionsWindow;
using Client::UI::NewUiTurn;
using Client::UI::OptionsEvent;
using Client::UI::OptionsSettings;
using Client::UI::OptionsWindow;
using Client::UI::OptionsWindowLayout;
using Client::UI::Point;
using Client::UI::Rect;

constexpr PixelSize Screen640{640, 480};
// Un paso de la lógica: 40 ms (App::LogicStep).
constexpr double StepMs = 40.0;

// Los buffers del laboratorio: op1_back3 de 5 × 8 sube en 8 × 8.
Client::UI::TextureSizes LabSizes()
{
    return Client::Tests::FakeTextureSizes({{0x72a, {128, 128}},
                                            {0x72c, {256, 64}},
                                            {0x72d, {8, 8}},
                                            {0x72e, {8, 8}},
                                            {0x72f, {128, 128}},
                                            {0x730, {256, 128}},
                                            {0x731, {16, 32}}});
}

OptionsWindow OpenWindow(const OptionsSettings& settings)
{
    OptionsWindow window(LabSizes(), nullptr);
    window.Arm(settings);
    window.Open();
    // Un paso sin nada la ubica (0x644520).
    const InputState nothing;
    static_cast<void>(window.Step(nothing, Screen640, 0.0, settings));
    return window;
}

std::optional<OptionsEvent> StepOnce(OptionsWindow& window, InputState& input, const OptionsSettings& current)
{
    std::optional<OptionsEvent> event = window.Step(input, Screen640, StepMs, current);
    input.ConsumeEdges();
    return event;
}

std::optional<OptionsEvent> ClickOn(OptionsWindow& window, InputState& input, Rect area, const OptionsSettings& current)
{
    Client::Tests::ClickAt(input, area.x + 5, area.y + 5);
    return StepOnce(window, input, current);
}

// 0x644520 con 30 líneas de 8: el marco de 213 × 348 al centro; las casillas a 52 del borde, una cada 41; el
// volumen 48 abajo de la última más su alto, el brillo 61 más abajo, y el botón a 301 de arriba.
TEST(OptionsWindowTest, TheFrameHasThirtyLines)
{
    const OptionsWindowLayout layout = LayoutOptionsWindow(Screen640, 8);

    EXPECT_EQ(layout.frame, (Rect{213, 66, 213, 348}));
    EXPECT_EQ(layout.checks[0], (Rect{265, 118, 16, 16}));
    EXPECT_EQ(layout.checks[1], (Rect{265, 159, 16, 16}));
    EXPECT_EQ(layout.checks[2], (Rect{265, 200, 16, 16}));
    EXPECT_EQ(layout.sliders[0], (Point{265, 264}));
    EXPECT_EQ(layout.sliders[1], (Point{265, 325}));
    EXPECT_EQ(layout.close, (Rect{265, 367, 108, 30}));
}

// 0x6446e0: las casillas y los deslizadores con los globales; el volumen hasta 9 y el brillo hasta 4.
TEST(OptionsWindowTest, ArmingReadsTheGlobals)
{
    const OptionsWindow window = OpenWindow(
        OptionsSettings{.autoAttack = false, .noticeSound = true, .slidesOn = true, .volume = 7, .glowDetail = 2});

    EXPECT_FALSE(window.Check(0).Checked());
    EXPECT_TRUE(window.Check(1).Checked());
    EXPECT_TRUE(window.Check(2).Checked());
    EXPECT_EQ(window.SliderAt(0).Value(), 7);
    EXPECT_EQ(window.SliderAt(0).Max(), 9);
    EXPECT_EQ(window.SliderAt(1).Value(), 2);
    EXPECT_EQ(window.SliderAt(1).Max(), 4);
    EXPECT_TRUE(window.SliderAt(0).IsOpen());
}

// 0x644730: cada casilla que cambia escribe su global.
TEST(OptionsWindowTest, EachCheckWritesItsGlobal)
{
    OptionsSettings current;
    OptionsWindow window = OpenWindow(current);
    InputState input;

    const std::optional<OptionsEvent> attack = ClickOn(window, input, window.Check(0).Area(), current);
    EXPECT_EQ(attack, (OptionsEvent{.kind = OptionsEvent::Kind::AutoAttack, .value = 0}));
    Client::UI::ApplyOption(current, *attack);
    EXPECT_FALSE(current.autoAttack);

    EXPECT_EQ(ClickOn(window, input, window.Check(1).Area(), current),
              (OptionsEvent{.kind = OptionsEvent::Kind::NoticeSound, .value = 1}));
    EXPECT_EQ(ClickOn(window, input, window.Check(2).Area(), current),
              (OptionsEvent{.kind = OptionsEvent::Kind::SlidesOn, .value = 0}));
    EXPECT_TRUE(window.IsOpen());
}

// La casilla termina el paso: un Esc del mismo paso queda para la escena (Q-C502).
TEST(OptionsWindowTest, ACheckEndsTheStepBeforeEscape)
{
    const OptionsSettings current;
    OptionsWindow window = OpenWindow(current);
    InputState input;
    Client::Tests::PressKey(input, Key::Escape);

    EXPECT_EQ(ClickOn(window, input, window.Check(0).Area(), current),
              (OptionsEvent{.kind = OptionsEvent::Kind::AutoAttack, .value = 0}));
    EXPECT_TRUE(window.IsOpen());
}

// El botón cierra sin sonido; Esc, con el 0x19 (de la CL10), y se lleva el apretón.
TEST(OptionsWindowTest, TheButtonAndEscapeClose)
{
    const OptionsSettings current;
    OptionsWindow button = OpenWindow(current);
    InputState input;
    EXPECT_EQ(ClickOn(button, input, button.CloseButton().Area(), current),
              (OptionsEvent{.kind = OptionsEvent::Kind::Closed}));
    EXPECT_FALSE(button.IsOpen());

    OptionsWindow escape = OpenWindow(current);
    Client::Tests::PressKey(input, Key::Escape);
    EXPECT_EQ(escape.Step(input, Screen640, StepMs, current), (OptionsEvent{.kind = OptionsEvent::Kind::Closed}));
    EXPECT_FALSE(escape.IsOpen());
    EXPECT_FALSE(input.TakePress(Key::Escape));
}

// Sin el foco, 0x644730 no corre: ni Esc ni los deslizadores.
TEST(OptionsWindowTest, WithoutTheFocusNothingHappens)
{
    const OptionsSettings current;
    OptionsWindow window = OpenWindow(current);
    InputState input;
    Client::Tests::PressKey(input, Key::Escape);

    EXPECT_EQ(window.Step(input, Screen640, StepMs, current, NewUiTurn{.mouse = false, .logic = false}), std::nullopt);
    EXPECT_TRUE(window.IsOpen());
    EXPECT_TRUE(input.TakePress(Key::Escape));
}

// 0x6447d9: el deslizador que se mueve escribe su valor si es otro que el global, y termina el paso igual si
// es el mismo. Apretar a la derecha de la perilla del volumen (5, en 312) lo sube a 6.
TEST(OptionsWindowTest, TheVolumeSliderWritesANewValue)
{
    OptionsSettings current;
    OptionsWindow window = OpenWindow(current);
    InputState input;
    Client::Tests::MoveMouse(input, 350, 268);
    Client::Tests::PressLeft(input);

    const std::optional<OptionsEvent> volume = StepOnce(window, input, current);
    EXPECT_EQ(volume, (OptionsEvent{.kind = OptionsEvent::Kind::Volume, .value = 6}));
    Client::UI::ApplyOption(current, *volume);
    EXPECT_EQ(current.volume, 6);

    // Todavía apretado: el mismo valor no escribe nada, y Esc no se lee.
    Client::Tests::PressKey(input, Key::Escape);
    EXPECT_EQ(StepOnce(window, input, current), std::nullopt);
    EXPECT_TRUE(window.IsOpen());
}

// 0x64480a: el brillo, con la perilla en 4 (al final, 350): apretar a su izquierda lo baja a 3.
TEST(OptionsWindowTest, TheGlowSliderWritesItsByte)
{
    OptionsSettings current;
    OptionsWindow window = OpenWindow(current);
    InputState input;
    Client::Tests::MoveMouse(input, 300, 330);
    Client::Tests::PressLeft(input);

    const std::optional<OptionsEvent> glow = StepOnce(window, input, current);
    EXPECT_EQ(glow, (OptionsEvent{.kind = OptionsEvent::Kind::GlowDetail, .value = 3}));
    Client::UI::ApplyOption(current, *glow);
    EXPECT_EQ(current.glowDetail, 3);
}

// 0x644860: el título centrado en el marco a 10 de arriba, los rótulos de las casillas 24 a la derecha y 4
// abajo, los de los deslizadores 18 arriba con el valor de los globales 85 a la derecha (el brillo, 2 · d +
// 5) y, al final, el rótulo del botón.
TEST(OptionsWindowTest, TheTextsShowTheGlobals)
{
    const auto texts = Client::Tests::MakeTextTable({{385, "Titulo"},
                                                     {386, "Uno"},
                                                     {387, "Dos"},
                                                     {919, "Tres"},
                                                     {389, "Volumen"},
                                                     {1840, "Brillo"},
                                                     {388, "Cerrar"}});
    const OptionsSettings armed;
    OptionsWindow window(LabSizes(), &texts);
    window.Arm(armed);
    window.Open();
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);

    window.Draw(batch, text, Screen640, OptionsSettings{.volume = 7, .glowDetail = 2});

    ASSERT_EQ(text.texts.size(), 9U);
    EXPECT_EQ(text.texts[0].text, "Titulo");
    EXPECT_EQ(text.texts[0].x, 301);
    EXPECT_EQ(text.texts[0].y, 76);
    EXPECT_EQ(text.texts[1].text, "Uno");
    EXPECT_EQ(text.texts[1].x, 289);
    EXPECT_EQ(text.texts[1].y, 122);
    EXPECT_EQ(text.texts[3].text, "Tres");
    EXPECT_EQ(text.texts[3].y, 204);
    EXPECT_EQ(text.texts[4].text, "Volumen");
    EXPECT_EQ(text.texts[4].x, 265);
    EXPECT_EQ(text.texts[4].y, 246);
    EXPECT_EQ(text.texts[5].text, "7");
    EXPECT_EQ(text.texts[5].x, 350);
    EXPECT_EQ(text.texts[6].text, "Brillo");
    EXPECT_EQ(text.texts[6].y, 307);
    EXPECT_EQ(text.texts[7].text, "9");
    EXPECT_EQ(text.texts[8].text, "Cerrar");
}
} // namespace
