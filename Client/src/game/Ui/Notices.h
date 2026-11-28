#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

// The login and selection notices, by number (CL5.5 and CL6.5; 06 § Los avisos and "La barra de la selección y el
// borrado"). 0x6431b0 picks each one's text and kind with a jump table; the F1 01 result picks the notice at 0x4e5ebc
// and 0x4e5f04, those of the F3 01 and the F3 02 at 0x4c2290 and 0x4e2fe9, and each one's OK goes on at 0x643560. The
// login ones are kind 2, one OK; the selection ones go by KindOf. The texts go by Text.bmd index: none is written
// here.
namespace Client::Ui
{
// The notice number 0x65ee60 takes.
using NoticeId = std::uint8_t;

// The ones the login opens on its own.
// The list's server with load 100 to 127 (0x645e10): text 416.
inline constexpr NoticeId ServerFullNotice = 0x63;
// 0x641bf0: texts 403 and 404.
inline constexpr NoticeId EmptyAccountNotice = 0x64;
inline constexpr NoticeId EmptyPasswordNotice = 0x65;
// The F1 00 version is not the client's (0x4c1e20): 405 and 406.
inline constexpr NoticeId VersionMismatchNotice = 0x6f;
// Did not connect, or the F1 00 did not accept (0x4c1e20): 402.
inline constexpr NoticeId ConnectFailedNotice = 0x70;

// The selection's: entering with a locked character (bit 0x1 of its flags, 0x6342c0): 434 (CL6.3).
inline constexpr NoticeId LockedCharacterNotice = 0x84;

// Creating and deleting (CL6.5; 06 § La selección de personaje, "Los avisos de la selección"): the replies to the
// F3 01 (0x4c2290) and the F3 02 (0x4e2b10), the wait, the delete button's checks (0x63edf0) and the name's
// (0x63d990).
inline constexpr NoticeId CreateRefusedNotice = 0x36;    // 1716, the F3 01 with 0
inline constexpr NoticeId CharacterLimitNotice = 0x37;   // 396, the F3 01 with 2
inline constexpr NoticeId PleaseWaitNotice = 0x5d;       // 471, no buttons, after sending
inline constexpr NoticeId ConfirmDeletionNotice = 0x5e;  // 1712 with the name, OK and cancel
inline constexpr NoticeId DeletionCodeNotice = 0x5f;     // 1713, with the code field
inline constexpr NoticeId CharacterDeletedNotice = 0x60; // 1714, the F3 02 with 1
inline constexpr NoticeId BlockedAccountNotice = 0x61;   // 417, flags with 0x12
inline constexpr NoticeId BlockedCharacterNotice = 0x62; // 439, the F3 02 with 3
inline constexpr NoticeId ForbiddenNameNotice = 0x72;    // 1715, a filtered name
inline constexpr NoticeId ShortNameNotice = 0x79;        // 390, menos de 4 bytes
inline constexpr NoticeId SymbolNameNotice = 0x7a;       // 391, a character out of range
inline constexpr NoticeId HighLevelNotice = 0x83;        // 1711, level 80 or more in another group
inline constexpr NoticeId InvalidCodeNotice = 0x89;      // 401, the F3 02 with another
inline constexpr NoticeId GuildMemberNotice = 0x92;      // 1654, in a guild, or the F3 02 with 0

// 0x6431b0's kind, which places the buttons (0x642bf0) and says what Enter and Esc do (0x642e30).
enum class NoticeKind : std::uint8_t
{
    NoButton, // 0: neither Enter nor Esc closes it
    Ok,       // 2: OK, Enter and Esc return it
    OkCancel, // 3: OK and Enter return it; cancel and Esc close it without returning it
    Code,     // 4: like 3, with the code field
};

[[nodiscard]] NoticeKind KindOf(NoticeId notice);

// One text, or two lines.
struct NoticeTexts
{
    std::size_t first = 0;
    std::optional<std::size_t> second;
};

// 0x6431b0's for the login and selection notices; the rest, none.
[[nodiscard]] std::optional<NoticeTexts> TextsOf(NoticeId notice);

// 0x4e5ebc and 0x4e5f04, or none with 1, the accepted one.
[[nodiscard]] std::optional<NoticeId> NoticeOfLoginResult(std::uint8_t result);

// 0x4c2290: 0x36 with 0 and 0x37 with 2; none with 1 (created) and with another.
[[nodiscard]] std::optional<NoticeId> NoticeOfCreateResult(std::uint8_t result);

// 0x4e2b10: 0x92 with 0, 0x60 with 1, 0x62 with 3 and 0x89 with another.
[[nodiscard]] NoticeId NoticeOfDeleteResult(std::uint8_t result);

// What a notice's OK does (0x643560).
enum class NoticeFollowUp : std::uint8_t
{
    Close,
    // The account window opens with the focus on the account, in state 2.
    ReopenAtAccount,
    // The account window opens with the focus on the password, in state 2.
    ReopenAtPassword,
    // PostMessage with WM_CLOSE (Q-C69).
    Quit,
    // The deletion confirmation's (0x5e): the code one opens (0x5f).
    AskForTheCode,
    // The code one's (0x5f): the F3 02 goes out with what was typed (0x643740) and the 0x5d opens.
    SendTheDeletion,
};

[[nodiscard]] NoticeFollowUp FollowUpOf(NoticeId notice);
} // namespace Client::Ui
