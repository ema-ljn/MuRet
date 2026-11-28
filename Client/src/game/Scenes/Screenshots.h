#pragma once

#include "Core/Expected.h"
#include "Core/LocalTime.h"
#include "Core/PixelSize.h"
#include "Render/Frame/IScreenReader.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Input
{
class InputState;
}

namespace Client::Render::Text
{
class ITextMeasure;
}

namespace Client::Tables
{
class TextTable;
}

namespace Client::Ui
{
struct GameUiState;
}

// The Print Screen screenshot (06 § La captura de pantalla): what 0x637700 does in all scenes, after the step
// (0x637cff) and around the draw (0x6381bb..0x6385da). It takes no rand(). *Static*.
namespace Client::Render::Frame
{
struct SceneCanvas;
} // namespace Client::Render::Frame
namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Scenes
{

// The screenshot's globals, in the .bss: only 0x637700 and 0x6202e0 read and write them.
struct ScreenshotState
{
    // 0x7d368a8: the requested screenshot. Print Screen toggles it (0x637d10..0x637d25) and the frame clears it
    // (0x6385da).
    bool requested = false;
    // 0x7d368ac: the name's number, from 0 to 9999. 0x6202e0 raises it (0x620345..0x62035a).
    std::int32_t counter = 0;
    // 0x7d368b0: 0x6202e0 sets it to 1 (0x6202ed) and nobody reads it.
    bool taken = false;
};

struct ScreenshotFrameContext
{
    // The message's chat (0x555190) and the hero's name (+0x1d9 of 0x743a170).
    Kernel::WorldState& world;
    // The texts' DC and the screen in pixels, to measure the message (06 § El chat).
    const Render::Text::ITextMeasure& measure;
    Core::PixelSize screen;
    // GetAsyncKeyState(0x10) at 0x638303: Shift down.
    bool shiftDown = false;
    // GetLocalTime (0x6381c7). Read only with the screenshot requested (ScreenshotFileName).
    std::function<Core::Expected<Core::LocalTime, std::string>()> localTime;
    // Text.bmd, with the 459. Without a table, the empty text.
    const Tables::TextTable* texts = nullptr;
    // The chosen server's code (ServerSession::ChosenServer), which gives its group's name (Q-C632).
    std::optional<std::uint16_t> chosenServer;
    // Where the file goes. The original opens it with the bare name, in the working folder: Main passes the empty one.
    std::filesystem::path folder;
};

// 0x6381c7..0x638211: sprintf("Screen(%02d_%02d-%02d_%02d)-%04d.jpg") with the month, the day, the hour, the minute
// and the counter.
[[nodiscard]] std::string ScreenshotFileName(const Core::LocalTime& time, std::int32_t counter);

// 0x638219..0x6382f7: sprintf with text 459 as the format and the name, and after it, with strcat, the wsprintfA of
// " [%s / %s]" with the group's name and the hero's.
[[nodiscard]] std::string ScreenshotMessage(std::string_view format, std::string_view fileName, std::string_view group,
                                            std::string_view hero);

// The screenshot between the step and the frame: the request, what the frame builds before drawing for after, and
// the cut of a held frame with the screenshot requested.
class Screenshots
{
public:
    // With the globals at zero, as in the .bss; the tests can start with others.
    Screenshots() = default;
    explicit Screenshots(const ScreenshotState& state);

    // 0x637cfd..0x637d25, after any scene's step: with the Print Screen edge (0x553520(0x2c), with its latch in
    // ui.keyLatches), toggles the request. A press that arrives with its release in the same frame is also the edge
    // (Q-C630). If a held frame had the screenshot requested, its error (FinishTheLoop).
    [[nodiscard]] Core::Expected<void, std::string> StepTheKey(Ui::GameUiState& ui, const Input::InputState& input);

    // 0x6381bb..0x63835a, before the scene's draw: with the screenshot requested, the name and the message, and without
    // Shift, the message in the chat (0x555190 with the 0x7d3a190 empty and type 1), which shows in the screenshot. The
    // original builds them every frame; nobody else reads them (only 0x637700 and 0x6202e0 read 0x7d36724), so MuRet
    // builds them only with the screenshot requested. If a held frame had the screenshot requested, its error.
    [[nodiscard]] Core::Expected<void, std::string> BeforeTheDraw(const ScreenshotFrameContext& context);

    // 0x638586..0x6385da, after the draw and before the swap: with the screenshot requested, what the canvas gathered
    // to GL, the read (0x6202e0), the JPEG at quality 100 (0x63b150, Data::Textures::WriteJpegFile), the counter, and
    // with Shift, the message (with the 0x7d3a194 empty), which no longer shows; the request, off. A file that does not
    // open is not an error (the original does not check it); libjpeg stopping is.
    [[nodiscard]] Core::Expected<void, std::string> AfterTheDraw(const ScreenshotFrameContext& context,
                                                                 Render::Frame::SceneCanvas& canvas,
                                                                 Render::Frame::IScreenReader& reader);

    // The end of a loop turn without minimizing (SceneManager::FinishTheLoop). If there was no draw and the screenshot
    // is requested, the original takes it anyway from a frame it does not present (0x6385aa with the scene draw's 0,
    // 0x6385e1): MuRet does not build that frame and stops at the next StepTheKey or BeforeTheDraw (P-C110). While
    // connecting (`connecting`), the original runs no frame (Q-C85): the request waits for the next draw.
    void FinishTheLoop(bool connecting);

    [[nodiscard]] const ScreenshotState& State() const;

private:
    // What BeforeTheDraw built for AfterTheDraw: the name (0x7d36724), the message (ebp − 0x14c) and Shift
    // (ebp − 0x164).
    struct Pending
    {
        std::string fileName;
        std::string message;
        bool shiftDown = false;
    };

    ScreenshotState m_state;
    std::optional<Pending> m_pending;
    // Whether BeforeTheDraw ran since the last FinishTheLoop.
    bool m_drawn = false;
    // The cut of a held frame with the screenshot requested.
    std::optional<std::string> m_heldFrame;
};
} // namespace Client::Scenes
