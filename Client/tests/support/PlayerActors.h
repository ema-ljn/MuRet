#pragma once

#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemTable.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "support/TableFiles.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <utility>
#include <vector>

// Ayudas para los tests de 0x512a30 y 0x4f2250: un jugador, las acciones de Player.bmd, un Item.bmd
// sintético y semillas del rand() de la CRT que dan el camino que pide un test.
namespace Client::Tests
{
// Las acciones de Player.bmd que mira 0x4ea030: 205, como las que vuelca P-C14 a. Sin keys: estos tests
// no avanzan la animación.
inline constexpr std::size_t PlayerActionCount = 205;

inline Render::Models::Skeleton PlayerActionsSkeleton()
{
    Render::Models::Skeleton skeleton;
    skeleton.actions.resize(PlayerActionCount);
    return skeleton;
}

// Un jugador sin nada en las manos, las alas ni la mascota, fuera de la zona segura.
inline Scenes::ActorState UnarmedPlayer(std::uint8_t characterClass = 0)
{
    Scenes::ActorState actor;
    actor.type = Data::Models::PlayerSkeletonModel;
    actor.characterClass = characterClass;
    actor.hands[0].model = Scenes::NoItemModel;
    actor.hands[1].model = Scenes::NoItemModel;
    actor.wings.model = Scenes::NoItemModel;
    actor.pet.model = Scenes::NoItemModel;
    return actor;
}

// Item.bmd con el byte +0x1e en 1 en el registro de cada modelo de `markedModels` (el tipo es el modelo
// menos 0x212) y todo lo demás en 0.
inline Data::Tables::ItemTable ItemsMarking(std::initializer_list<std::uint16_t> markedModels)
{
    constexpr std::size_t MarkOffset = 0x1e;
    std::vector<std::byte> plain(Data::Tables::ItemTableSize);
    for (const std::uint16_t model : markedModels)
    {
        const auto type = static_cast<std::size_t>(model - Data::Models::FirstItemModel);
        plain[type * Data::Tables::ItemRecordSize + MarkOffset] = std::byte{1};
    }
    const std::vector<std::byte> encrypted = EncryptRecords(std::move(plain), Data::Tables::ItemRecordSize);
    return Data::Tables::ItemTable(std::span<const std::byte>(encrypted));
}

// La primera semilla desde 1 con la que `wanted` da verdadero sobre un CrtRandom recién sembrado.
template <typename Predicate> std::uint32_t FirstSeedWhere(Predicate wanted)
{
    for (std::uint32_t seed = 1;; ++seed)
    {
        Render::Models::CrtRandom random(seed);
        if (wanted(random))
        {
            return seed;
        }
    }
}

// Un CrtRandom sembrado con `seed` después de `draws` rand().
inline Render::Models::CrtRandom AfterDraws(std::uint32_t seed, int draws)
{
    Render::Models::CrtRandom random(seed);
    for (int draw = 0; draw < draws; ++draw)
    {
        static_cast<void>(random.Next());
    }
    return random;
}
} // namespace Client::Tests
