#include "WorldObjects/WorldObjects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Kernel/WorldObject.h"
#include "Maps/MapId.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Models/Animation.h"
#include "Models/ObjectState.h"
#include "Models/Skeleton.h"
#include "MapScripts/Atlans/AtlansObjectStep.h"
#include "CastleSiege/CastleSiegeObjectStep.h"
#include "Events/ChaosCastle/ChaosCastle.h"
#include "MapScripts/Dungeon/DungeonObjectStep.h"
#include "Events/EventMapObjectStep.h"
#include "Math/Ftol.h"
#include "WorldObjects/IWorldObjectCreationServices.h"
#include "Kernel/KalimaWater.h"
#include "Effects/ObjectAlpha.h"
#include "MapScripts/Noria/NoriaObjectStep.h"
#include "MapScripts/Tarkan/TarkanObjectStep.h"
#include "WorldObjects/WorldObjectMapCreation.h"
#include "WorldObjects/WorldObjectRegistrations.h"
#include "WorldObjects/WorldObjectStepServices.h"
#include "Particles/ObjectFire.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Client::WorldObjects
{
namespace
{
// 0x606660: the animation speed (0x3e23d70a), and the lantern's scale and light in the login and the selection
// (0x3f4ccccd and 0x3e4ccccd).
constexpr float ObjectSpeed = std::bit_cast<float>(0x3E23D70Au);
constexpr std::int16_t DragonOwner = 0xb4;
constexpr float DragonEffectScale = std::bit_cast<float>(0x3FCCCCCDu);
constexpr float StreetLightScale = std::bit_cast<float>(0x3F4CCCCDu);
constexpr float StreetLightLight = std::bit_cast<float>(0x3E4CCCCDu);
// 0x6069bd: Bridge01 of map 0 widens its own culling margin (+0xdc).
constexpr std::uint8_t LorenciaMap = Maps::LorenciaMap;
constexpr std::int16_t LorenciaBridge = 0x50;
constexpr float BridgeCullingMargin = std::bit_cast<float>(0xC2480000u);
// The whirlpools of map 0x37 (0x606d03): at (8600; 25000; 5000), with scales 60 and 110.
constexpr std::uint8_t LoginMap = Maps::LoginMap;
constexpr std::int16_t RedWhirlpool = 0x56;
constexpr std::int16_t DarkWhirlpool = 0x5a;
constexpr glm::vec3 WhirlpoolPlace{8600.0f, 25000.0f, 5000.0f};
constexpr float RedWhirlpoolScale = 60.0f;
constexpr float DarkWhirlpoolScale = 110.0f;
// 0x604ab0 on map 0x37: the whirlpools' turn per step (0x6b8a20 and 0x6ba154) and the red one's alpha (0x3e4ccccd).
constexpr float RedWhirlpoolTurn = std::bit_cast<float>(0x3F19999Au);
constexpr float DarkWhirlpoolTurn = std::bit_cast<float>(0x3E6B851Fu);
constexpr float RedWhirlpoolAlpha = std::bit_cast<float>(0x3E4CCCCDu);
// 0x4e9980: a tenth of what the alpha still lacks (0x6b87b8).
constexpr float AlphaApproach = std::bit_cast<float>(0x3DCCCCCDu);
// 0x606660: the position times 0.000625 gives the sector (of 16 × 16 cells).
constexpr float SectorPerUnit = std::bit_cast<float>(0x3A23D70Au);
constexpr int SectorCount = 16;

bool HasSceneCases(ObjectScene scene)
{
    return scene == ObjectScene::Login || scene == ObjectScene::CharacterSelect;
}

// 0x5ffdf0: the alpha below which it does not draw (0x3c23d70a).
constexpr float MinimumAlpha = std::bit_cast<float>(0x3C23D70Au);

// The branch of 0x604ab0 that hides: types 0x54, 0x55, 0x57 and 0x59 of map 0x37.
bool IsLoginMarker(std::int16_t type)
{
    return type == 0x54 || type == 0x55 || type == 0x57 || type == 0x59;
}
// 06 § Lo propio de Lorencia: the operations stay in binary64 (the port's PC53/RN) up to the single binary32 store,
// without rounding 1 / scale first.
void ApplyLorenciaCases(Kernel::WorldObject& object, std::int16_t type, Kernel::WorldObjectRegistrations* registrations)
{
    constexpr float TreeSpeedFactor = std::bit_cast<float>(0x3ECCCCCDu);
    constexpr float FastObjectSpeed = std::bit_cast<float>(0x3E99999Au);
    switch (type)
    {
    case 0:
    case 1:
    {
        object.boundsMinimum = glm::vec3(-150.0f, -150.0f, 0.0f);
        object.boundsMaximum = glm::vec3(150.0f, 150.0f, 500.0f);
        const double reciprocalScale = 1.0 / static_cast<double>(object.transform.scale);
        object.speed = static_cast<float>(reciprocalScale * static_cast<double>(TreeSpeedFactor));
        return;
    }
    case 6:
    case 145:
    case 146:
        if (registrations != nullptr)
        {
            RegisterWorldObject(*registrations, object.identity);
        }
        return;
    case 52:
        object.state.blendMesh = 1;
        return;
    case 59:
        object.speed = 0.0f;
        return;
    case LorenciaBridge:
        object.cullingMargin = BridgeCullingMargin;
        return;
    case 90:
    case 150:
        object.state.blendMesh = 1;
        object.speed = FastObjectSpeed;
        return;
    case 96:
    case 97:
        object.speed = FastObjectSpeed;
        return;
    case 98:
    case 119:
        object.state.blendMesh = 2;
        return;
    case 105:
        object.state.blendMesh = 3;
        return;
    case 117:
    case 122:
        object.state.blendMesh = 4;
        return;
    case 118:
        object.state.blendMesh = 8;
        return;
    case 133:
        if (registrations != nullptr)
        {
            RegisterWorldObject(*registrations, object.identity);
        }
        object.boundsMaximum = glm::vec3(40.0f, 40.0f, 160.0f);
        object.state.hiddenMesh = Kernel::HiddenObject;
        return;
    default:
        // The native table ends here: it does NOT fall into map 1's switch.
        return;
    }
}

void RegisterAuxiliaryObject(Kernel::WorldObject& object, Kernel::WorldObjectRegistrations* registrations)
{
    if (registrations != nullptr)
    {
        RegisterWorldObject(*registrations, object.identity);
    }
}

void HideTallMarker(Kernel::WorldObject& object)
{
    object.boundsMaximum = glm::vec3(40.0f, 40.0f, 160.0f);
    object.state.hiddenMesh = Kernel::HiddenObject;
}

void ApplyMapOneCases(Kernel::WorldObject& object, std::int16_t type, Kernel::WorldObjectRegistrations* registrations)
{
    if (type != 59 && type != 60)
    {
        return;
    }
    RegisterAuxiliaryObject(object, registrations);
    if (type == 60)
    {
        HideTallMarker(object);
    }
}

bool CopyMapTwoTransform(Kernel::WorldObject& object)
{
    const auto signedLow = Math::OriginalFtolLow(static_cast<double>(object.transform.angles.z));
    if (!signedLow.has_value())
    {
        return false;
    }
    using Math::FullTurn;
    const auto remainder = static_cast<float>(*signedLow % FullTurn);
    object.copiedAngles.y = object.transform.angles.y;
    object.copiedPosition.z = object.transform.position.z;
    object.copiedPosition.x = object.transform.position.x;
    object.copiedAngles.x = object.transform.angles.x;
    object.copiedPosition.y = object.transform.position.y;
    object.transform.angles.z = remainder;
    object.copiedAngles.z = remainder;
    return true;
}

bool ApplyMapTwoCases(Kernel::WorldObject& object, std::int16_t type, Kernel::WorldObjectRegistrations* registrations)
{
    switch (type)
    {
    case 19:
    case 92:
    case 93:
        object.state.blendMesh = 0;
        return true;
    case 54:
    case 56:
        object.state.blendMesh = 1;
        return true;
    case 78:
        object.state.blendMesh = 3;
        return true;
    case 22:
    case 25:
    case 40:
    case 45:
    case 55:
    case 73:
        RegisterAuxiliaryObject(object, registrations);
        return true;
    case 91:
        RegisterAuxiliaryObject(object, registrations);
        HideTallMarker(object);
        return true;
    case 100:
        object.state.hiddenMesh = Kernel::HiddenObject;
        return true;
    case 20:
    case 65:
    case 86:
    case 88:
        return CopyMapTwoTransform(object);
    default:
        return true;
    }
}

void ApplyMapThreeCases(Kernel::WorldObject& object, std::int16_t type, Kernel::WorldObjectRegistrations* registrations)
{
    switch (type)
    {
    case 1:
        object.state.blendMesh = 1;
        return;
    case 8:
        RegisterAuxiliaryObject(object, registrations);
        return;
    case 9:
        object.state.blendMesh = 3;
        return;
    case 17:
    case 19:
    case 37:
        object.state.blendMesh = 0;
        return;
    case 18:
        object.state.blendMesh = 2;
        return;
    case 38:
        RegisterAuxiliaryObject(object, registrations);
        object.state.hiddenMesh = Kernel::HiddenObject;
        return;
    default:
        return;
    }
}

bool EmitModelEffect(Kernel::WorldObject& object, IWorldObjectCreationServices* services, std::int16_t type,
                     const glm::vec3& position, float scale, Kernel::WorldObjectIdentity owner)
{
    if (services == nullptr)
    {
        return false;
    }
    return services->CreateModelEffect({type, position, scale, owner}, object);
}

bool EmitNoriaTowerEffects(Kernel::WorldObject& object, IWorldObjectCreationServices* services)
{
    // 0x606b9d..0x606cb5: each callee can change source; do not cache the position between calls.
    constexpr std::array<std::int16_t, 5> EffectTypes{322, 323, 322, 323, 324};
    constexpr std::array<float, 5> OffsetsY{0.0f, 4.0f, 8.0f, 12.0f, 20.0f};
    constexpr double OffsetZ = 350.0;
    for (std::size_t index = 0; index < EffectTypes.size(); ++index)
    {
        const float y = index == 0 ? object.transform.position.y
                                   : static_cast<float>(static_cast<double>(object.transform.position.y) +
                                                        static_cast<double>(OffsetsY[index]));
        const auto z = static_cast<float>(static_cast<double>(object.transform.position.z) + OffsetZ);
        const glm::vec3 position(object.transform.position.x, y, z);
        if (!EmitModelEffect(object, services, EffectTypes[index], position, 0.0f, 0))
        {
            return false;
        }
    }
    return true;
}

std::optional<WorldObjectCreationError> ApplyDirectMapCases(Kernel::WorldObject& object, std::int16_t type,
                                                            std::int32_t& map,
                                                            Kernel::WorldObjectRegistrations* registrations,
                                                            WorldObjectCreationContext* context)
{
    const bool projection = context == nullptr;
    switch (map)
    {
    case LorenciaMap:
        ApplyLorenciaCases(object, type, registrations);
        return std::nullopt;
    case 1:
        ApplyMapOneCases(object, type, registrations);
        return std::nullopt;
    case 2:
        if (!ApplyMapTwoCases(object, type, registrations))
        {
            return WorldObjectCreationError::InvalidAngle;
        }
        return std::nullopt;
    case 3:
        ApplyMapThreeCases(object, type, registrations);
        if (!projection && type == 322 && !EmitNoriaTowerEffects(object, context->services))
        {
            return WorldObjectCreationError::UnsupportedEffect;
        }
        return std::nullopt;
    case 7:
        if (type == 39)
        {
            RegisterAuxiliaryObject(object, registrations);
            object.state.hiddenMesh = Kernel::HiddenObject;
        }
        return std::nullopt;
    case 8:
        if (type == 78)
        {
            RegisterAuxiliaryObject(object, registrations);
        }
        return std::nullopt;
    case LoginMap:
        if (type == RedWhirlpool || type == DarkWhirlpool)
        {
            object.transform.position = WhirlpoolPlace;
            object.transform.angles = glm::vec3(0.0f);
            object.transform.scale = type == RedWhirlpool ? RedWhirlpoolScale : DarkWhirlpoolScale;
        }
        return std::nullopt;
    default:
        return projection ? std::nullopt : ApplyDefaultWorldObjectCreation(object, *context);
    }
}

void ApplySceneCases(Kernel::WorldObject& object, std::int16_t type)
{
    switch (type)
    {
    case StreetLight:
        object.transform.scale = StreetLightScale;
        object.light = glm::vec3(StreetLightLight);
        return;
    case 160:
        object.state.blendMesh = 1;
        object.transform.scale = std::bit_cast<float>(0x3D343958u);
        object.light = glm::vec3(1.0f);
        object.vertexLight = false;
        return;
    case 161:
        object.transform.scale = StreetLightScale;
        object.state.blendMesh = 0;
        object.light = glm::vec3(1.0f);
        return;
    case 162:
        object.transform.scale = std::bit_cast<float>(0x400CCCCDu);
        object.state.blendMesh = 1;
        object.vertexLight = false;
        return;
    case 163:
        object.transform.scale = 3.0f;
        object.state.blendMesh = 0;
        object.light = glm::vec3(0.5f);
        return;
    case 164:
        object.state.blendMesh = 10;
        object.light = glm::vec3(1.0f);
        return;
    case DragonOwner:
        // The synchronous emission runs after these stores, before reading the map.
        object.state.hiddenMesh = Kernel::HiddenObject;
        object.light = glm::vec3(1.0f);
        return;
    default:
        return;
    }
}

std::optional<WorldObjectCreationError> ApplySceneCreationCases(Kernel::WorldObject& object, std::int16_t type,
                                                                ObjectScene scene, WorldObjectCreationContext* context)
{
    if (!HasSceneCases(scene))
    {
        return std::nullopt;
    }
    ApplySceneCases(object, type);
    if (context != nullptr && type == DragonOwner &&
        !EmitModelEffect(object, context->services, DragonOwner, object.transform.position, DragonEffectScale,
                         object.identity))
    {
        return WorldObjectCreationError::UnsupportedEffect;
    }
    return std::nullopt;
}

Kernel::WorldObject ObjectOf(const Data::Terrain::TerrainObject& record, int sectorX, int sectorY)
{
    Kernel::WorldObject object;
    object.type = record.type;
    object.sectorX = sectorX;
    object.sectorY = sectorY;
    object.transform.position = glm::vec3(record.position[0], record.position[1], record.position[2]);
    object.basePosition = object.transform.position;
    object.transform.angles = glm::vec3(record.angle[0], record.angle[1], record.angle[2]);
    object.transform.scale = record.scale;
    object.speed = ObjectSpeed;
    return object;
}

WorldObjectCreationResult CreateObjects(std::span<const Data::Terrain::TerrainObject> records, std::int32_t& map,
                                        ObjectScene scene, Kernel::WorldObjectRegistrations* registrations,
                                        WorldObjectCreationContext* context)
{
    WorldObjectCreationResult result;
    result.objects.reserve(records.size());
    for (std::size_t index = 0; index < records.size(); ++index)
    {
        const Data::Terrain::TerrainObject& record = records[index];
        // The sector is computed before reading Z/angles/scale and before any scene case.
        const int sectorX = SectorOf(record.position[0]);
        const int sectorY = SectorOf(record.position[1]);
        if (sectorX < 0 || sectorY < 0)
        {
            continue;
        }
        Kernel::WorldObject& object = result.objects.emplace_back(ObjectOf(record, sectorX, sectorY));
        if (registrations != nullptr)
        {
            object.identity = registrations->nextIdentity++;
        }
        const ObjectScene objectScene = context == nullptr ? scene : context->scene;
        const auto sceneError = ApplySceneCreationCases(object, record.type, objectScene, context);
        const auto error =
            sceneError.has_value() ? sceneError : ApplyDirectMapCases(object, record.type, map, registrations, context);
        if (error.has_value())
        {
            result.failure = WorldObjectCreationFailure{*error, index};
            return result;
        }
    }
    return result;
}
} // namespace

WorldObjectCreationResult CreateWorldObjects(std::span<const Data::Terrain::TerrainObject> records, std::uint8_t map,
                                             ObjectScene scene)
{
    std::int32_t mapNumber = map;
    return CreateObjects(records, mapNumber, scene, nullptr, nullptr);
}

WorldObjectCreationResult CreateWorldObjects(std::span<const Data::Terrain::TerrainObject> records, std::uint8_t map,
                                             ObjectScene scene, Kernel::WorldObjectRegistrations& registrations)
{
    std::int32_t mapNumber = map;
    return CreateObjects(records, mapNumber, scene, &registrations, nullptr);
}

WorldObjectCreationResult CreateWorldObjects(std::span<const Data::Terrain::TerrainObject> records,
                                             WorldObjectCreationContext& context)
{
    return CreateObjects(records, context.map, context.scene, &context.registrations, &context);
}

namespace
{
// 0x4e9980 and 0x4ec200, the common part of 0x604ab0: the alpha toward its target and, if it did not fall below 0.01
// (0x604c3d, which exits), the animation at `speed` (the object's, except Tarkan's 8) with `skeleton`'s action, if it
// has one. Returns whether it goes on.
bool AdvanceTheObject(Kernel::WorldObject& object, const Models::Skeleton* skeleton, float speed)
{
    Effects::StepObjectAlpha(object.targetAlpha, object.alpha, object.state.light, 0);
    if (object.alpha < MinimumAlpha)
    {
        return false;
    }
    if (skeleton != nullptr && object.animation.action < skeleton->actions.size())
    {
        Models::AdvanceAnimation(object.animation, skeleton->actions[object.animation.action], speed,
                                 Models::AnimationScene::Other);
    }
    return true;
}

// The map 0x37 cases of 0x604ab0: the markers hide and the whirlpools turn.
void ApplyLoginStepCases(Kernel::WorldObject& object)
{
    if (IsLoginMarker(object.type))
    {
        object.state.hiddenMesh = Kernel::HiddenObject;
    }
    else if (object.type == RedWhirlpool)
    {
        object.transform.angles.z -= RedWhirlpoolTurn;
        object.alpha = RedWhirlpoolAlpha;
        object.light = glm::vec3(1.0f, 0.0f, 0.0f);
        object.state.blendMesh = Models::EveryMeshBlends;
    }
    else if (object.type == DarkWhirlpool)
    {
        object.transform.angles.z -= DarkWhirlpoolTurn;
        object.state.blendMesh = Models::EveryMeshBlends;
        object.alpha = 1.0f;
        object.light = glm::vec3(1.0f);
    }
}

// 0x604bb4..0x604c1d: the roofs, before the alpha. On the 0, the tavern's (HouseWall05 and 06) go off with layer 4;
// on the 2, its own with layer 3 or from 10 up (*inferred*: the hero under a roof).
constexpr std::uint8_t DeviasMap = Maps::DeviasMap;
constexpr std::uint8_t NoriaMap = Maps::NoriaMap;
constexpr std::int32_t LorenciaRoofLayer = 4;
constexpr std::int32_t DeviasRoofLayer = 3;
constexpr std::int32_t DeviasHighRoofLayers = 10;

std::optional<float> RoofTargetAlpha(std::int16_t type, std::uint8_t map, std::int32_t layer)
{
    if (map == LorenciaMap && (type == 0x7d || type == 0x7e))
    {
        return layer == LorenciaRoofLayer ? 0.0f : 1.0f;
    }
    if (map == DeviasMap && (type == 0x51 || type == 0x52 || type == 0x60 || type == 0x62 || type == 0x63))
    {
        return layer == DeviasRoofLayer || layer >= DeviasHighRoofLayers ? 0.0f : 1.0f;
    }
    return std::nullopt;
}

// 0x604db9 and 0x60508f: the blendMesh light (+0x68), (rand() & 3 + 4) · 0.1f (0x6b87b8), the product of fild and
// fmul stored in float.
constexpr int FlickerBase = 4;
using Math::Tenth;

float FlickeringLight(Crt::CrtRandom& random)
{
    const int step = (random.Next() & 3) + FlickerBase;
    return static_cast<float>(static_cast<double>(step) * static_cast<double>(Tenth));
}

// 0x604d8e: the blendMesh V (+0x70) with the clock, (−ftol(T) % 1000) · 0.001f (0x6b89c8): neg on the low dword, cdq
// and signed idiv. Without a credited ftol (an infinite clock), nullopt.
constexpr std::int32_t ScrollPeriod = 1000;
using Math::Thousandth;

std::optional<float> ScrolledV(float worldClock)
{
    const std::optional<std::int32_t> low = Math::OriginalFtolLow(static_cast<double>(worldClock));
    if (!low)
    {
        return std::nullopt;
    }
    // 32-bit neg: wraps at −2^31, like the register.
    const auto negated = static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(*low));
    const std::int32_t scroll = negated % ScrollPeriod;
    return static_cast<float>(static_cast<double>(scroll) * static_cast<double>(Thousandth));
}

// Where the case of a map 0 type that calls 0x52f360 starts: the fire of FireLight01 and 02, of Bonfire01, the
// torches of DoungeonGate01 and Bridge01, and the Light01..03 markers (with classes 0 to 2, which then hide).
std::string_view LorenciaFireCase(std::int16_t type)
{
    switch (type)
    {
    case 0x32:
        return "0x604eb0";
    case 0x33:
        return "0x604ecb";
    case 0x34:
        return "0x604f22";
    case 0x37:
        return "0x604e78";
    case 0x50:
        return "0x604e40";
    case 0x82:
        return "0x604de3";
    case 0x83:
        return "0x604e02";
    default:
        return "0x604e21";
    }
}

// 0x604d6f: map 0's cases, by type − 0x32 (the table at 0x605b58).
Core::Expected<void, std::string> StepLorenciaCases(Kernel::WorldObject& object, Crt::CrtRandom& random,
                                                    const Kernel::ObjectStepGlobals& globals)
{
    switch (object.type)
    {
    case 0x76:
    case 0x77:
        if (const std::optional<float> v = ScrolledV(globals.worldClock))
        {
            object.state.uv.y = *v;
            return {};
        }
        return Core::Unexpected(std::string("0x604d94: ftol del reloj del mundo fuera de lo acreditado"));
    case 0x75:
    case 0x7a:
        object.state.light = FlickeringLight(random);
        return {};
    case 0x32:
    case 0x33:
    case 0x34:
    case 0x37:
    case 0x50:
    case 0x82:
    case 0x83:
    case 0x84:
    {
        const auto fire = [&](std::int32_t kind, const glm::vec3& offset) -> Core::Expected<void, std::string>
        {
            const auto emitted = Particles::EmitObjectFire(kind, object, offset, random, globals);
            if (!emitted.HasValue())
            {
                return Core::Unexpected(std::format("{}: {}", LorenciaFireCase(object.type), emitted.Error()));
            }
            return {};
        };
        if (object.type >= 0x82)
        {
            const auto emitted = fire(object.type - 0x82, glm::vec3(0.0f));
            if (!emitted.HasValue())
            {
                return emitted;
            }
            object.state.hiddenMesh = Kernel::HiddenObject;
            return {};
        }
        if (object.type == 0x50 || object.type == 0x37)
        {
            const bool bridge = object.type == 0x50;
            const auto first = fire(0, bridge ? glm::vec3(90.0f, -200.0f, 30.0f) : glm::vec3(-150.0f, -150.0f, 140.0f));
            if (!first.HasValue())
            {
                return first;
            }
            return fire(0, bridge ? glm::vec3(90.0f, 200.0f, 30.0f) : glm::vec3(150.0f, -150.0f, 140.0f));
        }
        const glm::vec3 offset = object.type == 0x32   ? glm::vec3(0.0f, 0.0f, 200.0f)
                                 : object.type == 0x33 ? glm::vec3(0.0f, -30.0f, 60.0f)
                                                       : glm::vec3(0.0f, 0.0f, 60.0f);
        const auto emitted = fire(0, offset);
        if (!emitted.HasValue())
        {
            return emitted;
        }
        if (object.type == 0x34)
        {
            object.state.light = static_cast<float>(static_cast<double>(random.Next() % 6 + 4) * Tenth);
        }
        return {};
    }
    case 0x5a:
    case 0x96:
        return StepObjectLamp(object, random, globals);
    default:
        return {};
    }
}

// 0x60506d: map 2's cases, by type − 0x14 (the table at 0x605c08).
Core::Expected<void, std::string> StepDeviasCases(Kernel::WorldObject& object, Crt::CrtRandom& random,
                                                  const Kernel::ObjectStepGlobals& globals)
{
    switch (object.type)
    {
    case 0x4e:
        object.state.light = FlickeringLight(random);
        return {};
    case 0x1e:
    case 0x42:
    {
        const auto emitted = Particles::EmitObjectFire(0, object, glm::vec3(0.0f, 0.0f, 50.0f), random, globals);
        if (!emitted.HasValue())
        {
            return Core::Unexpected(std::format("0x6050b9: {}", emitted.Error()));
        }
        return {};
    }
    case 0x14:
    case 0x41:
    case 0x56:
    case 0x58:
        return StepDeviasObjectDoor(object, globals);
    default:
        return {};
    }
}
// 0x603053..0x603088: the drop of 0x603011 comes out at ±16 from the bone in x and z, with rand() % 32 − 16 (and
// with 0x8000001f and the sign correction, which never runs with rand()).
constexpr int DropSpread = 32;
constexpr int DropCenter = 16;
} // namespace

