#include "Scenes/Screenshots.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Core/LocalTime.h"
#include "Data/Text/TextTable.h"
#include "Data/Textures/JpegImage.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/ActorState.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"
#include "support/FileBytes.h"
#include "support/RecordingFrameTarget.h"
#include "support/SceneDataFiles.h"
#include "support/TemporaryFolder.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// La captura de pantalla de Impr Pant (CL8 hito 6; 06 § La captura de pantalla): el flanco detrás del paso
// (0x637cff), el nombre y el mensaje antes del dibujo y la lectura, el JPEG y el contador después. Los textos y los
// nombres son inventados, y los archivos van a una carpeta temporal.
namespace
{
using Client::Core::LocalTime;
using Client::Input::Key;
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;
using Client::Scenes::IScreenReader;
using Client::Scenes::ScreenshotFileName;
using Client::Scenes::ScreenshotFrameContext;
using Client::Scenes::ScreenshotMessage;
using Client::Scenes::Screenshots;
using Client::Scenes::ScreenshotState;
using Client::Tests::FrameEvent;
using Client::Tests::RecordingFrameTarget;
using Client::Tests::TemporaryFolder;

constexpr std::size_t ScreenshotText = 459;
// El texto del grupo 0, el de los códigos 0 a 19 (UI::GroupNameText).
constexpr std::size_t FirstGroupText = 540;
constexpr std::uint16_t ServerOfTheFirstGroup = 3;
// La captura de un lienzo chico; el chat mide con la pantalla de 640 × 480, la de sus cortes.
constexpr Client::App::PixelSize SmallScreen{.width = 8, .height = 6};
constexpr Client::App::PixelSize ChatScreen{.width = 640, .height = 480};

// Tres píxeles por letra: los mensajes de los tests entran en un renglón del registro (UI::ChatWrapWidth).
class LetterMeasure final : public ITextMeasure
{
public:
    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 3, 13};
    }
};

// glReadPixels de mentira: un gris de 0x80 del tamaño que piden. Anota cada lectura, con lo que tenía el registro del
// chat y cuántos eventos tenía el frame que anota `target`, si hay uno.
class FakeScreenReader final : public IScreenReader
{
public:
    FakeScreenReader(const Client::Scenes::WorldState& world, const RecordingFrameTarget* target)
        : m_world(world), m_target(target)
    {
    }

    [[nodiscard]] std::vector<std::uint8_t> ReadTheScreen(Client::App::PixelSize screen) override
    {
        reads.push_back(Read{.screen = screen,
                             .chatEntries = m_world.Runtime().chat.log.entries.size(),
                             .events = m_target != nullptr && !m_target->frames.empty() ? m_target->frames.back().events
                                                                                        : std::vector<FrameEvent>{}});
        return std::vector<std::uint8_t>(
            static_cast<std::size_t>(screen.width) * static_cast<std::size_t>(screen.height) * 3, 0x80);
    }

    struct Read
    {
        Client::App::PixelSize screen{};
        std::size_t chatEntries = 0;
        std::vector<FrameEvent> events;
    };
    std::vector<Read> reads;

private:
    const Client::Scenes::WorldState& m_world;
    const RecordingFrameTarget* m_target;
};

// El mundo, la tabla, el reloj, la carpeta y el lienzo de una captura: el héroe "Rolo", el texto 459 "Guardada %s" y
// el grupo "Grupo Uno".
struct Shooting
{
    Shooting() : texts(Client::Tests::MakeTextTable({{ScreenshotText, "Guardada %s"}, {FirstGroupText, "Grupo Uno"}}))
    {
        Client::Scenes::ActorState& hero = world.Actors()[world.Runtime().hero];
        const std::string_view name = "Rolo";
        std::ranges::transform(name, hero.name.begin(), [](char c) { return static_cast<std::byte>(c); });
    }

    [[nodiscard]] ScreenshotFrameContext Context(bool shiftDown = false)
    {
        return ScreenshotFrameContext{.world = world,
                                      .measure = measure,
                                      .screen = ChatScreen,
                                      .shiftDown = shiftDown,
                                      .localTime = [this]() -> Client::Core::Expected<LocalTime, std::string>
                                      {
                                          ++timeReads;
                                          return time;
                                      },
                                      .texts = &texts,
                                      .chosenServer = chosenServer,
                                      .folder = folder.Path()};
    }

