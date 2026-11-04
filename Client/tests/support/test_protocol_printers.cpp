#include "support/ProtocolPrinters.h"

#include "Protocol/Character/CharacterList.h"

#include "support/CharacterPackets.h"

#include <gtest/gtest.h>

// support/ProtocolPrinters.h: lo que imprime GoogleTest de una entrada de la lista de personajes,
// que no puede ser el nombre: es del jugador (el plan de la CL6, "El nombre del personaje").
namespace
{
using Client::Protocol::Character::CharacterEntry;
using Client::Tests::NameBytes;

TEST(ProtocolPrintersTest, ACharacterEntryShowsItsSlotAndTheLengthOfItsName)
{
    CharacterEntry entry;
    entry.slot = 3;
    entry.name = NameBytes("char0001");

    EXPECT_EQ(::testing::PrintToString(entry), "CharacterEntry{lugar 3, nombre de 8 bytes}");
    entry.name = NameBytes("abcdefghij");
    EXPECT_EQ(::testing::PrintToString(entry), "CharacterEntry{lugar 3, nombre de 10 bytes}");
}
} // namespace
