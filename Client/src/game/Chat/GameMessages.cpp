#include "Chat/GameMessages.h"

#include "Actors/ActorName.h"
#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Kernel/ActorState.h"
#include "Render/Frame/ITextDrawer.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatLog.h"
#include "Ui/MessageArea.h"
#include "Ui/MessageLog.h"
#include "Ui/Widget.h"
#include "Audio/SoundId.h"
#include "Chat/ChatRuntime.h"
#include "Chat/ChatInput.h"
#include "Ui/GameUiState.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Chat
{
Ui::MessageSink MessageSinkOf(Kernel::WorldRuntime& runtime, const Render::Text::ITextMeasure& measure,
                              Core::PixelSize screen)
{
    Chat::ChatRuntime& chat = runtime.chat;
    return Ui::MessageSink{.log = chat.log,
                           .area = chat.area,
                           .modes = runtime.ui.chatModes,
                           .measure =
                               Ui::ChatMeasure{.measure = measure, .dcFont = runtime.hud.dcFont, .screen = screen},
                           .noticeSound = chat.noticeSound};
}

Core::Expected<void, std::string> AddGameMessage(Kernel::WorldRuntime& runtime,
                                                 const Render::Text::ITextMeasure& measure, Core::PixelSize screen,
                                                 std::string_view name, std::string_view text, std::int32_t type)
{
    auto sound = Ui::AddMessage(MessageSinkOf(runtime, measure, screen), name, text, type);
    if (!sound.HasValue())
        return Core::Unexpected(sound.Error());
    if (sound.Value())
        runtime.RequestSound(Audio::SoundId{*sound.Value()});
    return {};
}

ChatMessages::ChatMessages(Kernel::WorldRuntime& runtime, const Render::Text::ITextMeasure& measure,
                           std::function<Core::PixelSize()> screen)
    : m_runtime(runtime), m_measure(measure), m_screen(std::move(screen))
{
}

Core::Expected<void, std::string> ChatMessages::Add(std::string_view name, std::string_view text, std::int32_t type)
{
    const Core::PixelSize screen = m_screen ? m_screen() : Core::PixelSize{640, 480};
    return AddGameMessage(m_runtime, m_measure, screen, name, text, type);
}

Core::Expected<void, std::string> AddTheEmptyMessages(Kernel::WorldRuntime& runtime,
                                                      const Render::Text::ITextMeasure* measure)
{
    if (measure == nullptr)
        return {};
    // Two empty texts are not measured with a screen other than 640 × 480: ": " reaches neither 0x555190's 256 nor the
    // 20 bytes of 0x45bb20's split.
    for (std::size_t i = 0; i < Ui::MessageAreaRows; ++i)
    {
        if (auto added = AddGameMessage(runtime, *measure, Core::PixelSize{640, 480}, {}, {}, 0); !added.HasValue())
            return added;
    }
    return {};
}

Core::Expected<void, std::string>
StepTheMessageClocks(Kernel::WorldRuntime& runtime, const Render::Text::ITextMeasure& measure, Core::PixelSize screen)
{
    Ui::StepTheNoticeClock(runtime.chat.area, measure, runtime.hud.dcFont);
    auto sound = Ui::StepTheMessageClock(MessageSinkOf(runtime, measure, screen));
    if (!sound.HasValue())
        return Core::Unexpected(sound.Error());
    if (sound.Value())
        runtime.RequestSound(Audio::SoundId{*sound.Value()});
    // 0x637d2c and 0x637d44: the chat brake and the macros' one, by one per step (PrintScreen's 0x637cfd, between the
    // clocks and the brakes, does not look at them).
    if (runtime.chatInput.brake > 0)
        --runtime.chatInput.brake;
    if (runtime.hud.macroTime > 0)
        --runtime.hud.macroTime;
    return {};
}

void DrawTheMessages(const Kernel::WorldState& world, Kernel::HudState& hud, Render::SpriteBatch& sprites,
                     Render::Frame::ITextDrawer& text, Ui::Point mouse, bool inTheGame)
{
    const Kernel::WorldRuntime& runtime = world.Runtime();
    const Ui::MessageDrawContext context{.sprites = sprites,
                                         .text = text,
                                         .dcFont = hud.dcFont,
                                         .textColors = hud.textColors,
                                         .textWidth = hud.arrowsTextWidth,
                                         .textHeight = hud.arrowsTextHeight};
    Ui::DrawTheNotices(runtime.chat.area, hud.messageFrame, context, runtime.ui.chatModes.placement);
    if (runtime.ui.chatModes.placement != 1 && inTheGame)
        return;
    const Kernel::ActorState& hero = world.Hero();
    const Ui::MessageAreaView view{.mouseX = mouse.x,
                                   .mouseY = mouse.y,
                                   .chatOpen = runtime.ui.chatOpen,
                                   .heroSafeZone = hero.safeZone != 0,
                                   .heroName = Actors::HeroNameOf(hero),
                                   .showChat = runtime.ui.chatModes.showChat,
                                   .crywolfStage = runtime.crywolf.stage};
    Ui::DrawTheMessageArea(runtime.chat.area, hud.messageFrame, context, view);
}
} // namespace Client::Chat
