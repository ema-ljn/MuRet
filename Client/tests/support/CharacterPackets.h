#pragma once

#include "Protocol/Character/Opcodes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// La lista de personajes (F3 00, S->C) escrita a mano como la dice el 04 (La selección de
// personaje): para los tests del lector, de la ServerSession y de los FakeServers. Los nombres son
// sintéticos, como "char0001": ninguno sale del cable.
namespace Client::Tests
{
// Una entrada de la lista, campo por campo.
struct CharacterEntryFields
{
    std::uint8_t slot = 0;
    std::string_view name = "char0001"; // hasta 10 bytes; los que faltan van en 0
    std::uint16_t level = 1;
    std::uint8_t flags = 0;
    std::uint8_t classByte = 0;
    std::array<std::uint8_t, Protocol::Character::EquipmentSize> equipment{};
    std::uint8_t guildStatus = 0xFF;
};

// El byte de la entrada que 0x4c20a0 no lee (el [0xb], antes del nivel): en 0xEE, para ver que no
// se lee.
inline constexpr std::size_t UnreadEntryByte = 0xB;

// Los 10 bytes de `name`, con ceros detrás. Un nombre de más de 10 bytes hace fallar el test.
inline std::array<std::byte, Protocol::Character::NameSize> NameBytes(std::string_view name)
{
    EXPECT_LE(name.size(), Protocol::Character::NameSize) << "un nombre de más de 10 bytes";
    std::array<std::byte, Protocol::Character::NameSize> bytes{};
    const std::size_t size = std::min(name.size(), bytes.size());
    std::ranges::transform(name.substr(0, size), bytes.begin(),
                           [](char value) { return static_cast<std::byte>(value); });
    return bytes;
}

// Los 0x22 bytes de una entrada: el lugar, el nombre, el byte que no se lee, el nivel en
// little-endian, las banderas, la clase, el equipo y el estado en el gremio.
inline std::vector<std::byte> CharacterEntryBytes(const CharacterEntryFields& fields)
{
    std::vector<std::byte> bytes(Protocol::Character::EntrySize, std::byte{0});
    bytes[0] = std::byte{fields.slot};
    const auto name = NameBytes(fields.name);
    std::ranges::copy(name, bytes.begin() + 1);
    bytes[UnreadEntryByte] = std::byte{0xEE};
    bytes[0xC] = static_cast<std::byte>(fields.level & 0xFF);
    bytes[0xD] = static_cast<std::byte>(fields.level >> 8);
    bytes[0xE] = std::byte{fields.flags};
    bytes[0xF] = std::byte{fields.classByte};
    std::ranges::transform(fields.equipment, bytes.begin() + 0x10, [](std::uint8_t value) { return std::byte{value}; });
    bytes[0x21] = std::byte{fields.guildStatus};
    return bytes;
}

// La lista: C1, el tamaño, F3 00, las clases que se pueden crear, el número del aviso, la cantidad y
// las entradas, en ese orden. `count` puede no coincidir con las que trae.
inline std::vector<std::byte> CharacterListBytes(const std::vector<CharacterEntryFields>& entries, std::uint8_t count,
                                                 std::uint8_t creatableClasses = 0, std::uint8_t notice = 0)
{
    const std::size_t size = 7 + Protocol::Character::EntrySize * entries.size();
    EXPECT_LE(size, 0xFFu) << "una lista que no entra en un C1";
    std::vector<std::byte> bytes = {std::byte{0xC1}, static_cast<std::byte>(size), std::byte{0xF3},
                                    std::byte{0x00}, std::byte{creatableClasses},  std::byte{notice},
                                    std::byte{count}};
    for (const CharacterEntryFields& entry : entries)
    {
        const std::vector<std::byte> more = CharacterEntryBytes(entry);
        bytes.insert(bytes.end(), more.begin(), more.end());
    }
    return bytes;
}

// La lista con la cantidad que trae.
inline std::vector<std::byte> CharacterListBytes(const std::vector<CharacterEntryFields>& entries)
{
    return CharacterListBytes(entries, static_cast<std::uint8_t>(entries.size()));
}
} // namespace Client::Tests
