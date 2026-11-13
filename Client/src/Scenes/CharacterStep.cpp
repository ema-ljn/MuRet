#include "Scenes/CharacterStep.h"
#include "Protocol/Pet/PetOrders.h"
#include "Render/Models/OriginalRotation.h"
#include "World/Particles.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorActions.h"
#include "Scenes/ActorMotion.h"
#include "Scenes/ActorWalk.h"
#include "Scenes/PlayerActionSpeeds.h"
#include "Scenes/ActorCombat.h"
#include "Scenes/ActorConstruction.h"
#include "Scenes/ActorDying.h"
#include "Scenes/ActorState.h"
#include "Scenes/AttackStep.h"
#include "Scenes/CharacterHead.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/DynamicWorldLight.h"
#include "Scenes/ItemSetOptionList.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnStatHelpers.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldRuntime.h"
#include "UI/GameUiState.h"
#include "Scenes/WorldState.h"
#include "World/ObjectAlpha.h"
#include "World/OriginalFtol.h"
#include "World/TerrainCulling.h"
#include "World/TerrainHeight.h"
#include "World/TerrainWalls.h"
#include "World/WorldObjectStepServices.h"
#include "World/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace Client::Scenes
{
namespace
{
// 0x50919a y 0x5091c8: los dos lazos de 0x508f00 recorren 400 registros (0x69780 / 0x438); el 401 no.
constexpr std::size_t SteppedCharacters = 400;
// 0x412162: en el mapa 30 (0x1e), 0x412140 arma el panel del asedio.
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

// 0x5068b9..0x5068d4: con +0x78 & 0x20 (o & 0x1000, FrozenFlag) la cabeza no se mueve.
constexpr std::uint32_t StillHeadFlag = 0x20;
// 0x506acf: con +0x375 en 6 o más (sin signo), las manos no dan luz.
constexpr std::uint8_t FirstPkWithoutLights = 6;
// 0x506aec..0x506b42: los modelos de las manos que piden una luz a 0x5f9780, con su color (los tres DWORD
// de [ebp−0x1c]) y su radio. El 0x21e (0x506af7..0x506b0c): (0,8; 0,64; 0,4) con 3. El 0x225, el 0xa24, el
// 0xc1c y el 0x61f (0x506b2d..0x506b42): (0,64; 0,4; 0,24) con 2. Los 0,64 y 0,24 son un ulp más que los
// float más cercanos (0x3f23d70a y 0x3e75c28f): van por sus bits.
struct HandLight
{
    std::uint16_t model;
    glm::vec3 color;
    std::int32_t radius;
};
constexpr glm::vec3 BrightHandLight{std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3f23d70bu),
                                    std::bit_cast<float>(0x3ecccccdu)};
constexpr glm::vec3 DimHandLight{std::bit_cast<float>(0x3f23d70bu), std::bit_cast<float>(0x3ecccccdu),
                                 std::bit_cast<float>(0x3e75c290u)};
constexpr std::array<HandLight, 5> HandLights{
    HandLight{.model = 0x21e, .color = BrightHandLight, .radius = 3},
    HandLight{.model = 0x225, .color = DimHandLight, .radius = 2},
    HandLight{.model = 0xa24, .color = DimHandLight, .radius = 2},
    HandLight{.model = 0xc1c, .color = DimHandLight, .radius = 2},
    HandLight{.model = 0x61f, .color = DimHandLight, .radius = 2},
};
// 0x506d01 y 0x506d8b: los mapas del caso del 0x20a con efectos.
constexpr std::int32_t ClockEffectMap = 7;
constexpr std::int32_t RandomEffectMap = 9;
// 0x506e5d..0x506e63 y 0x506f62: las acciones del 0x20a con efectos.
constexpr std::uint8_t FirstEffectAction = 0x44;
constexpr std::uint8_t SecondEffectAction = 0x45;
constexpr std::uint8_t BoneEffectAction = 0x96;
// 0x5086d6..0x508712: en Kalima (los mapas 24 a 29 y el 36), las acciones del 0x20a que sacan el 0x6f4.
constexpr std::array<std::uint8_t, 7> KalimaParticleActions{0x20, 0x21, 0x1d, 0x15, 0x1e, 0x1f, 0x4b};
constexpr std::int32_t FirstKalimaMap = 24;
constexpr std::int32_t LastKalimaMap = 29;
constexpr std::int32_t SeventhKalimaMap = 36;

std::string MapText(std::int32_t map)
{
    return "mapa " + std::to_string(map);
}

template <std::size_t N> bool IsOneOf(const std::array<std::uint8_t, N>& actions, std::uint8_t action)
{
    return std::ranges::find(actions, action) != actions.end();
}

void ClearBit(std::uint8_t& bits, std::uint8_t bit)
{
    bits = static_cast<std::uint8_t>(bits & ~bit);
}

// 0x508ba0(c, c) desde 0x508ed8 con otro que el héroe publicado ([0x743a170]; con él sale en 0x508bad).
Core::Expected<void, std::string> StepOtherCharacter(ActorState& actor, std::size_t slot,
                                                     const CharacterStepContext& context)
{
    if (actor.movement != 0 && (IsNewFace(actor.type) ? context.newFaceSkeleton : context.playerSkeleton) == nullptr)
    {
        return Core::Unexpected(std::format("0x4f37c0: falta el esqueleto del personaje del lugar {}", slot));
    }
    const World::WorldTerrain& terrain = context.terrain != nullptr ? *context.terrain : context.world.Map().terrain;
    if (!terrain.attributes)
    {
        return Core::Unexpected(std::string("0x508ba0: falta el TerrainWall para reintentar el camino"));
    }
    const Render::Models::Skeleton empty;
    const Render::Models::Skeleton& skeleton = context.playerSkeleton != nullptr ? *context.playerSkeleton : empty;
    PlayerActionSpeeds& speeds = context.actionSpeeds != nullptr ? *context.actionSpeeds : context.world.ActionSpeeds();
    const ActorWalkContext walk{
        {context.world.Runtime(), context.scene, skeleton, context.items, context.random, &terrain.attributes->cells},
        speeds,
        false,
        IsNewFace(actor.type) ? context.newFaceSkeleton : nullptr};
    ActorMotionContext motion{context.world.Runtime(), context.random, {}};
    if (terrain.heights)
    {
        motion.height = [&](float x, float y)
        { return World::TerrainHeightAt(*terrain.heights, *terrain.attributes, x, y, context.fixedHeight); };
    }
    return StepOtherActorMotion(actor, walk, motion, terrain.attributes->cells);
}

// 0x4fc450(c) desde 0x4fd6fe, con el héroe publicado ([0x743a170]; con otro sale en 0x4fc464): los tres WORD
// de [0x7681eb0], con el registro propio abierto (Q-C246).
Core::Expected<void, std::string> StepTheOwnTimers(const CharacterStepContext& context)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    OwnCharacterRecord& own = world.OwnCharacter();
    ItemSetOptionList& setOptions = world.ItemSets().list;
    // El ítem del cursor, con lo que haya en sus globales (HeldItemOf).
    const OwnEquipmentOverride held = HeldItemOf(runtime);
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
    const bool newFace = IsNewFace(actor.type);
    if (actor.type != Data::Models::PlayerSkeletonModel && !newFace)
    {
        return Core::Unexpected(std::format("0x4f9580: el modelo {:#x} del lugar {} (0x5769ad8 + tipo · 0xe0) no está "
                                            "leído, solo Player.bmd (0x20a)",
                                            actor.type, slot));
    }
    const Render::Models::Skeleton* skeleton = newFace ? context.newFaceSkeleton : context.playerSkeleton;
    if (skeleton == nullptr)
    {
        return Core::Unexpected(
            std::format("0x4f9580: el modelo {:#x} del lugar {} no está cargado", actor.type, slot));
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
    const PlayerActionSpeeds& speeds =
        context.actionSpeeds != nullptr ? *context.actionSpeeds : context.world.ActionSpeeds();
    const float sharedSpeed = newFace ? info.speed : speeds.Speed(action, info.speed);
    float speed = sharedSpeed >= 0.0f ? sharedSpeed : 0.0f;
    if (newFace)
    {
        constexpr float NewFaceSpeedFactor = 2.0f; // 0x4f971a
        if (frozen)
        {
            return true;
        }
        return Render::Models::AdvanceAnimation(animation, info, speed * NewFaceSpeedFactor,
                                                Render::Models::AnimationScene::CharacterSelect);
    }
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

bool IsKalima(std::int32_t map);

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
        const auto& terrain = context.terrain != nullptr ? *context.terrain : context.world.Map().terrain;
        const auto stopped = SetActorStop(
            actor, ActorStopContext{.runtime = context.world.Runtime(),
                                    .scene = context.scene,
                                    .playerSkeleton = *context.playerSkeleton,
                                    .items = context.items,
                                    .random = context.random,
                                    .terrainWalls = terrain.attributes ? &terrain.attributes->cells : nullptr});
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
    // 0x4fce33/39: los dos sonidos se habilitan otra vez al terminar la animación.
    actor.firstFootstepSound = 0;
    actor.secondFootstepSound = 0;
    // 0x4fce3f..0x4fce57: el 0x20a sin el giro de +0x88 va a 0x4fcb10. 0x4fce68 y 0x4fce7a: el 0xc5 y el 0x1f5.
    if (IsNewFace(actor.type))
    {
        // 0x4fce68 → 0x4fcbb0: fuera del mapa 30, el 0xa8..0xac vuelve sin SetAction ni rand().
        if (context.world.Runtime().map == CastleMap)
        {
            return Core::Unexpected(std::string("0x42d890: la animación NewFace del mapa 30 no está portada"));
        }
        return {};
    }
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
Core::Expected<void, std::string> StepTheWeaponTrail(ActorState& actor, std::size_t slot,
                                                     Render::Models::CrtRandom& random)
{
    const std::uint8_t action = actor.animation.action;
    // 0x4fceea..0x4fcf25: antes del frame 3 (o con NaN) y fuera de la 0x8d copia la posición a +0x17c y sale.
    if (!(actor.animation.frame >= TrailFrame) && action != AlwaysTrailedAction)
    {
        actor.trailOrigin = actor.position;
        return {};
    }
    const bool left = action == FirstLeftHandAction || action == SecondLeftHandAction;
    const ActorItemPart& hand = actor.hands[left ? std::size_t{1} : std::size_t{0}];
    const int model = static_cast<std::int16_t>(hand.model);
    if (actor.type == Data::Models::PlayerSkeletonModel && LeavesATrail(action, model))
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

// Una lectura de la celda protegida del héroe: abre la entrada (0x40b570) y la cierra (0x40b800, o en línea
// con 0x40b240, 0x40b2f0 y 0x412890), con los cuatro rand() del último cierre.
std::int32_t ReadHeroCell(std::uint8_t value, std::uint8_t& depth, Render::Models::CrtRandom& random)
{
    const Render::Models::LogicalAccess access(depth, random, Render::Models::SmallLogicalCloseRandoms);
    return value;
}

// 0x4fbe10, por cada personaje que da un paso: compara la celda lógica del héroe (+0x41c y +0x420) con la
// guardada (0x743a180 y 0x743a184, .bss 0). Si es la misma y pasaron 10 s (el reloj del mundo menos 0x743a188,
// por 0,001f, en la x87) en una celda con el bit 8 del atributo, manda C1 06 AF 01 x y (CL9.5). Con la misma
// celda guarda el reloj solo si pasó ese tiempo; con otra, guarda la celda y el reloj. Cada comparación abre y
// cierra su entrada.
Core::Expected<void, std::string> ReportTheHeroCell(const CharacterStepContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    ActorState& hero = context.world.Actors()[runtime.hero];
    Render::Models::CrtRandom& random = context.random;
    if (ReadHeroCell(hero.cellX, hero.cellXAccesses, random) == runtime.reportedCellX &&
        ReadHeroCell(hero.cellY, hero.cellYAccesses, random) == runtime.reportedCellY)
    {
        // 0x4fbea8..0x4fbec5: fld, fsub, fmul y fcomp con 10; menor (o sin orden), nada.
        constexpr double Thousandth = static_cast<double>(std::bit_cast<float>(0x3a83126fu));
        constexpr double ReportDelay = 10.0;
        const double waited =
            (static_cast<double>(context.worldClock) - static_cast<double>(runtime.reportedCellTime)) * Thousandth;
        if (!(waited >= ReportDelay))
            return {};
        const World::WorldTerrain& terrain =
            context.terrain != nullptr ? *context.terrain : context.world.Map().terrain;
        if (terrain.attributes == nullptr)
            return Core::Unexpected(std::string("0x4fbed9: falta el .att del mapa (0x7d0d348)"));
        constexpr std::uint16_t ReportedCellBit = 8;
        const auto cell = Data::Terrain::CellIndex(static_cast<std::uint8_t>(runtime.reportedCellX),
                                                   static_cast<std::uint8_t>(runtime.reportedCellY));
        if ((terrain.attributes->cells[cell] & ReportedCellBit) == ReportedCellBit)
        {
            return Core::Unexpected(std::format("0x4fbee8: el héroe lleva 10 s en la celda ({}; {}) con el bit 8 y "
                                                "0x4fbe10 manda C1 06 AF 01 (CL9.5)",
                                                runtime.reportedCellX, runtime.reportedCellY));
        }
        runtime.reportedCellTime = context.worldClock;
        return {};
    }
    runtime.reportedCellX = ReadHeroCell(hero.cellX, hero.cellXAccesses, random);
    runtime.reportedCellY = ReadHeroCell(hero.cellY, hero.cellYAccesses, random);
    runtime.reportedCellTime = context.worldClock;
    return {};
}

// 0x4fd787..0x4fd7e1: +0x389 más uno, en un byte; con 15 o más, 0x4f5660 (SetActorDie); en Blood Castle (11 a 17)
// con +0x1a1, 0x4f5660 otra vez, también antes del 15.
Core::Expected<void, std::string> DieAgain(ActorState& actor, const CharacterStepContext& context)
{
    const ActorCombatContext combat{.world = context.world,
                                    .random = context.random,
                                    .playerSkeleton = context.playerSkeleton,
                                    .models = context.models,
                                    .items = context.items};
    constexpr std::uint8_t DyingAgain = 15;
    actor.dying = static_cast<std::uint8_t>(actor.dying + 1);
    if (actor.dying >= DyingAgain)
    {
        if (auto died = SetActorDie(actor, combat); !died.HasValue())
            return died;
    }
    const std::int32_t map = context.world.Runtime().map;
    constexpr std::int32_t FirstBloodCastleMap = 11;
    constexpr std::int32_t LastBloodCastleMap = 17;
    if (map >= FirstBloodCastleMap && map <= LastBloodCastleMap && actor.byte1a1 != 0)
        return SetActorDie(actor, combat);
    return {};
}

Core::Expected<void, std::string> StepTheRaven(ActorState& actor, std::size_t slot,
                                               const CharacterStepContext& context);

// 0x4fd5f0(c, c) desde 0x508edf, por el camino del 0x20a quieto (06, "El cuerpo de los personajes
// (0x4fd5f0)"). `hero` es c == [0x743a170].
// 0x417e62: el grito del cuervo.
constexpr std::int32_t RavenShoutSound = 0x1d1;

// Lo que el paso del cuervo lee del pool (DarkSpiritTargets): el personaje de +8 y el de +0x3b0, con signo. Entre 0 y
// 399 mira sus banderas (0x416a8b..0x416ab9); el 6 lee su posición con cualquier valor distinto de 0xffff (0x417858):
// los lugares que escriben el A8 (0 a 400) están en el pool de MuRet, y otro valor corta.
Core::Expected<DarkSpiritTargets, std::string> RavenTargetsOf(const DarkSpirit& spirit, WorldState& world,
                                                              std::size_t slot)
{
    constexpr std::int16_t PoolActors = 400;
    constexpr std::uint16_t NoAttackTarget = 0xffff;
    const auto actors = world.Actors();
    DarkSpiritTargets targets;
    if (spirit.target < actors.size())
    {
        targets.target = actors[spirit.target].position;
    }
    const auto attack = static_cast<std::int16_t>(spirit.target3b0);
    if (attack >= 0 && attack < PoolActors)
    {
        targets.attackInPool = true;
        targets.attackFlags = actors[static_cast<std::size_t>(attack)].flags78;
    }
    if (spirit.target3b0 != NoAttackTarget)
    {
        if (attack < 0 || static_cast<std::size_t>(attack) >= actors.size())
        {
            return Core::Unexpected(
                std::format("0x417869: el +0x3b0 del cuervo del lugar {} ({}) cae fuera del pool", slot, attack));
        }
        targets.attackPosition = actors[static_cast<std::size_t>(attack)].position;
    }
    return targets;
}

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
    // 0x4fd6f5: 0x4fbe10, la celda del héroe en los mapas 18 a 23.
    if (runtime.map >= FirstCellReportMap && runtime.map <= LastCellReportMap)
    {
        if (auto reported = ReportTheHeroCell(context); !reported.HasValue())
            return reported;
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
    // 0x4fd722: 0x4fb9e0, muriéndose (+0x389). Lo que sigue corre aunque lo haya borrado.
    if (auto died = StepActorDying(actor, slot,
                                   {.map = runtime.map,
                                    .scene = context.scene,
                                    .hero = hero,
                                    .random = context.random,
                                    .particles = context.particles,
                                    .worldClock = context.worldClock});
        !died.HasValue())
    {
        return died;
    }
    // 0x4fd72e: 0x4e9980, el alpha hacia su meta y la luz del blendMesh.
    World::StepObjectAlpha(actor.alphaTarget, actor.alpha, actor.blendMeshLight, actor.alphaMode);
    // 0x4fd736..0x4fd75e: +0x3d8 en 0: en los personajes solo lo escribe esta resta (*estático*).
    // 0x4fd770: 0x4fcd00.
    if (auto animated = StepTheAnimation(actor, slot, context); !animated.HasValue())
    {
        return animated;
    }
    // 0x4fd77b..0x4fd7dc: muriéndose, +0x389 sube (un byte, que da la vuelta) y desde 15 llama a 0x4f5660; en Blood
    // Castle (11 a 17) con +0x1a1, otra vez.
    if (actor.dying != 0)
    {
        if (auto died = DieAgain(actor, context); !died.HasValue())
            return died;
    }
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
    // 0x4fdb9a..0x50539e: con +0x381 distinto de 0, 0x4f97b0 y 0x4f5b70 y +0x381 += 1; desde [0x6c88ac], la habilidad
    // de +0x38e con el blanco de +0x3a4. Lo ponen en 1 el ataque al llegar (0x5653c1, HeroAttack) y el 18 con 0x78 y
    // 0x79 (0x4cb830, ActionReply).
    if (auto attacked = StepTheAttack(actor, slot, context); !attacked.HasValue())
    {
        return attacked;
    }
    // 0x5053a2..0x5053f0: +0x428 en −128 (0x511290, 0x511c77; los paquetes lo ponen en 0x96): nada.
    // 0x5053fc: 0x42a2b0, solo el 0x1a8.
    // 0x505410: 0x4fcee0.
    if (auto trailed = StepTheWeaponTrail(actor, slot, context.random); !trailed.HasValue())
    {
        return trailed;
    }
    // 0x50544b: 0x420dd0, después de la estela y antes de 0x505de0, también fuera del trapecio.
    if (actor.darkSpirit && (actor.flags78 & FrozenFlag) == 0)
    {
        if (auto raven = StepTheRaven(actor, slot, context); !raven.HasValue())
        {
            return raven;
        }
    }
    // 0x505453..0x5054aa: muriéndose, apaga 0x8000 y 0x10000 de +0x78.
    if (actor.dying != 0)
    {
        constexpr std::uint32_t DyingClearedFlags = 0x8000 | 0x10000;
        actor.flags78 &= ~DyingClearedFlags;
    }
    return {};
}

// 0x420dd0: el cuervo de +0x430, que el que llama da por existente y sin +0x78 & 0x1000.
Core::Expected<void, std::string> StepTheRaven(ActorState& actor, std::size_t slot, const CharacterStepContext& context)
{
    const WorldRuntime& runtime = context.world.Runtime();
    if (context.darkSpiritSkeleton == nullptr || context.particles == nullptr)
    {
        return Core::Unexpected(
            std::format("0x416a20: faltan el modelo o las partículas del cuervo del lugar {}", slot));
    }
    auto targets = RavenTargetsOf(*actor.darkSpirit, context.world, slot);
    if (!targets.HasValue())
    {
        return Core::Unexpected(targets.Error());
    }
    const SharedModelPlacement& player = context.world.PlayerModel();
    const DarkSpiritStepped stepped =
        StepDarkSpirit(*actor.darkSpirit, *context.darkSpiritSkeleton,
                       {.position = actor.position,
                        .flags = actor.flags78,
                        .angles = actor.angles,
                        .pet = actor.pet.model,
                        .safeZone = actor.safeZone,
                        .pose = actor.bodyPose,
                        .modelScale = player.scale,
                        .modelOrigin = player.origin},
                       static_cast<std::uint8_t>(runtime.map), *context.particles, context.worldClock, context.random,
                       context.counter, targets.Value());
    // 0x416fc8 y 0x4175de: el A7 que corta el ataque, por send(); sin la sesión, no sale.
    if (stepped.stopsTheAttack && context.send)
    {
        context.send(Protocol::Pet::WritePetCommand({.pet = Protocol::Pet::RavenPet,
                                                     .command = Protocol::Pet::StopTheAttackCommand,
                                                     .key = Protocol::Pet::NoTargetKey}));
    }
    // 0x417e67: 0x41ed10(0x1d1) (CL10).
    if (stepped.shouts && context.soundRequest)
    {
        context.soundRequest(RavenShoutSound);
    }
    return {};
}

bool IsKalima(std::int32_t map)
{
    return (map >= FirstKalimaMap && map <= LastKalimaMap) || map == SeventhKalimaMap;
}

// 0x4ea190 antes del sorteo común de 0x5068fd: la acción 0 sortea además con % 48 para Y.
void SetTheNewFaceHeadTarget(ActorState& actor, Render::Models::CrtRandom& random)
{
    constexpr std::uint8_t KeptHeadAction = 6;
    if (actor.animation.action == KeptHeadAction)
    {
        return;
    }
    if (actor.animation.action != 0)
    {
        actor.headTurn.target = {};
        return;
    }
    constexpr int OneIn = 32;
    if (random.Next() % OneIn != 0)
    {
        return;
    }
    constexpr int TurnSpan = 128;
    constexpr int TurnCentre = 64;
    constexpr int NodSpan = 48;
    constexpr int NodCentre = 16;
    constexpr float FullTurn = 360.0f;
    actor.headTurn.target.x = static_cast<float>((random.Next() % TurnSpan) - TurnCentre);
    actor.headTurn.target.y = static_cast<float>((random.Next() % NodSpan) - NodCentre);
    for (int axis = 0; axis < 2; ++axis)
    {
        if (actor.headTurn.target[axis] < 0.0f)
        {
            actor.headTurn.target[axis] += FullTurn;
        }
    }
}

// 0x4ec110(modelo, huesos propios + hueso · 0x30, offset, punto, 1) con la escala y la posición que 0x505ec6
// copió del personaje al modelo. Sin huesos propios (antes del primer dibujo), la matriz en cero (Q-C318).
glm::vec3 OwnBonePoint(const ActorState& actor, std::size_t bone, const glm::vec3& offset)
{
    const Render::Models::BoneTransform matrix =
        bone < actor.bodyPose.size() ? actor.bodyPose[bone] : Render::Models::BoneTransform(0.0f);
    return Render::Models::OriginalTransformPosition(Render::Models::OriginalMatrixOf(matrix), offset, actor.scale,
                                                     actor.position);
}

// 0x506d0a..0x506d7c: en la escena 5 y el mapa 7, el segundo de cada diez del reloj del mundo (ftol % 10000
// debajo de 1000) saca una burbuja 0x607 de la cabeza: 0x4ec110 con el hueso +0x54 del modelo (20 en Player.bmd)
// de los huesos propios (+0x120), (0, 20, −10), la escala (+0x68) y la posición (+0x6c) que 0x505ec6 copió
// del personaje, y 0x53fe70(0x607, punto, +0x1c, (1, 1, 1) de 0x506c82, 0, 1, 0). Antes del primer dibujo
// los huesos son los del operator new de 0x511290, sin iniciar: MuRet usa una matriz en cero (Q-C318).
Core::Expected<void, std::string> BlowTheAtlansBubble(const ActorState& actor, std::size_t slot,
                                                      const CharacterStepContext& context)
{
    constexpr std::int32_t ClockPeriod = 10000;
    constexpr std::int32_t BubbleWindow = 1000;
    const std::optional<std::int32_t> clock = World::OriginalFtolLow(static_cast<double>(context.worldClock));
    if (!clock)
    {
        return Core::Unexpected(std::string("0x506d10: ftol del reloj del mundo fuera de lo acreditado"));
    }
    if (*clock % ClockPeriod >= BubbleWindow)
    {
        return {};
    }
    constexpr glm::vec3 HeadOffset{0.0f, 20.0f, -10.0f};
    const glm::vec3 point = OwnBonePoint(actor, static_cast<std::size_t>(Data::Models::PlayerHeadBone), HeadOffset);
    if (context.particles == nullptr)
    {
        return Core::Unexpected(
            std::format("0x506d77: falta el pool de partículas de la burbuja de Atlans, lugar {}", slot));
    }
    const auto created = context.particles->CreateChecked(World::DropParticle, point, actor.angles, glm::vec3(1.0f), 0,
                                                          1.0f, context.worldClock, context.random, 0);
    if (!created.HasValue())
    {
        return Core::Unexpected(std::format("0x506d77: la burbuja 0x607/0 del lugar {} falla con error {}", slot,
                                            static_cast<unsigned>(created.Error())));
    }
    return {};
}

// 0x506d7f..0x506e54: en el mapa 9, en cualquier escena, dos rand() % 4 (rand() & 3): con 0, una gota 0x68/0 de
// 0x4ec110 con el hueso 26 (0x4e0) y después con el 35 (0x690) de los huesos propios, (−15, 0, 0), y
// 0x53fe70(0x68, punto, +0x1c, (1, 1, 1), 0, 1, 0). El segundo rand() sale aunque el primero no dé 0.
Core::Expected<void, std::string> DropTheDevilSquareRain(const ActorState& actor, std::size_t slot,
                                                         const CharacterStepContext& context)
{
    constexpr std::int32_t RainParticle = 0x68;
    constexpr int RainOdds = 4;
    constexpr std::array<std::size_t, 2> RainBones{26, 35};
    constexpr std::array<const char*, 2> RainAddresses{"0x506dff", "0x506e4f"};
    constexpr glm::vec3 RainOffset{-15.0f, 0.0f, 0.0f};
    for (std::size_t drop = 0; drop < RainBones.size(); ++drop)
    {
        if (context.random.Next() % RainOdds != 0)
            continue;
        const glm::vec3 point = OwnBonePoint(actor, RainBones[drop], RainOffset);
        if (context.particles == nullptr)
        {
            return Core::Unexpected(std::format(
                "{}: falta el pool de partículas de la lluvia de Devil Square, lugar {}", RainAddresses[drop], slot));
        }
        const auto created = context.particles->CreateChecked(RainParticle, point, actor.angles, glm::vec3(1.0f), 0,
                                                              1.0f, context.worldClock, context.random, 0);
        if (!created.HasValue())
        {
            return Core::Unexpected(std::format("{}: la gota 0x68/0 del lugar {} falla con error {}",
                                                RainAddresses[drop], slot, static_cast<unsigned>(created.Error())));
        }
    }
    return {};
}

// 0x505de0(c, c) desde 0x508ee6, con el 0x20a, hasta su final (06, "Los efectos de los personajes
// (0x505de0)"). `hero` es c == [0x743a170].
Core::Expected<void, std::string> StepTheEffects(ActorState& actor, std::size_t slot, bool hero,
                                                 const CharacterStepContext& context,
                                                 const Data::Terrain::TerrainLayer<std::uint16_t>* walls)
{
    // 0x505e0b: un modelo sin acciones (+0x26 de 0x5769ad8 + tipo · 0xe0) va a la caja de 0x505e15. El
    // 0x20a, Player.bmd, las tiene; los demás cortan antes, en 0x4f9580.
    const bool newFace = IsNewFace(actor.type);
    if (actor.type != Data::Models::PlayerSkeletonModel && !newFace)
    {
        return Core::Unexpected(std::format("0x505e0b: el modelo {:#x} del lugar {} no está leído en 0x505de0, solo el "
                                            "0x20a",
                                            actor.type, slot));
    }
    if (newFace && context.newFaceSkeleton != nullptr && context.newFaceSkeleton->actions.empty())
    {
        return Core::Unexpected(std::string("0x505e15: la caja del NewFace sin acciones no está portada"));
    }
    // 0x505ec6..0x505ee4: la posición, la escala y la acción van al modelo compartido (+0x6c, +0x68 y
    // +0xa0), que leen los 0x4ec110 de más abajo y que el dibujo vuelve a escribir antes de posar. MuRet
    // guarda la escala y la posición del de Player.bmd, que el cuervo lee en la zona segura antes de este
    // punto de su dueño (Q-C412); los de abajo leen las del propio personaje (OwnBonePoint).
    context.world.PlacePlayerModel(actor);
    // 0x505ef2: sin verse, nada más.
    if (!actor.seen)
    {
        return {};
    }
    // 0x505ef8..0x506866 y 0x50686e..0x5068b2: la zona segura es el bit 0x01 del byte bajo de la celda
    // lógica, la de +0x41c y +0x420 (06 § Caminar), no la de la posición dibujada.
    const LogicalCell cell = ReadLogicalCell(actor, context.random);
    if (walls == nullptr)
    {
        return Core::Unexpected(std::string("0x506362: faltan los atributos de la celda lógica del personaje"));
    }
    const std::uint16_t attribute = (*walls)[Data::Terrain::CellIndex(cell.x, cell.y)];
    actor.safeZone = static_cast<std::uint8_t>((attribute & World::SafeZoneWallBit) != 0 ? 1 : 0);
    // 0x5068b9..0x5069ab: la cabeza, sin +0x78 & 0x20 ni & 0x1000 (0x5068be..0x5068d4). 0x5068da: con otro
    // modelo, antes 0x4ea190, que ya cortó en 0x505e0b. 0x5068eb..0x5068fb: otro que el héroe, sin morirse
    // (+0x389), sortea con rand() % 32 y, si da 0, dos más hacia dónde mira (+0x34 y +0x38); después los dos
    // ángulos giran hacia el blanco (0x4e96f0 y 0x4e9560). El héroe no sortea ni gasta: gira hacia el blanco
    // que MoveHero escribe con el mouse (0x59b1c2..0x59b287, CL7.4), que MuRet todavía no escribe.
    if ((actor.flags78 & StillHeadFlag) == 0 && (actor.flags78 & FrozenFlag) == 0)
    {
        if (newFace)
        {
            constexpr std::uint8_t TrackingAction = 2;
            if (actor.animation.action == TrackingAction)
            {
                return Core::Unexpected(std::string("0x4ea229: falta el blanco +0x3a4 del NewFace (0x4ea0e0)"));
            }
            SetTheNewFaceHeadTarget(actor, context.random);
        }
        StepCharacterHead(actor.headTurn, !hero && actor.dying == 0, context.random);
    }
    if (auto cooldown = StepActorCooldown(actor, context.random, context.cooldown); !cooldown.HasValue())
    {
        return cooldown;
    }
    // 0x506acf..0x506b62: con +0x375 debajo de 6, la luz de cada mano, la derecha (+0x2b8) y después la
    // izquierda (+0x2d8): 0x5f9780 con la x y la y del objeto (+0x10, +0x14) sobre el buffer dinámico
    // 0x7b4ccc4, el de WorldState, que 0x5fbf60 repone en cada paso. Sin rand().
    if (actor.pk < FirstPkWithoutLights)
    {
        for (const ActorItemPart& hand : actor.hands)
        {
            const auto light =
                std::ranges::find(HandLights, hand.model, [](const HandLight& candidate) { return candidate.model; });
            if (light == HandLights.end())
            {
                continue;
            }
            if (auto added = World::AddDynamicLight(&context.world.DynamicLighting().Lighting(), actor.position,
                                                    light->color, light->radius, "0x506b50");
                !added.HasValue())
            {
                return Core::Unexpected(std::format("{} (la mano del lugar {})", added.Error(), slot));
            }
        }
    }
    // 0x506b64: +0x3d8 en 0. Positivo, con +0x3d4 en 0x611 sortea con rand() & 3 y, si da 0, tres más una
    // posición, y con 0x612 recorre los huesos con 0x4ec110; no crea nada (Q-C266). En los personajes +0x3d8 solo
    // lo escribe 0x4fd5f0, que lo baja si es positivo (0x4fd75e); los otros que escriben +0x3d4 y +0x3d8 son de la
    // UI (*estático*).
    // 0x506cac: rand() & 7, sin condición. El camino del 0x20a no lee el valor (*estático*, Q-C207).
    static_cast<void>(context.random.Next());
    // 0x506ccc..0x506cf1: el caso del tipo − 0x152; el 0x20a cae en 0x506cf8, que mira el mapa 7 solo en la
    // escena 5.
    const std::int32_t map = context.world.Runtime().map;
    if (newFace)
    {
        // 0x5085db: los seis helpers genéricos salen para estos tipos, salvo sus mapas 30 y 35.
        constexpr std::int32_t GenericEffectMap = 35;
        if (map == CastleMap || map == GenericEffectMap)
        {
            return Core::Unexpected(std::format("0x5085db: los efectos genéricos del NewFace en el mapa {} no "
                                                "están portados",
                                                map));
        }
        return {};
    }
    if (context.scene == SceneId::Game && map == ClockEffectMap)
    {
        if (auto bubbled = BlowTheAtlansBubble(actor, slot, context); !bubbled.HasValue())
        {
            return bubbled;
        }
    }
    if (map == RandomEffectMap)
    {
        if (auto rained = DropTheDevilSquareRain(actor, slot, context); !rained.HasValue())
        {
            return rained;
        }
    }
    const std::uint8_t action = actor.animation.action;
    if (action == FirstEffectAction || action == SecondEffectAction)
    {
        return Core::Unexpected(std::format("0x506e5d: la acción {:#x} del lugar {} saca los efectos 0x5ac y los de "
                                            "0x5189f0, que no están portados",
                                            action, slot));
    }
    if (action == BoneEffectAction)
    {
        return Core::Unexpected(std::format("0x506f62: la acción 0x96 del lugar {} saca diez efectos 0x5d9 en huesos "
                                            "de rand(), que no están portados",
                                            slot));
    }
    // 0x506fc1..0x507001: 0x4ec110 con el hueso de +0x2bd a un local que nadie lee, y a 0x50862f.
    // 0x50862f..0x5086ca: solo con el héroe publicado (0x508641), los pasos: el sonido de 0x505770 con +0x3e0 y
    // +0x3e1.
    if (hero)
    {
        for (const auto sound : TakeActorFootstepSounds(actor, map, context.world.Runtime().heroCellLayer))
        {
            if (sound && context.soundRequest)
            {
                context.soundRequest(*sound);
            }
        }
    }
    // 0x5086cf..0x508801: en Kalima, el 0x20a en las acciones 0x15, 0x1d a 0x21 y 0x4b (con el héroe, todas de los
    // pasos) saca el 0x6f4/1 en (0, −40, 0) girado por +0x1c (0x5fc8f0 y 0x5fcbd0), con rand() % 64
    // en la x (0x508793) y en la y (0x5087b6).
    if (IsKalima(map) && IsOneOf(KalimaParticleActions, action))
    {
        constexpr int FootstepSpan = 64, FootstepCenter = 32;
        constexpr double FootstepHeight = 50;
        constexpr std::int32_t FootstepParticle = 0x6f4, FootstepSubtype = 1;
        const auto matrix = Render::Models::OriginalAngleMatrix(actor.angles);
        glm::vec3 position = Render::Models::OriginalVectorRotate({0, -40, 0}, matrix);
        for (int axis = 0; axis < 3; ++axis)
            position[axis] = static_cast<float>(static_cast<double>(position[axis]) + actor.position[axis]);
        position.x =
            static_cast<float>(static_cast<double>(context.random.Next() % FootstepSpan - FootstepCenter) + position.x);
        position.y =
            static_cast<float>(static_cast<double>(context.random.Next() % FootstepSpan - FootstepCenter) + position.y);
        position.z = static_cast<float>(static_cast<double>(position.z) + FootstepHeight);
        if (context.particles == nullptr)
            return Core::Unexpected(
                std::format("0x5087fc: falta el pool de partículas de los pasos de Kalima, lugar {} ", slot));
        const auto made = context.particles->CreateKalimaChecked(
            FootstepParticle, position, actor.angles, glm::vec3(0.3f), FootstepSubtype, 1.0f, context.worldClock,
            context.random, static_cast<std::int32_t>(context.scene));
        if (!made.HasValue())
            return Core::Unexpected(std::string("0x5087fc: falló el alta6f4/1 de los pasos de Kalima"));
    }
    return {};
}

Core::Expected<void, std::string> StepACharacter(ActorState& actor, std::size_t slot, bool hero,
                                                 const CharacterStepContext& context)
{
    if (!actor.alive)
    {
        return {};
    }
    actor.seen = CharacterSeen(context.world.Runtime().terrainPolygon, actor.position);
    if (!hero)
    {
        if (auto other = StepOtherCharacter(actor, slot, context); !other.HasValue())
        {
            return other;
        }
    }
    if (auto body = StepTheBody(actor, slot, hero, context); !body.HasValue())
    {
        return body;
    }
    const World::WorldTerrain& terrain = context.terrain != nullptr ? *context.terrain : context.world.Map().terrain;
    return StepTheEffects(actor, slot, hero, context, terrain.attributes ? &terrain.attributes->cells : nullptr);
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

Core::Expected<void, std::string> StepTheSiegePanel(WorldRuntime& runtime)
{
    if (runtime.map != CastleMap)
    {
        runtime.siegePanelKind.reset();
        return {};
    }
    constexpr std::uint8_t NoGuild = 0xff;
    if (runtime.heroGuildPost != NoGuild)
    {
        return Core::Unexpected(std::format("0x412193: el panel del asedio con el puesto {:#x} en el gremio "
                                            "(0x6c649a) no está portado",
                                            runtime.heroGuildPost));
    }
    constexpr std::uint8_t NoGuildPanel = 0;
    // 0x412195: el panel nuevo sale de su constructor (0x20 bytes): en (0x1e9; 0x103), en la página 1, a la vista y
    // con el alfa en 1. El de la clase 0 que ya estaba queda como está.
    if (runtime.siegePanelKind != NoGuildPanel)
        runtime.siegePanel = UI::SiegePanelState{};
    runtime.siegePanelKind = NoGuildPanel;
    return {};
}

Core::Expected<void, std::string> StepCharacters(const CharacterStepContext& context)
{
    WorldState& world = context.world;
    // 0x508f06..0x508f0c: 0x412140 con el héroe, antes del lazo.
    if (auto panel = StepTheSiegePanel(world.Runtime()); !panel.HasValue())
    {
        return panel;
    }
    const WorldRuntime& runtime = world.Runtime();
    const World::WorldTerrain& terrain = context.terrain != nullptr ? *context.terrain : world.Map().terrain;
    if (!terrain.attributes)
    {
        return Core::Unexpected("0x508f14: el TerrainWall (0x7d0d348) del " + MapText(runtime.map) +
                                " no está cargado (0x61a560)");
    }
    Data::Terrain::TerrainLayer<std::uint16_t>& walls = terrain.attributes->cells;
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
        const bool hero = slot == runtime.hero;
        if (auto stepped = StepACharacter(actor, slot, hero, context); !stepped.HasValue())
        {
            return stepped;
        }
        // 0x508eed: 0x42da90 fuera del mapa 30 devuelve 0 (0x42da9a).
    }
    // Frontera (08 P-C44): 0x52e500 (0x5091d0), el paso de las estelas de 0x75e9870, sin rand(). Las
    // crea 0x52e470 desde las habilidades, los efectos y la acción 3 del caballo (0x444671); MuRet no
    // tiene la tabla. Corre también en la espera.
    return {};
}

Core::Expected<void, std::string> StepCharacter(ActorState& actor, const CharacterStepContext& context)
{
    return StepACharacter(actor, ActorPoolSize, false, context);
}
} // namespace Client::Scenes
