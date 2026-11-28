#include "Scenes/Screenshots.h"

#include "Actors/ActorName.h"
#include "Core/Expected.h"
#include "Core/LocalTime.h"
#include "Core/PixelSize.h"
#include "Tables/TextTable.h"
#include "Data/Textures/JpegWriter.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/Frame/IScreenReader.h"
#include "Chat/GameMessages.h"
#include "Render/Frame/SceneCanvas.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameText.h"
#include "Ui/GameUiState.h"
#include "FrontEnd/ServerListModel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::Scenes
{
namespace
{
// 0x637cfd: the Print Screen virtual key (VK_SNAPSHOT), which indexes its latch at 0x77f36d4.
constexpr std::uint8_t PrintScreenVirtualKey = 0x2c;
// 0x63821e: the Text.bmd text that gives the message's format (0x76debc0 = 0x76bd1dc + 459 · 300).
constexpr std::size_t ScreenshotText = 459;
// 0x638347 and 0x6385c4: 0x555190's type.
constexpr std::int32_t ScreenshotMessageType = 1;
// 0x620330: the quality 0x6202e0 passes to 0x63b150.
constexpr int ScreenshotQuality = 100;
// 0x62034b: the counter goes from 0 to 9999.
constexpr std::int32_t CounterModulus = 10000;

// 0x553520(0x2c): with the key down and the latch at 0, sets it to 1 and returns true; with the key up, resets it to 0
// (like GameUi.cpp's KeyEdge). The key down is also the press of the frame that arrived with its release (Q-C630).
bool PrintScreenEdge(Ui::GameUiState& ui, const Input::InputState& input)
{
    std::int32_t& latch = ui.keyLatches[PrintScreenVirtualKey];
    if (!input.IsDown(Input::Key::PrintScreen) && !input.WasPressed(Input::Key::PrintScreen))
    {
        latch = 0;
        return false;
    }
    if (latch != 0)
        return false;
    latch = 1;
    return true;
}

// 0x638232..0x6382ae: 0x7d36bb0 + 0x21e · 0x6d5ec4 (the chosen group's place, clamped between 0 and 30), where
// 0x4c1860 copied the group name: the text of the chosen server's group. Without a chosen server, empty (Q-C632).
std::string_view GroupNameOf(const Tables::TextTable* texts, std::optional<std::uint16_t> chosenServer)
{
    if (!chosenServer)
        return {};
    return Ui::TextAt(texts, FrontEnd::GroupNameText(FrontEnd::GroupOf(*chosenServer))).value_or(std::string_view{});
}
} // namespace

std::string ScreenshotFileName(const Core::LocalTime& time, std::int32_t counter)
{
    return std::format("Screen({:02}_{:02}-{:02}_{:02})-{:04}.jpg", time.month, time.day, time.hour, time.minute,
                       counter);
}

std::string ScreenshotMessage(std::string_view format, std::string_view fileName, std::string_view group,
                              std::string_view hero)
{
    const std::array<std::string_view, 1> arguments{fileName};
    std::string message = Ui::PlaceArguments(format, arguments);
    message += std::format(" [{} / {}]", group, hero);
    return message;
}

Screenshots::Screenshots(const ScreenshotState& state) : m_state(state) {}

Core::Expected<void, std::string> Screenshots::StepTheKey(Ui::GameUiState& ui, const Input::InputState& input)
{
    if (m_heldFrame)
        return Core::Unexpected(*m_heldFrame);
    // 0x637d10..0x637d25: a second edge before the frame undoes the request (Q-C634).
    if (PrintScreenEdge(ui, input))
        m_state.requested = !m_state.requested;
    return {};
}

Core::Expected<void, std::string> Screenshots::BeforeTheDraw(const ScreenshotFrameContext& context)
{
    m_drawn = true;
    if (m_heldFrame)
        return Core::Unexpected(*m_heldFrame);
    m_pending.reset();
    if (!m_state.requested)
        return {};
    // 0x6381bb: 0x41ef00, the 3D sound, belongs to CL10.
    if (!context.localTime)
        return Core::Unexpected(std::string("0x6381c7: la captura necesita la hora local (GetLocalTime)"));
    const auto time = context.localTime();
    if (!time.HasValue())
        return Core::Unexpected(std::format("0x6381c7: {}", time.Error()));
    Kernel::WorldState& world = context.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    const std::string_view format = Ui::TextAt(context.texts, ScreenshotText).value_or(std::string_view{});
    std::string fileName = ScreenshotFileName(time.Value(), m_state.counter);
    std::string message = ScreenshotMessage(format, fileName, GroupNameOf(context.texts, context.chosenServer),
                                            Actors::HeroNameOf(world.Hero()));
    // 0x6382f9..0x63835a: without Shift, the message goes before the draw and shows in the screenshot.
    if (!context.shiftDown)
    {
        if (auto added =
                Chat::AddGameMessage(runtime, context.measure, context.screen, {}, message, ScreenshotMessageType);
            !added.HasValue())
            return added;
    }
    m_pending = Pending{.fileName = std::move(fileName), .message = std::move(message), .shiftDown = context.shiftDown};
    return {};
}

Core::Expected<void, std::string> Screenshots::AfterTheDraw(const ScreenshotFrameContext& context,
                                                            Render::Frame::SceneCanvas& canvas,
                                                            Render::Frame::IScreenReader& reader)
{
    if (!m_state.requested || !m_pending)
        return {};
    const Pending pending = *std::exchange(m_pending, std::nullopt);
    // 0x638592..0x6385a7: with 0x7d366bc, the editor mode nobody turns on, 0x517570 would write
    // Data2\MonsterSetBase2.txt.
    // 0x6385aa → 0x6202e0: what was drawn, with what the canvas still gathers, as in Present (the 3D before the 2D).
    canvas.flushModels();
    canvas.flushSprites();
    m_state.taken = true;
    const std::vector<std::uint8_t> pixels = reader.ReadTheScreen(canvas.screen);
    const auto written = Data::Textures::WriteJpegFile(context.folder / pending.fileName, canvas.screen.width,
                                                       canvas.screen.height, pixels, ScreenshotQuality);
    // A file that does not open (Data::Textures::JpegWrite::NotOpened) goes on the same, with the counter and the
    // message (Q-C633).
    if (!written.HasValue())
        return Core::Unexpected(written.Error());
    m_state.counter = (m_state.counter + 1) % CounterModulus;
    // 0x6385af..0x6385d7: with Shift, the message goes after the read.
    if (pending.shiftDown)
    {
        if (auto added = Chat::AddGameMessage(context.world.Runtime(), context.measure, context.screen, {},
                                              pending.message, ScreenshotMessageType);
            !added.HasValue())
            return added;
    }
    // 0x6385da.
    m_state.requested = false;
    return {};
}

void Screenshots::FinishTheLoop(bool connecting)
{
    const bool drawn = std::exchange(m_drawn, false);
    if (drawn || connecting || !m_state.requested || m_heldFrame)
        return;
    m_heldFrame = std::string("0x6385aa: la captura de un frame que la escena no presenta (0x6385e1), con el glClear y "
                              "lo que dibujó la escena antes de salir con 0, es de P-C110");
}

const ScreenshotState& Screenshots::State() const
{
    return m_state;
}
} // namespace Client::Scenes
