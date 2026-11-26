#pragma once

#include <cstdint>

// The class byte of a character (+0x1d4 of its actor, the record's class): the base class in its low three bits, the
// second class above them.
namespace Client::Kernel
{
enum class CharacterClass : std::uint8_t
{
    DarkWizard = 0,
    DarkKnight = 1,
    Elf = 2,
    MagicGladiator = 3,
    DarkLord = 4,
};

inline constexpr std::uint8_t BaseClassMask = 7;

// The original's `& 7`.
[[nodiscard]] constexpr CharacterClass BaseClassOf(std::uint8_t packed) noexcept
{
    return static_cast<CharacterClass>(packed & BaseClassMask);
}

// For the sites that compare or store the whole byte, where the original does not mask.
[[nodiscard]] constexpr std::uint8_t ClassNumberOf(CharacterClass base) noexcept
{
    return static_cast<std::uint8_t>(base);
}
} // namespace Client::Kernel
