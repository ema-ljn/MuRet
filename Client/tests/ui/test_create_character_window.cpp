#include "UI/CreateCharacterWindow.h"

#include "App/WindowState.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/SpriteBatch.h"
#include "Render/TextureSlot.h"
#include "UI/Widget.h"

#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/UiInput.h"
#include "support/UiPrinters.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::UI::CreateCharacterWindow: la ventana de crear de la selección (+0x8498: 0x63d1a0,
// 0x63d4e0, 0x63d7c0, 0x63d880 y 0x63e0a0), sin la vista previa. Los nombres son sintéticos.
namespace
{
using Client::App::PixelSize;
using Client::Input::InputState;
using Client::Input::Key;
using Client::Tests::ClickAt;
using Client::Tests::TapKey;
using Client::Tests::TypeText;
using Client::UI::CreateCharacterWindow;
using Client::UI::CreateWindowEvent;
using Client::UI::CreateWindowLayout;
using Client::UI::LayoutCreateWindow;
using Client::UI::Point;
using Client::UI::Rect;

constexpr PixelSize Screen640{640, 480};

Client::UI::TextureSizes LabSizes()
{
    return Client::Tests::FakeTextureSizes(
        {{0xc, {512, 64}}, {0xd, {128, 128}}, {0x724, {64, 128}}, {0x725, {64, 128}}});
}

std::optional<CreateWindowEvent> StepOnce(CreateCharacterWindow& window, InputState& input)
{
    std::optional<CreateWindowEvent> event = window.Step(input, Screen640);
    input.ConsumeEdges();
    return event;
}

void ClickIn(CreateCharacterWindow& window, InputState& input, const Rect& area)
{
    ClickAt(input, area.x + 5, area.y + 5);
    static_cast<void>(StepOnce(window, input));
}

// 0x63d4e0 con ((W − 454) / 2, (H − 406) / 2): en 640 × 480, (93, 37), la tabla emulada del contrato.
TEST(CreateCharacterWindowTest, TheLayoutFollowsTheScreen)
{
    const CreateWindowLayout layout = LayoutCreateWindow(Screen640, 1);
    EXPECT_EQ(layout.box, (Rect{.x = 93, .y = 37, .width = 454, .height = 406}));
    EXPECT_EQ(layout.nameBack, (Rect{.x = 93, .y = 354, .width = 346, .height = 38}));
    EXPECT_EQ(layout.attributes, (Rect{.x = 439, .y = 87, .width = 108, .height = 80}));
    EXPECT_EQ(layout.strip, (Rect{.x = 93, .y = 392, .width = 454, .height = 51}));
    const std::vector<int> classTops{194, 220, 246, 283, 309};
    for (std::size_t i = 0; i < classTops.size(); ++i)
    {
        EXPECT_EQ(layout.classes[i], (Rect{.x = 439, .y = classTops[i], .width = 108, .height = 26})) << i;
    }
    EXPECT_EQ(layout.ok, (Rect{.x = 439, .y = 362, .width = 54, .height = 30}));
    EXPECT_EQ(layout.cancel, (Rect{.x = 493, .y = 362, .width = 54, .height = 30}));
    EXPECT_EQ(layout.nameField, (Point{171, 375}));
    // La caja de los atributos crece con el Dark Lord (0x647370(0, 96, 2)).
    EXPECT_EQ(LayoutCreateWindow(Screen640, 4).attributes.height, 96);
    const CreateWindowLayout at1024 = LayoutCreateWindow(PixelSize{1024, 768}, 0);
    EXPECT_EQ(at1024.box.x, 285);
    EXPECT_EQ(at1024.box.y, 181);
    // El campo en 640 × 480: ftol((285 + 78) / 1,6) = 226 y ftol((181 + 338) / 1,6) = 324.
    EXPECT_EQ(at1024.nameField, (Point{226, 324}));
}

// 0x63d7c0: pkt[4] es la clase más alta que se puede crear: habilita de la 0 a ella. MuRet la acota
// a 4 (el original escribe fuera de los cinco).
TEST(CreateCharacterWindowTest, TheClassesUpToPkt4AreEnabled)
{
    CreateCharacterWindow window(LabSizes(), nullptr);
    for (const auto& [creatable, enabled] :
         {std::pair{std::uint8_t{0}, std::size_t{1}}, std::pair{std::uint8_t{2}, std::size_t{3}},
          std::pair{std::uint8_t{4}, std::size_t{5}}, std::pair{std::uint8_t{7}, std::size_t{5}}})
    {
        window.EnableClasses(creatable);
        for (std::size_t i = 0; i < 5; ++i)
        {
            EXPECT_EQ(window.ClassButton(i).Enabled(), i < enabled) << static_cast<int>(creatable) << " " << i;
        }
    }
}

// El armado marca al Dark Knight (+0x890 en 1). Con pkt[4] en 0 queda marcado y deshabilitado, y el
// OK lo crea igual: 0x63d990 lee la clase, no el botón.
TEST(CreateCharacterWindowTest, TheKnightStartsChecked)
{
    CreateCharacterWindow window(LabSizes(), nullptr);
    window.EnableClasses(0);
    window.Open();
    EXPECT_EQ(window.ChosenClass(), 1);
    EXPECT_TRUE(window.ClassButton(1).Checked());
    EXPECT_FALSE(window.ClassButton(1).Enabled());
    InputState input;
    TypeText(input, "char0001");
    static_cast<void>(StepOnce(window, input));
    TapKey(input, Key::Enter);
    EXPECT_EQ(StepOnce(window, input), std::optional{CreateWindowEvent::Submitted});
    EXPECT_EQ(window.ChosenClass(), 1);
}

// 0x63d880: el clic en una clase habilitada la marca sola y la elige; en una deshabilitada, nada.
// La clase no vuelve a la 1 al cerrar y abrir.
TEST(CreateCharacterWindowTest, AClassClickChoosesIt)
{
    CreateCharacterWindow window(LabSizes(), nullptr);
    window.EnableClasses(2);
    window.Open();
    InputState input;
    const CreateWindowLayout layout = LayoutCreateWindow(Screen640, 1);

    ClickIn(window, input, layout.classes[2]);
    EXPECT_EQ(window.ChosenClass(), 2);
    for (std::size_t i = 0; i < 5; ++i)
    {
        EXPECT_EQ(window.ClassButton(i).Checked(), i == 2) << i;
    }
    ClickIn(window, input, layout.classes[4]);
    EXPECT_EQ(window.ChosenClass(), 2);

    window.Close();
    window.Open();
    EXPECT_EQ(window.ChosenClass(), 2);
}

// 0x63d670: el campo se vacía al abrir y al cerrar, y toma hasta 10 bytes (EM_LIMITTEXT(10)).
TEST(CreateCharacterWindowTest, TheNameIsClearedOnOpenAndClose)
{
    CreateCharacterWindow window(LabSizes(), nullptr);
    window.Open();
    InputState input;
    TypeText(input, "abcdefghijkl");
    static_cast<void>(StepOnce(window, input));
    EXPECT_EQ(window.Name(), "abcdefghij");
    TypeText(input, "\b");
    static_cast<void>(StepOnce(window, input));
    EXPECT_EQ(window.Name(), "abcdefghi");

    window.Open();
    EXPECT_EQ(window.Name(), "");
    TypeText(input, "abc");
    static_cast<void>(StepOnce(window, input));
    window.Close();
    EXPECT_EQ(window.Name(), "");
    EXPECT_FALSE(window.IsOpen());
}

// El OK o Enter piden crear (la ventana queda abierta: la cierra quien manda); cancelar o Esc la
// cierran. Cerrada, no atiende nada.
TEST(CreateCharacterWindowTest, EnterSubmitsAndEscCloses)
{
    const CreateWindowLayout layout = LayoutCreateWindow(Screen640, 1);
    CreateCharacterWindow window(LabSizes(), nullptr);
    window.Open();
    InputState input;

    ClickAt(input, layout.ok.x + 5, layout.ok.y + 5);
    EXPECT_EQ(StepOnce(window, input), std::optional{CreateWindowEvent::Submitted});
    EXPECT_TRUE(window.IsOpen());
    TapKey(input, Key::Enter);
    EXPECT_EQ(StepOnce(window, input), std::optional{CreateWindowEvent::Submitted});
    TapKey(input, Key::Escape);
    EXPECT_EQ(StepOnce(window, input), std::optional{CreateWindowEvent::Cancelled});
    EXPECT_FALSE(window.IsOpen());
    TapKey(input, Key::Enter);
    EXPECT_EQ(StepOnce(window, input), std::nullopt);

    window.Open();
    ClickAt(input, layout.cancel.x + 5, layout.cancel.y + 5);
    EXPECT_EQ(StepOnce(window, input), std::optional{CreateWindowEvent::Cancelled});
    EXPECT_FALSE(window.IsOpen());
}

// Con un aviso arriba (el del nombre, o el de la respuesta), la ventana no tiene el mouse: sus
// botones quedan sin encima y sin clic (0x65fc30 y 0x63c490).
TEST(CreateCharacterWindowTest, UnderANoticeTheButtonsLetGoOfTheMouse)
{
    const CreateWindowLayout layout = LayoutCreateWindow(Screen640, 1);
    CreateCharacterWindow window(LabSizes(), nullptr);
    window.Open();
    InputState input;
    Client::Tests::MoveMouse(input, layout.ok.x + 5, layout.ok.y + 5);
    static_cast<void>(StepOnce(window, input));
    ASSERT_EQ(window.OkButton().Frame(), 1);

    window.StepUnderANotice(input);

    EXPECT_EQ(window.OkButton().Frame(), 0);
}

// La textura de cada quad del lote, en orden: 0xffff sin textura.
std::vector<std::uint16_t> QuadSlots(const Client::Render::SpriteBatch& batch)
{
    std::vector<std::uint16_t> slots;
    for (const auto& command : batch.Commands())
    {
        const std::uint16_t slot = command.texture ? command.texture->value : std::uint16_t{0xffff};
        slots.insert(slots.end(), command.vertexCount / 6, slot);
    }
    return slots;
}

// 0x66a050 y 0x63e0a0 sin la vista previa: la base negra de toda la pantalla, el fondo del nombre,
// la caja de los atributos y la franja, los siete botones con los rótulos 20 a 24 (las clases), los
// atributos en 0xff00b4ff con sus rótulos, la descripción y el nombre con el cursor.
TEST(CreateCharacterWindowTest, TheDrawGoesBaseBacksButtonsAndTexts)
{
    const auto texts = Client::Tests::MakeTextTable({{20, "Wiz"},
                                                     {21, "Kni"},
                                                     {22, "Elf"},
                                                     {23, "Mag"},
                                                     {24, "Lor"},
                                                     {1701, "Str"},
                                                     {1702, "Agi"},
                                                     {1703, "Vit"},
                                                     {1704, "Ene"},
                                                     {1706, "Knight line"}});
    CreateCharacterWindow window(LabSizes(), &texts);
    window.Open();
    InputState input;
    TypeText(input, "char0001");
    static_cast<void>(StepOnce(window, input));
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);

