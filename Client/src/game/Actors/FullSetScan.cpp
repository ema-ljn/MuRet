#include "Actors/FullSetScan.h"

#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Kernel/CharacterAppearance.h"
#include "Kernel/CharacterClass.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

// 0x505820, checked against PE c32bbd5f (914 bytes, 0x505820..0x505bb1): 06, "El conjunto completo de
// 0x505820".
namespace Client::Actors
{
namespace
{
static_assert(ScannedPartCount == Kernel::BodyPartCount);

// 0x50582c..0x505846: class 3 starts at the armor (ecx = 2); the others, at the helmet (ecx = 1).
constexpr std::size_t HelmPart = 0;
constexpr std::size_t ArmorPart = 1;
// +0x298, the last part: both loops go down from it, and its index is the one they compare.
constexpr std::size_t BootsPart = ScannedPartCount - 1;
// cmp word, 0xffff (0x50585f).
constexpr std::uint16_t EmptyPart = 0xffff;
// and 0xf and cmp 9, signed (0x5058ab..0x5058b4).
constexpr std::uint8_t LevelBits = 0xf;
constexpr int LowestSetLevel = 9;
// The own record's armor, [0x7681eb4] + 0xc44 = +0xb6c + 3 · 0x48: the type of slot 3.
constexpr std::size_t OwnArmorSlot = 3;
// 0x505aa7..0x505ac1: the four with which [0x743a17c] stays at 1. In the repo's Item.bmd they are 8 15,
// 8 20, 8 23 and 8 32 (Storm Crow, Thunder Hawk, Hurricane and Volcano Armor).
constexpr std::array<std::uint16_t, 4> ArmorsThatKeepTheBonus{0x100f, 0x1014, 0x1017, 0x1020};

bool IsMagicGladiator(std::uint8_t characterClass)
{
    return Kernel::BaseClassOf(characterClass) == Kernel::CharacterClass::MagicGladiator;
}

// (movsx(model) − 0x212), with and 0x800001ff and the jns fix-up (0x505871..0x505893 and 0x5058b6..0x5058ce):
// the signed remainder of 512, which in C++ is %.
int ItemIndexOf(std::uint16_t model)
{
    return (static_cast<std::int16_t>(model) - Tables::FirstItemModel) % int{Tables::ItemModelsPerGroup};
}

int LevelOf(const ScannedPart& part)
{
    return part.level & LevelBits;
}

// 0x50584b..0x5058f4: the parts from the boots to `first`. Leaves in `lowestLevel` what the original leaves in
// [0x743a178]: 0 without a set and, with one, the smallest level, which it writes each time it drops (or repeats).
bool IsComplete(const ScannedCharacter& character, std::size_t first, std::int32_t& lowestLevel)
{
    lowestLevel = 0;
    for (std::size_t part = BootsPart + 1; part-- > first;)
    {
        if (character.body[part].model == EmptyPart)
        {
            return false;
        }
    }
    const ScannedPart& boots = character.body[BootsPart];
    const int index = ItemIndexOf(boots.model);
    int lowest = LevelOf(boots);
    for (std::size_t part = BootsPart + 1; part-- > first;)
    {
        const int level = LevelOf(character.body[part]);
        if (level < LowestSetLevel || ItemIndexOf(character.body[part].model) != index)
        {
            lowestLevel = 0;
            return false;
        }
        // cmp [ebp−8], eax and jl: with the previous minimum greater or equal.
        if (lowest >= level)
        {
            lowest = level;
            lowestLevel = level;
        }
    }
    return true;
}

// 0x50591f..0x505ba3: the own record between an open and a close through the protection table.
bool KeepsTheBonus(const Kernel::OwnCharacterRecord& own, std::uint8_t& ownRecordAccesses, Crt::CrtRandom& random)
{
    const Models::LogicalAccess access(ownRecordAccesses, random, Models::OwnRecordCloseRandoms);
    return std::ranges::find(ArmorsThatKeepTheBonus, own.equipment[OwnArmorSlot].id) != ArmorsThatKeepTheBonus.end();
}
} // namespace

ScannedCharacter ScannedCharacterOf(const Kernel::ActorState& actor)
{
    ScannedCharacter character{.characterClass = actor.characterClass};
    for (std::size_t part = 0; part < ScannedPartCount; ++part)
    {
        character.body[part] = {.model = actor.body[part].model, .level = actor.body[part].level};
    }
    return character;
}

ScannedCharacter ScannedCharacterOf(const Kernel::CharacterAppearance& appearance)
{
    ScannedCharacter character{.characterClass = appearance.characterClass};
    for (std::size_t part = 0; part < ScannedPartCount; ++part)
    {
        const Kernel::WornItem& worn = appearance.bodyParts[part];
        character.body[part] = {.model = worn.model, .level = worn.level};
    }
    return character;
}

FullSetScan ScanFullSet(const ScannedCharacter& character, const Kernel::OwnCharacterRecord& own,
                        std::uint8_t& ownRecordAccesses, Crt::CrtRandom& random)
{
    const bool magicGladiator = IsMagicGladiator(character.characterClass);
    FullSetScan scan;
    scan.complete = IsComplete(character, magicGladiator ? ArmorPart : HelmPart, scan.globals.dword743a178);
    scan.globals.byte743a17c = 1;
    if (magicGladiator && scan.complete && !KeepsTheBonus(own, ownRecordAccesses, random))
    {
        scan.globals.byte743a17c = 0;
    }
    return scan;
}

FullSetScan ScanTheDrawnCharacter(Kernel::WorldState& world, const ScannedCharacter& character, Crt::CrtRandom& random,
                                  int frames)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    if (frames == 0)
    {
        // A drawing between frames neither consumes nor publishes the original's scan again.
        std::uint8_t depth = 1;
        Crt::CrtRandom copy = random;
        return ScanFullSet(character, world.OwnCharacter(), depth, copy);
    }
    FullSetScan scan;
    for (int frame = 0; frame < frames; ++frame)
    {
        scan = ScanFullSet(character, world.OwnCharacter(), runtime.ownRecordDepth, random);
        runtime.fullSetScan = scan.globals;
    }
    return scan;
}
} // namespace Client::Actors
