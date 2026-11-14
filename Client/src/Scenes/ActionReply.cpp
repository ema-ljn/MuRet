#include "Scenes/ActionReply.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Protocol/Movement.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActionSend.h"
#include "Scenes/ActorActions.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/MonsterCreation.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace Client::Scenes
{
namespace
{
// 0x5110d0 sin ninguno: el lugar de después de los 400.
constexpr std::size_t NoActor = 400;
// 0x4cb781: el caballo oscuro en +0x318.
constexpr std::uint16_t DarkHorsePet = 0x1c16;
// 0x4cb7b9..0x4cb7fa: (pkt[5] − 1.0) · 45.0 (0x6b87d4 y 0x6b8848) y la celda · 100.0 + 50.0 (0x6b86b4 y 0x6b86b0).
constexpr double DirectionBase = 1.0;
constexpr double DegreesPerDirection = 45.0;
constexpr double CellSize = 100.0;
constexpr double HalfCell = 50.0;
// 0x4cb853..0x4cb864: la acción 0x12 pone la 0x98 y pide el sonido 0x6f.
constexpr std::uint8_t ShieldAction = 0x12;
constexpr std::uint8_t ShieldAnimation = 0x98;
constexpr std::int32_t ShieldSound = 0x6f;
// 0x4cb820: 0x78 y 0x79 van a 0x4f4a10.
constexpr std::uint8_t FirstStrikeAction = 0x78;
constexpr std::uint8_t LastStrikeAction = 0x79;
// 0x4cb812: 0x7a y 0x7b van a 0x4f2250.
constexpr std::uint8_t FirstStopAction = 0x7a;
constexpr std::uint8_t LastStopAction = 0x7b;
// 0x4cbc92: la 0x7e pone la 5.
constexpr std::uint8_t SalutedAction = 0x7e;
constexpr std::uint8_t SalutedAnimation = 5;
// 0x4cb871..0x4cbcc8: de 0x80 a 0x9b, la animación de cada acción y la del elfo (+0x1d4 & 7 == 2).
constexpr std::uint8_t FirstTableAction = 0x80;
constexpr std::uint8_t LastTableAction = 0x9b;
constexpr std::uint8_t ClassMask = 7;
constexpr std::uint8_t ElfClass = 2;

struct TableAnimation
{
    std::uint8_t other = 0;
    std::uint8_t elf = 0;
};

constexpr TableAnimation Both(std::uint8_t animation)
{
    return TableAnimation{animation, animation};
}

// Por acción desde 0x80, con el bloque de cada una (las de 0x92 en adelante no miran la clase).
constexpr std::array<TableAnimation, LastTableAction - FirstTableAction + 1> Animations{{
    {0xc5, 0xc7}, // 0x80, 0x4cb871
    {0xcb, 0xcc}, // 0x81, 0x4cb8a3
    {0xc9, 0xca}, // 0x82, 0x4cb8d7
    {0x99, 0x9a}, // 0x83, 0x4cb90b
    {0x9b, 0x9c}, // 0x84, 0x4cb93d
    {0x9d, 0x9e}, // 0x85, 0x4cb971
    {0xa3, 0xa4}, // 0x86, 0x4cb9a5
    {0xa1, 0xa2}, // 0x87, 0x4cb9d7
    {0xa5, 0xa6}, // 0x88, 0x4cba0b
    {0xa7, 0xa8}, // 0x89, 0x4cba3f
    {0x9f, 0xa0}, // 0x8a, 0x4cbad9
    {0xa9, 0xaa}, // 0x8b, 0x4cba71
    {0xab, 0xac}, // 0x8c, 0x4cbaa5
    {0xad, 0xae}, // 0x8d, 0x4cbb0b
    {0xaf, 0xb0}, // 0x8e, 0x4cbb3f
    {0xb1, 0xb2}, // 0x8f, 0x4cbb73
    {0xb3, 0xb4}, // 0x90, 0x4cbba5
    {0xb5, 0xb6}, // 0x91, 0x4cbbd9
    Both(0xb7),   // 0x92, 0x4cbc0d
    Both(0xb8),   // 0x93, 0x4cbc20
    Both(0xc0),   // 0x94, 0x4cbc33
    Both(0xb9),   // 0x95, 0x4cbc46
    Both(0xba),   // 0x96, 0x4cbc59
    Both(0xbb),   // 0x97, 0x4cbc6c
    Both(0xbc),   // 0x98, 0x4cbc7f
    Both(0xbd),   // 0x99, 0x4cbca2
    Both(0xbe),   // 0x9a, 0x4cbcb5
    Both(0xbf),   // 0x9b, 0x4cbcc8
}};

// La animación que 0x4cb750 le pasa a 0x4ea030 (0x4cbcdb: cualquier otra, la acción misma).
std::uint8_t AnimationOf(const ActorState& actor, std::uint8_t action)
{
    if (action == ShieldAction)
        return ShieldAnimation;
    if (action == SalutedAction)
        return SalutedAnimation;
    if (action < FirstTableAction || action > LastTableAction)
        return action;
    const TableAnimation& animation = Animations[static_cast<std::size_t>(action - FirstTableAction)];
    return (actor.characterClass & ClassMask) == ElfClass ? animation.elf : animation.other;
}

Core::Expected<void, std::string> StopTheActor(ActorState& actor, const WorldEntryResources& resources,
                                               const ActionSender& sendAction)
{
    const auto* player = resources.models.SkeletonOf(Data::Models::PlayerSkeletonModel);
    if (player == nullptr)
        return Core::Unexpected(std::string("0x4cb813: falta Player.bmd para 0x4f2250"));
    const ActorStopContext context{
        .runtime = resources.world.Runtime(),
        .scene = SceneId::Game,
        .playerSkeleton = *player,
        .items = resources.items,
        .random = resources.cloth.random,
        .terrainWalls =
            resources.world.Map().terrain.attributes ? &resources.world.Map().terrain.attributes->cells : nullptr,
        .sendAction = sendAction,
    };
    const auto stopped = SetActorStop(actor, context);
    if (!stopped.HasValue())
        return Core::Unexpected(stopped.Error());
    if (stopped.Value().sound)
        resources.world.Runtime().lastSoundId = stopped.Value().sound;
    return {};
}
} // namespace

Core::Expected<void, std::string> ApplyActionReply(const Protocol::Movement::Action& reply,
                                                   const WorldEntryResources& resources, const ActionSender& sendAction)
{
    // 0x4cb769: sin nadie con la clave, el original escribe en el lugar 400, fantasma (Q-C778); MuRet lo descarta.
    const std::size_t index = ActorWithKey(resources.world, reply.key);
    if (index == NoActor)
        return {};
    ActorState& actor = resources.world.Actors()[index];
    if (actor.pet.model == DarkHorsePet)
        return {};
    // 0x4cb794..0x4cb7fa: el ángulo, el Movement en 0 y la posición en el centro de la celda blanco (Q-C779). Todo en
    // la x87 y guardado en float; con bytes las cuentas son exactas.
    actor.angles.z = static_cast<float>((static_cast<double>(reply.direction) - DirectionBase) * DegreesPerDirection);
    actor.movement = 0;
    actor.position.x = static_cast<float>(static_cast<double>(actor.targetX) * CellSize + HalfCell);
    actor.position.y = static_cast<float>(static_cast<double>(actor.targetY) * CellSize + HalfCell);
    const std::uint8_t action = reply.action;
    if (action >= FirstStopAction && action <= LastStopAction)
        return StopTheActor(actor, resources, sendAction);
    if (action >= FirstStrikeAction && action <= LastStrikeAction)
        return Core::Unexpected(std::format(
            "0x4cb821: la acción {:#x} de otro va a 0x4f4a10 (sin leer, con sus rand(): ocho llamadas a 0x69fd70, una "
            "en la rama del jugador en 0x4f4db9), que es del hito 3 de la CL9; después pone +0x381 en 1, el frame "
            "(+0x114) en 0, +0x3a4 con [0x57678b0] y [0x5767964] con el índice {}",
            action, index));
    // 0x4ea030 lee las acciones del modelo del personaje (0x5769ad8 + tipo · 0xe0): un monstruo o un NPC, el que
    // cargó su alta; un modelo que no está es el registro sin acciones y no pone nada (0x4ea056). Sin Player.bmd, el
    // jugador es una guarda de contrato.
    if (actor.type == Data::Models::PlayerSkeletonModel)
    {
        const auto* player = resources.models.SkeletonOf(Data::Models::PlayerSkeletonModel);
        if (player == nullptr)
            return Core::Unexpected(std::format("0x4ea030: la acción {:#x} sin Player.bmd", action));
        Render::Models::SetAction(actor.animation, *player, AnimationOf(actor, action));
    }
    else if (auto set = SetMonsterAction(actor, AnimationOf(actor, action), 0x4cbcdd, resources); !set.HasValue())
    {
        return set;
    }
    // 0x4cb864: 0x41ed10(0x6f, 0, 0). MuRet no tiene audio hasta la CL10.
    if (action == ShieldAction)
        resources.world.Runtime().lastSoundId = ShieldSound;
    return {};
}
} // namespace Client::Scenes