    window.DrawBase(batch, Screen640);
    window.Draw(batch, text, Screen640);

    const std::uint16_t marker = Client::Tests::TextMarkerSlot.value;
    const std::vector<std::uint16_t> slots = QuadSlots(batch);
    ASSERT_GE(slots.size(), 4u + 7u);
    EXPECT_EQ((std::vector<std::uint16_t>(slots.begin(), slots.begin() + 4)),
              (std::vector<std::uint16_t>{0xffff, 0xc, 0xffff, 0xffff}));
    // Cada botón, con su rótulo detrás si lo tiene.
    EXPECT_EQ(
        (std::vector<std::uint16_t>(slots.begin() + 4, slots.begin() + 16)),
        (std::vector<std::uint16_t>{0xd, marker, 0xd, marker, 0xd, marker, 0xd, marker, 0xd, marker, 0x724, 0x725}));
    std::vector<std::string> drawn;
    for (const auto& recorded : text.texts)
    {
        drawn.push_back(recorded.text);
    }
    EXPECT_EQ(drawn, (std::vector<std::string>{"Wiz", "Kni", "Elf", "Mag", "Lor", "28", "Str", "20", "Agi", "25", "Vit",
                                               "10", "Ene", "Knight line", "char0001", "_"}));
    // Los valores en (ax + 76, ay + 10 + 17 i) de 640 × 480, en 0xff00b4ff; los rótulos en ax + 22.
    EXPECT_EQ(text.texts[5].x, 439 + 76);
    EXPECT_EQ(text.texts[5].y, 87 + 10);
    EXPECT_EQ(text.texts[5].style.colors.text, 0xff00b4ffu);
    EXPECT_EQ(text.texts[6].x, 439 + 22);
    EXPECT_EQ(text.texts[6].style.colors.text, 0xffffffffu);
    EXPECT_EQ(text.texts[11].y, 87 + 10 + 17 * 3);
    // La descripción en (x + 10, y + 355 + 12), y el nombre en el campo.
    EXPECT_EQ(text.texts[13].x, 93 + 10);
    EXPECT_EQ(text.texts[13].y, 37 + 355 + 12);
    EXPECT_EQ(text.texts[14].x, 171);
    EXPECT_EQ(text.texts[14].y, 375);
    // La clase elegida (la 1) sale marcada: el cuadro 3, la fila 78 de cha_bt.
    EXPECT_EQ(window.ClassButton(1).Frame(), 3);
}

