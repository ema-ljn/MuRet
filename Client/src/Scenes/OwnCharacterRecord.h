#pragma once

#include "Protocol/Character/Opcodes.h"
#include "World/ItemState.h"
#include "Scenes/OwnDerivedStats.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Client::Protocol::Character
{
struct CharacterSelectResult;
}

namespace Client::Scenes
{
struct ActorState;

// 54eec0: doce words ID FFFF; el resto de cada ItemState parte de cero.
inline constexpr std::size_t OwnEquipmentSlotCount = 12;
inline constexpr std::uint16_t EmptyOwnItemId = 0xffff;

// Proyección tipada del registro propio: bootstrap cero salvo los doce IDs FFFF (54eec0/54ed60/54ed00).
struct OwnCharacterRecord
{
    OwnCharacterRecord();
    void PrepareFrom(const ActorState& actor);
    // 4c40e1..4c4180: no reemplaza identidad ni equipo. Q-C144 pone la cola ausente a cero.
    void ApplyStats(const Protocol::Character::CharacterSelectResult& reply);
    // 4c47c8: ID, scalar1 y flags1b; conserva occupied y el resto del registro.
    void ClearEquipmentForEntry();
    // 4c563c: ID, occupied y flags1b; conserva scalar1 y el resto del registro.
    void ClearEquipmentForInventory();
    std::array<std::byte, Protocol::Character::NameSize + 1> name{};
    std::uint8_t characterClass = 0;
    std::uint8_t skin = 0;
    std::uint16_t level = 0;
    std::array<World::ItemState, OwnEquipmentSlotCount> equipment{};
    std::uint32_t experience = 0;
    std::uint32_t nextExperience = 0;
    std::uint16_t levelUpPoints = 0;
    std::uint16_t strength = 0;
    std::uint16_t agility = 0;
    std::uint16_t vitality = 0;
    std::uint16_t energy = 0;
    std::uint16_t leadership = 0;
    std::uint16_t life = 0;
    std::uint16_t maxLife = 0;
    std::uint16_t mana = 0;
    std::uint16_t maxMana = 0;
    std::uint16_t bp = 0;
    std::uint16_t maxBp = 0;
    std::uint32_t zen = 0;
    std::uint16_t addedPoints = 0;
    std::uint16_t maxAddedPoints = 0;
    // 4c414f..4c415b: stores sin fuente wire; significado todavía no acreditado.
    std::uint8_t byte3c = 0;
    std::uint16_t word3e = 0;
    std::uint16_t word40 = 0;
    std::uint16_t word42 = 0;
    // S+48/4a: el contrato acredita el store, no su significado.
    std::uint16_t tailWord48 = 0;
    std::uint16_t tailWord4a = 0;
    OwnDerivedStats derived;
};
} // namespace Client::Scenes
