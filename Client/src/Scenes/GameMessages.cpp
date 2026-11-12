#include "Scenes/GameMessages.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/WorldRuntime.h"
#include "UI/ChatLog.h"
#include "UI/MessageArea.h"
#include "UI/MessageLog.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Scenes
{
UI::MessageSink MessageSinkOf(WorldRuntime& runtime, const Render::Text::ITextMeasure& measure,
                              App::PixelSize screen)
{
    ChatRuntime& chat = runtime.chat;
    return UI::MessageSink{.log = chat.log,
                           .area = chat.area,
                           .modes = runtime.ui.chatModes,
                           .measure = UI::ChatMeasure{.measure = measure, .dcFont = chat.dcFont, .screen = screen},
                           .noticeSound = chat.noticeSound};
}

Core::Expected<void, std::string> AddGameMessage(WorldRuntime& runtime, const Render::Text::ITextMeasure& measure,
                                                 App::PixelSize screen, std::string_view name, std::string_view text,
                                                 std::int32_t type)
{
    auto sound = UI::AddMessage(MessageSinkOf(runtime, measure, screen), name, text, type);
    if (!sound.HasValue())
        return Core::Unexpected(sound.Error());
    if (sound.Value())
        runtime.lastSoundId = *sound.Value();
    return {};
}

Core::Expected<void, std::string> StepTheMessageClocks(WorldRuntime& runtime,
                                                       const Render::Text::ITextMeasure& measure,
                                                       App::PixelSize screen)
{
    UI::StepTheNoticeClock(runtime.chat.area, measure, runtime.chat.dcFont);
    auto sound = UI::StepTheMessageClock(MessageSinkOf(runtime, measure, screen));
    if (!sound.HasValue())
        return Core::Unexpected(sound.Error());
    if (sound.Value())
        runtime.lastSoundId = *sound.Value();
    return {};
}
} // namespace Client::Scenes
