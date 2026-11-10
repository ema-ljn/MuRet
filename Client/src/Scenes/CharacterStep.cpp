#include "Scenes/CharacterStep.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorActions.h"
#include "Scenes/ActorState.h"
#include "Scenes/ItemSetOptionList.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnStatHelpers.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/ObjectAlpha.h"
#include "World/TerrainCulling.h"
#include "World/TerrainWalls.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace Client::Scenes
{
namespace
{
// 0x50919a y 0x5091c8: los dos lazos de 0x508f00 recorren 400 registros (0x69780 / 0x438); el 401 no.
constexpr std::size_t SteppedCharacters = 400;
// 0x412162: en el mapa 30 (0x1e), 0x412140 arma los objetos del asedio.
constexpr std::int32_t CastleMap = 30;

// 0x4fd603: el tipo que pasa su nombre a 0x4381f0.
constexpr std::uint16_t NamedType = 0xc5;
// 0x4fbe2e..0x4fbe41: 0x4fbe10 corre solo en los mapas 18 a 23.
constexpr std::int32_t FirstCellReportMap = 18;
constexpr std::int32_t LastCellReportMap = 23;
// 0x4fb910 y 0x505425: el tipo que cae y gira en 0x4fb380 y que después sigue 0x52f360.
constexpr std::uint16_t FallingType = 0xf1;
// 0x4fcd0c y 0x4fc797: con +0x88 en 4, 0x4fcd00 gira a unos 0x20a y 0x4fc790 sortea un mensaje.
constexpr std::uint8_t MessageKind = 4;
// 0x6c88b0: los 11 bytes EUC-KR que 0x4fcda2 compara con el nombre (+0x1d9), con strcmp.
constexpr std::array<std::uint8_t, 11> MessageName{0xc8, 0xb2, 0xb1, 0xdd, 0xc0, 0xc7, 0x20, 0xb1, 0xc3, 0xbc, 0xf6};
// 0x4f9580 con el 0x20a: por 1,5 (0x6b8b94) en las acciones 3 y 4 con +0x3e3; por 0,5 (0x6b882c) en la 0x3f
// pasado el frame 6 (0x6b87f4) y en la 0x44; por 0,1f (0x6b8a34) en las 0x4d, 0x4f y 0x5a pasado el 5,5
// (0x6b8a40); en la clase 4, las 0x94, 0x51 y 0x5b entre el frame 1 (0x6b87d4) y el 3 (0x6b86b8) llaman a
// 0x59e680.
constexpr float FasterFactor = 1.5f;
constexpr float HalfFactor = 0.5f;
constexpr float TenthFactor = 0.1f;
constexpr std::uint8_t FirstFasterAction = 3;
constexpr std::uint8_t LastFasterAction = 4;
constexpr std::uint8_t SlowedAction = 0x3f;
constexpr float SlowedFrame = 6.0f;
constexpr std::array<std::uint8_t, 3> ParticleActions{0x4d, 0x4f, 0x5a};
constexpr float ParticleFrame = 5.5f;
constexpr std::uint8_t ClassMask = 7;
constexpr std::uint8_t ClassFour = 4;
constexpr std::array<std::uint8_t, 3> ClassFourActions{0x94, 0x51, 0x5b};
constexpr float ClassFourFirstFrame = 1.0f;
constexpr float ClassFourLastFrame = 3.0f;
constexpr std::uint8_t HalvedAction = 0x44;
// 0x4f973a: con +0x78 & 0x1000 la animación no avanza y 0x4f9580 devuelve 0, "terminó".
constexpr std::uint32_t FrozenFlag = 0x1000;
// 0x4fcb10: lo que hace con la acción que terminó.
constexpr std::uint8_t FirstMarkAction = 0xc3;
constexpr std::uint8_t SecondMarkAction = 0xc4;
constexpr std::uint8_t FirstStoppedAfter = 0x0d;
constexpr std::uint8_t FirstSpecialStop = 0xbd;
constexpr std::uint8_t LastSpecialStop = 0xbf;
constexpr std::uint8_t FirstCombatStop = 0x54;
constexpr std::uint8_t LastCombatStop = 0x55;
constexpr std::uint8_t FirstKeptAction = 0x22;
constexpr std::uint8_t LastKeptAction = 0xc2;
constexpr std::array<std::uint8_t, 7> KeptActions{0x8b, 0x8c, 0x46, 0x47, 0x44, 0x49, 0x4b};
constexpr std::uint8_t FirstKeptRange = 0x6a;
constexpr std::uint8_t LastKeptRange = 0x75;
// 0x4fceea..0x4fcf04: antes del frame 3 (0x6b86b8), salvo en la 0x8d, 0x4fcee0 sale sin estela.
constexpr float TrailFrame = 3.0f;
constexpr std::uint8_t AlwaysTrailedAction = 0x8d;
// 0x4fcf3e..0x4fcf4a: con las acciones 0x25 y 0x26, la estela mira la mano izquierda.
constexpr std::uint8_t FirstLeftHandAction = 0x25;
constexpr std::uint8_t SecondLeftHandAction = 0x26;
// 0x4fcf87..0x4fd082: las acciones y los modelos del 0x20a que dejan estela (0x52e470).
constexpr std::array<std::uint8_t, 3> TrailedActions{0x84, 0x85, 0x4c};
constexpr std::uint8_t FirstSwingAction = 0x39;
constexpr std::uint8_t LastSwingAction = 0x3c;
constexpr int FirstSwordModel = 0x212;
constexpr int EndOfSwordModels = 0x412;
constexpr std::uint8_t FirstSwordAction = 0x23;
constexpr std::uint8_t LastSwordAction = 0x29;
constexpr int SpearModel = 0x415;
constexpr int FirstPoleModel = 0x417;
constexpr int EndOfPoleModels = 0x812;
constexpr std::uint8_t FirstPoleAction = 0x38;
constexpr std::uint8_t LastPoleAction = 0x3c;
constexpr int EndOfBowModels = 0xa12;
constexpr std::uint8_t FirstBowAction = 0x2a;
constexpr std::uint8_t LastBowAction = 0x2d;

// 0x4fc57e, 0x4fc5b0 y 0x4fc606: el bit de +0x3c que 0x4fc450 apaga con cada WORD en 0.
constexpr std::uint8_t Word3eBit = 0x01;
constexpr std::uint8_t Word40Bit = 0x02;
constexpr std::uint8_t Word42Bit = 0x08;

// 0x506acf: con +0x375 en 6 o más (sin signo), las manos no dan luz.
constexpr std::uint8_t FirstPkWithoutLights = 6;
// 0x506aec..0x506b26: los modelos de las manos que piden una luz a 0x5f9780.
constexpr std::array<std::uint16_t, 5> LightModels{0x21e, 0x225, 0xa24, 0xc1c, 0x61f};
// 0x506d01 y 0x506d8b: los mapas del caso del 0x20a con efectos.
constexpr std::int32_t ClockEffectMap = 7;
constexpr std::int32_t RandomEffectMap = 9;
// 0x506e5d..0x506e63 y 0x506f62: las acciones del 0x20a con efectos.
constexpr std::uint8_t FirstEffectAction = 0x44;
constexpr std::uint8_t SecondEffectAction = 0x45;
constexpr std::uint8_t BoneEffectAction = 0x96;
// 0x508654..0x508666: las acciones con los pasos del héroe.
constexpr std::uint8_t FirstStepAction = 0x0d;
constexpr std::uint8_t LastStepAction = 0x21;
constexpr std::array<std::uint8_t, 3> OtherStepActions{0x8b, 0x8c, 0x4b};
// 0x508e79: el tipo que, muriéndose, 0x508ba0 baja a la altura del terreno.
constexpr std::uint16_t SinkingDeadType = 0x154;
// 0x508be3: el tipo con el que 0x508ba0 no busca camino.
constexpr std::uint16_t PathlessType = 0x159;

std::string MapText(std::int32_t map)
{
    return "mapa " + std::to_string(map);
}

template <std::size_t N> bool IsOneOf(const std::array<std::uint8_t, N>& actions, std::uint8_t action)
{
    return std::ranges::find(actions, action) != actions.end();
}

bool HasStepSounds(std::uint8_t action)
{
    return (action >= FirstStepAction && action <= LastStepAction) || IsOneOf(OtherStepActions, action);
}

void ClearBit(std::uint8_t& bits, std::uint8_t bit)
{
    bits = static_cast<std::uint8_t>(bits & ~bit);
}

// 0x508ba0(c, c) desde 0x508ed8 con otro que el héroe publicado ([0x743a170]; con él sale en 0x508bad).
Core::Expected<void, std::string> StepOtherCharacter(ActorState& actor, std::size_t slot,
                                                     Render::Models::CrtRandom& random)
{
    // 0x508bb6: muriéndose (+0x389), sale; el 0x154 antes toma la altura del terreno (0x508e76..0x508e8e).
    if (actor.dying != 0)
    {
        if (actor.type == SinkingDeadType)
        {
            return Core::Unexpected(std::format("0x508e89: el {:#x} del lugar {} se muere y toma la altura "
                                                "de 0x5f93c0, que no está portada",
                                                actor.type, slot));
        }
        return {};
    }
    // 0x508bc4: caminando (+0x378), 0x508e26..0x508e6e sigue el camino (0x4f37c0, 0x4ea270, 0x4f2250 y 0x508a80).
    if (actor.movement != 0)
    {
        return Core::Unexpected(std::format("0x508e27: el personaje del lugar {} camina (+0x378); 0x4f37c0 y 0x508a80 "
                                            "no están portados",
                                            slot));
    }
    // 0x508bd2: +0x3e5 en 0, sin salir: solo lo sube 0x4c9790 (0x4c982b), un paquete que MuRet no aplica
    // (*estático*). 0x508be3: el 0x159 sale sin leer la celda.
    if (actor.type == PathlessType)
    {
        return {};
    }
    // 0x508bef..0x508c75: la X lógica (+0x41c) en un acceso que se cierra con 0x412890, contra la copia en
    // claro (+0x39a); 0x508c86..0x508ce9: la Y (+0x420), con 0x40b800, contra +0x39b. Con las dos iguales sale
    // en 0x508cfb, con 8 rand() si los dos cierres dejan su cuenta en 0 (Q-C236). MuRet guarda una sola copia de
    // cada una (0x511290 las escribe iguales): siempre coinciden, y el camino de 0x508d01..0x508e1a (0x4eac00 y
    // +0x378 en 1) no se alcanza.
    {
        const Render::Models::LogicalAccess access(actor.cellXAccesses, random,
                                                   Render::Models::SmallLogicalCloseRandoms);
    }
    {
        const Render::Models::LogicalAccess access(actor.cellYAccesses, random,
                                                   Render::Models::SmallLogicalCloseRandoms);
    }
    return {};
}

// 0x4fc450(c) desde 0x4fd6fe, con el héroe publicado ([0x743a170]; con otro sale en 0x4fc464): los tres WORD
// de [0x7681eb0], con el registro propio abierto (Q-C246).
Core::Expected<void, std::string> StepTheOwnTimers(const CharacterStepContext& context)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    OwnCharacterRecord& own = world.OwnCharacter();
    ItemSetOptionList& setOptions = world.ItemSets().list;
    // El ítem de 0x7879f80 no reemplaza nada: [0x7879fbc] está en 0 (OwnEquipmentOverride).
    const OwnEquipmentOverride held{};
    // 0x4fc46a..0x4fc556: abre el registro propio en línea; 0x4fc617..0x4fc767 lo cierra en línea, con los
    // 3.848 rand() de 0x40b4d0 si la cuenta queda en 0.
    const Render::Models::LogicalAccess access(runtime.ownRecordDepth, context.random,
                                               Render::Models::OwnRecordCloseRandoms);
    // 0x4fc55b..0x4fc58a: +0x3e baja si es positivo y, en 0, apaga el bit 0x01 de +0x3c y llama a 0x54fe10.
    if (own.word3e > 0)
    {
        --own.word3e;
    }
    if (own.word3e == 0)
    {
        ClearBit(own.speedAdjustBits, Word3eBit);
        if (auto updated = UpdateOwnAttackAndMagicSpeeds(own, held, runtime.ownRecordDepth, context.random);
            !updated.HasValue())
        {
            return updated;
        }
    }
    // 0x4fc594..0x4fc5e0: +0x40, el bit 0x02, las 16 marcas de la lista ([0x6eed3c] + 0x937c) en 0, 0x54efe0
    // y 0x54f9b0.
    if (own.word40 > 0)
    {
        --own.word40;
    }
    if (own.word40 == 0)
    {
        ClearBit(own.speedAdjustBits, Word40Bit);
        setOptions.used = {};
        if (auto updated = UpdateOwnWords50To56(own, held, context.items, &setOptions); !updated.HasValue())
        {
            return updated;
        }
        if (auto updated = UpdateOwnWords5aAnd5c(own, held, context.items, &setOptions); !updated.HasValue())
        {
            return updated;
        }
    }
    // 0x4fc5ea..0x4fc612: +0x42, el bit 0x08 y 0x54fe10 otra vez.
    if (own.word42 > 0)
    {
        --own.word42;
    }
    if (own.word42 == 0)
    {
        ClearBit(own.speedAdjustBits, Word42Bit);
        if (auto updated = UpdateOwnAttackAndMagicSpeeds(own, held, runtime.ownRecordDepth, context.random);
            !updated.HasValue())
        {
            return updated;
        }
    }
    return {};
}

// strcmp(+0x1d9, 0x6c88b0) en 0x4fcda2: los 10 bytes del nombre y +0x1e3 contra los 11 primeros. Con los
// 11 iguales el resultado depende de +0x1e4, que MuRet no tiene: cuenta como igual.
bool HasTheMessageName(const ActorState& actor)
{
    for (std::size_t i = 0; i < actor.name.size(); ++i)
    {
        if (std::to_integer<std::uint8_t>(actor.name[i]) != MessageName[i])
        {
            return false;
        }
    }
    return actor.byte1e3 == MessageName[actor.name.size()];
}

// 0x4f9580(c, c): la velocidad de la acción, sus factores y 0x4ec200. Devuelve si terminó, lo que 0x4f9580
// devuelve en 0.
Core::Expected<bool, std::string> AdvanceTheBody(ActorState& actor, std::size_t slot,
                                                 const CharacterStepContext& context)
{
    // 0x4f958e..0x4f95a5: el modelo compartido del tipo. MuRet tiene solo Player.bmd.
    if (actor.type != Data::Models::PlayerSkeletonModel)
    {
        return Core::Unexpected(std::format("0x4f9580: el modelo {:#x} del lugar {} (0x5769ad8 + tipo · 0xe0) no está "
                                            "leído, solo Player.bmd (0x20a)",
                                            actor.type, slot));
    }
    const Render::Models::Skeleton* skeleton = context.playerSkeleton;
    if (skeleton == nullptr)
    {
        return Core::Unexpected(std::format("0x4f9580: Player.bmd, el modelo del lugar {}, no está cargado", slot));
    }
    const bool frozen = (actor.flags78 & FrozenFlag) == FrozenFlag;
    // 0x4f95a7: sin acciones (+0x26 en 0) la velocidad queda en 0 y 0x4ec200 sale sin avanzar (0x4ec214).
    if (skeleton->actions.empty())
    {
        return frozen;
    }
    Render::Models::AnimationState& animation = actor.animation;
    const std::uint8_t action = animation.action;
    if (action >= skeleton->actions.size())
    {
        return Core::Unexpected(std::format("0x4f95c0: la acción {:#x} del lugar {} lee fuera de las {} de "
                                            "Player.bmd",
                                            action, slot, skeleton->actions.size()));
    }
    const Render::Models::ActionInfo& info = skeleton->actions[action];
    // 0x4f95c0..0x4f95d4: una velocidad negativa o NaN cuenta 0.
    float speed = info.speed >= 0.0f ? info.speed : 0.0f;
    if (actor.byte3e3 != 0 && action >= FirstFasterAction && action <= LastFasterAction)
    {
        speed *= FasterFactor;
    }
    const float frame = animation.frame;
    if (action == SlowedAction)
    {
        if (frame > SlowedFrame)
        {
            speed *= HalfFactor;
        }
    }
    else if (IsOneOf(ParticleActions, action))
    {
        if (frame > ParticleFrame)
        {
            speed *= TenthFactor;
        }
    }
    else if ((actor.characterClass & ClassMask) == ClassFour && IsOneOf(ClassFourActions, action) &&
             frame > ClassFourFirstFrame && frame < ClassFourLastFrame)
    {
        return Core::Unexpected(std::format("0x4f969d: la acción {:#x} de la clase 4 del lugar {} pide 0x59e680, que "
                                            "no está portado",
                                            action, slot));
    }
    if (action == HalvedAction)
    {
        speed *= HalfFactor;
    }
    // 0x4f96d4..0x4f9737: los otros tipos. 0x4f973a: con +0x78 & 0x1000 devuelve 0 sin avanzar.
    if (frozen)
    {
        return true;
    }
    // 0x4f9774: 0x4ec200, que devuelve 0 al terminar; en la escena 4 lo decide el frame + 2 (Q-C21).
    const Render::Models::AnimationScene scene = context.scene == SceneId::CharacterSelect
                                                     ? Render::Models::AnimationScene::CharacterSelect
                                                     : Render::Models::AnimationScene::Other;
    return Render::Models::AdvanceAnimation(animation, info, speed, scene);
}

// 0x4fcb10(c, c) desde 0x4fce57, con la acción que terminó.
Core::Expected<void, std::string> StopAtTheEnd(ActorState& actor, std::size_t slot, const CharacterStepContext& context)
{
    const std::uint8_t action = actor.animation.action;
    // 0x4fcb17..0x4fcb23 y 0x4fcb87..0x4fcb9c: con +0x38a en 0, 0x52e810 una vez.
    if (action == FirstMarkAction || action == SecondMarkAction)
    {
        return Core::Unexpected(std::format("0x4fcb87: la acción {:#x} del lugar {} terminó; +0x38a y 0x52e810 no "
                                            "están portados",
                                            action, slot));
    }
    // 0x4fcb25..0x4fcb63: vuelven a la de parado (0x4f2250) las de antes de la 0x0d, las 0xbd a 0xbf, las 0x54
    // y 0x55 y, de la 0x22 a la 0xc2, las que no son 0x8b, 0x8c, 0x46, 0x47, 0x44, 0x49, 0x4b ni de la 0x6a a la
    // 0x75.
    const bool kept = action < FirstKeptAction || action > LastKeptAction || IsOneOf(KeptActions, action) ||
                      (action >= FirstKeptRange && action <= LastKeptRange);
    const bool stops = action < FirstStoppedAfter || (action >= FirstSpecialStop && action <= LastSpecialStop) ||
                       (action >= FirstCombatStop && action <= LastCombatStop) || !kept;
    if (stops)
    {
        const auto stopped = SetActorStop(actor, ActorStopContext{.runtime = context.world.Runtime(),
                                                                  .scene = context.scene,
                                                                  .playerSkeleton = *context.playerSkeleton,
                                                                  .items = context.items,
                                                                  .random = context.random});
        if (!stopped.HasValue())
        {
            return Core::Unexpected(stopped.Error());
        }
        // El sonido de Fenrir del sufijo (0x41ed10) no suena: MuRet todavía no tiene audio (CL10).
    }
    // 0x4fcb71..0x4fcb7a: la 0x44, también la que puso 0x4f2250, vuelve al frame 0.
    if (actor.animation.action == HalvedAction)
    {
        actor.animation.frame = 0.0f;
    }
    return {};
}

// 0x4fcd00(c, c, b) desde 0x4fd770.
Core::Expected<void, std::string> StepTheAnimation(ActorState& actor, std::size_t slot,
                                                   const CharacterStepContext& context)
{
    // 0x4fcd0c..0x4fcd55 y 0x4fc790: con +0x88 en 4 (lo ponen paquetes que MuRet no aplica), en los mapas 0 y 2
    // los 0x20a de subtipo 0xd1 a 0xd3 giran y, al terminar, van a 0x4fc8c0; y 0x4fc790 sortea con rand() & 1
    // un mensaje por tipo (0x557170).
    if (actor.byte88 == MessageKind)
    {
        return Core::Unexpected(std::format("0x4fcd0c: con +0x88 en 4, el giro de 0x4fcd00 y el mensaje de 0x4fc790 "
                                            "del lugar {} no están portados",
                                            slot));
    }
    // 0x4fcd65..0x4fce15: una vez cada más de 4.000 ms (timeGetTime; la primera, con 0x7437488 = ahora − 5.000),
    // con el nombre de 0x6c88b0, el mensaje de 0x557170. MuRet no tiene ese reloj: corta con el nombre, sin
    // mirarlo (Q-C248).
    if (HasTheMessageName(actor))
    {
        return Core::Unexpected(std::format("0x4fcda2: el nombre del lugar {} es el de 0x6c88b0, y el mensaje de "
                                            "0x557170 y su reloj (0x7437488) no están portados",
                                            slot));
    }
    // 0x4fce1d: 0x4f9580.
    const auto finished = AdvanceTheBody(actor, slot, context);
    if (!finished.HasValue())
    {
        return Core::Unexpected(finished.Error());
    }
    if (!finished.Value())
    {
        return {};
    }
    // 0x4fce29..0x4fce39: +0x3c4 en −1 y +0x3e0 y +0x3e1 en 0, que MuRet no tiene: +0x3e0 y +0x3e1 los leen los
    // pasos del héroe (0x505770), que cortan.
    // 0x4fce3f..0x4fce57: el 0x20a sin el giro de +0x88 va a 0x4fcb10. 0x4fce68 y 0x4fce7a: el 0xc5 y el 0x1f5.
    return StopAtTheEnd(actor, slot, context);
}

// 0x4fcf26..0x4fd106: con el 0x20a, si la acción y el modelo del arma (con signo) dejan estela.
bool LeavesATrail(std::uint8_t action, int model)
{
    if (IsOneOf(TrailedActions, action) || (action >= FirstSwingAction && action <= LastSwingAction))
    {
        return true;
    }
    if (model >= FirstSwordModel && model < EndOfSwordModels)
    {
        return (action >= FirstSwordAction && action <= LastSwordAction) || action == AlwaysTrailedAction;
    }
    if (model == SpearModel || (model >= FirstPoleModel && model < EndOfPoleModels))
    {
        return action >= FirstPoleAction && action <= LastPoleAction;
    }
    if (model >= EndOfPoleModels && model < EndOfBowModels)
    {
        return action >= FirstBowAction && action <= LastBowAction;
    }
    return false;
}

// 0x4fcee0(c, c, b) desde 0x505410, con el 0x20a.
Core::Expected<void, std::string> StepTheWeaponTrail(const ActorState& actor, std::size_t slot,
                                                     Render::Models::CrtRandom& random)
{
    const std::uint8_t action = actor.animation.action;
    // 0x4fceea..0x4fcf25: antes del frame 3 (o con NaN) y fuera de la 0x8d copia la posición a +0x17c, que MuRet
    // no tiene, y sale.
    if (!(actor.animation.frame >= TrailFrame) && action != AlwaysTrailedAction)
    {
        return {};
    }
    const bool left = action == FirstLeftHandAction || action == SecondLeftHandAction;
    const ActorItemPart& hand = actor.hands[left ? std::size_t{1} : std::size_t{0}];
    const int model = static_cast<std::int16_t>(hand.model);
    if (LeavesATrail(action, model))
    {
        return Core::Unexpected(std::format("0x4fd236: la estela de la acción {:#x} del lugar {} con el modelo {:#x} "
                                            "(0x52e470) no está portada",
                                            action, slot, hand.model));
    }
    // 0x4fd5c1..0x4fd5df: 0x441260 (solo el 0x1a7), 0x42fa40 (el 0x1b1 y el 0x1b3), 0x432bd0 y 0x426d90 (los
    // 0x1b8 a 0x1ba). 0x432bf1: rand() sin condición, antes de mirar el tipo (0x1ab a 0x1b4).
    static_cast<void>(random.Next());
    return {};
}

// 0x4fd5f0(c, c) desde 0x508edf, por el camino del 0x20a quieto (06, "El cuerpo de los personajes
// (0x4fd5f0)"). `hero` es c == [0x743a170].
Core::Expected<void, std::string> StepTheBody(ActorState& actor, std::size_t slot, bool hero,
                                              const CharacterStepContext& context)
{
    const WorldRuntime& runtime = context.world.Runtime();
    // 0x4fd603..0x4fd693: el 0xc5 pasa su nombre y +0x1d6 a 0x4381f0.
    if (actor.type == NamedType)
    {
        return Core::Unexpected(std::format("0x4fd603: el 0xc5 del lugar {} pasa su nombre a 0x4381f0, que no está "
                                            "portado",
                                            slot));
    }
    // 0x4fd69b..0x4fd6ef: la posición, la escala y la acción al modelo compartido (+0x6c, +0x68 y +0xa0), que
    // MuRet no tiene: 0x4f9580 y 0x4ec200 leen la acción del personaje.
    // 0x4fd6f5: 0x4fbe10 compara la celda lógica del héroe con la anterior (0x743a180..0x743a188) y manda un
    // paquete con send (0x6b8588); con error cierra el socket (0x4e8380).
    if (runtime.map >= FirstCellReportMap && runtime.map <= LastCellReportMap)
    {
        return Core::Unexpected("0x4fbe10: en el " + MapText(runtime.map) +
                                " el paso de cada personaje compara la celda del héroe y la manda, sin portar");
    }
    // 0x4fd6fe: 0x4fc450.
    if (hero)
    {
        if (auto timers = StepTheOwnTimers(context); !timers.HasValue())
        {
            return timers;
        }
    }
    // 0x4fd70e: 0x4fb380. +0x398 en 0 (solo lo escriben paquetes que MuRet no aplica) y +0x399 en 0 (0x511290,
    // 0x511d3e; lo suben los mismos paquetes): no gira ni se corre (*estático*). El 0xf1 cae (0x4fb910).
    if (actor.type == FallingType)
    {
        return Core::Unexpected(
            std::format("0x4fb910: el 0xf1 del lugar {} cae con 0x5f93c0 y gira, sin portar", slot));
    }
    // 0x4fd722: 0x4fb9e0, muriéndose (+0x389).
    if (actor.dying != 0)
    {
        return Core::Unexpected(std::format("0x4fb9e0: el personaje del lugar {} se muere (+0x389); sus partículas "
                                            "y 0x4f5660 no están portados",
                                            slot));
    }
    // 0x4fd72e: 0x4e9980, el alpha hacia su meta y la luz del blendMesh.
    World::StepObjectAlpha(actor.alphaTarget, actor.alpha, actor.blendMeshLight, actor.alphaMode);
    // 0x4fd736..0x4fd75e: +0x3d8 en 0: en los personajes solo lo escribe esta resta (*estático*).
    // 0x4fd770: 0x4fcd00.
    if (auto animated = StepTheAnimation(actor, slot, context); !animated.HasValue())
    {
        return animated;
    }
    // 0x4fd77b..0x4fd7dc: muriéndose, que ya cortó.
    // 0x4fd7e4: rand() % 6 sin condición; (r + 2) · 0,1f va a un local que este camino no lee (Q-C247).
    static_cast<void>(context.random.Next());
    // 0x4fd835..0x4fdada: fuera del mapa 30, con +0x1a0 positivo, seis rand() por hueso y un 0x614. +0x1a0 en 0:
    // lo pone 0x511290 (0x511c62) y lo sube solo 0x4f97b0 (0x4f9ba2), desde la habilidad de abajo (*estático*).
    // 0x4fdae0..0x4fdb92: las 0x4d, 0x4f y 0x5a pasado el frame 5,5 sacan la partícula 0x5ac del hueso 42.
    const std::uint8_t action = actor.animation.action;
    if (IsOneOf(ParticleActions, action) && actor.animation.frame > ParticleFrame)
    {
        return Core::Unexpected(std::format("0x4fdb70: la acción {:#x} del lugar {} saca la partícula 0x5ac en el "
                                            "hueso 42 (0x4ec110), sin portar",
                                            action, slot));
    }
    // 0x4fdb9a..0x4fdbeb: +0x381 en 0, debajo de [0x6c88ac] (15): salta a 0x50539e sin la habilidad. Lo escribe
    // solo este camino (*estático*).
    // 0x5053a2..0x5053f0: +0x428 en −128 (0x511290, 0x511c77; los paquetes lo ponen en 0x96): nada.
    // 0x5053fc: 0x42a2b0, solo el 0x1a8.
    // 0x505410: 0x4fcee0.
    if (auto trailed = StepTheWeaponTrail(actor, slot, context.random); !trailed.HasValue())
    {
        return trailed;
    }
    // 0x505418..0x505444: el 0xf1, que ya cortó. 0x50544b: 0x420dd0 con +0x430 en 0 (0x511290, 0x511878): el
    // cuervo, que MuRet no crea. 0x50545e..0x5054aa: muriéndose, que ya cortó.
    return {};
}

// 0x505de0(c, c) desde 0x508ee6: hasta su final con el héroe y hasta la cabeza con los demás.
Core::Expected<void, std::string> StepTheEffects(ActorState& actor, std::size_t slot, bool hero,
                                                 const CharacterStepContext& context,
                                                 const Data::Terrain::TerrainLayer<std::uint16_t>& walls)
{
    // 0x505e0b: un modelo sin acciones (+0x26 de 0x5769ad8 + tipo · 0xe0) va a la caja de 0x505e15. El
    // 0x20a, Player.bmd, las tiene; los demás cortan antes, en 0x4f9580.
    if (actor.type != Data::Models::PlayerSkeletonModel)
    {
        return Core::Unexpected(std::format("0x505e0b: el modelo {:#x} del lugar {} no está leído en 0x505de0, solo el "
                                            "0x20a",
                                            actor.type, slot));
    }
    // 0x505ec6..0x505ee4: la posición, la escala y la acción van al modelo compartido (+0x6c, +0x68 y
    // +0xa0), que leen los 0x4ec110 de más abajo y que el dibujo vuelve a escribir antes de posar. MuRet
    // no tiene el modelo compartido.
    // 0x505ef2: sin verse, nada más.
    if (!actor.seen)
    {
        return {};
    }
    // 0x505ef8..0x506866 y 0x50686e..0x5068b2: la zona segura es el bit 0x01 del byte bajo de la celda
    // lógica, la de +0x41c y +0x420 (06 § Caminar), no la de la posición dibujada.
    const LogicalCell cell = ReadLogicalCell(actor, context.random);
    const std::uint16_t attribute = walls[Data::Terrain::CellIndex(cell.x, cell.y)];
    actor.safeZone = static_cast<std::uint8_t>((attribute & World::SafeZoneWallBit) != 0 ? 1 : 0);
    // 0x5068b9..0x5069ab: la cabeza. Con otro que el héroe, sin +0x78 & 0x20 ni & 0x1000, sortea con rand() % 32
    // y, si da 0, dos más hacia dónde mira (+0x34 y +0x38, 0x5068fd..0x506950).
    if (!hero)
    {
        return Core::Unexpected(std::format("0x5068b9: la cabeza del personaje del lugar {} (rand() % 32 en 0x5068fd) "
                                            "y lo que sigue de 0x505de0 con otro que el héroe no están portados",
                                            slot));
    }
    // Frontera (08 P-C44): con el héroe no sortea ni gasta rand(): gira +0x28 y +0x2c hacia +0x34 y +0x38, el
    // blanco que MoveHero escribe con el mouse (0x59b1c2..0x59b287, CL7.4). MuRet no tiene esos campos del
    // héroe; los lee la pose del dibujo, que corta antes.
    // 0x5069ae: +0x3e5 en 0, sin los veinte efectos: solo lo sube 0x4c9790 (0x4c982b), un paquete que
    // MuRet no aplica (*estático*).
    // 0x506acf..0x506b62: con +0x375 debajo de 6, la luz de cada mano, la derecha y la izquierda.
    if (actor.pk < FirstPkWithoutLights)
    {
        for (const ActorItemPart& hand : actor.hands)
        {
            if (std::ranges::find(LightModels, hand.model) != LightModels.end())
            {
                return Core::Unexpected(std::format("0x5f9780: la luz del modelo {:#x} en la mano (0x506b50) va al "
                                                    "buffer dinámico 0x7b4ccc4, que MuRet no tiene",
                                                    hand.model));
            }
        }
    }
    // 0x506b64: +0x3d8 en 0, sin los efectos de +0x3d4: en los personajes solo lo escribe 0x4fd5f0, que lo
    // baja si es positivo (0x4fd75e); los otros que escriben +0x3d4 y +0x3d8 son de la UI (*estático*).
    // 0x506cac: rand() & 7, sin condición. El camino del 0x20a no lee el valor (*estático*, Q-C207).
    static_cast<void>(context.random.Next());
    // 0x506ccc..0x506cf1: el caso del tipo − 0x152; el 0x20a cae en 0x506cf8, que mira el mapa 7 solo en la
    // escena 5.
    const std::int32_t map = context.world.Runtime().map;
    if (context.scene == SceneId::Game && map == ClockEffectMap)
    {
        return Core::Unexpected(std::string("0x506d01: en el mapa 7 el jugador saca el efecto 0x607 según el reloj "
                                            "del mundo (0x5767b88), que no está portado"));
    }
    if (map == RandomEffectMap)
    {
        return Core::Unexpected(std::string("0x506d8b: en el mapa 9 el jugador sortea dos efectos 0x68 con rand() & 3, "
                                            "que no están portados"));
    }
    const std::uint8_t action = actor.animation.action;
    if (action == FirstEffectAction || action == SecondEffectAction)
    {
        return Core::Unexpected(std::format("0x506e5d: la acción {:#x} del jugador saca los efectos 0x5ac y los de "
                                            "0x5189f0, que no están portados",
                                            action));
    }
    if (action == BoneEffectAction)
    {
        return Core::Unexpected(std::string("0x506f62: la acción 0x96 del jugador saca diez efectos 0x5d9 en huesos "
                                            "de rand(), que no están portados"));
    }
    // 0x506fc1..0x507001: 0x4ec110 con el hueso de +0x2bd a un local que nadie lee, y a 0x50862f.
    // 0x50862f..0x5086ca: los pasos del héroe, el sonido de 0x505770 con +0x3e0 y +0x3e1.
    if (HasStepSounds(action))
    {
        return Core::Unexpected(std::format("0x508668: los pasos del héroe en la acción {:#x} (0x505770, con +0x3e0 y "
                                            "+0x3e1) no están portados",
                                            action));
    }
    // 0x5086cf..0x508801: las acciones de ahí son todas de los pasos, que ya cortaron.
    return {};
}
} // namespace