Core::Expected<void, std::string> StepTheWaterspoutFrame(Kernel::WorldObject& object, Crt::CrtRandom& random,
                                                         float worldClock)
{
    // 0x603011: the blendMesh light at 1.0 (0x3f800000), before the ftol.
    object.state.light = 1.0f;
    const std::optional<float> v = ScrolledV(worldClock);
    if (!v)
    {
        return Core::Unexpected(std::string("0x60301e: ftol del reloj del mundo fuera de lo acreditado"));
    }
    object.state.uv.y = *v;
    // 0x60303c: rand() % 2; with 1 there are no drops.
    if ((random.Next() % 2) != 0)
    {
        return {};
    }
    // 0x603053 and 0x603077: the x and the z of the first drop, in that order; the y, −20 (0xc1a00000).
    const int x = (random.Next() % DropSpread) - DropCenter;
    const int z = (random.Next() % DropSpread) - DropCenter;
    return Core::Unexpected(std::format("0x6030c7: la gota 0x5f2 del 0x69 (Waterspout01), a ({}, −20, {}) del "
                                        "hueso 1, va a las partículas del juego (0x53fe70, la tabla 0x743a198), "
                                        "que MuRet no tiene",
                                        x, z));
}

void StepWorldObject(Kernel::WorldObject& object, std::uint8_t map, const Models::Skeleton* skeleton)
{
    if (AdvanceTheObject(object, skeleton, object.speed) && map == LoginMap)
    {
        ApplyLoginStepCases(object);
    }
}

