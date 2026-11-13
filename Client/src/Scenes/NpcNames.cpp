#include "Scenes/NpcNames.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace Client::Scenes
{
NpcNameTable NpcNameTableOf(std::span<const NpcName> loaded)
{
    NpcNameTable table(NpcNameCount);
    std::ranges::copy(loaded.first(std::min(loaded.size(), table.size())), table.begin());
    return table;
}

std::optional<std::string_view> NpcNameOf(const NpcNameTable& table, std::int32_t type)
{
    // 0x54ebbe..0x54ebd0: MOV SI con ESI en 0 y CMP ESI,EDX.
    for (const NpcName& entry : table)
    {
        if (static_cast<std::int32_t>(entry.type) != type)
            continue;
        const std::string_view name(entry.name.data(), entry.name.size());
        return name.substr(0, name.find('\0'));
    }
    return std::nullopt;
}
} // namespace Client::Scenes
