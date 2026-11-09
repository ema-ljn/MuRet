#include "Scenes/WorldState.h"

#include "Protocol/Character/CharacterList.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "World/ItemState.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Client::Scenes::WorldState: lo que WinMain deja en cero y dura de una escena a otra (06, "Los
// personajes de la lista"; 04, el registro propio).
namespace
{
using Client::Protocol::Character::CharacterEntry;
using Client::Render::Models::CrtRandom;
using Client::Scenes::WorldState;

constexpr std::uint32_t Seed = 0x0badf00du;

// El estado del rand() de la CRT sembrado con Seed después de `draws` llamadas.
std::uint32_t StateAfter(int draws)
{
    CrtRandom random(Seed);
    for (int i = 0; i < draws; ++i)
    {
        static_cast<void>(random.Next());
    }
    return random.State();
}

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

// 06, "La entrada al mundo", paso 1: al pasar a la escena 3 la selección abre el registro propio, le
// copia el nivel, la clase y la piel 0 del elegido y el nombre con un strcpy, que deja lo que sigue al
// cero (0x634433..0x63444e); el cierre gasta 3.848 rand() si lo deja sin accesos (0x40f370, 0x40b4d0).
// Después baja la bandera de la carga del mundo (0x633efe, 0x634469, 0x634631).
TEST(WorldStateTest, PreparingTheChosenCopiesItIntoTheOwnRecord)
{
    struct Case
    {
        std::uint8_t depth;
        int draws;
    };
    for (const Case& probe : {Case{0, Client::Render::Models::OwnRecordCloseRandoms}, Case{1, 0}})
    {
        SCOPED_TRACE(static_cast<int>(probe.depth));
        WorldState world;
        Client::Scenes::OwnCharacterRecord& own = world.OwnCharacter();
        own.name.fill(std::byte{0x7a});
        own.skin = 5;
        own.characterClass = 9;
        world.Runtime().loadingOnceFlag = 1;
        world.Runtime().ownRecordDepth = probe.depth;
        CharacterEntry chosen;
        chosen.name[0] = std::byte{0x61};
        chosen.name[1] = std::byte{0x62};
        chosen.name[2] = std::byte{0x63};
        chosen.level = 0x0123;
        chosen.classByte = 0x30;
        CrtRandom random(Seed);

        world.PrepareSelectedActor(chosen, random);

        EXPECT_EQ(own.name,
                  (std::array<std::byte, 11>{std::byte{0x61}, std::byte{0x62}, std::byte{0x63}, std::byte{0},
                                             std::byte{0x7a}, std::byte{0x7a}, std::byte{0x7a}, std::byte{0x7a},
                                             std::byte{0x7a}, std::byte{0x7a}, std::byte{0x7a}}));
        EXPECT_EQ(own.level, 0x0123);
        EXPECT_EQ(own.characterClass, Client::Protocol::Character::CharacterClassOf(0x30));
        EXPECT_EQ(own.skin, 0);
        EXPECT_EQ(world.Runtime().loadingOnceFlag, 0);
        EXPECT_EQ(world.Runtime().ownRecordDepth, probe.depth);
        EXPECT_EQ(random.State(), StateAfter(probe.draws));
    }
}

// Un nombre de diez bytes llega sin cero en la lista: el strcpy lee el cero de +0x1e3 y lo escribe en
// el último byte del registro.
TEST(WorldStateTest, AFullNameIsTerminatedInTheOwnRecord)
{
    WorldState world;
    world.OwnCharacter().name.fill(std::byte{0x7a});
    CharacterEntry chosen;
    chosen.name.fill(std::byte{0x6d});
    CrtRandom random(Seed);

    world.PrepareSelectedActor(chosen, random);

    std::array<std::byte, 11> expected{};
    expected.fill(std::byte{0x6d});
    expected[10] = std::byte{0};
    EXPECT_EQ(world.OwnCharacter().name, expected);
}
} // namespace