Core::Expected<void, std::string> StepWorldObjectChecked(Kernel::WorldObject& object, std::uint8_t map,
                                                         const Models::Skeleton* skeleton, Crt::CrtRandom& random,
                                                         const Kernel::ObjectStepGlobals& globals)
{
    // 0x425070: the 33 (Aida) and the 54 (the selection) reach 0x4250f0 through the chain of 0x605a62.
    constexpr std::uint8_t AidaMap = Maps::AidaMap;
    constexpr std::uint8_t SelectionWorldMap = Maps::CharacterSelectMap;
    const bool aida = map == AidaMap || map == SelectionWorldMap;
    constexpr std::uint8_t DungeonMap = Maps::DungeonMap;
    constexpr std::uint8_t LostTowerMap = Maps::LostTowerMap;
    constexpr std::uint8_t ArenaMap = Maps::ArenaMap;
    constexpr std::uint8_t AtlansMap = Maps::AtlansMap;
    constexpr std::uint8_t TarkanMap = Maps::TarkanMap;
    constexpr std::uint8_t IcarusMap = Maps::IcarusMap;
    // 0x43ff10: the 31 (Land of Trials) reaches 0x43ffc0 through the same chain.
    constexpr std::uint8_t LandOfTrialsMap = Maps::LandOfTrialsMap;
    // The 30 (Castle Siege) reaches 0x42c850, the second link, earlier.
    constexpr std::uint8_t CastleSiegeMap = Maps::CastleSiegeMap;
    constexpr std::uint8_t DevilSquareMap = Maps::DevilSquareMap;
    constexpr std::uint8_t FirstBloodCastleMap = 11;
    constexpr std::uint8_t LastBloodCastleMap = 17;
    const bool bloodCastle = map >= FirstBloodCastleMap && map <= LastBloodCastleMap;
    const bool chaosCastle = Maps::IsChaosCastle(map);
    // 0x4303c0: the 34 (Crywolf) reaches 0x4303e0, the last link of the chain of 0x605a62.
    constexpr std::uint8_t CrywolfMap = Maps::CrywolfMap;
    if (map != LorenciaMap && map != DungeonMap && map != DeviasMap && map != NoriaMap && map != LostTowerMap &&
        map != ArenaMap && map != AtlansMap && map != TarkanMap && map != DevilSquareMap && map != IcarusMap &&
        map != LoginMap && map != LandOfTrialsMap && map != CastleSiegeMap && map != CrywolfMap &&
        !Maps::IsKalimaMap(map) && !aida && !bloodCastle && !chaosCastle)
    {
        return Core::Unexpected(
            std::format("0x604ab0: el paso de los objetos del mapa {} no está leído", static_cast<int>(map)));
    }
    // 0x604ac0..0x604ba0: the 9 takes its rand() and requests its sound before everything else.
    if (map == DevilSquareMap)
    {
        if (auto prelude = Events::StepDevilSquarePrelude(random, globals); !prelude.HasValue())
            return prelude;
    }
    if (const std::optional<float> target = RoofTargetAlpha(object.type, map, globals.heroCellLayer))
    {
        object.targetAlpha = *target;
    }
    // 0x604c67..0x604c8e: the speed goes from +0xd8 to a copy, which Tarkan's 8 multiplies.
    const float speed =
        map == TarkanMap ? MapScripts::Tarkan::TarkanAnimationSpeed(object.type, object.speed) : object.speed;
    if (!AdvanceTheObject(object, skeleton, speed))
    {
        return {};
    }
    if (Maps::IsKalimaMap(map))
    {
        // 0x605a62→0x438a90: AL1 cuts the later handlers, even without a per-type store.
        constexpr std::int16_t FirstHiddenKalimaObject = 37;
        constexpr std::int16_t LastHiddenKalimaObject = 40;
        if (object.type >= FirstHiddenKalimaObject && object.type <= LastHiddenKalimaObject)
            object.state.hiddenMesh = Kernel::HiddenObject;
        return {};
    }
    if (aida)
    {
        return StepAidaObject(object, random, globals);
    }
    if (map == LandOfTrialsMap)
    {
        return StepLandOfTrialsObject(object, random, globals);
    }
    if (map == CastleSiegeMap)
    {
        return CastleSiege::StepCastleSiegeObject(object, random, globals);
    }
    if (map == CrywolfMap)
    {
        return StepCrywolfObject(object, random, globals);
    }
    if (bloodCastle)
    {
        Events::StepBloodCastleObject(object);
        return {};
    }
    // 0x605a62: no link of the chain looks at maps 18 to 23; the common part remains.
    if (chaosCastle)
        return {};
    switch (map)
    {
    case LorenciaMap:
        return StepLorenciaCases(object, random, globals);
    case DeviasMap:
        return StepDeviasCases(object, random, globals);
    case NoriaMap:
        return MapScripts::Noria::StepNoriaObject(object, random, globals);
    case DungeonMap:
        return MapScripts::Dungeon::StepDungeonObject(object, random, globals);
    case LostTowerMap:
        return MapScripts::Dungeon::StepLostTowerObject(object, random, globals);
    case ArenaMap:
        return MapScripts::Dungeon::StepArenaObject(object, globals);
    case AtlansMap:
        return MapScripts::Atlans::StepAtlansObject(object, random, globals);
    case TarkanMap:
        return MapScripts::Tarkan::StepTarkanObject(object, globals);
    case IcarusMap:
        // 0x605ab4 via 0x605ae8: the 10 has no cases; 0x6063d0 goes separately (MapScripts/Icarus/IcarusObjectStep.h).
        return {};
    case DevilSquareMap:
        // 0x605ae8 sends it to the chain of 0x605a62, which does nothing on the 9.
        return {};
    default:
        ApplyLoginStepCases(object);
        return {};
    }
}

