#include "Scenes/NpcNames.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace Client::Scenes
{
NpcNameTable NpcNameTableOf(std::span<const NpcName> loaded)
{
    NpcNameTable table(NpcNameCount);
    std::ranges::copy(loaded.first(std::min(loaded.size(), table.size())), table.begin());
    return table;
}

std::optional<std::string> NpcNameOf(const NpcNameTable& table, std::int32_t type)
{
    // 0x54ebbe..0x54ebd0: MOV SI con ESI en 0 y CMP ESI,EDX.
    const auto found = std::ranges::find_if(table, [type](const NpcName& entry)
                                            { return static_cast<std::int32_t>(entry.type) == type; });
    if (found == table.end())
        return std::nullopt;
    std::string name;
    for (auto entry = found; entry != table.end(); ++entry)
    {
        // Después del primero, el word del tipo del siguiente (el byte bajo primero) y su nombre.
        if (entry != found)
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
} // namespace Client::Scenes
