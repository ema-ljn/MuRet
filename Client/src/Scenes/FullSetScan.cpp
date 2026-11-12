#include "Scenes/FullSetScan.h"

#include "Data/Models/PlayerModels.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

// 0x505820, cotejado con el PE c32bbd5f (914 bytes, 0x505820..0x505bb1): 06, "El conjunto completo de
// 0x505820".
namespace Client::Scenes
{
namespace
{
static_assert(ScannedPartCount == GameLogic::BodyPartCount);

// 0x50582c..0x505846: la clase 3 empieza por la armadura (ecx = 2); las otras, por el casco (ecx = 1).
constexpr std::uint8_t ClassBits = 7;
constexpr std::uint8_t MagicGladiatorClass = 3;
constexpr std::size_t HelmPart = 0;
constexpr std::size_t ArmorPart = 1;
// +0x298, la última parte: los dos lazos van de ella para abajo, y su índice es el que comparan.
constexpr std::size_t BootsPart = ScannedPartCount - 1;
// cmp word, 0xffff (0x50585f).
constexpr std::uint16_t EmptyPart = 0xffff;
// and 0xf y cmp 9, con signo (0x5058ab..0x5058b4).
constexpr std::uint8_t LevelBits = 0xf;
constexpr int LowestSetLevel = 9;
// La armadura del registro propio, [0x7681eb4] + 0xc44 = +0xb6c + 3 · 0x48: el tipo del lugar 3.
constexpr std::size_t OwnArmorSlot = 3;
// 0x505aa7..0x505ac1: las cuatro con las que [0x743a17c] queda en 1. En el Item.bmd del repo son el 8 15,
// el 8 20, el 8 23 y el 8 32 (Storm Crow, Thunder Hawk, Hurricane y Volcano Armor).
constexpr std::array<std::uint16_t, 4> ArmorsThatKeepTheBonus{0x100f, 0x1014, 0x1017, 0x1020};

bool IsMagicGladiator(std::uint8_t characterClass)
{
    return (characterClass & ClassBits) == MagicGladiatorClass;
}

// (movsx(modelo) − 0x212), con and 0x800001ff y el ajuste de jns (0x505871..0x505893 y 0x5058b6..0x5058ce):
// el resto con signo de 512, que en C++ es el %.
int ItemIndexOf(std::uint16_t model)
{
    return (static_cast<std::int16_t>(model) - Data::Models::FirstItemModel) % int{Data::Models::ItemModelsPerGroup};
}

int LevelOf(const ScannedPart& part)
{
    return part.level & LevelBits;
}

// 0x50584b..0x5058f4: las partes de las botas a `first`. Deja en `lowestLevel` lo que el original deja en
// [0x743a178]: 0 sin conjunto y, con él, el nivel más chico, que escribe cada vez que baja (o se repite).
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
        // cmp [ebp−8], eax y jl: con el mínimo de antes mayor o igual.
        if (lowest >= level)
        {
            lowest = level;
            lowestLevel = level;
        }
    }
    return true;
}

// 0x50591f..0x505ba3: el registro propio entre una apertura y un cierre por la tabla de protección.
bool KeepsTheBonus(const OwnCharacterRecord& own, std::uint8_t& ownRecordAccesses, Render::Models::CrtRandom& random)
{
    const Render::Models::LogicalAccess access(ownRecordAccesses, random, Render::Models::OwnRecordCloseRandoms);
    return std::ranges::find(ArmorsThatKeepTheBonus, own.equipment[OwnArmorSlot].id) != ArmorsThatKeepTheBonus.end();
}
} // namespace

ScannedCharacter ScannedCharacterOf(const ActorState& actor)
{
    ScannedCharacter character{.characterClass = actor.characterClass};
    for (std::size_t part = 0; part < ScannedPartCount; ++part)
    {
        character.body[part] = {.model = actor.body[part].model, .level = actor.body[part].level};
    }
    return character;
}

ScannedCharacter ScannedCharacterOf(const GameLogic::CharacterAppearance& appearance)
{
    ScannedCharacter character{.characterClass = appearance.characterClass};
    for (std::size_t part = 0; part < ScannedPartCount; ++part)
    {
        const GameLogic::WornItem& worn = appearance.bodyParts[part];
        character.body[part] = {.model = worn.model, .level = worn.level};
    }
    return character;
}

FullSetScan ScanFullSet(const ScannedCharacter& character, const OwnCharacterRecord& own,
                        std::uint8_t& ownRecordAccesses, Render::Models::CrtRandom& random)
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

FullSetScan ScanTheDrawnCharacter(WorldState& world, const ScannedCharacter& character,
                                  Render::Models::CrtRandom& random, int frames)
{
    WorldRuntime& runtime = world.Runtime();
    if (frames == 0)
    {
        // Un dibujo entre frames no vuelve a consumir ni a publicar el scan del original.
        std::uint8_t depth = 1;
        Render::Models::CrtRandom copy = random;
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
} // namespace Client::Scenes