    // Un frame entero: antes del dibujo, el lienzo y después.
    [[nodiscard]] Client::Core::Expected<void, std::string> Frame(Screenshots& screenshots, bool shiftDown = false)
    {
        if (auto before = screenshots.BeforeTheDraw(Context(shiftDown)); !before.HasValue())
            return before;
        Client::Scenes::SceneCanvas& canvas = target.BeginFrame({});
        auto after = screenshots.AfterTheDraw(Context(shiftDown), canvas, reader);
        target.Present();
        return after;
    }

    [[nodiscard]] std::vector<std::string> Messages() const
    {
        std::vector<std::string> lines;
        for (const auto& entry : world.Runtime().chat.log.entries)
            lines.push_back(entry.text);
        return lines;
    }

    Client::Scenes::WorldState world;
    LetterMeasure measure;
    Client::Data::Text::TextTable texts;
    TemporaryFolder folder{"screenshots"};
    LocalTime time{.month = 3, .day = 7, .hour = 9, .minute = 5};
    int timeReads = 0;
    std::optional<std::uint16_t> chosenServer = ServerOfTheFirstGroup;
    RecordingFrameTarget target{SmallScreen};
    FakeScreenReader reader{world, &target};
    Client::UI::GameUiState ui;
    Client::Input::InputState input;
};

// 0x6381c7..0x638211: el mes, el día, la hora y el minuto con dos cifras, y el contador con cuatro.
TEST(ScreenshotsTest, TheNameHasTheDateTheTimeAndTheCounter)
{
    EXPECT_EQ(ScreenshotFileName(LocalTime{.month = 3, .day = 7, .hour = 9, .minute = 5}, 42),
              "Screen(03_07-09_05)-0042.jpg");
    EXPECT_EQ(ScreenshotFileName(LocalTime{.month = 12, .day = 31, .hour = 23, .minute = 59}, 9999),
              "Screen(12_31-23_59)-9999.jpg");
}

// 0x638219..0x6382f7: el 459 con el nombre en su %s, y " [grupo / héroe]" detrás, aunque estén vacíos.
TEST(ScreenshotsTest, TheMessageIsText459WithTheGroupAndTheHero)
{
    EXPECT_EQ(ScreenshotMessage("Guardada %s", "Screen(03_07-09_05)-0000.jpg", "Grupo Uno", "Rolo"),
              "Guardada Screen(03_07-09_05)-0000.jpg [Grupo Uno / Rolo]");
    EXPECT_EQ(ScreenshotMessage("", "x.jpg", "", ""), " [ / ]");
}

// 0x637cff: el flanco con su latch (0x77f36d4 + 4 · 0x2c) invierte el pedido una vez por apretón; un segundo apretón
// antes del frame lo deshace (Q-C634).
TEST(ScreenshotsTest, PrintScreenTogglesTheRequestOncePerPress)
{
    Shooting shooting;
    Screenshots screenshots;
    Client::Tests::PressKey(shooting.input, Key::PrintScreen);
    ASSERT_TRUE(screenshots.StepTheKey(shooting.ui, shooting.input).HasValue());
    EXPECT_TRUE(screenshots.State().requested);
    EXPECT_EQ(shooting.ui.keyLatches[0x2c], 1);
    shooting.input.ConsumeEdges();
    ASSERT_TRUE(screenshots.StepTheKey(shooting.ui, shooting.input).HasValue());
    EXPECT_TRUE(screenshots.State().requested) << "la tecla que sigue abajo no vuelve a contar";

    Client::Tests::ReleaseKey(shooting.input, Key::PrintScreen);
    shooting.input.ConsumeEdges();
    ASSERT_TRUE(screenshots.StepTheKey(shooting.ui, shooting.input).HasValue());
    EXPECT_EQ(shooting.ui.keyLatches[0x2c], 0);
    Client::Tests::PressKey(shooting.input, Key::PrintScreen);
    ASSERT_TRUE(screenshots.StepTheKey(shooting.ui, shooting.input).HasValue());
    EXPECT_FALSE(screenshots.State().requested);
}

