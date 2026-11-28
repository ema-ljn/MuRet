#include "Scenes/Screenshots.h"

#include "Core/Expected.h"
#include "Core/LocalTime.h"
#include "Core/PixelSize.h"
#include "Tables/TextTable.h"
#include "Data/Textures/JpegImage.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/Frame/IScreenReader.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Kernel/ActorState.h"
#include "Render/Frame/SceneCanvas.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameUiState.h"
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
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The Print Screen screenshot (CL8 milestone 6; 06 § La captura de pantalla): the edge after the step (0x637cff), the
// name and the message before the draw and the read, the JPEG and the counter after. The texts and names are made up,
// and the files go to a temporary folder.
namespace
{
using Client::Core::LocalTime;
using Client::Input::Key;
using Client::Render::Frame::IScreenReader;
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;
using Client::Scenes::ScreenshotFileName;
using Client::Scenes::ScreenshotFrameContext;
using Client::Scenes::ScreenshotMessage;
using Client::Scenes::Screenshots;
using Client::Scenes::ScreenshotState;
using Client::Tests::FrameEvent;
using Client::Tests::RecordingFrameTarget;
using Client::Tests::TemporaryFolder;

constexpr std::size_t ScreenshotText = 459;
// Group 0's text, that of codes 0 to 19 (FrontEnd::GroupNameText).
constexpr std::size_t FirstGroupText = 540;
constexpr std::uint16_t ServerOfTheFirstGroup = 3;
// The capture of a small canvas; the chat measures with the 640 × 480 screen, the one its wraps use.
constexpr Client::Core::PixelSize SmallScreen{.width = 8, .height = 6};
constexpr Client::Core::PixelSize ChatScreen{.width = 640, .height = 480};

// Three pixels per letter: the tests' messages fit in one chat log line (Chat::ChatWrapWidth).
class LetterMeasure final : public ITextMeasure
{
public:
    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 3, 13};
    }
};

// Fake glReadPixels: a 0x80 grey of the requested size. It records each read, with what the chat log had and how many
// events the frame `target` records had, if there is one.
class FakeScreenReader final : public IScreenReader
{
public:
    FakeScreenReader(const Client::Kernel::WorldState& world, const RecordingFrameTarget* target)
        : m_world(world), m_target(target)
    {
    }

    [[nodiscard]] std::vector<std::uint8_t> ReadTheScreen(Client::Core::PixelSize screen) override
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
        Client::Core::PixelSize screen{};
        std::size_t chatEntries = 0;
        std::vector<FrameEvent> events;
    };
    std::vector<Read> reads;

private:
    const Client::Kernel::WorldState& m_world;
    const RecordingFrameTarget* m_target;
};