// Con el Dark Lord, el quinto atributo: "25" y el texto 1738, en ay + 78; y la descripción partida
// en dos renglones de menos de 75 bytes (SplitIntoLines), el segundo 19 más abajo.
TEST(CreateCharacterWindowTest, TheDarkLordHasCommandAndTwoLines)
{
    const std::string longLine(60, 'a');
    const auto texts = Client::Tests::MakeTextTable({{1738, "Com"}, {1709, longLine + " " + std::string(30, 'b')}});
    CreateCharacterWindow window(LabSizes(), &texts);
    window.EnableClasses(4);
    window.Open();
    InputState input;
    ClickIn(window, input, LayoutCreateWindow(Screen640, 4).classes[4]);
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);

    window.Draw(batch, text, Screen640);

    std::vector<std::string> drawn;
    for (const auto& recorded : text.texts)
    {
        drawn.push_back(recorded.text);
    }
    // 0x632c90 corta por bytes, en menos de 75: 74 en el primero y el resto en el segundo.
    EXPECT_EQ(drawn, (std::vector<std::string>{"26", "20", "20", "15", "25", "Com",
                                               longLine + " " + std::string(13, 'b'), std::string(17, 'b'), "_"}));
    EXPECT_EQ(text.texts[4].y, 87 + 78);
    EXPECT_EQ(text.texts[7].y, 37 + 355 + 12 + 19);
}

