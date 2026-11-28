#pragma once

#include "Chat/ChatBubbles.h"
#include "Chat/ChatInput.h"
#include "Chat/ChatLog.h"
#include "Chat/WhisperSenders.h"
#include "Ui/MessageArea.h"

#include <optional>
#include <string>

namespace Client::Chat
{
// The chat (CL8 milestone 3; 06 § El chat), which lives from the login to the game: the new log (0x576350c's object)
// and the old area and the notices (0x77d9868 and 0x77f3b88 with their clocks). What its drawings write goes in
// HudState.
struct ChatRuntime
{
    ChatLogState log{};
    Ui::MessageAreaState area{};
    // 0x77f3b78 (.bss 0): the sound of the filter's notices. The F3 30 (0x4dd570) and the Esc menu (0x62567b) change
    // it.
    bool noticeSound = false;
    // 0x77f3b98 (.bss 0): with 1, the 02 does nothing (0x4e34f1). /whisper off and on and the Esc menu write it.
    bool whisperBlocked = false;
    // 0x57678c4: the 11 bytes of the last outgoing whisper's target (0x4b87e9, on sending). The 0C with 0 names the
    // error with them (0x4e3684); with none, empty like the .bss.
    std::string lastWhisperTarget{};
    // 0x77997f8 and 0x77f3b9c: those who whispered while the own level was below 10 (0x554d20).
    WhisperSenders whisperSenders{};
    // 0x77e1be0: the bubbles.
    ChatBubbleTable bubbles{};
    // +0x510 and +0x511 of the selection's bar (the new UI's window +0x7f50, 0x7d53728): the first 0D with 1 that
    // arrives in the selection (0x63eda0), never written again because the bar is built once (0x63e6e0). Empty, +0x510
    // at 0.
    std::optional<std::string> selectionNotice{};
    // 0x6c7a8c (.data 1): Slide.bmd's banners on. The F3 30's bit 0x10 turns it off (0x4dd7fa).
    bool slidesOn = true;
    // The chat input (the EDITs of [0x57634f8], their buffers, the throttle and the macros).
    ChatInputState input{};
};
} // namespace Client::Chat