bool CharacterSeen(const World::TerrainPolygon& polygon, const glm::vec3& position)
{
    constexpr double CellsPerUnit = static_cast<double>(0.01f);
    constexpr double ViewMargin = -20.0;
    const auto cells = [](float coordinate)
    { return static_cast<double>(static_cast<float>(static_cast<double>(coordinate) * CellsPerUnit)); };
    return World::PolygonContains(polygon, cells(position.x), cells(position.y), ViewMargin);
}

LogicalCell ReadLogicalCell(ActorState& actor, Render::Models::CrtRandom& random)
{
    LogicalCell cell;
    {
        const Render::Models::LogicalAccess access(actor.cellYAccesses, random,
                                                   Render::Models::SmallLogicalCloseRandoms);
        cell.y = actor.cellY;
    }
    {
        const Render::Models::LogicalAccess access(actor.cellXAccesses, random,
                                                   Render::Models::SmallLogicalCloseRandoms);
        cell.x = actor.cellX;
    }
    return cell;
}

Core::Expected<void, std::string> StepCharacters(const CharacterStepContext& context)
{
    WorldState& world = context.world;
    const WorldRuntime& runtime = world.Runtime();
    // 0x508f06..0x508f0c: 0x412140 con el héroe. Fuera del mapa 30 llama a 0x412560, que solo suelta el
    // objeto de 0x5763530 si lo hay, y ese objeto lo crea 0x412140 en el mapa 30 (*estático*).
    if (runtime.map == CastleMap)
    {
        return Core::Unexpected(std::string("0x412140: en el mapa 30 el paso de los personajes arma los objetos del "
                                            "asedio (0x5763530), que no están portados"));
    }
    if (!world.Map().terrain.attributes)
    {
        return Core::Unexpected("0x508f14: el TerrainWall (0x7d0d348) del " + MapText(runtime.map) +
                                " no está cargado (0x61a560)");
    }
    Data::Terrain::TerrainLayer<std::uint16_t>& walls = world.Map().terrain.attributes->cells;
    World::ClearCharacterWalls(walls);
    // 0x5fba90 lee los globales de 0x78ecbf0 y 0x78ecc00: el trapecio del último dibujo de cualquier escena.
    const World::TerrainPolygon& polygon = runtime.terrainPolygon;
    const auto actors = world.Actors();
    for (std::size_t slot = 0; slot < SteppedCharacters; ++slot)
    {
        ActorState& actor = actors[slot];
        // 0x508f49..0x509165: vivo, sin morirse y sin el 8 de +0x88, ocupa su celda lógica.
        if (actor.alive && actor.dying == 0 && actor.byte88 != World::NonBlockingKind)
        {
            const LogicalCell cell = ReadLogicalCell(actor, context.random);
            World::MarkCharacterWall(walls, cell.x, cell.y);
        }
        // 0x50916d..0x5091a0: vivo o no, si se ve, con el trapecio del último 0x5fb570.
        actor.seen = CharacterSeen(polygon, actor.position);
    }
    for (std::size_t slot = 0; slot < SteppedCharacters; ++slot)
    {
        ActorState& actor = actors[slot];
        // 0x508ea7: sin vivir, nada.
        if (!actor.alive)
        {
            continue;
        }
        // 0x508eac..0x508ed2: otra vez, con el mismo trapecio.
        actor.seen = CharacterSeen(polygon, actor.position);
        // 0x508ed8: 0x508ba0, que sale con el héroe (0x508bad).
        const bool hero = slot == runtime.hero;
        if (!hero)
        {
            if (auto other = StepOtherCharacter(actor, slot, context.random); !other.HasValue())
            {
                return other;
            }
        }
        // 0x508edf: 0x4fd5f0, el cuerpo, la animación y el alpha. Corre también en la espera, con el 54 de la
        // selección (la frontera de T8, P-C41 a).
        if (auto body = StepTheBody(actor, slot, hero, context); !body.HasValue())
        {
            return body;
        }
        // 0x508ee6: 0x505de0.
        if (auto effects = StepTheEffects(actor, slot, hero, context, walls); !effects.HasValue())
        {
            return effects;
        }
        // 0x508eed: 0x42da90 fuera del mapa 30 devuelve 0 (0x42da9a).
    }
    // Frontera (08 P-C44): 0x52e500 (0x5091d0), el paso de las estelas de 0x75e9870, sin rand(). Las
    // crea 0x52e470 desde las habilidades, los efectos y la acción 3 del caballo (0x444671); MuRet no
    // tiene la tabla. Corre también en la espera.
    return {};
}
} // namespace Client::Scenes
