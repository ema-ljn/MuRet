#include "Scenes/WorldState.h"

#include "Scenes/ActorState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "World/ItemState.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <type_traits>

// Client::Scenes::WorldState: lo que WinMain deja en cero y dura de una escena a otra (06, "Los
// personajes de la lista"; 04, el registro propio).
namespace
{
using Client::Scenes::WorldState;

// Un solo dueño: copiarlo o moverlo daría otra copia de la historia del pool.
static_assert(!std::is_copy_constructible_v<WorldState>);
static_assert(!std::is_copy_assignable_v<WorldState>);
static_assert(!std::is_move_constructible_v<WorldState>);
static_assert(!std::is_move_assignable_v<WorldState>);

// El REP STOSD de WinMain (0x4bafa6..0x4bafb3): 401 registros en cero, también el que queda fuera de
// los 400 que recorren el dibujo y el picking.
TEST(WorldStateTest, StartsWith401ZeroedRecords)
{
    const WorldState world;
    ASSERT_EQ(world.Actors().size(), 401u);
    for (std::size_t index = 0; index < world.Actors().size(); ++index)
    {
        SCOPED_TRACE(index);
        const Client::Scenes::ActorState& actor = world.Actors()[index];
        EXPECT_EQ(actor.position.x, 0.0f);
        EXPECT_EQ(actor.position.y, 0.0f);
        EXPECT_EQ(actor.position.z, 0.0f);
        EXPECT_FALSE(actor.previousHad1c15);
        EXPECT_EQ(actor.revision, 0u);
    }
}

// WinMain llena de ceros el registro propio (04, 0x4bafbc..0x4bafc2): el tipo de los doce lugares
// arranca en 0, no en −1. El −1 lo pone recién la respuesta del F3 03 (06, fila 7 de 0x4c3940).
TEST(WorldStateTest, TheOwnRecordStartsZeroed)
{
    const WorldState world;
    const Client::Scenes::OwnCharacterRecord& own = world.OwnCharacter();
    EXPECT_EQ(own.name, (std::array<std::byte, 11>{}));
    EXPECT_EQ(own.characterClass, 0);
    EXPECT_EQ(own.skin, 0);
    EXPECT_EQ(own.level, 0);
    EXPECT_EQ(own.zen, 0u);
    ASSERT_EQ(own.equipment.size(), 12u);
    for (const Client::World::ItemState& item : own.equipment)
    {
        EXPECT_EQ(item.id, 0);
        EXPECT_EQ(item.byte0a, 0);
        EXPECT_EQ(item.optionCount, 0);
    }
}
} // namespace
