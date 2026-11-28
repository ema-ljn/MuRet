#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Kernel/IOnScreenMessages.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Frame/ITextDrawer.h"
#include "Ui/MessageLog.h"
#include "Ui/Widget.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace Client::Kernel
{
class WorldState;
struct ActorState;
struct HudState;
struct WorldRuntime;
} // namespace Client::Kernel

// The game messages on WorldRuntime's chat (CL8 milestone 3; 06 § El chat): 0x555190 for whoever sends a message (the
// network, the commands) and the two clocks 0x637700 runs after any scene's step.
namespace Client::Chat
{

// 0x555190's input over the chat and the UI modes of `runtime`, measuring with `measure` on a `screen` screen and
// with the DC's font the last draw left.
[[nodiscard]] Ui::MessageSink MessageSinkOf(Kernel::WorldRuntime& runtime, const Render::Text::ITextMeasure& measure,
                                            Core::PixelSize screen);

// 0x555190(name, text, type). The sound it asks for (the filter's 0x26) stays in WorldRuntime::lastSoundId.
[[nodiscard]] Core::Expected<void, std::string> AddGameMessage(Kernel::WorldRuntime& runtime,
                                                               const Render::Text::ITextMeasure& measure,
                                                               Core::PixelSize screen, std::string_view name,
                                                               std::string_view text, std::int32_t type);

// AddGameMessage as the modules ask for it: on `runtime`, measured with `measure` on a screen of `screen()` (640 × 480
// without it).
class ChatMessages final : public Kernel::IOnScreenMessages
{
public:
    ChatMessages(Kernel::WorldRuntime& runtime, const Render::Text::ITextMeasure& measure,
                 std::function<Core::PixelSize()> screen);

    [[nodiscard]] Core::Expected<void, std::string> Add(std::string_view name, std::string_view text,
                                                        std::int32_t type) override;

private:
    Kernel::WorldRuntime& m_runtime;
    const Render::Text::ITextMeasure& m_measure;
    std::function<Core::PixelSize()> m_screen;
};

// The 120 calls to 0x555190 with two empty .bss texts with which the login (0x634b4a), the selection (0x6341d3) and
// the game's first step (0x636881) start: they age the log and empty the old area. Without a measurer (the tests that
// do not look at it), nothing.
[[nodiscard]] Core::Expected<void, std::string> AddTheEmptyMessages(Kernel::WorldRuntime& runtime,
                                                                    const Render::Text::ITextMeasure* measure);

// 0x637cf3 and 0x637cf8: the notices' clock (0x5547e0) and the old area's (0x5554b0), one per step; then, 0x637d2c
// and 0x637d44 lower the chat brake and the macros' one.
[[nodiscard]] Core::Expected<void, std::string>
StepTheMessageClocks(Kernel::WorldRuntime& runtime, const Render::Text::ITextMeasure& measure, Core::PixelSize screen);
// The common part of the three scenes' draw before their UI (0x6376a0..0x6376b6 in the game, 0x63552a..0x63553f in
// the login and 0x634949..0x63495f in the selection): the notices (0x554810) and, with the chat log in its window or
// outside the game, the old area (0x5554e0). It writes in `hud` what the draw leaves: an intermediate MuRet draw
// passes a copy.
void DrawTheMessages(const Kernel::WorldState& world, Kernel::HudState& hud, Render::SpriteBatch& sprites,
                     Render::Frame::ITextDrawer& text, Ui::Point mouse, bool inTheGame);
} // namespace Client::Chat
