#include "UI/Notices.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace Client::UI
{
namespace
{
NoticeTexts One(std::size_t text)
{
    return NoticeTexts{.first = text, .second = std::nullopt};
}

NoticeId Notice(NoticeId notice)
{
    return notice;
}
} // namespace

std::optional<NoticeTexts> TextsOf(NoticeId notice)
{
    // La tabla de saltos de 0x6431b0, con los textos de 0x76bd1dc + 300 · índice.
    switch (notice)
    {
    case 0x15:
        return One(407);
    case 0x16:
        return One(414);
    case 0x17:
        return One(415);
    case 0x18:
    case ServerFullNotice:
        return One(416);
    case 0x19:
    case BlockedAccountNotice:
        return One(417);
    case 0x1a:
    case VersionMismatchNotice:
        return NoticeTexts{.first = 405, .second = 406};
    case 0x1b:
        return One(408);
    case 0x1c:
        return One(409);
    case 0x1d:
    case 0x1e:
    case 0x1f:
    case 0x20:
        return One(410 + static_cast<std::size_t>(notice - 0x1d));
    case 0x22:
    case 0x23:
    case 0x24:
        return One(597 + static_cast<std::size_t>(notice - 0x22));
    case 0x25:
        return One(433);
    case 0x26:
        return One(435);
    case EmptyAccountNotice:
        return One(403);
    case EmptyPasswordNotice:
        return One(404);
    case ConnectFailedNotice:
        return One(402);
    case LockedCharacterNotice:
        return One(434);
    case CreateRefusedNotice:
        return One(1716);
    case CharacterLimitNotice:
        return One(396);
    case PleaseWaitNotice:
        return One(471);
    case ConfirmDeletionNotice:
        return One(1712);
    case DeletionCodeNotice:
        return One(1713);
    case CharacterDeletedNotice:
        return One(1714);
    case BlockedCharacterNotice:
        return One(439);
    case ForbiddenNameNotice:
        return One(1715);
    case ShortNameNotice:
        return One(390);
    case SymbolNameNotice:
        return One(391);
    case HighLevelNotice:
        return One(1711);
    case InvalidCodeNotice:
        return One(401);
    case GuildMemberNotice:
        return One(1654);
    default:
        return std::nullopt;
    }
}

NoticeKind KindOf(NoticeId notice)
{
    switch (notice)
    {
    case PleaseWaitNotice:
        return NoticeKind::NoButton;
    case ConfirmDeletionNotice:
        return NoticeKind::OkCancel;
    case DeletionCodeNotice:
        return NoticeKind::Code;
    default:
        return NoticeKind::Ok;
    }
}

std::optional<NoticeId> NoticeOfCreateResult(std::uint8_t result)
{
    switch (result)
    {
    case 0:
        return Notice(CreateRefusedNotice);
    case 2:
        return Notice(CharacterLimitNotice);
    default:
        return std::nullopt;
    }
}

NoticeId NoticeOfDeleteResult(std::uint8_t result)
{
    switch (result)
    {
    case 0:
        return GuildMemberNotice;
    case 1:
        return CharacterDeletedNotice;
    case 3:
        return BlockedCharacterNotice;
    default:
        return InvalidCodeNotice;
    }
}

std::optional<NoticeId> NoticeOfLoginResult(std::uint8_t result)
{
    // 0x4e5ebc y 0x4e5f04; lo que no está en las tablas va al default de 0x4e2d66, el 0x1b.
    switch (result)
    {
    case 0:
        return Notice(0x15);
    case 1:
        return std::nullopt;
    case 2:
        return Notice(0x16);
    case 3:
        return Notice(0x17);
    case 4:
        return Notice(0x18);
    case 5:
        return Notice(0x19);
    case 6:
        return Notice(0x1a);
    case 8:
        return Notice(0x1c);
    case 9:
        return Notice(0x25);
    case 10:
        return Notice(0x1d);
    case 11:
        return Notice(0x1e);
    case 12:
        return Notice(0x1f);
    case 13:
        return Notice(0x20);
    case 17:
        return Notice(0x26);
    case 192:
    case 208:
        return Notice(0x22);
    case 193:
    case 209:
        return Notice(0x23);
    case 194:
    case 210:
        return Notice(0x24);
    default:
        return Notice(0x1b);
    }
}

NoticeFollowUp FollowUpOf(NoticeId notice)
{
    switch (notice)
    {
    case 0x15:
    case EmptyPasswordNotice:
        return NoticeFollowUp::ReopenAtPassword;
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19:
    case 0x1b:
    case 0x1d:
    case 0x1e:
    case 0x1f:
    case 0x20:
    case 0x22:
    case 0x23:
    case 0x24:
    case 0x25:
    case 0x26:
    case EmptyAccountNotice:
        return NoticeFollowUp::ReopenAtAccount;
    case 0x1a:
    case 0x1c:
    case VersionMismatchNotice:
    case ConnectFailedNotice:
        return NoticeFollowUp::Quit;
    case ConfirmDeletionNotice:
        return NoticeFollowUp::AskForTheCode;
    case DeletionCodeNotice:
        return NoticeFollowUp::SendTheDeletion;
    default:
        return NoticeFollowUp::Close;
    }
}
} // namespace Client::UI
