#pragma once

#include "Core/Expected.h"
#include "Chat/ChatLog.h"
#include "Ui/MessageArea.h"
#include "Audio/SoundId.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// The game's message input (CL8 milestone 3; 06 § El chat): 0x555190(name, text, type), called from 98 places in the
// original, and its clock (0x5554b0). It goes first to the new log (0x45bb20 through vtable + 0x94, with the fourth
// argument at 0) and then, if it is for it, to the old area.
namespace Client::Ui
{
// What 0x555190 and its clock touch: both areas, the log's modes, the DC to measure with and the notices' sound option
// (0x77f3b78, which the F3 30 sets and the Esc menu changes).
struct MessageSink
{
    Chat::ChatLogState& log;
    MessageAreaState& area;
    Chat::ChatModes modes;
    Chat::ChatMeasure measure;
    bool noticeSound = false;
};

// 0x555190. Returns the sound the log asks for (0x26 with the filter, Chat/ChatLog.h) or the cut's error.
[[nodiscard]] Core::Expected<std::optional<Audio::SoundId>, std::string>
AddMessage(const MessageSink& sink, std::string_view name, std::string_view text, std::int32_t type);

// 0x5554b0, per step: with the old area's clock at 0 or less before lowering it, 150 and an empty message (0x555190
// with 0x77f3bcc and 0x77f3bc8, two empty .bss texts), which ages the log and shifts the area.
[[nodiscard]] Core::Expected<std::optional<Audio::SoundId>, std::string> StepTheMessageClock(const MessageSink& sink);
} // namespace Client::Ui
