#include "FrontEnd/DeleteChecks.h"

#include "Tables/TextTable.h"
#include "Protocol/FrontEnd/CharacterList.h"
#include "Ui/GameText.h"
#include "Ui/Notices.h"
#include "FrontEnd/ServerListModel.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Client::FrontEnd
{
namespace
{
// Byte 0x6d5e24, which nobody writes.
constexpr std::uint16_t HighLevel = 80;
// +0x1d8.
constexpr std::uint8_t BlockingFlags = 0x12;
// The name of the group that lets anyone be deleted (the 12's).
constexpr std::size_t FreeGroupText = 559;
} // namespace

bool FreeDeletionGroup(const Tables::TextTable* texts, std::optional<std::uint16_t> chosenServer)
{
    if (!chosenServer)
    {
        return true;
    }
    const std::string_view name = Ui::TextAt(texts, GroupNameText(GroupOf(*chosenServer))).value_or(std::string_view{});
    const std::string_view free = Ui::TextAt(texts, FreeGroupText).value_or(std::string_view{});
    return name == free;
}

Ui::NoticeId NoticeOfDeleteButton(const Protocol::FrontEnd::CharacterEntry& chosen, bool freeDeletionGroup)
{
    if (chosen.guildStatus != Protocol::FrontEnd::NoGuildStatus)
    {
        return Ui::GuildMemberNotice;
    }
    if (chosen.level >= HighLevel && !freeDeletionGroup)
    {
        return Ui::HighLevelNotice;
    }
    if ((chosen.flags & BlockingFlags) != 0)
    {
        return Ui::BlockedAccountNotice;
    }
    return Ui::ConfirmDeletionNotice;
}
} // namespace Client::FrontEnd