// Q-C630: el apretón que llega con su soltada en el mismo frame (Impr Pant en Windows) es el flanco de ese paso, y el
// paso siguiente, ya sin él, vuelve el latch a 0.
TEST(ScreenshotsTest, APressThatCameWithItsReleaseIsAnEdge)
{
    Shooting shooting;
    Screenshots screenshots;
    Client::Tests::TapKey(shooting.input, Key::PrintScreen);
    ASSERT_FALSE(shooting.input.IsDown(Key::PrintScreen));
    ASSERT_TRUE(screenshots.StepTheKey(shooting.ui, shooting.input).HasValue());
    EXPECT_TRUE(screenshots.State().requested);
    shooting.input.ConsumeEdges();
    ASSERT_TRUE(screenshots.StepTheKey(shooting.ui, shooting.input).HasValue());
    EXPECT_TRUE(screenshots.State().requested);
    EXPECT_EQ(shooting.ui.keyLatches[0x2c], 0);
}

// Sin el pedido, el frame no lee la hora ni la pantalla, ni escribe nada.
TEST(ScreenshotsTest, WithoutTheRequestTheFrameDoesNothing)
{
    Shooting shooting;
    Screenshots screenshots;
    ASSERT_TRUE(shooting.Frame(screenshots).HasValue());
    EXPECT_EQ(shooting.timeReads, 0);
    EXPECT_TRUE(shooting.reader.reads.empty());
    EXPECT_TRUE(shooting.Messages().empty());
    EXPECT_TRUE(std::filesystem::is_empty(shooting.folder.Path()));
    EXPECT_EQ(screenshots.State().counter, 0);
}

// 0x6381bb..0x6385da sin Shift: el mensaje entra antes del dibujo (sale en la captura), y después la lectura de la
// pantalla del lienzo, el JPEG con el nombre en la carpeta, 0x7d368b0, el contador y el pedido apagado.
TEST(ScreenshotsTest, TheFrameWritesTheJpegAfterTheMessage)
{
    Shooting shooting;
    Screenshots screenshots(ScreenshotState{.requested = true, .counter = 41});
    ASSERT_TRUE(shooting.Frame(screenshots).HasValue());

    const std::string name = "Screen(03_07-09_05)-0041.jpg";
    EXPECT_EQ(shooting.Messages(), (std::vector<std::string>{"Guardada " + name + " [Grupo Uno / Rolo]"}));
    EXPECT_EQ(shooting.world.Runtime().chat.log.entries[0].type, 1);
    ASSERT_EQ(shooting.reader.reads.size(), 1u);
    EXPECT_EQ(shooting.reader.reads[0].chatEntries, 1u) << "el mensaje ya estaba";
    EXPECT_EQ(shooting.reader.reads[0].screen.width, SmallScreen.width);
    EXPECT_EQ(shooting.reader.reads[0].screen.height, SmallScreen.height);
    const auto file = Client::Tests::ReadFileBytes(shooting.folder.Path() / name);
    ASSERT_TRUE(file.has_value());
    const auto decoded = Client::Data::Textures::DecodeJpeg(std::as_bytes(std::span(*file)), 16);
    ASSERT_TRUE(decoded.HasValue());
    EXPECT_EQ(decoded.Value().width, SmallScreen.width);
    EXPECT_EQ(decoded.Value().height, SmallScreen.height);
    EXPECT_EQ(screenshots.State().counter, 42);
    EXPECT_TRUE(screenshots.State().taken);
    EXPECT_FALSE(screenshots.State().requested);
    EXPECT_EQ(shooting.timeReads, 1);
}

// 0x638303 y 0x6385af..0x6385d7: con Shift, el mensaje va después de la lectura y no sale en la captura.
TEST(ScreenshotsTest, WithShiftTheMessageGoesAfterTheRead)
{
    Shooting shooting;
    Screenshots screenshots(ScreenshotState{.requested = true});
    ASSERT_TRUE(shooting.Frame(screenshots, true).HasValue());
    ASSERT_EQ(shooting.reader.reads.size(), 1u);
    EXPECT_EQ(shooting.reader.reads[0].chatEntries, 0u);
    EXPECT_EQ(shooting.Messages().size(), 1u);
}

