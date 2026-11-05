#include "UI/DeleteChecks.h"

#include "Data/Text/TextTable.h"
#include "Protocol/Character/CharacterList.h"
#include "UI/GameText.h"
#include "UI/Notices.h"
#include "UI/ServerListModel.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Client::UI
{
namespace
{
// El byte 0x6d5e24, 0x50, que nadie escribe.
constexpr std::uint16_t HighLevel = 80;
// Las banderas (+0x1d8) que no dejan borrar.
constexpr std::uint8_t BlockingFlags = 0x12;
// El nombre del grupo que deja borrar a cualquiera (el del 12).
constexpr std::size_t FreeGroupText = 559;
} // namespace

bool FreeDeletionGroup(const Data::Text::TextTable* texts, std::optional<std::uint16_t> chosenServer)
{
    if (!chosenServer)
    {
        return true;
    }
    const std::string_view name = TextAt(texts, GroupNameText(GroupOf(*chosenServer))).value_or(std::string_view{});
    const std::string_view free = TextAt(texts, FreeGroupText).value_or(std::string_view{});
    return name == free;
}

NoticeId NoticeOfDeleteButton(const Protocol::Character::CharacterEntry& chosen, bool freeDeletionGroup)
{
    if (chosen.guildStatus != Protocol::Character::NoGuildStatus)
    {
        return GuildMemberNotice;
    }
    if (chosen.level >= HighLevel && !freeDeletionGroup)
    {
        return HighLevelNotice;
    }
    if ((chosen.flags & BlockingFlags) != 0)
    {
        return BlockedAccountNotice;
    }
    return ConfirmDeletionNotice;
}
} // namespace Client::UI
