#pragma once

#include "Core/Expected.h"
#include "Messenger/LetterModel.h"
#include "Ui/OldWindows.h"
#include "Audio/SoundId.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

// The chat with a friend and the letters in the old windows (CL9 milestone 10; 06 § HUD y ventanas, "Las cartas y el
// chat con un amigo"): what the packets (Messenger/GameLetters) and the friends window ask of the windows of cases 1,
// 2, 6 and 7 of 0x48ee60, the window list at [0x5763524] and the letters tab. Those windows' vtable is in
// Messenger/LetterWindows.cpp, behind Ui/OldWindowSteps.h.
namespace Client::Messenger
{
// Text.bmd.
inline constexpr std::size_t ChatTitleText = 994;
inline constexpr std::size_t ChatClosedText = 995;
inline constexpr std::size_t ChatLeftText = 402;
inline constexpr std::size_t LetterReadTitleText = 1054;
// 0x41ed10: a message in a chat that is not on view (0x4a5e50).
inline constexpr Audio::SoundId ChatWaitingSound{0x28};
// For the chat (0x4942d0 and 0x4dc81e check from 0xfd; 0xfc is the everyone-offline one).
inline constexpr std::uint8_t FirstOfflineFriendState = 0xfd;

// 0x4a6430: the chat with friend `name` among the window list's (0x495530 of 10 bytes, skipping those of type 0 or 2);
// −1 if one's friend is empty; 0 without any.
[[nodiscard]] std::int32_t FindTheChatWith(Ui::OldWindowManager& manager, std::string_view name, std::string_view hero);
// 0x4a6af0, 0x4a6910 and 0x4a6bc0: the friends with the C1 CA requested (+0x158).
[[nodiscard]] bool IsAPendingChat(const Ui::OldWindowManager& manager, std::string_view name);
void AddAPendingChat(Ui::OldWindowManager& manager, std::string_view name);
void ForgetAPendingChat(Ui::OldWindowManager& manager, std::string_view name);
// 0x4a4990: the chat at the end of +0xb4.
void AddTheChatToTheList(Ui::OldWindowManager& manager, std::int32_t id);
// 0x4a5e50: sound 0x28 and, if it was not there, the chat at the end of +0x118.
void MarkTheChatWaiting(Ui::OldWindowManager& manager, std::int32_t id, std::optional<Audio::SoundId>& sound,
                        std::function<void(Audio::SoundId)> soundRequest = {});
// 0x495530: the chat's friend (the first member who is not the hero, with under 3; type 1), none (type 0) or the room
// of 3 or more (type 2).
[[nodiscard]] std::string ChatFriend(const Ui::OldWindow& chat, std::string_view hero, std::int32_t& kind);
// 0x493f80: a line with the name of member `speaker` (0x461530; 0xff or none, empty) to the lines' vtable + 0x94
// (0x45f6f0): with the name and the text empty, nothing; otherwise, to the first deque and all wrapped again at the
// list's width (0x45fea0). Wrapping a line of 20 bytes or more measures with `measure`: without it, it cuts.
[[nodiscard]] Core::Expected<void, std::string> AddAChatLine(Ui::OldWindowManager& manager, Ui::OldWindow& chat,
                                                             std::uint8_t speaker, std::string_view text,
                                                             std::int32_t kind, std::int32_t extra,
                                                             const Ui::OldWindowMeasure* measure);
// 0x493c90: the new member or its number and state; the title (994 and the names, 0x461600), the windows tab
// (0x4914f0), 0x495630(0) with 2 or more and the lines list narrower with 3 or more (the 9, which wraps the lines
// again). Returns how many members there are.
[[nodiscard]] Core::Expected<std::int32_t, std::string>
AddAChatMember(Ui::OldWindowManager& manager, Ui::OldWindow& chat, std::string_view name, std::uint8_t index,
               std::uint8_t state, const Ui::OldWindowTexts& texts, std::string_view hero,
               const Ui::OldWindowMeasure* measure);
// 0x493e90: with 3 or more, removes the member and rebuilds the title; with under 3 afterwards, the lines list wide.
[[nodiscard]] Core::Expected<void, std::string>
RemoveAChatMember(Ui::OldWindowManager& manager, Ui::OldWindow& chat, std::string_view name,
                  const Ui::OldWindowTexts& texts, std::string_view hero, const Ui::OldWindowMeasure* measure);
// 0x4942d0: the friends that can be invited: the model's, minus the members and those offline (0xfd+).
void FillTheInvites(Ui::OldWindowManager& manager, Ui::OldWindow& chat, std::string_view hero);
// 0x495630: with 1, the EDIT off and the title with the 995 in front; with 0, the EDIT on and the title without it.
void CloseTheChat(Ui::OldWindow& chat, std::int32_t closed, const Ui::OldWindowTexts& texts);
// 0x4a6840 and 0x4a75c0 over the chats of +0xb4: those with the invite list rebuild it; all receive the 402 and close.
void RefreshTheInvites(Ui::OldWindowManager& manager, std::string_view hero);
[[nodiscard]] Core::Expected<void, std::string>
CloseTheChats(Ui::OldWindowManager& manager, const Ui::OldWindowTexts& texts, const Ui::OldWindowMeasure* measure);
// 0x4a0960 over the friends window's letters tab, if it exists.
void RefreshTheLettersTab(Ui::OldWindowManager& manager);
// 0x499cb0: the letter (0x138 bytes at +0x778) and the body split by '\n' (strtok, without the empty ones) to the list
// at +0x8b0 (0x464ae0, which wraps them all again at the list's width with each one), with the 0xe.
[[nodiscard]] Core::Expected<void, std::string> SetTheLetter(Ui::OldWindowManager& manager, Ui::OldWindow& read,
                                                             const LetterEntry& entry, std::string_view body,
                                                             const Ui::OldWindowMeasure* measure);
// 0x493a70, from the chat's destructor (0x493160): the C1 03 01 through the room's connection (0x493510) and 0x49dfd0.
[[nodiscard]] Core::Expected<void, std::string> LeaveTheChatRoom(Ui::OldWindowManager& manager,
                                                                 const Ui::OldWindow& chat);
// 0x4a6550, timer 1002 (WM_TIMER, 0x4b1a5b): the C1 03 05 for each chat of +0xb4 of the window list, in its order,
// that exists (0x490a00) and has its room's connection (0x493510).
void PingTheChatRooms(Ui::OldWindowManager& manager);
// What the drawing of the chat (0x493fc0), the read window (0x499d60) and the writing window (0x498480) does before
// drawing: dragged or resizing (the state at 4 or 1), 0x45aa50's 8 to their children and, resizing, the 9 to their
// lists (the writing window has no lists and sends no 9; the 9 of the chat's lines and a letter's body wraps them
// again measuring with `measure`, the drawing's DC). The drawing itself is in Messenger/LetterWindowsDraw.
[[nodiscard]] Core::Expected<void, std::string> SettleTheLetterKindsForTheDraw(Ui::OldWindowManager& manager,
                                                                               Ui::OldWindow& window,
                                                                               const Ui::OldWindowMeasure& measure);

// 0x497890 (the portrait's vtable + 0x40): with the character built, its angles (Messenger,
// PortraitCallKind::SetAngle) and +0x6ac and +0x6b0 at `angle`; unbuilt, nothing.
[[nodiscard]] Core::Expected<void, std::string> SetThePortraitAngle(const Ui::OldWindowStepView* view,
                                                                    Ui::OldPortraitPart& portrait, float angle,
                                                                    std::string_view where);
// 0x4978d0 (the vtable + 0x44): +0x6b8 and +0x6bc at `zoom`.
void SetThePortraitZoom(Ui::OldPortraitPart& portrait, float zoom);
} // namespace Client::Messenger
