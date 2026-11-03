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
    case 0x61:
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
    default:
        return std::nullopt;
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
    default:
        return NoticeFollowUp::Close;
    }
}
} // namespace Client::UI
