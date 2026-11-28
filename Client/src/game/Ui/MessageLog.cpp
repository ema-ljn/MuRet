#include "Ui/MessageLog.h"

#include "Core/Expected.h"
#include "Chat/ChatLog.h"
#include "Ui/MessageArea.h"
#include "Audio/SoundId.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Client::UI
{
Core::Expected<std::optional<Audio::SoundId>, std::string> AddMessage(const MessageSink& sink, std::string_view name,
                                                                      std::string_view text, std::int32_t type)
{
    auto sound = AddToTheLog(sink.log, sink.modes, sink.measure, name, text, type, 0, sink.noticeSound);
    if (!sound.HasValue())
        return sound;
    AddToTheMessageArea(sink.area, sink.measure.measure, sink.measure.dcFont, name, text, type);
    return sound;
}

Core::Expected<std::optional<Audio::SoundId>, std::string> StepTheMessageClock(const MessageSink& sink)
{
    const bool expired = sink.area.clock < 1;
    --sink.area.clock;
    if (!expired)
        return std::optional<Audio::SoundId>{};
    sink.area.clock = MessageClockSteps;
    return AddMessage(sink, {}, {}, 0);
}
} // namespace Client::UI
