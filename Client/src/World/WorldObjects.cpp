#include "World/WorldObjects.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Animation.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/Skeleton.h"
#include "World/DungeonObjectStep.h"
#include "World/IWorldObjectCreationServices.h"
#include "World/KalimaWorld.h"
#include "World/ObjectAlpha.h"
#include "World/NoriaObjectStep.h"
#include "World/OriginalFtol.h"
#include "World/TarkanObjectStep.h"
#include "World/WorldObjectMapCreation.h"
#include "World/WorldObjectRegistrations.h"
#include "World/WorldObjectStepServices.h"

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

namespace Client::World
{
namespace
{
// 0x606660: la velocidad de la animación (0x3e23d70a), y la escala y la luz del farol en el login y la
// selección (0x3f4ccccd y 0x3e4ccccd).
constexpr float ObjectSpeed = std::bit_cast<float>(0x3E23D70Au);
constexpr std::int16_t DragonOwner = 0xb4;
constexpr float DragonEffectScale = std::bit_cast<float>(0x3FCCCCCDu);
constexpr float StreetLightScale = std::bit_cast<float>(0x3F4CCCCDu);
constexpr float StreetLightLight = std::bit_cast<float>(0x3E4CCCCDu);
// 0x6069bd: Bridge01 del mapa 0 amplía su margen individual de recorte (+0xdc).
constexpr std::uint8_t LorenciaMap = 0;
constexpr std::int16_t LorenciaBridge = 0x50;
constexpr float BridgeCullingMargin = std::bit_cast<float>(0xC2480000u);
// Los remolinos del mapa 0x37 (0x606d03): a (8.600; 25.000; 5.000), con escalas 60 y 110.
constexpr std::uint8_t LoginMap = 0x37;
constexpr std::int16_t RedWhirlpool = 0x56;
constexpr std::int16_t DarkWhirlpool = 0x5a;
constexpr glm::vec3 WhirlpoolPlace{8600.0f, 25000.0f, 5000.0f};
constexpr float RedWhirlpoolScale = 60.0f;
constexpr float DarkWhirlpoolScale = 110.0f;
// 0x604ab0 en el mapa 0x37: el giro de los remolinos por paso (0x6b8a20 y 0x6ba154) y el alpha del
// rojo (0x3e4ccccd).
constexpr float RedWhirlpoolTurn = std::bit_cast<float>(0x3F19999Au);
constexpr float DarkWhirlpoolTurn = std::bit_cast<float>(0x3E6B851Fu);
constexpr float RedWhirlpoolAlpha = std::bit_cast<float>(0x3E4CCCCDu);
// 0x4e9980: un décimo de lo que le falta al alpha (0x6b87b8).
constexpr float AlphaApproach = std::bit_cast<float>(0x3DCCCCCDu);
// 0x606660: la posición por 0,000625 da el sector (de 16 × 16 celdas).
constexpr float SectorPerUnit = std::bit_cast<float>(0x3A23D70Au);
constexpr int SectorCount = 16;

bool HasSceneCases(ObjectScene scene)
{
    return scene == ObjectScene::Login || scene == ObjectScene::CharacterSelect;
}

// 0x5ffdf0: el alpha por debajo del cual no dibuja (0x3c23d70a).
constexpr float MinimumAlpha = std::bit_cast<float>(0x3C23D70Au);

// La rama de 0x604ab0 que oculta: los tipos 0x54, 0x55, 0x57 y 0x59 del mapa 0x37.
bool IsLoginMarker(std::int16_t type)
{
    return type == 0x54 || type == 0x55 || type == 0x57 || type == 0x59;
}
// 06 § Lo propio de Lorencia: las operaciones permanecen en binary64 (PC53/RN del port)
// hasta el único store binary32, sin redondear primero 1 / escala.
void ApplyLorenciaCases(WorldObject& object, std::int16_t type, WorldObjectRegistrations* registrations)
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
        object.state.hiddenMesh = HiddenObject;
        return;
    default:
        // La tabla nativa termina acá: NO cae al switch del mapa 1.
        return;
    }
}