Kernel::ObjectDraw DrawOf(const Kernel::WorldObject& object, const glm::vec3& terrainLight, std::uint8_t map)
{
    Kernel::ObjectDraw draw;
    draw.alpha = object.alpha;
    draw.state = object.state;
    draw.vertexLight = object.vertexLight;
    // 0x5ffce0: with per-vertex light, the terrain's plus the object's; without it, the terrain's times 0.1 (0x6b87b8)
    // plus the object's: the G and B products are stored in float before the sum, and R's is added from the x87.
    if (object.vertexLight)
    {
        draw.color = terrainLight + object.light;
    }
    else
    {
        draw.color.x = static_cast<float>((static_cast<double>(terrainLight.x) * static_cast<double>(AlphaApproach)) +
                                          static_cast<double>(object.light.x));
        draw.color.y = (terrainLight.y * AlphaApproach) + object.light.y;
        draw.color.z = (terrainLight.z * AlphaApproach) + object.light.z;
    }
    // 0x5ffdf0 does not draw with the alpha below 0.01, and 0x6002e0 exits with +0x58 = −2.
    draw.posed = !(object.alpha < MinimumAlpha);
    if (!draw.posed || object.state.hiddenMesh == Kernel::HiddenObject)
    {
        draw.kind = Kernel::ObjectDrawKind::Hidden;
        return draw;
    }
    if (map == LorenciaMap && object.type == Waterspout)
    {
        // 0x600a4a: without RenderBody's glColor; the 0x69 goes with per-vertex light (+0xe8), which overrides each
        // corner's color.
        draw.kind = Kernel::ObjectDrawKind::Waterspout;
        return draw;
    }
    constexpr std::uint8_t TarkanMap = Maps::TarkanMap;
    constexpr std::int16_t TarkanChromeObject = 0x51;
    if (map == TarkanMap && object.type == TarkanChromeObject)
    {
        draw.kind = Kernel::ObjectDrawKind::TarkanChrome;
        return draw;
    }
    if (map == LoginMap && object.type == DarkWhirlpool)
    {
        // The map 55 case: mesh 0 alone, white and unlit, through the blendMesh branch (GL_ZERO,
        // GL_ONE_MINUS_SRC_COLOR). Before its first step, without the blendMesh, 0x4ecc30 binds no texture and does not
        // change the blend (Q-C135): MuRet does not draw it.
        draw.kind = object.state.blendMesh == Models::EveryMeshBlends ? Kernel::ObjectDrawKind::SubtractiveMesh
                                                                      : Kernel::ObjectDrawKind::Hidden;
        draw.color = glm::vec3(1.0f);
        draw.alpha = 1.0f;
        draw.state.light = 1.0f;
        draw.vertexLight = false;
        return draw;
    }
    draw.kind = Kernel::ObjectDrawKind::Body;
    return draw;
}