// El nombre toma las letras de Latin-1 (el EDIT no filtra): "Peña" es "Pe\xf1a".
TEST(CreateCharacterWindowTest, TheNameTakesLatin1Letters)
{
    CreateCharacterWindow window(LabSizes(), nullptr);
    window.Open();
    InputState input;
    TypeText(input, "Pe\xc3\xb1"
                    "a");
    static_cast<void>(StepOnce(window, input));
    EXPECT_EQ(window.Name(), "Pe\xf1"
                             "a");
}

// El buffer de 0x7798bf8: el OK copia el campo con su cero (GetWindowTextA) y lo de detrás queda; se
// vacía solo al abrir (0x553570).
TEST(CreateCharacterWindowTest, TheBufferKeepsWhatWasBehindTheName)
{
    CreateCharacterWindow window(LabSizes(), nullptr);
    window.Open();
    InputState input;
    TypeText(input, "abcdef g");
    static_cast<void>(StepOnce(window, input));
    TapKey(input, Key::Enter);
    ASSERT_EQ(StepOnce(window, input), std::optional{CreateWindowEvent::Submitted});
    TypeText(input, "\b\b\b");
    static_cast<void>(StepOnce(window, input));
    TapKey(input, Key::Enter);
    ASSERT_EQ(StepOnce(window, input), std::optional{CreateWindowEvent::Submitted});

    EXPECT_EQ(window.Buffer().substr(0, 9), std::string_view("abcde\0 g\0", 9));
    window.Open();
    EXPECT_EQ(window.Buffer().substr(0, 9), std::string_view("\0\0\0\0\0\0\0\0\0", 9));
}

// Con un aviso arriba, el EDIT sigue con el foco de Win32: lo que se escribe entra al nombre, sin
// clic en los botones.
TEST(CreateCharacterWindowTest, UnderANoticeTheNameStillTakesTheTyping)
{
    CreateCharacterWindow window(LabSizes(), nullptr);
    window.Open();
    InputState input;
    TypeText(input, "abc");
    window.StepUnderANotice(input);
    input.ConsumeEdges();
    TypeText(input, "\bx");
    window.StepUnderANotice(input);
    EXPECT_EQ(window.Name(), "abx");
}
} // namespace
