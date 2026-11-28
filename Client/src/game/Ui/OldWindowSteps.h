#pragma once

#include "Core/Expected.h"
#include "Messenger/FriendModel.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindows.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

// What Ui/OldWindows.cpp (the manager, the frame and the box) and Messenger/FriendWindows.cpp (the friends window,
// its tabs and the add box) share: the base window's and the frame's methods, and the friends classes' vtable. Not
// for use outside Ui.
namespace Client::Ui::Steps
{
// 0x45a210 (with the id from 0x459ef0), 0x45ad80 and 0x491ca0.
[[nodiscard]] OldWindowBase MakeTheBase(OldWindowManager& manager);
[[nodiscard]] OldButton MakeTheButton(OldWindowManager& manager);
[[nodiscard]] OldWindow MakeTheFrame(OldWindowManager& manager);

// Neither hidden (3) nor minimized (5).
[[nodiscard]] bool IsShowing(std::int32_t state);

// 0x459f00 with the step's mouse.
[[nodiscard]] bool MouseIn(const OldWindowStepView& view, std::int32_t x, std::int32_t y, std::int32_t width,
                           std::int32_t height, std::int32_t mode);

// vtable + 0x3c..+0x48: the inner area. The frame and its derived ones (0x48f890..0x48f8e0) take off the border and
// the bar; the friends window's tabs (0x48fb40..0x48fb70: the x at 0x48fb40, the y at 0x48fb50, the width and the
// height), nothing.
[[nodiscard]] std::int32_t ClientX(const OldWindow& window, std::int32_t offset);
[[nodiscard]] std::int32_t ClientY(const OldWindow& window, std::int32_t offset);
[[nodiscard]] std::int32_t ClientWidth(const OldWindow& window);
[[nodiscard]] std::int32_t ClientHeight(const OldWindow& window);

// 0x45abc0.
void PlaceAgainstTheParent(OldWindowManager& manager, OldWindowBase& base);
// 0x45abc0's 9 without looking at the message: what 0x45fea0 and 0x465270 (the vtable + 0x90 of the chat rows and of
// a letter's body) do at the start.
void SizeAgainstTheParent(OldWindowManager& manager, OldWindowBase& base);

// 0x45aa80 from the queue to the rectangle (see OldWindows.cpp).
[[nodiscard]] bool ClaimTheMouse(OldWindowManager& manager, const OldWindowBase& base, const OldWindowGlobals& globals,
                                 const OldWindowStepView& view);

// 0x468d50, the EDIT's vtable + 0x20, and 0x45aa80 on an EDIT (see OldWindows.cpp).
void StepTheEditMouse(OldWindowManager& manager, OldEdit& edit, const OldWindowGlobals& globals,
                      const OldWindowStepView& view);
void StepTheEdit(OldWindowManager& manager, OldEdit& edit, std::int32_t arg, const OldWindowGlobals& globals,
                 const OldWindowStepView& view);

// 0x4682a0(0), the EDIT's vtable + 0x30: SetFocus (the orphan EDIT of a closed box loses it) and the arrows' list on
// it. MuRet does not have 0x4673a0's IME (with the focus on the game window and without a filter).
void FocusTheEdit(OldWindowManager& manager, OldEdit& edit, const OldWindowGlobals& globals);

// 0x45aa80 and 0x45aa50 on a button.
void StepTheButton(OldWindowManager& manager, OldButton& button, std::int32_t arg, const OldWindowGlobals& globals,
                   const OldWindowStepView& view);
void PostAndSettle(OldWindowManager& manager, OldButton& button, std::int32_t type);

// 0x45aa80 on a window (vtable + 0x18) and 0x45aa50 (the message and the step with 1).
[[nodiscard]] Core::Expected<void, std::string> StepTheWindow(OldWindowManager& manager, OldWindow& window,
                                                              std::int32_t arg, const OldWindowGlobals& globals,
                                                              const OldWindowStepView& view);
[[nodiscard]] Core::Expected<void, std::string> PostAndStep(OldWindowManager& manager, OldWindow& window,
                                                            std::int32_t type, const OldWindowGlobals& globals,
                                                            const OldWindowStepView& view);

// Any window's vtable + 0x30.
[[nodiscard]] Core::Expected<void, std::string> SettleTheWindow(OldWindowManager& manager, OldWindow& window,
                                                                const OldWindowGlobals& globals,
                                                                const OldWindowStepView& view);

// 0x490670(id, mostrar).
void ShowTheWindow(OldWindowManager& manager, std::int32_t id, bool show);

// The friends window, its tabs and the add box (Messenger/FriendWindows.cpp).
[[nodiscard]] bool IsAFriendsKind(OldWindowKind kind);

// 0x461f00, the friend lists' vtable + 0x88 (0x460970: the friends tab and, in the chat, the members one and the
// invite one). With the mouse over row `row` (element `at`): the click picks it and flips its mark, the double click
// is the 0xd to the parent (0x490b40) and the right click picks it, flips the mark and opens the whisper with the
// name (0x4694c0). Returns 1: the step goes on.
[[nodiscard]] std::int32_t HoverAFriendRow(OldWindowManager& manager, OldList& list,
                                           std::deque<Messenger::FriendEntry>& friends, std::int32_t row,
                                           std::size_t at, const OldWindowGlobals& globals,
                                           const OldWindowStepView& view);
// vtable + 0x28 (0x4a2be0, 0x49d0b0, 0x49ea50, 0x4a11b0 and 0x4a3850).
[[nodiscard]] Core::Expected<void, std::string> HandleTheFriendsMessage(OldWindowManager& manager, OldWindow& window,
                                                                        const OldWindowGlobals& globals,
                                                                        const OldWindowStepView& view);
// vtable + 0x1c (0x4a2f70, 0x49dc90, 0x49eb20, 0x4a2010 and 0x4a38a0).
[[nodiscard]] Core::Expected<void, std::string> StepTheFriendsChildren(OldWindowManager& manager, OldWindow& window,
                                                                       std::int32_t arg,
                                                                       const OldWindowGlobals& globals,
                                                                       const OldWindowStepView& view);
// vtable + 0x58 (0x4a3030, 0x49dcf0, 0x49eb50, 0x4a2070 and 0x4a38e0).
[[nodiscard]] Core::Expected<void, std::string> AfterTheFriendsMove(OldWindowManager& manager, OldWindow& window,
                                                                    const OldWindowGlobals& globals,
                                                                    const OldWindowStepView& view);
// vtable + 0x30 (0x4a23c0, 0x49ca40, 0x49e690, 0x4a0830 and 0x4a3700).
[[nodiscard]] Core::Expected<void, std::string> SettleTheFriendsKinds(OldWindowManager& manager, OldWindow& window,
                                                                      const OldWindowGlobals& globals,
                                                                      const OldWindowStepView& view);
// 0x48ee60's case 3: the constructor (the ids) and the init (0x4a2280).
[[nodiscard]] Core::Expected<OldWindow, std::string> MakeTheFriendsWindow(OldWindowManager& manager,
                                                                          const OldWindowRequest& request,
                                                                          const OldWindowTexts& texts,
                                                                          const OldWindowMeasure& measure);
// Case 4: the constructor and the init (0x4a35a0).
[[nodiscard]] OldWindow MakeTheAddFriendBox(OldWindowManager& manager, const OldWindowRequest& request,
                                            const OldWindowTexts& texts);
// 0x49e6e0(id, title) and 0x49e710(id) on the friends window's windows tab, if present.
void AddTheWindowRow(OldWindowManager& manager, std::int32_t id, std::string_view title);
void ForgetTheWindowRow(OldWindowManager& manager, std::int32_t id);
// 0x4914f0: with the friends window, its windows tab with those of the first map that are not it and not minimized
// (5), in id order.
void FillTheWindowsTab(OldWindowManager& manager);
// 0x490200 with the friends window: keeps its place in +0xc4..+0xe4.
void SaveTheFriendsPlace(OldWindowManager& manager, const OldWindow& window);

// CL9 milestone 10: the chat with a friend and a letter's windows (Messenger/LetterWindows.cpp).
[[nodiscard]] bool IsALetterKind(OldWindowKind kind);
// 0x48ee60's cases 1, 2, 6 and 7: the constructor and the init (0x493220, 0x4999d0 and 0x497f50). The read and write
// windows build the portrait's character (OldWindowStepView::portrait); without it, they fail (P-C212).
[[nodiscard]] Core::Expected<OldWindow, std::string>
MakeTheLetterKind(OldWindowManager& manager, const OldWindowRequest& request, const OldWindowTexts& texts,
                  const OldWindowMeasure& measure, const OldWindowStepView* view);
// vtable + 0x28 (0x4944c0, 0x49a1f0 and 0x498820).
[[nodiscard]] Core::Expected<void, std::string> HandleTheLetterKindsMessage(OldWindowManager& manager,
                                                                            OldWindow& window,
                                                                            const OldWindowGlobals& globals,
                                                                            const OldWindowStepView& view);
// vtable + 0x1c (0x495450, 0x49b180 and 0x499780).
[[nodiscard]] Core::Expected<void, std::string> StepTheLetterKindsChildren(OldWindowManager& manager, OldWindow& window,
                                                                           std::int32_t arg,
                                                                           const OldWindowGlobals& globals,
                                                                           const OldWindowStepView& view);
// vtable + 0x30 (0x493410, 0x499c00 and 0x498350).
[[nodiscard]] Core::Expected<void, std::string> SettleTheLetterKinds(OldWindowManager& manager, OldWindow& window,
                                                                     const OldWindowGlobals& globals,
                                                                     const OldWindowStepView& view);
// The focus of 0x48ee60's 1 to the chat's EDIT (0x48f839) and that of 0x497f50 to the name's in the write window.
void FocusTheChatEdit(OldWindowManager& manager, OldWindow& window, const OldWindowGlobals& globals);
// vtable + 0x4c: 0x499840 in the write window (without subject or body, yes; otherwise, box 5 with the 1009 and no);
// the rest, 0x48f8f0 (yes).
[[nodiscard]] Core::Expected<bool, std::string> MayCloseTheWindow(OldWindowManager& manager, OldWindow& window,
                                                                  const OldWindowGlobals& globals,
                                                                  const OldWindowStepView& view);
// The destructors: the chat's (0x493160) leaves the room (0x493a70); the read window's (0x499900) releases its letter
// from +0x94 (0x4916b0).
[[nodiscard]] Core::Expected<void, std::string> ForgetTheLetterKind(OldWindowManager& manager, const OldWindow& window);
// 0x4a11b0, the letters tab's vtable + 0x28.
[[nodiscard]] Core::Expected<void, std::string> HandleTheLettersTabMessage(OldWindowManager& manager, OldWindow& tab,
                                                                           const OldWindowGlobals& globals,
                                                                           const OldWindowStepView& view);
// 0x4a2070, the letters tab's vtable + 0x58: the mark-all and the sorting headers.
void AfterTheLettersTabMove(OldWindowManager& manager, OldWindow& tab, const OldWindowGlobals& globals,
                            const OldWindowStepView& view);
// 0x4a0960 on the tab: the model's copy with the previously chosen one, and 0x4a09d0(0).
void RefreshTheLetters(OldWindowManager& manager, OldWindow& tab);
// 0x49d0b0 with the 10 and 3: the chat with the friend (0x4a6430), or the C1 CA if there is none and none was asked
// for (0x4a6af0, 0x4a6910).
[[nodiscard]] Core::Expected<void, std::string> ChatWithTheFriend(OldWindowManager& manager, std::string_view name,
                                                                  const OldWindowStepView& view);
// 0x49d0b0 with the 10 and 4 (0x49dbd4): the write window with the chosen one's name (0x49cad0), if any. Returns its
// id (0 without a window) for the friends tab's queue at 0x49dc37.
[[nodiscard]] Core::Expected<std::int32_t, std::string> WriteToTheFriend(OldWindowManager& manager,
                                                                         const Messenger::FriendEntry* selected,
                                                                         const OldWindowGlobals& globals,
                                                                         const OldWindowStepView& view);
} // namespace Client::Ui::Steps