int SectorOf(float coordinate)
{
    // 0x60666c: the product stays in the x87 (exact) and ftol truncates toward 0: from −1600 to 0 it is still sector 0.
    // 0x60668d..0x6066ab compares the low dword (EAX) signed with 0 and with 16, so a NaN lands in 0 (Q-C186). With an
    // infinity or outside int64, unobserved, the port does not create it.
    const double product = static_cast<double>(coordinate) * static_cast<double>(SectorPerUnit);
    const std::optional<std::int32_t> sector = Math::OriginalFtolLow(product);
    if (!sector.has_value() || *sector < 0 || *sector >= SectorCount)
    {
        return -1;
    }
    return *sector;
}

glm::vec3 StreetLightGlowLight(int random)
{
    constexpr int Span = 30;
    constexpr int Base = 70;
    constexpr double Step = static_cast<double>(0.01f);
    constexpr double Green = 0.5;
    constexpr double Blue = static_cast<double>(0.2f);
    // `cdq` and `idiv`: with a non-negative rand(), the remainder.
    const auto light = static_cast<float>(static_cast<double>((random % Span) + Base) * Step);
    return {light, static_cast<float>(static_cast<double>(light) * Green),
            static_cast<float>(static_cast<double>(light) * Blue)};
}

int SectorX(const Kernel::WorldObject& object)
{
    return object.sectorX;
}

int SectorY(const Kernel::WorldObject& object)
{
    return object.sectorY;
}

std::vector<std::size_t> DrawOrder(std::span<const Kernel::WorldObject> objects)
{
    std::vector<std::size_t> order(objects.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [&objects](std::size_t a, std::size_t b)
                     {
                         const int sectorA = (SectorX(objects[a]) * SectorCount) + SectorY(objects[a]);
                         const int sectorB = (SectorX(objects[b]) * SectorCount) + SectorY(objects[b]);
                         return sectorA < sectorB;
                     });
    return order;
}
} // namespace Client::WorldObjects