void RegisterAuxiliaryObject(WorldObject& object, WorldObjectRegistrations* registrations)
{
    if (registrations != nullptr)
    {
        RegisterWorldObject(*registrations, object.identity);
    }
}

void HideTallMarker(WorldObject& object)
{
    object.boundsMaximum = glm::vec3(40.0f, 40.0f, 160.0f);
    object.state.hiddenMesh = HiddenObject;
}

void ApplyMapOneCases(WorldObject& object, std::int16_t type, WorldObjectRegistrations* registrations)
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

bool CopyMapTwoTransform(WorldObject& object)
{
    const auto signedLow = OriginalFtolLow(static_cast<double>(object.transform.angles.z));
    if (!signedLow.has_value())
    {
        return false;
    }
    constexpr std::int32_t FullTurn = 360;
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

bool ApplyMapTwoCases(WorldObject& object, std::int16_t type, WorldObjectRegistrations* registrations)
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
        object.state.hiddenMesh = HiddenObject;
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

void ApplyMapThreeCases(WorldObject& object, std::int16_t type, WorldObjectRegistrations* registrations)
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
        object.state.hiddenMesh = HiddenObject;
        return;
    default:
        return;
    }
}

bool EmitModelEffect(WorldObject& object, IWorldObjectCreationServices* services, std::int16_t type,
                     const glm::vec3& position, float scale, WorldObjectIdentity owner)
{
    if (services == nullptr)
    {
        return false;
    }
    return services->CreateModelEffect({type, position, scale, owner}, object);
}

bool EmitNoriaTowerEffects(WorldObject& object, IWorldObjectCreationServices* services)
{
    // 0x606b9d..0x606cb5: cada callee puede cambiar source; no cachear posición entre llamadas.
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

std::optional<WorldObjectCreationError> ApplyDirectMapCases(WorldObject& object, std::int16_t type, std::int32_t& map,
                                                            WorldObjectRegistrations* registrations,
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
            object.state.hiddenMesh = HiddenObject;
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

void ApplySceneCases(WorldObject& object, std::int16_t type)
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
        // La emisión síncrona se ejecuta después de estos stores, antes de leer el mapa.
        object.state.hiddenMesh = HiddenObject;
        object.light = glm::vec3(1.0f);
        return;
    default:
        return;
    }
}

