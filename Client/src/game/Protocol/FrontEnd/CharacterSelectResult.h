#pragma once

#include <cstdint>

// Campos de F3/03 sin transformar: Client/docs/research/04-protocolo.md, "La entrada al mundo".
namespace Client::Protocol::Character
{
struct CharacterSelectResult
{
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint8_t map = 0;
    std::uint8_t direction = 0;
    std::uint32_t experience = 0;
    std::uint32_t nextExperience = 0;
    std::uint16_t levelUpPoints = 0;
    std::uint16_t strength = 0;
    std::uint16_t agility = 0;
    std::uint16_t vitality = 0;
    std::uint16_t energy = 0;
    std::uint16_t life = 0;
    std::uint16_t maxLife = 0;
    std::uint16_t mana = 0;
    std::uint16_t maxMana = 0;
    std::uint16_t bp = 0;
    std::uint16_t maxBp = 0;
    std::uint32_t zen = 0;
    std::uint8_t pk = 0;
    std::uint8_t controlCode = 0;
    std::uint16_t addedPoints = 0;
    std::uint16_t maxAddedPoints = 0;
    // Q-C144: el original lee 0x32..0x33, aunque sean padding del struct del GameServer.
    std::uint16_t leadership = 0;
};
} // namespace Client::Protocol::Character
