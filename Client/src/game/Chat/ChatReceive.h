#pragma once

#include "App/IChatRoomLinks.h"
#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Protocol/Chat/Messages.h"
#include "Protocol/Player/OptionData.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Guild/GuildPanels.h"
#include "Kernel/SceneId.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace Client::Tables
{
class TextTable;
}

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

// The incoming chat (CL8 milestone 3; 04 § El chat que llega and 06 § El chat): what 0x4e2b10 and its functions do
// with the 00, the 01, the 02, the 0C, the 0D and the F3 30, on WorldRuntime. None of them looks at the session's
// state or the encryption flag.
namespace Client::Chat
{

struct ChatReceiveContext
{
    Kernel::WorldState& world;
    // The CRT's rand(): drawn by the closes of the own record in 0x554d20 and in 0x4dd570.
    Crt::CrtRandom& random;
    // What the messages are measured with; without a measurer, the ones that go to the chat stop with an error.
    const Render::Text::ITextMeasure* measure = nullptr;
    Core::PixelSize screen{640, 480};
    // 0x6d5dc8
    std::optional<Kernel::SceneId> scene{};
    // Text.bmd: the 482 of the 0C and the 483 of the 0D with 2.
    const Tables::TextTable* texts = nullptr;
    // 0x6d5edc: the selected one's slot in the selection, from 0 to 4. Empty, the −1.
    std::optional<std::uint8_t> chosenSlot{};
    // The session's GameServer, for what opening the guild's window 2 closes (0x4e345c, CL9 milestone 8). Without it,
    // the Guild Master's 01 stops if there is something to close with a wire.
    Guild::GuildSender guildSend{};
    // 0x4c6098: at the login scene the 00 asks the ConnectServer for the list again through the open socket.
    std::function<void()> requestServerList{};
    // The connections of the rooms of the chat with a friend (the session); without them, the CA that enters one stops.
    App::IChatRoomLinks* rooms = nullptr;
    // CL9 milestone 10: the game parts the letters' portrait character uses (Messenger/LetterPortrait), which outlive
    // the packet. nullptr: the C7 stops at 0x4dcd0e (P-C212).
    const Scenes::WorldEntryResources* worldEntry = nullptr;
};

// 0x4c6098: at the login scene (2), requestServerList and nothing else.
// 0x4c6070 outside scene 2: the name of pkt[3..12] and, by pkt[0xd], the text to the party chat (~, type 4), to the
// guild notice (@@, type 6), to the guild's (@, type 5), only to a bubble that also goes to the log (#, 0x557560 with
// 1) or to a bubble and the normal chat (0x557560 with 0 and type 3). Each branch copies 59, 58, 59, 59 or 60 bytes to
// a zeroed 61-byte buffer.
[[nodiscard]] Core::Expected<void, std::string> ApplyChat(const Protocol::Chat::Chat& chat,
                                                          const ChatReceiveContext& context);

// 0x4e33fc: the key's character (0x5110d0, the 401st if there is none) and its bubble (0x557170 with 0 and without a
// color). With the hero at +0x368 = 0x80 and that character named "길드 마스터" (0x6c8404), without a bubble, 0x4e3450:
// opens the guild's window 2 (0x6599a0(0x40000, 0)), turns on the sign-up field (0x77f3b68), closes the chat's
// (0x6c8e68), empties the buffers (0x553570(0)), leaves the Tab count (0x6c8e6c) at 0 and, to the hero without a
// guild (+0x1f2 at −1), gives the 1999.
[[nodiscard]] Core::Expected<void, std::string> ApplyKeyChat(const Protocol::Chat::KeyChat& chat,
                                                             const ChatReceiveContext& context);

// 0x4e34ea: with 0x77f3b98 at 0, the sender to 0x554d20's list (an access to the own record, 3848 rand() on closing
// it), the sound 0x26 with 0x77f3b78 and the whisper (0x555190 with type 0) with 60 bytes from pkt[0xd].
[[nodiscard]] Core::Expected<void, std::string> ApplyWhisper(const Protocol::Chat::Whisper& whisper,
                                                             const ChatReceiveContext& context);

// 0x4e366f: with pkt[3] at 0, the whisper error (0x555190 with the last target, 0x57678c4, the text 482 and type 2).
[[nodiscard]] Core::Expected<void, std::string> ApplyWhisperResult(const Protocol::Chat::WhisperResult& result,
                                                                   const ChatReceiveContext& context);

// 0x4c65b0: 0, a centered notice (0x554610 without highlight); 1, outside the selection, a system message and, in it,
// the first one goes to the bar's strip (0x63eda0) and the others to the chat; 2, the guild notice (the 483 with the
// text, highlighted) and its window (0x651680, CL9.4: it stops); 10 to 15 with text, in scene 5, to the queues of the
// Slide.bmd banner (0x46b720, Ui::QueueTheSlide).
[[nodiscard]] Core::Expected<void, std::string> ApplyNotice(const Protocol::Chat::Notice& notice,
                                                            const ChatReceiveContext& context);

// 0x4dd570: with an access to the own record (3848 rand() on closing it), the selected one's skill keys; then, the
// automatic attack, the notices' sound, the signs, Q, W and E, and the chat log's lines and transparency. Without a
// selected one, it stops before opening the record (it would write in +0x16c).
[[nodiscard]] Core::Expected<void, std::string> ApplyOptionData(const Protocol::Player::OptionData& data,
                                                                const ChatReceiveContext& context);
} // namespace Client::Chat
