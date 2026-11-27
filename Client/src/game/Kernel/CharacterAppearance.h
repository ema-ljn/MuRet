#pragma once

#include "Protocol/FrontEnd/CharacterList.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// A selection character's clothes: from the class and the list's 17 equipment bytes to each part's models, as
// 0x517270, 0x5139f0 and 0x512930 do. Contract: Client/docs/research/06-render-escenas-y-ui.md, "Los personajes de la
// lista", and the CL6 plan (CL6.3). All *static*, from the disassembly.
namespace Client::Kernel
{
// The five body parts, in 0x5139f0's order (+0x218 to +0x298 of the character).
enum class BodyPart : std::uint8_t
{
    Helm,
    Armor,
    Pants,
    Gloves,
    Boots,
};
inline constexpr std::size_t BodyPartCount = 5;

// What a part wears: the model (0x212 plus the item's type, or a class part), the level (0x513340) and whether it is
// excellent or of a set (b[9]'s and b[10]'s bits, *inferred* what the second is).
struct WornItem
{
    std::uint16_t model = 0;
    std::uint8_t level = 0;
    bool excellent = false;
    bool ancient = false;

    friend bool operator==(const WornItem&, const WornItem&) = default;
};

struct CharacterAppearance
{
    // The class in bits 0 to 2 and the second class in bit 3 (Protocol::FrontEnd::CharacterClassOf).
    std::uint8_t characterClass = 0;
    // 0x517270's third argument: 0 from the list (0x4c20a0).
    std::uint8_t skin = 0;
    std::array<WornItem, BodyPartCount> bodyParts{};
    std::optional<WornItem> rightHand;
    std::optional<WornItem> leftHand;
    std::optional<std::uint16_t> wings;
    std::optional<std::uint16_t> pet;
    // 0x512930: the class's head, which goes under the open helmets.
    std::optional<std::uint16_t> head;
    // 0x512930: by class and whether the skin is 0.
    float scale = 0.9f;
};

using Equipment = std::span<const std::byte, Protocol::FrontEnd::EquipmentSize>;

// `part`'s class model: HelmClass to BootClass (0x2212, 0x221a, 0x2222, 0x222a and 0x2232) plus (class >> 3) · 5 +
// (class & 7).
[[nodiscard]] std::uint16_t ClassPartModel(BodyPart part, std::uint8_t characterClass);

// 0x513340: a part's 3 bits, 0 to 7, to the item's level (0x51338c's table).
[[nodiscard]] std::uint8_t ItemLevelOf(std::uint8_t bits);

// 0x512930: the class's head (the class's helmet part) under an open helmet, 0x1012, 0x1014 or 0x101c to 0x101f; with
// any other, std::nullopt (the original leaves −1 at +0x1f8).
[[nodiscard]] std::optional<std::uint16_t> HeadUnder(std::uint16_t helm, std::uint8_t characterClass);

// 0x512930: the scale by class and whether the skin is 0. Classes 5 to 7 have no case in its two jump tables (0x512a00
// and 0x512a14) and leave it alone: std::nullopt.
[[nodiscard]] std::optional<float> ClassScale(std::uint8_t characterClass, std::uint8_t skin);

// 0x517270 outside scene 2: the class's five parts and nothing in the hands, the wings or the pet; the head and the
// scale of 0x512930.
[[nodiscard]] CharacterAppearance ClassAppearance(std::uint8_t characterClass, std::uint8_t skin);

// And 0x5139f0 with the equipment, field by field (06): the hands, the five parts (an index 0x1ff is the class's), the
// wings, the pet, the levels and the excellent and set bits; then the head and the scale. A hand of type 0xfff keeps
// model 0x1211: CMP AX,0x1fff never takes it as empty. Even with no meshes, the consumer spends 0x509340's rand() calls
// (Q-C102).
[[nodiscard]] CharacterAppearance AppearanceOf(std::uint8_t characterClass, std::uint8_t skin, Equipment equipment);
} // namespace Client::Kernel