std::optional<WorldObjectCreationError> ApplySceneCreationCases(WorldObject& object, std::int16_t type,
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

WorldObject ObjectOf(const Data::Terrain::TerrainObject& record, int sectorX, int sectorY)
{
    WorldObject object;
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
                                        ObjectScene scene, WorldObjectRegistrations* registrations,
                                        WorldObjectCreationContext* context)
{
    WorldObjectCreationResult result;
    result.objects.reserve(records.size());
    for (std::size_t index = 0; index < records.size(); ++index)
    {
        const Data::Terrain::TerrainObject& record = records[index];
        // El sector se calcula antes de leer Z/ángulos/escala y antes de cualquier caso de escena.
        const int sectorX = SectorOf(record.position[0]);
        const int sectorY = SectorOf(record.position[1]);
        if (sectorX < 0 || sectorY < 0)
        {
            continue;
        }
        WorldObject& object = result.objects.emplace_back(ObjectOf(record, sectorX, sectorY));
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
                                             ObjectScene scene, WorldObjectRegistrations& registrations)
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
// 0x4e9980 y 0x4ec200, lo común de 0x604ab0: el alpha hacia su meta y, si no quedó por debajo de 0,01
// (0x604c3d, que sale), la animación a `speed` (la del objeto, salvo el 8 de Tarkan) con la acción de
// `skeleton`, si tiene. Devuelve si sigue.
bool AdvanceTheObject(WorldObject& object, const Render::Models::Skeleton* skeleton, float speed)
{
    StepObjectAlpha(object.targetAlpha, object.alpha, object.state.light, 0);
    if (object.alpha < MinimumAlpha)
    {
        return false;
    }
    if (skeleton != nullptr && object.animation.action < skeleton->actions.size())
    {
        Render::Models::AdvanceAnimation(object.animation, skeleton->actions[object.animation.action], speed,
                                         Render::Models::AnimationScene::Other);
    }
    return true;
}

// Los casos del mapa 0x37 de 0x604ab0: los marcadores se ocultan y los remolinos giran.
void ApplyLoginStepCases(WorldObject& object)
{
    if (IsLoginMarker(object.type))
    {
        object.state.hiddenMesh = HiddenObject;
    }
    else if (object.type == RedWhirlpool)
    {
        object.transform.angles.z -= RedWhirlpoolTurn;
        object.alpha = RedWhirlpoolAlpha;
        object.light = glm::vec3(1.0f, 0.0f, 0.0f);
        object.state.blendMesh = Render::Models::EveryMeshBlends;
    }
    else if (object.type == DarkWhirlpool)
    {
        object.transform.angles.z -= DarkWhirlpoolTurn;
        object.state.blendMesh = Render::Models::EveryMeshBlends;
        object.alpha = 1.0f;
        object.light = glm::vec3(1.0f);
    }
}

// 0x604bb4..0x604c1d: los techos, antes del alpha. En el 0, los de la taberna (HouseWall05 y 06) se apagan
// con la capa 4; en el 2, los suyos con la capa 3 o desde la 10 (*inferido*: el héroe bajo techo).
constexpr std::uint8_t DeviasMap = 2;
constexpr std::uint8_t NoriaMap = 3;
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

// 0x604db9 y 0x60508f: la luz del blendMesh (+0x68), (rand() & 3 + 4) · 0,1f (0x6b87b8), el producto de
// fild y fmul guardado en float.
constexpr int FlickerBase = 4;
constexpr float Tenth = std::bit_cast<float>(0x3DCCCCCDu);

float FlickeringLight(Render::Models::CrtRandom& random)
{
    const int step = (random.Next() & 3) + FlickerBase;
    return static_cast<float>(static_cast<double>(step) * static_cast<double>(Tenth));
}

// 0x604d8e: la V del blendMesh (+0x70) con el reloj, (−ftol(T) % 1000) · 0,001f (0x6b89c8): neg sobre el
// dword bajo, cdq e idiv con signo. Sin un ftol acreditado (un reloj infinito), nullopt.
constexpr std::int32_t ScrollPeriod = 1000;
constexpr float Thousandth = std::bit_cast<float>(0x3A83126Fu);

std::optional<float> ScrolledV(float worldClock)
{
    const std::optional<std::int32_t> low = OriginalFtolLow(static_cast<double>(worldClock));
    if (!low)
    {
        return std::nullopt;
    }
    // neg en 32 bits: envuelve en −2^31, como el registro.
    const auto negated = static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(*low));
    const std::int32_t scroll = negated % ScrollPeriod;
    return static_cast<float>(static_cast<double>(scroll) * static_cast<double>(Thousandth));
}

// Dónde empieza el caso de un tipo del mapa 0 que llama a 0x52f360: el fuego de FireLight01 y 02, de
// Bonfire01, las antorchas de DoungeonGate01 y de Bridge01, y los marcadores Light01..03 (con las clases 0 a
// 2, que después se ocultan).
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

// 0x604d6f: los casos del mapa 0, por el tipo − 0x32 (la tabla de 0x605b58).
Core::Expected<void, std::string> StepLorenciaCases(WorldObject& object, Render::Models::CrtRandom& random,
                                                    const ObjectStepGlobals& globals)
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
            const auto emitted = EmitObjectFire(kind, object, offset, random, globals);
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
            object.state.hiddenMesh = HiddenObject;
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

// 0x60506d: los casos del mapa 2, por el tipo − 0x14 (la tabla de 0x605c08).
Core::Expected<void, std::string> StepDeviasCases(WorldObject& object, Render::Models::CrtRandom& random,
                                                  const ObjectStepGlobals& globals)
{
    switch (object.type)
    {
    case 0x4e:
        object.state.light = FlickeringLight(random);
        return {};
    case 0x1e:
    case 0x42:
    {
        const auto emitted = EmitObjectFire(0, object, glm::vec3(0.0f, 0.0f, 50.0f), random, globals);
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
// 0x603053..0x603088: la gota de 0x603011 sale a ±16 del hueso en x y en z, con rand() % 32 − 16
// (and con 0x8000001f y la corrección del signo, que con rand() no corre).
constexpr int DropSpread = 32;
constexpr int DropCenter = 16;
} // namespace

Core::Expected<void, std::string> StepTheWaterspoutFrame(WorldObject& object, Render::Models::CrtRandom& random,
                                                         float worldClock)
{
    // 0x603011: la luz del blendMesh en 1,0 (0x3f800000), antes del ftol.
    object.state.light = 1.0f;
    const std::optional<float> v = ScrolledV(worldClock);
    if (!v)
    {
        return Core::Unexpected(std::string("0x60301e: ftol del reloj del mundo fuera de lo acreditado"));
    }
    object.state.uv.y = *v;
    // 0x60303c: rand() % 2; con 1 no hay gotas.
    if ((random.Next() % 2) != 0)
    {
        return {};
    }
    // 0x603053 y 0x603077: la x y la z de la primera gota, en ese orden; la y, −20 (0xc1a00000).
    const int x = (random.Next() % DropSpread) - DropCenter;
    const int z = (random.Next() % DropSpread) - DropCenter;
    return Core::Unexpected(std::format("0x6030c7: la gota 0x5f2 del 0x69 (Waterspout01), a ({}, −20, {}) del "
                                        "hueso 1, va a las partículas del juego (0x53fe70, la tabla 0x743a198), "
                                        "que MuRet no tiene",
                                        x, z));
}

void StepWorldObject(WorldObject& object, std::uint8_t map, const Render::Models::Skeleton* skeleton)
{
    if (AdvanceTheObject(object, skeleton, object.speed) && map == LoginMap)
    {
        ApplyLoginStepCases(object);
    }
}

Core::Expected<void, std::string> StepWorldObjectChecked(WorldObject& object, std::uint8_t map,
                                                         const Render::Models::Skeleton* skeleton,
                                                         Render::Models::CrtRandom& random,
                                                         const ObjectStepGlobals& globals)
{
    // 0x425070: el 33 (Aida) y el 54 (la selección) llegan a 0x4250f0 por la cadena de 0x605a62.
    constexpr std::uint8_t AidaMap = 33;
    constexpr std::uint8_t SelectionWorldMap = 54;
    const bool aida = map == AidaMap || map == SelectionWorldMap;
    constexpr std::uint8_t DungeonMap = 1;
    constexpr std::uint8_t LostTowerMap = 4;
    constexpr std::uint8_t TarkanMap = 8;
    constexpr std::uint8_t IcarusMap = 10;
    if (map != LorenciaMap && map != DungeonMap && map != DeviasMap && map != NoriaMap && map != LostTowerMap &&
        map != TarkanMap && map != IcarusMap && map != LoginMap && !IsKalimaMap(map) && !aida)
    {
        // 0x604ac0..0x604ba0: el 9 saca rand() antes de todo, y cada mapa tiene sus casos.
        return Core::Unexpected(
            std::format("0x604ab0: el paso de los objetos del mapa {} no está leído", static_cast<int>(map)));
    }
    if (const std::optional<float> target = RoofTargetAlpha(object.type, map, globals.heroCellLayer))
    {
        object.targetAlpha = *target;
    }
    // 0x604c67..0x604c8e: la velocidad sale de +0xd8 a una copia, que el 8 de Tarkan multiplica.
    const float speed = map == TarkanMap ? TarkanAnimationSpeed(object.type, object.speed) : object.speed;
    if (!AdvanceTheObject(object, skeleton, speed))
    {
        return {};
    }
    if (IsKalimaMap(map))
    {
        // 605a62→438a90: AL1 corta los handlers posteriores, incluso sin un store por tipo.
        constexpr std::int16_t FirstHiddenKalimaObject = 37;
        constexpr std::int16_t LastHiddenKalimaObject = 40;
        if (object.type >= FirstHiddenKalimaObject && object.type <= LastHiddenKalimaObject)
            object.state.hiddenMesh = HiddenObject;
        return {};
    }
    if (aida)
    {
        return StepAidaObject(object, random, globals);
    }
    switch (map)
    {
    case LorenciaMap:
        return StepLorenciaCases(object, random, globals);
    case DeviasMap:
        return StepDeviasCases(object, random, globals);
    case NoriaMap:
        return StepNoriaObject(object, random, globals);
    case DungeonMap:
        return StepDungeonObject(object, random, globals);
    case LostTowerMap:
        return StepLostTowerObject(object, random, globals);
    case TarkanMap:
        return StepTarkanObject(object, globals);
    case IcarusMap:
        // 0x605ab4 por 0x605ae8: el 10 no tiene casos; 0x6063d0 va aparte (Scenes/IcarusObjectStep.h).
        return {};
    default:
        ApplyLoginStepCases(object);
        return {};
    }
}

ObjectDraw DrawOf(const WorldObject& object, const glm::vec3& terrainLight, std::uint8_t map)
{
    ObjectDraw draw;
    draw.alpha = object.alpha;
    draw.state = object.state;
    draw.vertexLight = object.vertexLight;
    // 0x5ffce0: con la luz por vértice, la del terreno más la del objeto; sin ella, la del terreno por
    // 0,1 (0x6b87b8) más la del objeto: el producto de G y B se guarda en float antes de la suma, y el de
    // R se suma desde la x87.
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
    // 0x5ffdf0 no dibuja con el alpha por debajo de 0,01, y 0x6002e0 sale con +0x58 = −2.
    draw.posed = !(object.alpha < MinimumAlpha);
    if (!draw.posed || object.state.hiddenMesh == HiddenObject)
    {
        draw.kind = ObjectDrawKind::Hidden;
        return draw;
    }
    if (map == LorenciaMap && object.type == Waterspout)
    {
        // 0x600a4a: sin el glColor de RenderBody; el 0x69 va con la luz por vértice (+0xe8), que pisa el
        // color de cada esquina.
        draw.kind = ObjectDrawKind::Waterspout;
        return draw;
    }
    constexpr std::uint8_t TarkanMap = 8;
    constexpr std::int16_t TarkanChromeObject = 0x51;
    if (map == TarkanMap && object.type == TarkanChromeObject)
    {
        draw.kind = ObjectDrawKind::TarkanChrome;
        return draw;
    }
    if (map == LoginMap && object.type == DarkWhirlpool)
    {
        // El caso del mapa 55: la malla 0 sola, en blanco y sin luz, por la rama del blendMesh
        // (GL_ZERO, GL_ONE_MINUS_SRC_COLOR). Antes de su primer paso, sin el blendMesh, 0x4ecc30 no ata
        // textura ni cambia la mezcla (Q-C135): MuRet no la dibuja.
        draw.kind = object.state.blendMesh == Render::Models::EveryMeshBlends ? ObjectDrawKind::SubtractiveMesh
                                                                              : ObjectDrawKind::Hidden;
        draw.color = glm::vec3(1.0f);
        draw.alpha = 1.0f;
        draw.state.light = 1.0f;
        draw.vertexLight = false;
        return draw;
    }
    draw.kind = ObjectDrawKind::Body;
    return draw;
}

int SectorOf(float coordinate)
{
    // 0x60666c: el producto queda en la x87 (exacto) y ftol trunca hacia el 0: de −1.600 a 0 todavía es
    // el sector 0. 0x60668d..0x6066ab compara con signo el dword bajo (EAX) con 0 y con 16, así que un
    // NaN cae en el 0 (Q-C186). Con un infinito o fuera de int64, sin observar, el port no lo crea.
    const double product = static_cast<double>(coordinate) * static_cast<double>(SectorPerUnit);
    const std::optional<std::int32_t> sector = OriginalFtolLow(product);
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
    // `cdq` e `idiv`: con un rand() no negativo, el resto.
    const auto light = static_cast<float>(static_cast<double>((random % Span) + Base) * Step);
    return {light, static_cast<float>(static_cast<double>(light) * Green),
            static_cast<float>(static_cast<double>(light) * Blue)};
}

int SectorX(const WorldObject& object)
{
    return object.sectorX;
}

int SectorY(const WorldObject& object)
{
    return object.sectorY;
}

std::vector<std::size_t> DrawOrder(std::span<const WorldObject> objects)
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
} // namespace Client::World