// 0x6385aa → 0x6202e0: lo que juntó el lienzo llega a GL antes de leer, el 3D antes del 2D, como en el Present.
TEST(ScreenshotsTest, TheCanvasIsFlushedBeforeTheRead)
{
    Shooting shooting;
    Screenshots screenshots(ScreenshotState{.requested = true});
    ASSERT_TRUE(shooting.Frame(screenshots).HasValue());
    ASSERT_EQ(shooting.reader.reads.size(), 1u);
    const std::vector<FrameEvent>& events = shooting.reader.reads[0].events;
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].kind, FrameEvent::Kind::Models);
    EXPECT_EQ(events[1].kind, FrameEvent::Kind::Flush);
}

// 0x62034b: el contador da la vuelta en 10.000.
TEST(ScreenshotsTest, TheCounterWrapsAtTenThousand)
{
    Shooting shooting;
    Screenshots screenshots(ScreenshotState{.requested = true, .counter = 9999});
    ASSERT_TRUE(shooting.Frame(screenshots).HasValue());
    EXPECT_TRUE(std::filesystem::exists(shooting.folder.Path() / "Screen(03_07-09_05)-9999.jpg"));
    EXPECT_EQ(screenshots.State().counter, 0);
}

// Q-C633: si el archivo no abre, 0x6202e0 no lo mira: el contador sube y el mensaje dice el nombre igual.
TEST(ScreenshotsTest, AFileThatDoesNotOpenStillCountsAndSaysSo)
{
    Shooting shooting;
    Screenshots screenshots(ScreenshotState{.requested = true});
    ScreenshotFrameContext context = shooting.Context();
    context.folder = shooting.folder.Path() / "no-existe";
    ASSERT_TRUE(screenshots.BeforeTheDraw(context).HasValue());
    Client::Scenes::SceneCanvas& canvas = shooting.target.BeginFrame({});
    ASSERT_TRUE(screenshots.AfterTheDraw(context, canvas, shooting.reader).HasValue());
    shooting.target.Present();
    EXPECT_EQ(screenshots.State().counter, 1);
    EXPECT_FALSE(screenshots.State().requested);
    EXPECT_EQ(shooting.Messages().size(), 1u);
}

// Q-C632: sin servidor elegido, el grupo vacío; sin Text.bmd, el 459 también.
TEST(ScreenshotsTest, WithoutAChosenServerTheGroupIsEmpty)
{
    Shooting shooting;
    shooting.chosenServer.reset();
    Screenshots screenshots(ScreenshotState{.requested = true});
    ASSERT_TRUE(shooting.Frame(screenshots).HasValue());
    EXPECT_EQ(shooting.Messages(), (std::vector<std::string>{"Guardada Screen(03_07-09_05)-0000.jpg [ / Rolo]"}));
}

// P-C110: un frame que la escena retiene con la captura pedida corta en el paso o el dibujo que siguen; mientras
// conecta, el pedido espera al próximo dibujo.
TEST(ScreenshotsTest, AHeldFrameWithTheRequestCuts)
{
    {
        Shooting shooting;
        Screenshots screenshots(ScreenshotState{.requested = true});
        screenshots.FinishTheLoop(true);
        ASSERT_TRUE(shooting.Frame(screenshots).HasValue());
        EXPECT_EQ(shooting.reader.reads.size(), 1u);
        screenshots.FinishTheLoop(false);
        EXPECT_TRUE(screenshots.StepTheKey(shooting.ui, shooting.input).HasValue()) << "un frame dibujado no corta";
    }
    Shooting shooting;
    Screenshots screenshots(ScreenshotState{.requested = true});
    screenshots.FinishTheLoop(false);
    const auto step = screenshots.StepTheKey(shooting.ui, shooting.input);
    ASSERT_FALSE(step.HasValue());
    EXPECT_NE(step.Error().find("0x6385aa"), std::string::npos) << step.Error();
    const auto before = screenshots.BeforeTheDraw(shooting.Context());
    ASSERT_FALSE(before.HasValue());
    EXPECT_TRUE(shooting.reader.reads.empty());
    EXPECT_TRUE(shooting.Messages().empty());
}

// Sin pedido, un frame retenido no corta nada.
TEST(ScreenshotsTest, AHeldFrameWithoutTheRequestGoesOn)
{
    Shooting shooting;
    Screenshots screenshots;
    screenshots.FinishTheLoop(false);
    EXPECT_TRUE(screenshots.StepTheKey(shooting.ui, shooting.input).HasValue());
    EXPECT_TRUE(screenshots.BeforeTheDraw(shooting.Context()).HasValue());
}
} // namespace
