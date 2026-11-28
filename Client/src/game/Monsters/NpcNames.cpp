#include "Monsters/NpcNames.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace Client::Monsters
{
NpcNameTable NpcNameTableOf(std::span<const NpcName> loaded)
{
    NpcNameTable table(NpcNameCount);
    std::ranges::copy(loaded.first(std::min(loaded.size(), table.size())), table.begin());
    return table;
}

std::optional<std::string> NpcNameOf(const NpcNameTable& table, std::int32_t type)
{
    // 0x54ebbe..0x54ebd0: MOV SI with ESI at 0 and CMP ESI,EDX.
    const auto found = std::ranges::find_if(table, [type](const NpcName& entry)
                                            { return static_cast<std::int32_t>(entry.type) == type; });
    if (found == table.end())
        return std::nullopt;
    return NpcNameAt(table, static_cast<std::size_t>(found - table.begin()));
}

std::string NpcNameAt(const NpcNameTable& table, std::size_t index)
{
    std::string name;
    if (index >= table.size())
        return name;
    const auto first = table.begin() + static_cast<std::ptrdiff_t>(index);
    for (auto entry = first; entry != table.end(); ++entry)
    {
        // After the first, the next record's type word (low byte first) and its name.
        if (entry != first)
        {
            for (const unsigned shift : {0u, 8u})
            {
                const auto byte = static_cast<char>((entry->type >> shift) & 0xffu);
                if (byte == '\0')
                    return name;
                name.push_back(byte);
            }
        }
        for (const char byte : entry->name)
        {
            if (byte == '\0')
                return name;
            name.push_back(byte);
        }
    }
    return name;
}
} // namespace Client::Monsters
