#include "Scenes/ActorActions.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GameData.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Scenes
{
namespace
{
bool IsBow(std::int16_t weapon)
{
    return (weapon >= 0xa1a && weapon < 0xa21) || weapon == 0xa22 || (weapon >= 0xa24 && weapon < 0xc12);
}

Core::Expected<std::uint8_t, std::string> WeaponAction(const ActorState& actor, const GameData& data, std::int32_t map)
{
    const auto right = std::bit_cast<std::int16_t>(actor.hands[0].model);
    const auto left = std::bit_cast<std::int16_t>(actor.hands[1].model);
    const std::uint8_t standing = (actor.appearance.characterClass & 7u) == 2 ? 2 : 1;
    if ((right == -1 && left == -1) || (actor.safeZone && (map < 11 || map > 23)))
        return standing;
    if ((right >= 0x212 && right < 0xa12) || (right >= 0xc12 && right < 0xe12))
    {
        if (right == 0x813 || right == 0x814)
            return 5;
        if (!data.items)
            return Core::Unexpected(std::string("Stop: falta Item.bmd"));
        const auto record = data.items->table.Record(static_cast<std::size_t>(right - 0x212));
        if (!record)
            return Core::Unexpected(std::string("Stop: falta registro de metadata del arma"));
        if ((*record)[0x1e] == std::byte{0})
            return 3;
        if (right < 0x812)
            return right == 0x227 || right == 0x231 || right == 0x229 ? 0x8a : 4;
        return 6;
    }
    if ((left >= 0xa12 && left < 0xa19) || left == 0xa23 || left == 0xa26 || left == 0xa27)
        return 7;
    return IsBow(right) ? std::uint8_t{8} : standing;
}
} // namespace

Core::Expected<void, std::string> SetActorStop(ActorState& actor, WorldState& world, const GameData& data,
                                               const IModelLibrary& models, Render::Models::ClothWorld& cloth)
{
    actor.run = 0;
    if (actor.type != 0x20a)
        return Core::Unexpected(std::string("Stop: falta binding de tipo no020a y tabla de sonidos"));
    const auto right = std::bit_cast<std::int16_t>(actor.hands[0].model);
    const auto left = std::bit_cast<std::int16_t>(actor.hands[1].model);
    std::optional<std::uint8_t> action;
    if (actor.pet.model == 0x1c37 && !actor.safeZone)
    {
        world.Runtime().movementSpeed.itemLatch = 0;
        action = right == -1 ? (left == -1 ? std::uint8_t{0x76} : std::uint8_t{0x79})
                             : (left == -1 ? std::uint8_t{0x78} : std::uint8_t{0x77});
    }
    else if (actor.pet.model == 0x1c16 && !actor.safeZone)
        action = std::uint8_t{0x4a};
    else if (actor.safeZone && left == 0x1c17)
        action = std::uint8_t{0x48};
    else if ((actor.pet.model == 0x1c14 || actor.pet.model == 0x1c15) && !actor.safeZone)
        action = right == -1 && left == -1 ? std::uint8_t{0xb} : std::uint8_t{0xc};
    else
    {
        bool flying = !actor.safeZone && actor.wings.model != 0xffff;
        const std::int32_t map = world.Runtime().map;
        if (map == 7 || (map >= 24 && map <= 29) || map == 36)
        {
            if (!world.Runtime().stopTileFlags)
                return Core::Unexpected(std::string("Stop: falta resultado5f82a0/flags de tile para mapa de vuelo"));
            flying = flying || ((*world.Runtime().stopTileFlags & 1u) != 1u);
        }
        flying = flying || actor.state376 == 0x101;
        if ((actor.characterFlags & 0x200000u) != 0)
        {
            if (actor.animation.body.action != 0xca)
                action = std::uint8_t{0xca};
        }
        else if (flying)
            action = IsBow(right) ? std::uint8_t{0xa} : std::uint8_t{9};
        else
        {
            auto chosen = WeaponAction(actor, data, map);
            if (!chosen.HasValue())
                return Core::Unexpected(chosen.Error());
            action = chosen.Value();
        }
    }
    if (action)
    {
        const auto* skeleton = models.SkeletonOf(actor.type);
        if (skeleton == nullptr)
            return Core::Unexpected(std::string("Stop: falta esqueleto del actor"));
        Render::Models::SetAction(actor.animation.body, *skeleton, *action);
        if (*action == 0xca)
            return Core::Unexpected(std::string("Stop: falta envío18 de estado especial"));
    }
    // Sufijo T: no se quema un presupuesto total; cada rama consume su sorteo al alcanzarse.
    if (cloth.random.Next() % 16 != 0)
        return {};
    if (actor.subtype >= 209 && actor.subtype <= 211)
        return Core::Unexpected(std::string("Stop: falta tabla de efectos05769ad8 para subtype209..211"));
    if (actor.pet.model != 0x1c37)
        return {};
    if (cloth.random.Next() % 3 != 0)
        return {};
    const int effect = 0x279 + (cloth.random.Next() % 2);
    world.Runtime().lastSoundId = effect;
    ++world.Runtime().soundRequestCount;
    return {}; // Audio digital se integra enCL10; el disparador y sus RNG quedan observables.
}
Core::Expected<void, std::string> SetActorWalk(ActorState& actor, WorldState& world, const GameData& data,
                                               const IModelLibrary& models, Render::Models::ClothWorld& cloth)
{
    auto& runtime = world.Runtime();
    const auto map = runtime.map;
    const auto characterClass = static_cast<std::uint8_t>(actor.appearance.characterClass & 7u);
    const bool flightMap = map == 7 || (map >= 24 && map <= 29) || map == 36;
    if (actor.safeZone != 0)
        actor.run = 0;
    else if (actor.run < 40)
    {
        const auto& equipment = actor.body[flightMap ? 3 : 4];
        if (characterClass == 3 || characterClass == 4 || (equipment.model != 0xffff && equipment.level >= 5) ||
            actor.pet.model == 0x1c37)
        {
            if (actor.pet.model == 0x1c37)
                ++runtime.movementSpeed.itemCounterBits;
            else
                ++actor.run;
        }
    }
    std::optional<std::uint8_t> action;
    const auto right = std::bit_cast<std::int16_t>(actor.hands[0].model);
    const auto left = std::bit_cast<std::int16_t>(actor.hands[1].model);
    if (actor.type != 0x20a)
        action = std::uint8_t{2};
    else
    {
        const float first = std::bit_cast<float>(0x3ea8f5c3u);
        const float second = std::bit_cast<float>(0x3eae147bu);
        const float factor = (actor.objectFlags & 2u) != 0 ? 0.5f : 1.0f;
        for (std::size_t i = 0xd; i <= 0x14; ++i)
            runtime.playerActionSpeeds[i] = first * factor;
        for (std::size_t i = 0x16; i <= 0x21; ++i)
            runtime.playerActionSpeeds[i] = second * factor;
        runtime.playerActionSpeeds[0x4b] = first * factor;
        runtime.playerActionSpeeds[0x49] = first * factor;
        if (actor.pet.model == 0x1c37 && actor.safeZone == 0)
            return Core::Unexpected(std::string("Walk: faltan acciones4e9f50/4e9da0 deFenrir"));
        if (actor.pet.model == 0x1c16 && actor.safeZone == 0)
            action = std::uint8_t{0x4b};
        else if (left == 0x1c17 && actor.safeZone != 0)
            action = std::uint8_t{0x49};
        else if ((actor.pet.model == 0x1c14 || actor.pet.model == 0x1c15) && actor.safeZone == 0)
        {
            if (actor.pet.model != 0x1c15 || (map != 8 && map != 10))
                action = right == -1 && left == -1 ? std::uint8_t{0x20} : std::uint8_t{0x21};
        }
        else if (actor.safeZone == 0 && actor.wings.model != 0xffff)
            action = IsBow(right) ? std::uint8_t{0x1f} : std::uint8_t{0x1e};
        else if (actor.safeZone == 0 && flightMap)
            action = actor.run >= 40 ? std::uint8_t{0x1d} : std::uint8_t{0x15};
        else if ((right == -1 && left == -1) || (actor.safeZone != 0 && (map < 11 || map > 23)))
            action =
                actor.run >= 40 ? std::uint8_t{0x16} : (characterClass == 2 ? std::uint8_t{0xe} : std::uint8_t{0xd});
        else
        {
            const bool high = actor.run >= 40;
            const auto metadataFlag = [&]() -> Core::Expected<bool, std::string>
            {
                if (!data.items)
                    return Core::Unexpected(std::string("Walk: falta Item.bmd"));
                const auto record = data.items->table.Record(static_cast<std::size_t>(right - 0x212));
                if (!record)
                    return Core::Unexpected(std::string("Walk: falta registro de metadata del arma"));
                return (*record)[0x1e] != std::byte{0};
            };
            if (right >= 0x212 && right < 0x812)
            {
                if (high && left >= 0x212 && left < 0x812)
                    action = std::uint8_t{0x18};
                else
                {
                    const auto flagged = metadataFlag();
                    if (!flagged.HasValue())
                        return Core::Unexpected(flagged.Error());
                    if (!flagged.Value())
                        action = high ? std::uint8_t{0x17} : std::uint8_t{0xf};
                    else if (right == 0x227 || right == 0x231 || right == 0x229)
                        action = high ? std::uint8_t{0x8c} : std::uint8_t{0x8b};
                    else
                        action = high ? std::uint8_t{0x19} : std::uint8_t{0x10};
                }
            }
            else if (right >= 0xc12 && right < 0xe12)
            {
                const auto flagged = metadataFlag();
                if (!flagged.HasValue())
                    return Core::Unexpected(flagged.Error());
                action = flagged.Value() ? (high ? std::uint8_t{0x1a} : std::uint8_t{0x12})
                                         : (high ? std::uint8_t{0x17} : std::uint8_t{0xf});
            }
            else if (!high && (right == 0x813 || right == 0xa14))
                action = std::uint8_t{0x11};
            else if (right >= 0x812 && right < 0xa12)
                action = high ? std::uint8_t{0x1a} : std::uint8_t{0x12};
            else if ((left >= 0xa12 && left < 0xa19) || left == 0xa23 || left == 0xa26 || left == 0xa27)
                action = high ? std::uint8_t{0x1b} : std::uint8_t{0x13};
            else if (IsBow(right))
                action = high ? std::uint8_t{0x1c} : std::uint8_t{0x14};
            else
                action = high ? std::uint8_t{0x16} : (characterClass == 2 ? std::uint8_t{0xe} : std::uint8_t{0xd});
        }
    }
    if (action)
    {
        const auto* skeleton = models.SkeletonOf(actor.type);
        if (skeleton == nullptr)
            return Core::Unexpected(std::string("Walk: falta esqueleto del actor"));
        Render::Models::SetAction(actor.animation.body, *skeleton, *action);
    }
    const auto emit = [&](std::int32_t id)
    {
        runtime.lastSoundId = id;
        ++runtime.soundRequestCount;
    };
    if (actor.type == 0x16d)
    {
        emit(0x7c);
        return {};
    }
    if (characterClass == 4 && actor.pet.model == 0x1c16 && actor.safeZone == 0)
    {
        emit(0x1c3 + (cloth.random.Next() % 3));
        return {};
    }
    const int actual = actor.animation.body.action;
    if (actor.pet.model == 0x1c37 && actor.safeZone == 0)
    {
        if (actual >= 0x6a && actual <= 0x75)
        {
            emit(0x273 + (cloth.random.Next() % 3));
            return {};
        }
        if (actual >= 0x7a && actual <= 0x7d)
        {
            emit(0x273 + (cloth.random.Next() % 2));
            return {};
        }
    }
    const bool hero = &actor == &world.Actors()[runtime.hero];
    if (cloth.random.Next() % (hero ? 64 : 16) != 0)
        return {};
    if (actor.type == 0x20a && (actor.subtype < 209 || actor.subtype > 211))
        return {};
    if (actor.subtype >= 209 && actor.subtype <= 211)
        emit(0x7b);
    auto sound = runtime.modelSoundIds.find(actor.type);
    if (sound == runtime.modelSoundIds.end())
        return Core::Unexpected(std::string("Walk: falta metadata de sonidos05769ad8"));
    if (sound->second[0] == -1)
        return {};
    const int extra = actor.subtype == 9 ? 5 : 0;
    const int selection = cloth.random.Next() % 2;
    sound = runtime.modelSoundIds.find(actor.type);
    if (sound == runtime.modelSoundIds.end())
        return Core::Unexpected(std::string("Walk: desapareció metadata de sonidos05769ad8"));
    emit(sound->second[static_cast<std::size_t>(selection)] + 0xd2 + extra);
    return {};
}

} // namespace Client::Scenes
