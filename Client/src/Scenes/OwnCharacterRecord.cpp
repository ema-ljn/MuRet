#include "Scenes/OwnCharacterRecord.h"

#include "Core/Log.h"
#include "Protocol/Character/CharacterSelectResult.h"
#include "World/ItemState.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace Client::Scenes
{
void OwnCharacterRecord::ResetForSelection()
{
    byte6a = 0;
    words6c.fill(0);
    constexpr std::size_t ResetItems = 8;
    for (std::size_t slot = 0; slot < ResetItems; ++slot)
    {
        equipment[slot].scalar1Low = 0;
    }
}

std::optional<std::uint8_t> OwnCharacterRecord::SkillAt(std::size_t index) const
{
    constexpr std::size_t BytesPerWord = 4;
    constexpr std::uint32_t BitsPerByte = 8;
    constexpr std::size_t WordBytes = 16 * BytesPerWord;
    if (index >= WordBytes)
    {
        if (index - WordBytes >= skillsAc.size())
            return std::nullopt;
        return skillsAc[index - WordBytes];
    }
    const std::uint32_t word = words6c[index / BytesPerWord];
    return static_cast<std::uint8_t>((word >> (static_cast<std::uint32_t>(index % BytesPerWord) * BitsPerByte)) &
                                     0xffu);
}

void OwnCharacterRecord::SetSkillAt(std::size_t index, std::uint8_t skill)
{
    constexpr std::size_t BytesPerWord = 4;
    constexpr std::uint32_t BitsPerByte = 8;
    constexpr std::size_t WordBytes = 16 * BytesPerWord;
    if (index >= WordBytes)
    {
        if (index - WordBytes < skillsAc.size())
            skillsAc[index - WordBytes] = skill;
        return;
    }
    const std::uint32_t shift = static_cast<std::uint32_t>(index % BytesPerWord) * BitsPerByte;
    std::uint32_t& word = words6c[index / BytesPerWord];
    word = (word & ~(0xffu << shift)) | (static_cast<std::uint32_t>(skill) << shift);
}

void OwnCharacterRecord::ApplyStats(const Protocol::Character::CharacterSelectResult& reply, Core::Log& log)
{
    experience = reply.experience;
    nextExperience = reply.nextExperience;
    levelUpPoints = reply.levelUpPoints;
    strength = reply.strength;
    agility = reply.agility;
    vitality = reply.vitality;
    energy = reply.energy;
    life = reply.life;
    maxLife = reply.maxLife;
    mana = reply.mana;
    maxMana = reply.maxMana;
    bp = reply.bp;
    maxBp = reply.maxBp;
    zen = reply.zen;
    addedPoints = reply.addedPoints;
    maxAddedPoints = reply.maxAddedPoints;
    leadership = reply.leadership;
    // 04: "además pone en 0 el byte de +0x3c y los WORD de +0x3e, +0x40 y +0x42".
    speedAdjustBits = 0;
    word3e = 0;
    word40 = 0;
    word42 = 0;
    // Q-C144: lo que el original lee fuera del paquete no se inventa.
    word48 = 0;
    word4a = 0;
    log.Warning("Q-C144: la respuesta del F3 03 no trae pkt[0x34..0x37]: +0x48 y +0x4a del registro propio quedan "
                "en 0");
}

void OwnCharacterRecord::ClearEquipmentForEntry()
{
    // 0x4c47c8..0x4c47da, de a 0x48 desde +0xb70: [eax−4] = bx (−1 desde 0x4c47a8), [eax] = 0 y
    // [eax+0x17] = 0.
    for (World::ItemState& item : equipment)
    {
        item.id = EmptyOwnItemId;
        item.scalar1Low = 0;
        item.flags1b = 0;
    }
}

void OwnCharacterRecord::ClearEquipmentForInventory()
{
    // 0x4c564d..0x4c565f, de a 0x48 desde +0xba8: [eax−0x3c] = di (−1 desde 0x4c55d6), [eax] = 0 y
    // [eax−0x21] = 0.
    for (World::ItemState& item : equipment)
    {
        item.id = EmptyOwnItemId;
        item.occupied = 0;
        item.flags1b = 0;
    }
}

std::uint8_t OwnSkillByte(const OwnCharacterRecord& own, std::size_t index)
{
    return own.SkillAt(index).value_or(std::uint8_t{0});
}
} // namespace Client::Scenes
