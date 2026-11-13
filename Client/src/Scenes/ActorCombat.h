#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"

#include <string>

namespace Client::Render::Models
{
class CrtRandom;
struct Skeleton;
} // namespace Client::Render::Models
namespace Client::Scenes
{
class IModelLibrary;
class WorldState;
struct ActorState;
struct WorldEntryResources;

// Lo que leen 0x4f4a10 y 0x4f5660 (CL9 hito 3): el mundo (el héroe de 0x743a170, el registro propio, el mapa y el
// último sonido pedido), el rand() de la CRT, los esqueletos para 0x4ea030 (Player.bmd aparte, como lo tiene MoveHero)
// e Item.bmd para la marca +0x1e del arma.
struct ActorCombatContext
{
    WorldState& world;
    Render::Models::CrtRandom& random;
    const Render::Models::Skeleton* playerSkeleton = nullptr;
    const IModelLibrary* models = nullptr;
    const Data::Tables::ItemTable* items = nullptr;
};
[[nodiscard]] ActorCombatContext CombatContextOf(const WorldEntryResources& resources);

// 0x4f4a10, SetPlayerAttack: la acción de pegar según el modelo, las armas y la montura, el sonido con el frame en 0 y
// +0x38f + 1. La llaman el ataque del héroe al llegar (0x5650c0) y las acciones 0x78/0x79 del 0x18 (0x4cb750, hito
// 1). Corta en lo que es de otra ola o de otro hito, con su dirección.
[[nodiscard]] Core::Expected<void, std::string> SetActorAttack(ActorState& actor, const ActorCombatContext& context);

// 0x4f5660, SetPlayerDie: la vida propia en 0 si es el héroe, la acción de morir (0xc3 o 6) y el sonido con el frame
// en 0, que después queda en 0,001.
[[nodiscard]] Core::Expected<void, std::string> SetActorDie(ActorState& actor, const ActorCombatContext& context);
} // namespace Client::Scenes
