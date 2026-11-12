#include "UI/WhisperSenders.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Client::UI
{
std::string_view WhisperSenderAt(const WhisperSenders& senders, std::size_t slot)
{
    const std::string_view all(senders.bytes.data(), senders.bytes.size());
    const std::string_view from = all.substr(slot * WhisperSenderSlotBytes);
    const std::size_t end = from.find('\0');
    return end == std::string_view::npos ? from : from.substr(0, end);
}

void RememberTheWhisperSender(WhisperSenders& senders, std::uint16_t level, std::string_view name)
{
    // 0x554f89: el WORD del nivel, sin signo, contra el 10 del argumento.
    if (level >= WhisperSenderLevel)
        return;
    const std::size_t end = name.find('\0');
    if (end != std::string_view::npos)
        name = name.substr(0, end);
    for (std::size_t slot = 0; slot < WhisperSenderSlots; ++slot)
    {
        if (WhisperSenderAt(senders, slot) == name)
            return;
    }
    const auto at = static_cast<std::size_t>(senders.next) * WhisperSenderSlotBytes;
    const std::size_t room = senders.bytes.size() - at - 1;
    const std::string_view copied = name.substr(0, std::min(name.size(), room));
    std::copy(copied.begin(), copied.end(), senders.bytes.begin() + static_cast<std::ptrdiff_t>(at));
    senders.bytes[at + copied.size()] = '\0';
    ++senders.next;
    if (senders.next > static_cast<std::int32_t>(WhisperSenderSlots) - 1)
        senders.next = 0;
}

void ClearTheWhisperSenders(WhisperSenders& senders)
{
    senders.bytes.fill('\0');
}
} // namespace Client::UI
