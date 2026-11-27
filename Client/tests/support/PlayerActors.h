#pragma once

#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemTable.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
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

inline Models::Skeleton PlayerActionsSkeleton()
{
    Models::Skeleton skeleton;
    skeleton.actions.resize(PlayerActionCount);
    return skeleton;
}

// Un jugador sin nada en las manos, las alas ni la mascota, fuera de la zona segura.
inline Scenes::ActorState UnarmedPlayer(std::uint8_t characterClass = 0)
{
    Scenes::ActorState actor;
    actor.type = Tables::PlayerSkeletonModel;
    actor.characterClass = characterClass;
    actor.hands[0].model = Scenes::NoItemModel;
    actor.hands[1].model = Scenes::NoItemModel;
    actor.wings.model = Scenes::NoItemModel;
    actor.pet.model = Scenes::NoItemModel;
    // 0x511290: sin gremio (+0x1f2 en −1) y +0x376 en 0xffff (0x511d72), el de un jugador sin transformar.
    actor.word1f2 = Scenes::NoItemModel;
    actor.word376 = 0xffff;
    return actor;
}

// Item.bmd con el byte +0x1e en 1 en el registro de cada modelo de `markedModels` (el tipo es el modelo
// menos 0x212) y todo lo demás en 0.
inline Tables::ItemTable ItemsMarking(std::initializer_list<std::uint16_t> markedModels)
{
    constexpr std::size_t MarkOffset = 0x1e;
    std::vector<std::byte> plain(Tables::ItemTableSize);
    for (const std::uint16_t model : markedModels)
    {
        const auto type = static_cast<std::size_t>(model - Tables::FirstItemModel);
        plain[type * Tables::ItemRecordSize + MarkOffset] = std::byte{1};
    }
    const std::vector<std::byte> encrypted = EncryptRecords(std::move(plain), Tables::ItemRecordSize);
    return Tables::ItemTable(std::span<const std::byte>(encrypted));
}

// La primera semilla desde 1 con la que `wanted` da verdadero sobre un CrtRandom recién sembrado.
template <typename Predicate> std::uint32_t FirstSeedWhere(Predicate wanted)
{
    for (std::uint32_t seed = 1;; ++seed)
    {
        Crt::CrtRandom random(seed);
        if (wanted(random))
        {
            return seed;
        }
    }
}

// Un CrtRandom sembrado con `seed` después de `draws` rand().
inline Crt::CrtRandom AfterDraws(std::uint32_t seed, int draws)
{
    Crt::CrtRandom random(seed);
    for (int draw = 0; draw < draws; ++draw)
    {
        static_cast<void>(random.Next());
    }
    return random;
}
} // namespace Client::Tests