// The world, the table, the clock, the folder and the canvas of a screenshot: the hero "Rolo", text 459 "Guardada %s"
// and the group "Grupo Uno".
struct Shooting
{
private:
    // The case with two screenshots must not reserve two WorldState on the Windows stack.
    std::unique_ptr<Client::Kernel::WorldState> m_world = std::make_unique<Client::Kernel::WorldState>();

public:
    Shooting() : texts(Client::Tests::MakeTextTable({{ScreenshotText, "Guardada %s"}, {FirstGroupText, "Grupo Uno"}}))
    {
        Client::Kernel::ActorState& hero = world.Hero();
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

    // A whole frame: before the draw, the canvas and after.
    [[nodiscard]] Client::Core::Expected<void, std::string> Frame(Screenshots& screenshots, bool shiftDown = false)
    {
        if (auto before = screenshots.BeforeTheDraw(Context(shiftDown)); !before.HasValue())
            return before;
        Client::Render::Frame::SceneCanvas& canvas = target.BeginFrame({});
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

    Client::Kernel::WorldState& world = *m_world;
    LetterMeasure measure;
    Client::Tables::TextTable texts;
    TemporaryFolder folder{"screenshots"};
    LocalTime time{.month = 3, .day = 7, .hour = 9, .minute = 5};
    int timeReads = 0;
    std::optional<std::uint16_t> chosenServer = ServerOfTheFirstGroup;
    RecordingFrameTarget target{SmallScreen};
    FakeScreenReader reader{world, &target};
    Client::Ui::GameUiState ui;
    Client::Input::InputState input;
};

// 0x6381c7..0x638211: the month, the day, the hour and the minute with two digits, and the counter with four.
TEST(ScreenshotsTest, TheNameHasTheDateTheTimeAndTheCounter)
{
    EXPECT_EQ(ScreenshotFileName(LocalTime{.month = 3, .day = 7, .hour = 9, .minute = 5}, 42),
              "Screen(03_07-09_05)-0042.jpg");
    EXPECT_EQ(ScreenshotFileName(LocalTime{.month = 12, .day = 31, .hour = 23, .minute = 59}, 9999),
              "Screen(12_31-23_59)-9999.jpg");
}

// 0x638219..0x6382f7: 459 with the name in its %s, and " [group / hero]" after it, even if they are empty.
TEST(ScreenshotsTest, TheMessageIsText459WithTheGroupAndTheHero)
{
    EXPECT_EQ(ScreenshotMessage("Guardada %s", "Screen(03_07-09_05)-0000.jpg", "Grupo Uno", "Rolo"),
              "Guardada Screen(03_07-09_05)-0000.jpg [Grupo Uno / Rolo]");
    EXPECT_EQ(ScreenshotMessage("", "x.jpg", "", ""), " [ / ]");
}

// 0x637cff: the edge with its latch (0x77f36d4 + 4 · 0x2c) toggles the request once per press; a second press before
// the frame undoes it (Q-C634).
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

// Q-C630: the press that arrives with its release in the same frame (Print Screen on Windows) is that step's edge,
// and the next step, already without it, sets the latch back to 0.
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

// 0x6381bb..0x6385da without Shift: the message goes in before the draw (it shows in the screenshot), and then the
// read of the canvas's screen, the JPEG with the name in the folder, 0x7d368b0, the counter and the request turned
// off.
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

// 0x638303 and 0x6385af..0x6385d7: with Shift, the message goes after the read and does not show in the screenshot.
TEST(ScreenshotsTest, WithShiftTheMessageGoesAfterTheRead)
{
    Shooting shooting;
    Screenshots screenshots(ScreenshotState{.requested = true});
    ASSERT_TRUE(shooting.Frame(screenshots, true).HasValue());
    ASSERT_EQ(shooting.reader.reads.size(), 1u);
    EXPECT_EQ(shooting.reader.reads[0].chatEntries, 0u);
    EXPECT_EQ(shooting.Messages().size(), 1u);
}

// 0x6385aa → 0x6202e0: what the canvas gathered reaches GL before the read, the 3D before the 2D, as in the Present.
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

// 0x62034b.
TEST(ScreenshotsTest, TheCounterWrapsAtTenThousand)
{
    Shooting shooting;
    Screenshots screenshots(ScreenshotState{.requested = true, .counter = 9999});
    ASSERT_TRUE(shooting.Frame(screenshots).HasValue());
    EXPECT_TRUE(std::filesystem::exists(shooting.folder.Path() / "Screen(03_07-09_05)-9999.jpg"));
    EXPECT_EQ(screenshots.State().counter, 0);
}

// Q-C633: if the file does not open, 0x6202e0 does not check: the counter goes up and the message says the name
// anyway.
TEST(ScreenshotsTest, AFileThatDoesNotOpenStillCountsAndSaysSo)
{
    Shooting shooting;
    Screenshots screenshots(ScreenshotState{.requested = true});
    ScreenshotFrameContext context = shooting.Context();
    context.folder = shooting.folder.Path() / "no-existe";
    ASSERT_TRUE(screenshots.BeforeTheDraw(context).HasValue());
    Client::Render::Frame::SceneCanvas& canvas = shooting.target.BeginFrame({});
    ASSERT_TRUE(screenshots.AfterTheDraw(context, canvas, shooting.reader).HasValue());
    shooting.target.Present();
    EXPECT_EQ(screenshots.State().counter, 1);
    EXPECT_FALSE(screenshots.State().requested);
    EXPECT_EQ(shooting.Messages().size(), 1u);
}

// Q-C632: without a chosen server, the group is empty; without Text.bmd, 459 too.
TEST(ScreenshotsTest, WithoutAChosenServerTheGroupIsEmpty)
{
    Shooting shooting;
    shooting.chosenServer.reset();
    Screenshots screenshots(ScreenshotState{.requested = true});
    ASSERT_TRUE(shooting.Frame(screenshots).HasValue());
    EXPECT_EQ(shooting.Messages(), (std::vector<std::string>{"Guardada Screen(03_07-09_05)-0000.jpg [ / Rolo]"}));
}

// P-C110: a frame the scene holds with the screenshot requested stops in the step or the draw that follow; while it
// connects, the request waits for the next draw.
TEST(ScreenshotsTest, AHeldFrameWithTheRequestCuts)
{
    {
        const std::unique_ptr<Shooting> shootingOnHeap = std::make_unique<Shooting>();
        Shooting& shooting = *shootingOnHeap;
        Screenshots screenshots(ScreenshotState{.requested = true});
        screenshots.FinishTheLoop(true);
        ASSERT_TRUE(shooting.Frame(screenshots).HasValue());
        EXPECT_EQ(shooting.reader.reads.size(), 1u);
        screenshots.FinishTheLoop(false);
        EXPECT_TRUE(screenshots.StepTheKey(shooting.ui, shooting.input).HasValue()) << "un frame dibujado no corta";
    }
    const std::unique_ptr<Shooting> shootingOnHeap = std::make_unique<Shooting>();
    Shooting& shooting = *shootingOnHeap;
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

TEST(ScreenshotsTest, AHeldFrameWithoutTheRequestGoesOn)
{
    Shooting shooting;
    Screenshots screenshots;
    screenshots.FinishTheLoop(false);
    EXPECT_TRUE(screenshots.StepTheKey(shooting.ui, shooting.input).HasValue());
    EXPECT_TRUE(screenshots.BeforeTheDraw(shooting.Context()).HasValue());
}
} // namespace
