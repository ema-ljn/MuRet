#include "Scenes/OwnCharacterRecord.h"

#include "Protocol/Character/CharacterSelectResult.h"
#include "Scenes/ActorState.h"
#include "World/ItemState.h"

#include <cstddef>

namespace Client::Scenes
{
OwnCharacterRecord::OwnCharacterRecord()
{
    for (World::ItemState& item : equipment)
    {
        item.id = EmptyOwnItemId;
    }
}

void OwnCharacterRecord::PrepareFrom(const ActorState& actor)
{
    // 63441d/427/430, luego REPMOVS hasta el primer NUL incluido; no limpia el sufijo.
    level = actor.level;
    characterClass = actor.appearance.characterClass;
    skin = actor.appearance.skin;
    for (std::size_t i = 0; i < name.size(); ++i)
    {
        name[i] = actor.name[i];
        if (actor.name[i] == std::byte{0})
        {
            break;
        }
    }
}
void OwnCharacterRecord::ApplyStats(const Protocol::Character::CharacterSelectResult& reply)
{
    experience = reply.experience;
    nextExperience = reply.nextExperience;
    levelUpPoints = reply.levelUpPoints;
    strength = reply.strength;
    agility = reply.agility;
    vitality = reply.vitality;
    energy = reply.energy;
    leadership = reply.leadership;
    life = reply.life;
    maxLife = reply.maxLife;
    mana = reply.mana;
    maxMana = reply.maxMana;
    bp = reply.bp;
    maxBp = reply.maxBp;
    zen = reply.zen;
    addedPoints = reply.addedPoints;
    maxAddedPoints = reply.maxAddedPoints;
    byte3c = 0;
    word3e = 0;
    word40 = 0;
    word42 = 0;
    tailWord48 = 0;
    tailWord4a = 0;
}

void OwnCharacterRecord::ClearEquipmentForEntry()
{
    for (World::ItemState& item : equipment)
    {
        item.id = EmptyOwnItemId;
        item.scalar1Low = 0;
        item.flags1b = 0;
    }
}

void OwnCharacterRecord::ClearEquipmentForInventory()
{
    for (World::ItemState& item : equipment)
    {
        item.id = EmptyOwnItemId;
        item.occupied = 0;
        item.flags1b = 0;
    }
}
} // namespace Client::Scenes
