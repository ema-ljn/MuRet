#pragma once

#include "Protocol/FrontEnd/CharacterList.h"

#include <cstdint>
#include <variant>

// Recepciones aceptadas por la sesión, en orden, con su propio payload aunque cambie la vista UI.
namespace Client::App
{
struct CharacterListReceived
{
    Protocol::FrontEnd::CharacterList list;
};

struct CharacterCreated
{
    Protocol::FrontEnd::CharacterEntry entry;
};

struct CharacterDeleted
{
    std::uint8_t slot = 0;
};

using CharacterEvent = std::variant<CharacterListReceived, CharacterCreated, CharacterDeleted>;
} // namespace Client::App
