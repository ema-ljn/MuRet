#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

// The C1 05 18 [dir] [action] the client sends (04 § La acción, 18): the still hero's turn (0x59b74c), sitting down
// (0x4f2416..0x4f2bea) and the emote (0x576a60 and the cases of 0x576eb0). All four take the direction from a
// character's +0x24 angle with the same arithmetic. *Static*.
namespace Client::Actors
{
// Whoever sends a C1 18 over the GameServer socket (App::ServerSession::SendAction), with the direction and the action.
using ActionSender = std::function<void(std::uint8_t direction, std::uint8_t action)>;

// ftol((angle + 22.5f) · 0.0222f + 1) & 0xff and then the signed remainder by 8 (0x576b44..0x576b6e,
// 0x4f248a..0x4f24b4, 0x59b456), which with the & 0xff before it is & 7. The arithmetic is in double, without rounding
// to float in between, like the x87 with PC53 (04 § Caminar y pegar). _ftol truncates toward 0: from −112.5° to
// −22.5° it gives 0 (Q-C777). Empty if _ftol has no portable result (Math::OriginalFtolLow).
[[nodiscard]] std::optional<std::uint8_t> ActionDirection(float angle);

// The C1 18 of `action` with the direction of `angle`. Without `send`, or with an angle _ftol does not resolve, the
// error, tagged with `where`.
[[nodiscard]] Core::Expected<void, std::string> SendTheAction(float angle, std::uint8_t action,
                                                              const ActionSender& send, std::string_view where);
} // namespace Client::Actors
