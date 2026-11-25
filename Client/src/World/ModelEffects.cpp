#include "World/ModelEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Models/Animation.h"
#include "Models/LogicalAccess.h"
#include "Models/ObjectState.h"
#include "Math/OriginalRotation.h"
#include "Math/RoundedMath.h"
#include "Models/Skeleton.h"
#include "Math/Ftol.h"
#include "World/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::World
{
namespace
{
// 0x518cf0, el caso 0xb4: la altura del vuelo, la vida, la velocidad de vuelo y la de la animación.
constexpr float DragonHeight = 3400.0f;
// 0x518d3c..0x518d7f: la escala de 0 o menos.
constexpr float DefaultScale = 0.9f;
constexpr std::int32_t DragonLife = 1000;
constexpr glm::vec3 DragonVelocity{0.0f, -35.0f, 0.0f};
// 0x518d0a..0x518e3c: la velocidad de la animación que pone lo común; 0x51eebb: la vida del 0x61f.
constexpr float CommonAnimationSpeed = 0.3f;
constexpr std::int32_t ArrivalLife = 20;
// 0x51e4f6: el teletransporte no consume rand().
constexpr std::int32_t TeleportLife = 10;

// 0x52096c..0x520b72: los topes de los ángulos (0x6b869c, 0x6ba2a8), el alabeo por paso (0x6b882c), el
// radio de la partida (0x6b8ac8, 0x6ba2a4), el giro (0x6b86cc, 0x6b8b94), la vida que se vuelve a
// llenar y el frame que vuelve a 0 (0x6b8bf4).
constexpr float FullTurn = Math::FullTurnAsFloat;
constexpr float RollStep = 0.5f;
constexpr float RollTarget = 30.0f;
constexpr double StartRadius = 800.0;
constexpr double TurnWindow = 60.0;
constexpr double TurnRate = 1.5;
constexpr std::int32_t LifeRefill = 10;
constexpr float LastFrame = 40.0f;
constexpr std::uint8_t NoAction = 0xff;
constexpr std::uint8_t TurningRight = 0;
constexpr std::uint8_t TurningLeft = 1;
constexpr std::uint8_t Straight = 2;

using Math::F32;

// Un ángulo fuera de (−360, 360) vuelve a 0: `fcomp` con 360 (C0 apagado, mayor o igual) y con −360
// (C0 o C3, menor o igual, o sin orden).
float WithinATurn(float angle)
{
    if (angle >= FullTurn)
    {
        angle = 0.0f;
    }
    if (!(angle > -FullTurn))
    {
        angle = 0.0f;
    }
    return angle;
}

void StepDragon(ModelEffect& dragon)
{
    glm::vec3& angles = dragon.transform.angles;
    const glm::vec3& position = dragon.transform.position;
    // La distancia a la partida, en la x87 hasta la comparación.
    const double dx = static_cast<double>(position.x) - static_cast<double>(dragon.start.x);
    const double dy = static_cast<double>(position.y) - static_cast<double>(dragon.start.y);
    const double dz = static_cast<double>(position.z) - static_cast<double>(dragon.start.z);
    const double distance = std::sqrt(((dz * dz) + (dy * dy)) + (dx * dx));
    angles.z = WithinATurn(angles.z);
    angles.y = WithinATurn(angles.y);
    // El alabeo, medio grado por paso hacia la meta del estado.
    dragon.reference = dragon.state == TurningRight ? RollTarget : dragon.state == TurningLeft ? -RollTarget : 0.0f;
    if (dragon.reference > angles.y)
    {
        angles.y = F32(static_cast<double>(angles.y) + static_cast<double>(RollStep));
    }
    if (!(dragon.reference >= angles.y))
    {
        angles.y = F32(static_cast<double>(angles.y) - static_cast<double>(RollStep));
    }
    const double yaw = angles.z;
    const auto turn = [&dragon, &angles]()
    { angles.z = F32((static_cast<double>(dragon.turnDirection) * TurnRate) + static_cast<double>(angles.z)); };
    if (distance <= StartRadius && !(distance < -StartRadius))
    {
        // Cerca de la partida: derecho, y que dé la vuelta cuando se aleje.
        dragon.state = Straight;
        dragon.flagEc = 0.0f;
        dragon.turnPending = 1.0f;
    }
    else
    {
        // `test ah, 0x40` (C3): sin orden (NaN) también es "igual a 0".
        if (dragon.turnPending < 0.0f || dragon.turnPending > 0.0f)
        {
            dragon.reference = angles.z;
            dragon.turnDirection = -dragon.turnDirection;
            dragon.turnPending = 0.0f;
        }
        if (!(yaw - TurnWindow > static_cast<double>(dragon.reference)))
        {
            turn();
            dragon.state = TurningLeft;
        }
        else if (yaw + TurnWindow >= static_cast<double>(dragon.reference))
        {
            turn();
            dragon.state = TurningRight;
        }
        else
        {
            dragon.turnPending = 1.0f;
            dragon.state = Straight;
        }
    }
    if (dragon.life <= LifeRefill)
    {
        dragon.life = DragonLife;
    }
    // `test ah, 1` (C0): sin orden (NaN) es "menor", y no vuelve a 0.
    if (dragon.animation.frame >= LastFrame)
    {
        dragon.animation.frame = 0.0f;
        dragon.animation.previousKey = 0;
        dragon.animation.action = 0;
    }
}

float CreationScale(float scale)
{
    return scale > 0.0f ? scale : DefaultScale;
}

// 0x518f95..0x519006 y 0x51a97b: sólo los stores de la rama del dragón.
void InitializeDragonEffect(ModelEffect& effect)
{
    effect.transform.position.z =
        static_cast<float>(static_cast<double>(effect.transform.position.z) + static_cast<double>(DragonHeight));
    effect.transform.angles.y = 0.0f;
    effect.life = DragonLife;
    effect.velocity = DragonVelocity;
    effect.speed = 1.0f;
    effect.state = 0;
    effect.reference = 0.0f;
    effect.turnPending = 1.0f;
    effect.turnDirection = 1.0f;
    effect.flagEc = 1.0f;
    effect.light = glm::vec3(1.0f);
    effect.start = effect.transform.position;
    effect.previousPosition = effect.transform.position;
}

// 0x518d28..0x518e36: lo que realmente escribe el prefijo; los demás campos del slot sobreviven.
void InitializeCommonEffect(ModelEffect& effect, std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                            const glm::vec3& light, float scale)
{
    effect.alive = true;
    effect.type = type;
    effect.subtype = 0;
    effect.transform.scale = CreationScale(scale);
    effect.transform.position = position;
    effect.transform.angles = angles;
    effect.vertexLight = true;
    effect.objectState = Models::ObjectState{};
    effect.animation.action = 0;
    effect.animation.frame = 0.0f;
    effect.animation.previousKey = 0;
    effect.alpha = 1.0f;
    effect.speed = CommonAnimationSpeed;
    effect.state = 0;
    effect.light = light;
    effect.velocity = glm::vec3(0.0f);
    effect.previousPosition = position;
    effect.hasOwner = false;
    effect.ownerContext = nullptr;
    effect.owner = {};
    effect.renderFlags = 0;
}

} // namespace

std::optional<std::size_t> ModelEffects::Begin(const ModelEffectArguments& arguments)
{
    return BeginModelEffect(m_effects, arguments);
}

std::optional<std::size_t> BeginModelEffect(std::span<ModelEffect> table, const ModelEffectArguments& arguments)
{
    for (std::size_t slot = 0; slot < table.size(); ++slot)
    {
        ModelEffect& effect = table[slot];
        if (effect.alive)
            continue;
        InitializeCommonEffect(effect, arguments.type, arguments.position, arguments.angles, arguments.light,
                               arguments.scale);
        effect.subtype = arguments.subtype;
        effect.hasOwner = arguments.owner != nullptr;
        effect.ownerContext = arguments.owner;
        effect.itemType = nullptr;
        effect.itemLevel = nullptr;
        effect.disturbanceState = arguments.key;
        effect.state = arguments.byte88;
        effect.byte89 = arguments.byte89;
        effect.byte1bd = arguments.byte1bd;
        effect.dword100 = 0;
        return slot;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::CreateKalimaEffect(const glm::vec3& position, const glm::vec3& angles,
                                                            const glm::vec3& light, Crt::CrtRandom& random)
{
    constexpr std::int16_t KalimaType = 9;
    constexpr std::int32_t KalimaLife = 30, ScaleSpan = 3;
    constexpr double ScaleStep = static_cast<double>(0.1f), ScaleBase = static_cast<double>(0.6f);
    constexpr float InitialSpeed = 10;
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeCommonEffect(effect, KalimaType, position, angles, light, 0.0f);
        effect.itemType = nullptr;
        effect.itemLevel = nullptr;
        effect.life = KalimaLife;
        effect.transform.scale = F32(static_cast<double>(random.Next() % ScaleSpan) * ScaleStep + ScaleBase);
        effect.speed = InitialSpeed;
        effect.disturbanceState = 0;
        effect.velocity = {-0.1f, -0.1f, -0.5f};
        return slot;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::CreateButterflyEffect(const glm::vec3& position, const glm::vec3& angles,
                                                               const glm::vec3& light, std::int32_t subtype,
                                                               float ownerScale, bool selectionScene,
                                                               Crt::CrtRandom& random)
{
    constexpr std::int32_t ButterflyLife = 250, LargeSubtype = 3;
    constexpr double OwnerPush = static_cast<double>(std::bit_cast<float>(0xc0a00000u)); // 0x6b95b4.
    constexpr std::int16_t Hidden = -2;
    constexpr std::uint16_t NoDisturbance = 0xffff;
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeCommonEffect(effect, ButterflyEffect, position, angles, light, 0.0f);
        effect.subtype = subtype;
        effect.disturbanceState = NoDisturbance;
        effect.itemType = nullptr;
        effect.itemLevel = nullptr;
        if (selectionScene)
            effect.objectState.hiddenMesh = Hidden;
        effect.life = ButterflyLife;
        effect.speed = CommonAnimationSpeed;
        effect.vertexLight = false;
        effect.state = static_cast<std::uint8_t>(random.Next() & 1);
        effect.velocity = glm::vec3(0.0f, F32(static_cast<double>(ownerScale) * OwnerPush), 0.0f);
        effect.transform.scale = subtype == LargeSubtype ? std::bit_cast<float>(0x3f666666u) : 0.25f;
        return slot;
    }
    return std::nullopt;
}

namespace
{
// 0x6b87b8: el décimo de las escalas y del empuje de las piedras.
constexpr double StoneTenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
constexpr std::uint16_t NoDisturbance = 0xffff;

// El prefijo de 0x518cf0 con el +0x8a en −1 y sin dueño (+0x108 en 0): los que crean los objetos del 1 y
// del 4 y el chorro.
void InitializeOwnerlessEffect(ModelEffect& effect, std::int16_t type, const glm::vec3& position,
                               const glm::vec3& angles, const glm::vec3& light)
{
    InitializeCommonEffect(effect, type, position, angles, light, 0.0f);
    effect.disturbanceState = NoDisturbance;
    effect.itemType = nullptr;
    effect.itemLevel = nullptr;
}

// fld del corrimiento girado y fadd de la posición: cada eje se guarda de su suma.
void AddRotated(glm::vec3& position, const glm::vec3& rotated)
{
    for (int axis = 0; axis < 3; ++axis)
    {
        position[axis] = F32(static_cast<double>(rotated[axis]) + static_cast<double>(position[axis]));
    }
}
} // namespace

std::optional<std::size_t> ModelEffects::CreateFadingDecal(const glm::vec3& position, const glm::vec3& angles,
                                                           const glm::vec3& light, std::int32_t subtype,
                                                           std::uint16_t disturbance)
{
    constexpr std::int32_t DecalLife = 20, FirstDecalLife = 15;
    constexpr float DecalScale = 0.5f; // 0x51ee9d: 0x3f000000.
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeCommonEffect(effect, FadingDecalEffect, position, angles, light, 0.0f);
        effect.subtype = subtype;
        effect.disturbanceState = disturbance;
        effect.itemType = nullptr;
        effect.itemLevel = nullptr;
        effect.life = DecalLife;
        effect.transform.scale = DecalScale;
        if (subtype == 0)
            effect.life = FirstDecalLife; // 0x51eeae
        return slot;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::CreatePetSummon(const glm::vec3& position, const glm::vec3& angles,
                                                         const glm::vec3& light, void* owner, std::int32_t subtype)
{
    constexpr std::int32_t LastPlainSubtype = 3;
    if (subtype < 0 || subtype > LastPlainSubtype)
        return std::nullopt;
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeOwnerlessEffect(effect, ColoredDecalEffect, position, angles, light);
        effect.subtype = subtype;
        effect.hasOwner = owner != nullptr;
        effect.ownerContext = owner;
        effect.life = ArrivalLife; // 0x51eec0: la del 0x61e y el 0x61f.
        return slot;
    }
    return std::nullopt;
}

namespace
{
// +0x108: el dueño y lo que de él se lee en vivo.
void SetOwner(ModelEffect& effect, const ModelEffectOwner& owner)
{
    effect.owner = owner;
    effect.hasOwner = owner.type != nullptr || owner.effect != nullptr || owner.angles != nullptr;
}

// 0x51a6e4..0x51a85c: la vida de cada grieta, o nada con el subtipo que sale por 0x51db73 sin caso.
std::optional<std::int32_t> CrackLife(std::int16_t type, std::int32_t subtype)
{
    constexpr std::int32_t Short = 35, Long = 40;
    switch (type)
    {
    case FirstEarthquakeCrack: // 0x51a6e4: 0x23 o 0x3c.
        if (subtype == 0)
            return Short;
        if (subtype == 1)
            return 60;
        return std::nullopt;
    case 0xfc: // 0x51a73c: 0x14 o 0x32.
        if (subtype == 0 || subtype == 1)
            return 20;
        if (subtype == 2)
            return 50;
        return std::nullopt;
    case 0xfd:  // 0x51a78b
    case 0xfe:  // 0x51a7ac
    case 0x100: // 0x51a7e3
        return Short;
    default: // 0xff (0x51a7d0), 0x101 (0x51a804) y 0x102 (0x51a828).
        return Long;
    }
}
} // namespace

std::optional<std::uint16_t> OwnerTypeOf(const ModelEffectOwner& owner)
{
    if (owner.effect != nullptr)
        return static_cast<std::uint16_t>(owner.effect->type);
    if (owner.type != nullptr)
        return *owner.type;
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::CreateEarthquake(const glm::vec3& position, const glm::vec3& angles,
                                                          const glm::vec3& light, std::uint8_t state,
                                                          const ModelEffectOwner& owner, std::uint8_t& ownRecordDepth,
                                                          std::uint8_t& ownRecordCount, Crt::CrtRandom& random)
{
    constexpr std::int32_t EarthquakeLife = 20, Subtypes = 100;
    constexpr double Yaw = 330.0;           // 0x6b8c8c
    constexpr double CopiedPitch = 80.0;    // 0x6b9ffc
    constexpr double CopiedYaw = 180.0;     // 0x6b86bc
    constexpr float EarthquakeLift = 50.0f; // 0x51a6d5: 0x42480000.
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeOwnerlessEffect(effect, EarthquakeEffect, position, angles, light);
        // 0x518db5..0x518dcb: los argumentos comunes de las altas 0xfa son cero aun al reutilizar un lugar.
        effect.byte89 = 0;
        effect.byte1bd = 0;
        effect.dword100 = 0;
        effect.state = state;
        SetOwner(effect, owner);
        effect.copiedAngles = effect.transform.angles;
        effect.start = effect.transform.position;
        effect.life = EarthquakeLife;
        effect.subtype = random.Next() % Subtypes;
        glm::vec3& turned = effect.transform.angles;
        turned.z = F32(static_cast<double>(turned.z) + Yaw);
        effect.copiedAngles.x = F32(static_cast<double>(effect.copiedAngles.x) + CopiedPitch);
        effect.copiedAngles.z = F32(static_cast<double>(effect.copiedAngles.z) + CopiedYaw);
        effect.turnDirection = EarthquakeLift;
        // 0x51a8d8..0x51a90f: la cuenta sube entre la apertura y el cierre.
        const Models::LogicalAccess access(ownRecordDepth, random, Models::OwnRecordCloseRandoms);
        ++ownRecordCount;
        return slot;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::CreateEarthquakeBurst(const glm::vec3& position, const glm::vec3& angles,
                                                               const glm::vec3& light)
{
    const auto slot = Begin({.type = EarthquakeBurstEffect, .position = position, .angles = angles, .light = light});
    if (!slot)
        return slot;
    auto& effect = m_effects[*slot];
    constexpr std::int32_t BurstLife = 15;
    constexpr double Sink = 15.0;
    effect.life = BurstLife;
    effect.transform.scale = 0.5f;
    effect.transform.position.z = F32(static_cast<double>(effect.transform.position.z) - Sink);
    effect.light = glm::vec3(1.0f);
    effect.objectState.blendMesh = 0;
    effect.objectState.light = 1.5f;
    effect.previousPosition = effect.transform.position;
    return slot;
}

std::optional<std::size_t> ModelEffects::CreateEarthquakeCrack(std::int16_t type, const glm::vec3& position,
                                                               const glm::vec3& angles, const glm::vec3& light,
                                                               std::int32_t subtype, const ModelEffectOwner& owner,
                                                               std::int16_t size)
{
    if (type < FirstEarthquakeCrack || type > LastEarthquakeCrack)
        return std::nullopt;
    constexpr double SizeStep = static_cast<double>(std::bit_cast<float>(0x3c23d70au)); // 0x6b86c0: 0,01f.
    constexpr std::int16_t KeepsItsBlend = 0xfd, AlsoKeepsItsBlend = 0x100;
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeCommonEffect(effect, type, position, angles, light, 0.0f);
        effect.subtype = subtype;
        effect.disturbanceState = static_cast<std::uint16_t>(size);
        effect.itemType = nullptr;
        effect.itemLevel = nullptr;
        SetOwner(effect, owner);
        const std::optional<std::int32_t> life = CrackLife(type, subtype);
        if (!life)
            return slot;
        effect.life = *life;
        effect.transform.scale = F32(static_cast<double>(size) * SizeStep);
        if (type != KeepsItsBlend && type != AlsoKeepsItsBlend)
            effect.objectState.blendMesh = 0;
        if (subtype == 1 && (type == 0xfc || type == 0xff || type == LastEarthquakeCrack))
            effect.renderFlags = CrackRenderFlags; // 0x51a850
        return slot;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::CreateGallopShockWave(const glm::vec3& position, const glm::vec3& angles,
                                                               const glm::vec3& light, Crt::CrtRandom& random)
{
    constexpr int ScaleSpan = 10;
    constexpr double ScaleBase = static_cast<double>(std::bit_cast<float>(0x41200000u)); // 0x6b87a8: 10.
    constexpr std::int32_t GallopLife = 20;
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeOwnerlessEffect(effect, ShockWaveEffect, position, angles, light);
        effect.subtype = GallopShockWaveSubtype;
        effect.life = GallopLife;
        // fild, más 10 (0x6b87a8) y por 0,1f (0x6b8a34), en la x87 hasta el fstp.
        effect.transform.scale = F32((static_cast<double>(random.Next() % ScaleSpan) + ScaleBase) * StoneTenth);
        return slot;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::CreateStompShockWave(const glm::vec3& position, const glm::vec3& angles,
                                                              const glm::vec3& light)
{
    constexpr std::int32_t StompLife = 30;
    constexpr float StompScale = std::bit_cast<float>(0x41a00000u); // 0x51f4b4: 20.
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeOwnerlessEffect(effect, ShockWaveEffect, position, angles, light);
        effect.subtype = StompShockWaveSubtype;
        effect.life = StompLife;
        effect.transform.scale = StompScale;
        return slot;
    }
    return std::nullopt;
}

Core::Expected<std::uint16_t, std::string> UnmaskedCellAttribute(const Data::Terrain::TerrainAttributes* attributes,
                                                                 const glm::vec3& position, std::string_view address)
{
    // 0x6b86c0: el 0,01f de los dos fmul; el producto de dos floats entra entero en el double.
    constexpr double CellsPerUnit = static_cast<double>(std::bit_cast<float>(0x3c23d70au));
    constexpr std::uint32_t RowShift = 8;
    const std::optional<std::int32_t> x = Math::OriginalFtolLow(static_cast<double>(position.x) * CellsPerUnit);
    const std::optional<std::int32_t> y = Math::OriginalFtolLow(static_cast<double>(position.y) * CellsPerUnit);
    if (!x || !y)
        return Core::Unexpected(std::format("{}: la posición por 0,01f no entra en el entero de 0x69fd3c, y su "
                                            "continuación no está observada",
                                            address));
    if (attributes == nullptr)
        return Core::Unexpected(std::format("{}: falta la tabla de atributos (0x7d0d348) para 0x5f8290", address));
    // 0x5f8290: shl eax, 8 y add, en 32 bits; el índice firmado sólo cae en la tabla entre 0 y 0xffff.
    const std::uint32_t index = (static_cast<std::uint32_t>(*y) << RowShift) + static_cast<std::uint32_t>(*x);
    if (index >= Data::Terrain::TerrainCellCount)
        return Core::Unexpected(
            std::format("{}: la celda ({}, {}) de 0x5f8290 cae fuera de los atributos (0x7d0d348)", address, *x, *y));
    return attributes->cells[index];
}

Core::Expected<std::optional<std::size_t>, std::string>
ModelEffects::CreateGroundStone(std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                                const glm::vec3& light, const Data::Terrain::TerrainAttributes* attributes,
                                Crt::CrtRandom& random)
{
    if (type != FirstGroundStoneEffect && type != SecondGroundStoneEffect)
        return std::optional<std::size_t>{};
    constexpr std::int32_t GroundStoneLife = 40;
    constexpr int ScaleSpan = 30, Degrees = 360;
    constexpr double ScaleStep = static_cast<double>(std::bit_cast<float>(0x3c23d70au));      // 0x6b86c0: 0,01f.
    constexpr double FirstScaleBase = static_cast<double>(std::bit_cast<float>(0x3f99999au)); // 0x6b8bb0: 1,2f.
    constexpr double SecondScaleBase = 1.0;                                                   // 0x6b87d4.
    // 0x51cc9b..0x51ccbb: el byte bajo del WORD con el bit 0x04, el 0x08 o el 0x10.
    constexpr std::uint16_t BlockingBits = 0x04 | 0x08 | 0x10;
    const bool first = type == FirstGroundStoneEffect;
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeOwnerlessEffect(effect, type, position, angles, light);
        const auto attribute =
            UnmaskedCellAttribute(attributes, effect.transform.position, first ? "0x51cc8b" : "0x51cd3b");
        if (!attribute.HasValue())
            return Core::Unexpected(attribute.Error());
        if ((attribute.Value() & BlockingBits) != 0)
        {
            // 0x51cdc0: el lugar queda con el prefijo escrito y el byte vivo en 0.
            effect.alive = false;
            return std::optional<std::size_t>{};
        }
        effect.life = GroundStoneLife;
        effect.transform.scale = F32(static_cast<double>(random.Next() % ScaleSpan) * ScaleStep +
                                     (first ? FirstScaleBase : SecondScaleBase));
        effect.speed = CommonAnimationSpeed; // 0x51ccea: el mismo 0x3e99999a del prefijo.
        effect.transform.angles.z = static_cast<float>(random.Next() % Degrees);
        // 0x51cd0c: 0x4ea030(efecto, 0) sólo cambia la acción si es otra; el prefijo la dejó en 0 (0x518dc4).
        return std::optional(slot);
    }
    return std::optional<std::size_t>{};
}

std::optional<std::size_t> ModelEffects::CreateDebris(std::int16_t type, const glm::vec3& position,
                                                      const glm::vec3& angles, const glm::vec3& light,
                                                      Crt::CrtRandom& random, std::int32_t subtype)
{
    constexpr int PushMask = 127, PushBase = 32, LifeMask = 15, LifeBase = 32, ScaleMask = 3, ScaleBase = 8;
    constexpr int Turn = 360, FallSpan = 5, FallBase = 2;
    constexpr double Lift = 50.0; // 0x6b86b0
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeOwnerlessEffect(effect, type, position, angles, light);
        effect.subtype = subtype; // 0x518d36: el quinto argumento en +0x4.
        // 0x51bafb..0x51bb35: el empuje en un local y la z 50 más.
        const glm::vec3 push{0.0f, F32(static_cast<double>((random.Next() & PushMask) + PushBase) * StoneTenth), 0.0f};
        glm::vec3& place = effect.transform.position;
        place.z = F32(static_cast<double>(place.z) + Lift);
        // 0x51bb38..0x51bb6f.
        effect.life = (random.Next() & LifeMask) + LifeBase;
        effect.transform.scale = F32(static_cast<double>((random.Next() & ScaleMask) + ScaleBase) * StoneTenth);
        // 0x51bb72..0x51bba4: la z de los ángulos y el empuje girado (0x5fc8f0, 0x5fcbd0) a +0xcc.
        glm::vec3& turn = effect.transform.angles;
        turn.z = static_cast<float>(random.Next() % Turn);
        effect.velocity = Math::OriginalVectorRotate(push, Math::OriginalAngleMatrix(effect.transform.angles));
        // 0x51bbac..0x51bbf5: la caída en +0xe4; el 0x10b con el subtipo 0 pasa al 1 y suma otro rand() % 5.
        effect.turnDirection = static_cast<float>((random.Next() % FallSpan) + FallBase);
        if (type == DebrisEffect && effect.subtype == 0)
        {
            effect.subtype = 1;
            effect.turnDirection =
                F32(static_cast<double>(random.Next() % FallSpan) + static_cast<double>(effect.turnDirection));
        }
        // 0x51bbfb..0x51bc3b.
        turn.x = static_cast<float>(random.Next() % Turn);
        turn.y = static_cast<float>(random.Next() % Turn);
        turn.z = static_cast<float>(random.Next() % Turn);
        effect.previousPosition = place;
        return slot;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::CreateFallingStone(const glm::vec3& position, const glm::vec3& angles,
                                                            const glm::vec3& light, Crt::CrtRandom& random)
{
    constexpr int LifeMask = 15, LifeBase = 24, ScaleMask = 7, ScaleBase = 6, FallMask = 3;
    constexpr int SideMask = 63, SideCenter = 32, BackMask = 31, BackBase = 50, HeightMask = 127, HeightBase = 200;
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeOwnerlessEffect(effect, FallingStoneEffect, position, angles, light);
        effect.life = (random.Next() & LifeMask) + LifeBase;
        effect.transform.scale = F32(static_cast<double>((random.Next() & ScaleMask) + ScaleBase) * StoneTenth);
        // fild y fchs: con 0 queda −0.
        effect.turnDirection = -static_cast<float>(random.Next() & FallMask);
        glm::vec3 offset{};
        offset.x = static_cast<float>((random.Next() & SideMask) - SideCenter);
        offset.y = -static_cast<float>((random.Next() & BackMask) + BackBase);
        offset.z = static_cast<float>((random.Next() & HeightMask) + HeightBase);
        AddRotated(effect.transform.position,
                   Math::OriginalVectorRotate(offset, Math::OriginalAngleMatrix(effect.transform.angles)));
        effect.previousPosition = effect.transform.position;
        return slot;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::CreateFlameJet(const glm::vec3& position, const glm::vec3& angles,
                                                        const glm::vec3& light, std::uint8_t& ownRecordDepth,
                                                        Crt::CrtRandom& random)
{
    constexpr std::int32_t FlameJetLife = 40;
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeOwnerlessEffect(effect, FlameJetEffect, position, angles, light);
        effect.life = FlameJetLife;
        const Models::LogicalAccess access(ownRecordDepth, random, Models::OwnRecordCloseRandoms);
        return slot;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::CreateFlameColumn(const ModelEffectArguments& arguments)
{
    constexpr std::int32_t FlameColumnLife = 15; // 0xf, 0x51ecb0
    if (arguments.type != FlameJetEffect || arguments.subtype != FlameColumnSubtype)
        return std::nullopt;
    const std::optional<std::size_t> slot = Begin(arguments);
    if (!slot)
        return std::nullopt;
    m_effects[*slot].life = FlameColumnLife;
    return slot;
}

std::optional<std::size_t> ModelEffects::CreateOwnedFlameJet(const ModelEffectArguments& arguments,
                                                             std::uint8_t& ownRecordDepth, std::uint8_t ownRecordCount,
                                                             Crt::CrtRandom& random)
{
    constexpr std::int32_t FlameJetLife = 40;
    if (arguments.type != FlameJetEffect || arguments.subtype != 0)
        return std::nullopt;
    const std::optional<std::size_t> slot = Begin(arguments);
    if (!slot)
        return std::nullopt;
    ModelEffect& effect = m_effects[*slot];
    effect.life = FlameJetLife;
    const Models::LogicalAccess access(ownRecordDepth, random, Models::OwnRecordCloseRandoms);
    effect.word8c = ownRecordCount;
    return slot;
}

std::optional<std::size_t> ModelEffects::CreateStoneChip(std::int16_t type, const glm::vec3& position,
                                                         const glm::vec3& angles, const glm::vec3& light,
                                                         Crt::CrtRandom& random)
{
    return CreateStoneChip(type, position, angles, light, random, 0);
}

std::optional<std::size_t> ModelEffects::CreateStoneChip(std::int16_t type, const glm::vec3& position,
                                                         const glm::vec3& angles, const glm::vec3& light,
                                                         Crt::CrtRandom& random, std::int32_t subtype)
{
    // 0x51b659 → 0x51b72b: los pedazos 0xea y 0xeb de las rocas 0xe7..0xe9 (CL11, lote switch) van por el mismo
    // camino, con el subtipo 0.
    constexpr std::int16_t FirstRockShard = 0xea, SecondRockShard = 0xeb;
    const bool chip = type == FirstStoneChipEffect || type == SecondStoneChipEffect;
    const bool shard = (type == FirstRockShard || type == SecondRockShard) && subtype == 0;
    const bool splinter = (type == FirstSplinterEffect || type == SecondSplinterEffect) && subtype == 0;
    // 0x51b426 → 0x51b509: la /0xc del 0xc8 y el 0xc9 (la de la columna 0x5de/3).
    const bool column = chip && subtype == ColumnStoneChipSubtype;
    if ((!chip && !shard && !splinter) ||
        (subtype != 0 && subtype != RisingStoneChipSubtype && subtype != SpinningStoneChipSubtype && !column))
        return std::nullopt;
    constexpr int PushMask = 255, PushBase = 64, LifeMask = 15, LifeBase = 32, ScaleMask = 3, ScaleBase = 8;
    constexpr int ColumnScaleBase = 15; // 0x51b564: la escala de la /0xc, ((& 3) + 15) · 0,1f.
    constexpr int Degrees = 360, FallMask = 15, FallBase = 8;
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeOwnerlessEffect(effect, type, position, angles, light);
        effect.subtype = subtype;
        if (splinter)
        {
            // 0x51b66b..0x51b6d6: los tres corrimientos, cada uno en float antes de sumarse.
            constexpr int SpreadMask = 127, SpreadCenter = 64, Lift = 180; // 0x7f, 0x40 y 0xb4
            const auto dx = static_cast<float>((random.Next() & SpreadMask) - SpreadCenter);
            const auto dy = static_cast<float>((random.Next() & SpreadMask) - SpreadCenter);
            const auto dz = static_cast<float>(random.Next() % Lift);
            effect.transform.position.x = F32(static_cast<double>(dx) + static_cast<double>(position.x));
            effect.transform.position.y = F32(static_cast<double>(dy) + static_cast<double>(position.y));
            effect.transform.position.z = F32(static_cast<double>(dz) + static_cast<double>(position.z));
        }
        // 0x51b735: la /1 inicializa a cero, sin llamar a rand() para el empuje.
        const float impulse = subtype == RisingStoneChipSubtype
                                  ? 0.0f
                                  : F32(static_cast<double>((random.Next() & PushMask) + PushBase) * StoneTenth);
        const glm::vec3 push{0.0f, impulse, 0.0f};
        effect.life = (random.Next() & LifeMask) + LifeBase;
        effect.transform.scale =
            F32(static_cast<double>((random.Next() & ScaleMask) + (column ? ColumnScaleBase : ScaleBase)) * StoneTenth);
        effect.transform.angles.z = static_cast<float>(random.Next() % Degrees);
        effect.velocity = Math::OriginalVectorRotate(push, Math::OriginalAngleMatrix(effect.transform.angles));
        effect.turnDirection = static_cast<float>((random.Next() & FallMask) + FallBase);
        if (subtype == RisingStoneChipSubtype)
            effect.copiedAngles = {};
        return slot;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::CreateGroundStoneChip(std::int16_t type, const glm::vec3& position,
                                                               const glm::vec3& angles, const glm::vec3& light,
                                                               Crt::CrtRandom& random)
{
    if (type != FirstStoneChipEffect && type != SecondStoneChipEffect && type != 0xe5)
        return std::nullopt;
    constexpr int PushMask = 255, PushBase = 64, LifeMask = 15, LifeBase = 32, ScaleMask = 3, ScaleBase = 15;
    constexpr int Degrees = 360, FallMask = 15, FallBase = 28;
    constexpr double PushStep = std::bit_cast<double>(0x3fc999999999999aull);            // 0x6ba1e8: 0,2.
    constexpr double ScaleStep = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu)); // 0x6b8a48: 0,05f.
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeOwnerlessEffect(effect, type, position, angles, light);
        effect.subtype = GroundStoneChipSubtype;
        const glm::vec3 push{0.0f, F32(static_cast<double>((random.Next() & PushMask) + PushBase) * PushStep), 0.0f};
        effect.life = (random.Next() & LifeMask) + LifeBase;
        effect.transform.scale = F32(static_cast<double>((random.Next() & ScaleMask) + ScaleBase) * ScaleStep);
        effect.transform.angles.z = static_cast<float>(random.Next() % Degrees);
        effect.velocity = Math::OriginalVectorRotate(push, Math::OriginalAngleMatrix(effect.transform.angles));
        effect.turnDirection = static_cast<float>((random.Next() & FallMask) + FallBase);
        return slot;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::CreateIceShard(const glm::vec3& position, const glm::vec3& angles,
                                                        const glm::vec3& light, Crt::CrtRandom& random)
{
    constexpr int PushMask = 255, PushBase = 64, LifeMask = 15, LifeBase = 32, ScaleMask = 3, ScaleBase = 8;
    constexpr int Degrees = 360, FallMask = 15, FallBase = 8;
    constexpr double Tenth = Math::TenthAsDouble;                        // 0x6b87b8: 0,1f.
    constexpr double Rise = 50.0;                                        // 0x6b86b0
    constexpr std::int16_t ShardBlendMesh = 0;                           // 0x51b6e0: ESI, en 0 (0x518d08)
    constexpr float ShardBlendLight = std::bit_cast<float>(0x3e99999au); // 0x51b6e3: 0,3f
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeOwnerlessEffect(effect, IceShardEffect, position, angles, light);
        effect.objectState.blendMesh = ShardBlendMesh;
        effect.objectState.light = ShardBlendLight;
        const glm::vec3 push{0.0f, F32(static_cast<double>((random.Next() & PushMask) + PushBase) * Tenth), 0.0f};
        glm::vec3& at = effect.transform.position;
        at.z = F32(static_cast<double>(at.z) + Rise);
        effect.previousPosition = at;
        effect.life = (random.Next() & LifeMask) + LifeBase;
        effect.transform.scale = F32(static_cast<double>((random.Next() & ScaleMask) + ScaleBase) * Tenth);
        effect.transform.angles.z = static_cast<float>(random.Next() % Degrees);
        effect.velocity = Math::OriginalVectorRotate(push, Math::OriginalAngleMatrix(effect.transform.angles));
        effect.turnDirection = static_cast<float>((random.Next() & FallMask) + FallBase);
        return slot;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::CreateIcarusEffect(std::int16_t type, const glm::vec3& position,
                                                            const glm::vec3& angles, const glm::vec3& light,
                                                            Crt::CrtRandom& random)
{
    if (type != IcarusCloudEffect && type != IcarusFlareEffect)
        return std::nullopt;
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeOwnerlessEffect(effect, type, position, angles, light);
        glm::vec3& at = effect.transform.position;
        if (type == IcarusCloudEffect)
        {
            constexpr std::int32_t CloudLife = 2;
            constexpr float CloudScale = 10.0f;
            constexpr double Forward = 200.0, Down = 190.0;
            effect.life = CloudLife;
            effect.objectState.blendMesh = 0;
            effect.transform.scale = CloudScale;
            at.y = F32(static_cast<double>(at.y) + Forward);
            at.z = F32(static_cast<double>(at.z) - Down);
            effect.vertexLight = false;
            effect.previousPosition = at;
            return slot;
        }
        constexpr float FlareLight = std::bit_cast<float>(0x3e99999au);
        constexpr int SpeedSpan = 5, SpeedBase = 9;
        constexpr double Half = Math::HalfAsDouble;
        // 0x6ba1d0 (70° en radianes, de float a double) y 0x6ba1c8 (30°).
        constexpr double Steep = std::bit_cast<double>(0x3ff38c3540000000ull);
        constexpr double Side = std::bit_cast<double>(0x3fe0c15240000000ull);
        constexpr float FlareScale = 3.0f;
        constexpr std::int32_t FlareLife = 400;
        effect.light = glm::vec3(FlareLight);
        const double speed = static_cast<double>(random.Next() % SpeedSpan + SpeedBase) * Half;
        const double cosSteep = Math::RoundedCos(Steep);
        effect.velocity.x = F32((Math::RoundedSin(Side) * cosSteep) * speed);
        effect.velocity.y = F32((Math::RoundedCos(Side) * cosSteep) * speed);
        effect.velocity.z = F32(Math::RoundedSin(Steep) * speed);
        effect.transform.scale = FlareScale;
        effect.life = FlareLife;
        return slot;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::CreateAidaWisp(const glm::vec3& position, const glm::vec3& angles,
                                                        const glm::vec3& light, Crt::CrtRandom& random)
{
    constexpr std::int32_t WispLife = 1000;
    constexpr int Spread = 200, Turn = 360, ScaleSpan = 5, PushMask = 7;
    constexpr double Center = 100.0, Push = 5.0;
    constexpr double PushStep = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));  // 0x6b87b8.
    constexpr double ScaleStep = static_cast<double>(std::bit_cast<float>(0x3ccccccdu)); // 0x6ba1fc.
    constexpr float Dim = std::bit_cast<float>(0x3c23d70au), Blue = std::bit_cast<float>(0x3cf5c28fu);
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeOwnerlessEffect(effect, AidaWispEffect, position, angles, light);
        effect.state = 0;
        effect.objectState.light = 1.0f;
        effect.speed = 1.0f;
        effect.life = WispLife;
        effect.turnDirection = 1.0f;
        effect.flagEc = 1.0f;
        glm::vec3& at = effect.transform.position;
        // 0x519033..0x51908d: la z, la y y la x, en ese orden.
        for (int axis = 2; axis >= 0; --axis)
            at[axis] = F32(static_cast<double>(random.Next() % Spread) - Center + static_cast<double>(at[axis]));
        effect.transform.angles.z =
            F32(static_cast<double>(random.Next() % Turn) + static_cast<double>(effect.transform.angles.z));
        // 0x5190a9..0x5190d8: la x y la z de la velocidad con esi, que el prefijo dejó en 0.
        effect.velocity = {0.0f, F32(static_cast<double>(random.Next() & PushMask) * PushStep - Push), 0.0f};
        effect.transform.scale = F32(static_cast<double>(random.Next() % ScaleSpan) * ScaleStep +
                                     static_cast<double>(effect.transform.scale));
        effect.start = at;
        effect.light = {Dim, Dim, Blue};
        effect.previousPosition = at;
        return slot;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::CreateGatePlane(const glm::vec3& position, const glm::vec3& angles,
                                                         const glm::vec3& light, bool hasOwner)
{
    constexpr std::int32_t GatePlaneLife = 100;
    constexpr float GatePlaneMeshLight = std::bit_cast<float>(0x3e99999au); // 0x51d742.
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeOwnerlessEffect(effect, GatePlaneEffect, position, angles, light);
        effect.hasOwner = hasOwner;
        effect.life = GatePlaneLife;
        effect.objectState.light = GatePlaneMeshLight;
        effect.start = effect.transform.position;
        return slot;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::CreateFullSetFlare(const glm::vec3& position, const glm::vec3& angles,
                                                            const glm::vec3& light, void* owner)
{
    constexpr std::int32_t FullSetFlareLife = 30; // 0x51e492.
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeOwnerlessEffect(effect, IcarusFlareEffect, position, angles, light);
        effect.subtype = FullSetFlareSubtype;
        effect.hasOwner = owner != nullptr;
        effect.ownerContext = owner;
        effect.life = FullSetFlareLife;
        effect.speed = 0.0f; // 0x51e499.
        return slot;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::CreateSkillFlare(const glm::vec3& position, const glm::vec3& angles,
                                                          const glm::vec3& light, std::int32_t subtype, void* owner)
{
    constexpr std::int32_t SkillFlareLife = 1000; // 0x51e4a6.
    if (subtype != SwellFlareSubtype && subtype != GuardFlareSubtype)
        return std::nullopt;
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeOwnerlessEffect(effect, IcarusFlareEffect, position, angles, light);
        effect.subtype = subtype;
        effect.hasOwner = owner != nullptr;
        effect.ownerContext = owner;
        effect.life = SkillFlareLife;
        effect.speed = 0.0f; // 0x51e4ad.
        return slot;
    }
    return std::nullopt;
}

namespace
{
// 0x51db7a..0x51ddab: los lugares del rayo por rand() % 30 y los huesos del buffer global (0x68b19fc, 0x68b1abc,
// 0x68b187c, 0x68b217c, 0x68b21ac y 0x68b220c: 0x68b181c + hueso · 0x30).
constexpr std::int32_t LightningLife = 100;
constexpr std::int32_t LightningPlaces = 30;
constexpr std::size_t NeckBone = 10, ShoulderBone = 14, BackBone = 2, LeftClawBone = 50, RightClawBone = 51,
                      TailBone = 53;
constexpr double LightningScaleStep = static_cast<double>(std::bit_cast<float>(0x3b03126fu)); // 0x6b89bc: 0,002f.
constexpr double LightningScaleBase = static_cast<double>(std::bit_cast<float>(0x3e99999au)); // 0x6b89dc: 0,3f.
constexpr double LightningShrink = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));    // 0x6b8854: 0,2f.
constexpr double LightningGrowth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));    // 0x6b87b8: 0,1f.
constexpr double LightningLift = 110.0;                                                       // 0x6ba184.
constexpr float LightningAlpha = std::bit_cast<float>(0x3f333333u);                           // 0,7f.
constexpr int LightningAngleSpan = 360;
constexpr int LightningSpreadX = 240, LightningCentreX = 120, LightningSpreadY = 10, LightningCentreY = 5;
constexpr std::uint16_t LightningFlare = 0x5ac;
constexpr float LightningFlareScale = 2.0f;
constexpr std::int32_t LightningDecalLife = 200;

float Shifted(int offset, float value)
{
    return F32(static_cast<double>(offset) + static_cast<double>(value));
}
} // namespace

Core::Expected<std::optional<std::size_t>, std::string>
ModelEffects::CreateLightning(const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light, void* owner,
                              Crt::CrtRandom& random, const std::function<glm::vec3(std::size_t)>& bonePoint,
                              FrameSprites& sprites)
{
    if (!bonePoint)
        return Core::Unexpected(
            std::string("0x51dc42: falta el buffer global de la pose (0x68b181c) para el rayo 0x14d"));
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeOwnerlessEffect(effect, LightningEffect, position, angles, light);
        effect.hasOwner = owner != nullptr;
        effect.ownerContext = owner;
        effect.life = LightningLife;
        effect.transform.scale =
            F32(static_cast<double>(random.Next() % 100) * LightningScaleStep + LightningScaleBase);
        effect.subtype = 0;
        effect.alpha = LightningAlpha;
        for (int axis = 0; axis < 3; ++axis)
            effect.transform.angles[axis] = static_cast<float>(random.Next() % LightningAngleSpan);
        effect.light = light;
        float& scale = effect.transform.scale;
        glm::vec3& place = effect.transform.position;
        const std::int32_t where = random.Next() % LightningPlaces;
        if (where == 1 || where == 2)
            place = bonePoint(NeckBone);
        else if (where == 3)
        {
            scale = F32(static_cast<double>(scale) - LightningShrink);
            place = bonePoint(ShoulderBone);
        }
        else if (where == 4 || where == 5)
            place = bonePoint(BackBone);
        else if (where == 6 || where == 7)
        {
            scale = F32(static_cast<double>(scale) - LightningShrink);
            place = bonePoint((random.Next() & 1) == 0 ? LeftClawBone : RightClawBone);
        }
        else if (where == 8)
        {
            scale = F32(static_cast<double>(scale) - LightningShrink);
            place = bonePoint(TailBone);
        }
        else if (where == 9 || where == 10)
        {
            effect.alive = false;
            effect.alpha = 0.0f;
        }
        else
        {
            scale = F32(static_cast<double>(scale) + LightningGrowth);
            place.x = Shifted((random.Next() % LightningSpreadX) - LightningCentreX, place.x);
            place.y = Shifted((random.Next() % LightningSpreadY) - LightningCentreY, place.y);
            place.z = F32(static_cast<double>(place.z) + LightningLift);
        }
        effect.previousPosition = place;
        // 0x51ddab..0x51dded: la luz guardada menos 0,3f, cada canal en la x87 hasta su fstp.
        const glm::vec3 flare(F32(static_cast<double>(effect.light.x) - LightningScaleBase),
                              F32(static_cast<double>(effect.light.y) - LightningScaleBase),
                              F32(static_cast<double>(effect.light.z) - LightningScaleBase));
        static_cast<void>(sprites.Create(LightningFlare, place, LightningFlareScale, flare, 0.0f, 0));
        return std::optional(slot);
    }
    return std::optional<std::size_t>{};
}

std::optional<std::size_t> ModelEffects::CreateLightningDecal(const glm::vec3& position, const glm::vec3& angles,
                                                              const glm::vec3& light, std::int32_t subtype, void* owner,
                                                              std::uint32_t systemTime)
{
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeOwnerlessEffect(effect, LightningDecalEffect, position, angles, light);
        effect.subtype = subtype;
        effect.hasOwner = owner != nullptr;
        effect.ownerContext = owner;
        effect.life = LightningDecalLife;
        effect.textureFrame = 0;
        effect.alpha = 1.0f;
        effect.textureTime = systemTime;
        effect.transform.position.z = 0.0f; // 0x51de4d: la z en 0 (esi), después de la hora.
        effect.previousPosition = effect.transform.position;
        return slot;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::Create(std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                                                const glm::vec3& light, float scale)
{
    return Create(
        ModelEffectArguments{.type = type, .position = position, .angles = angles, .light = light, .scale = scale});
}

std::optional<std::size_t> ModelEffects::Create(const ModelEffectArguments& arguments)
{
    if (arguments.subtype != 0 ||
        (arguments.type != DragonEffect && arguments.type != ArrivalEffect && arguments.type != TeleportEffect))
        return std::nullopt;
    const auto slot = Begin(arguments);
    if (!slot)
        return std::nullopt;
    ModelEffect& effect = m_effects[*slot];
    if (arguments.type == DragonEffect)
    {
        InitializeDragonEffect(effect);
    }
    else
    {
        // 0x51eebb (llegada /0) y 0x51e4f6: el resto del lugar queda como lo dejó el prefijo.
        effect.life = arguments.type == ArrivalEffect ? ArrivalLife : TeleportLife;
    }
    return slot;
}

std::optional<std::size_t> ModelEffects::CreateCrywolfTideEffect(const ModelEffectArguments& arguments)
{
    if (arguments.type != 0x145 || arguments.subtype != 2)
        return std::nullopt;
    const auto slot = Begin(arguments);
    if (!slot)
        return std::nullopt;
    auto& effect = m_effects[*slot];
    effect.objectState.blendMesh = Models::EveryMeshBlends;
    effect.life = 30;
    effect.transform.scale = 0;
    effect.velocity = {0, -40, 0};
    return slot;
}

std::optional<std::size_t> ModelEffects::CreateWaveEffect(const ModelEffectArguments& arguments)
{
    constexpr std::int32_t FirstWave = 2, LastSlowWave = 4, LastWave = 6;
    if (arguments.type != 0x114 || arguments.subtype < FirstWave || arguments.subtype > LastWave)
        return std::nullopt;
    const auto slot = Begin(arguments);
    if (!slot)
        return std::nullopt;
    auto& effect = m_effects[*slot];
    constexpr double Lift = 50.0;                                                          // 0x6b86b0
    constexpr double ScalePerKey = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu)); // 0x6b8a48, 0,05f
    effect.start = arguments.position;
    // 0x51c173: el +50 de lo común y el −50 del subtipo, cada uno con su fstp.
    effect.transform.position.z =
        F32(static_cast<double>(F32(static_cast<double>(arguments.position.z) + Lift)) - Lift);
    effect.objectState.blendMesh = Models::EveryMeshBlends;
    effect.objectState.light = 1;
    const bool slow = effect.subtype <= LastSlowWave;
    effect.life = slow ? 15 : 10;
    effect.transform.scale = F32(static_cast<double>(std::bit_cast<std::int16_t>(arguments.key)) * ScalePerKey);
    effect.disturbanceState = 0xffff;
    effect.turnDirection = std::bit_cast<float>(slow ? 0x3c23d70au : 0x3da3d70au); // 0,01f y 0,08f
    effect.transform.angles = {slow ? 90.0f : -90.0f, 0, arguments.angles.z};
    return slot;
}

std::optional<std::size_t> ModelEffects::CreatePiercingShot(const ModelEffectArguments& arguments)
{
    constexpr std::int32_t GlowLife = 10;
    constexpr glm::vec3 Offset{0, -100, 0};
    constexpr double Height = 150;
    if (arguments.type != PiercingShotGlowEffect || arguments.subtype != 0)
        return std::nullopt;
    const auto slot = Begin(arguments);
    if (!slot)
        return std::nullopt;
    auto& effect = m_effects[*slot];
    effect.life = GlowLife;
    const glm::vec3 rotated = Math::OriginalVectorRotate(Offset, Math::OriginalAngleMatrix(arguments.angles));
    effect.transform.position.x = F32(static_cast<double>(rotated.x) + arguments.position.x);
    effect.transform.position.y = F32(static_cast<double>(rotated.y) + arguments.position.y);
    effect.transform.position.z = F32(static_cast<double>(rotated.z) + arguments.position.z + Height);
    return slot;
}

std::optional<std::size_t> ModelEffects::CreatePiercingEffect(const ModelEffectArguments& arguments)
{
    if (arguments.type != 0x115)
        return std::nullopt;
    const auto slot = Begin(arguments);
    if (!slot)
        return std::nullopt;
    auto& effect = m_effects[*slot];
    constexpr std::int32_t ShortSubtype = 1, ShortLife = 6, LongLife = 10;
    constexpr double Lift = 130.0; // 0x6ba194
    effect.transform.scale = 2;
    effect.life = effect.subtype == ShortSubtype ? ShortLife : LongLife;
    effect.objectState.blendMesh = Models::EveryMeshBlends;
    effect.velocity = {0, -60, 0};
    effect.start = arguments.position;
    effect.transform.position.z = F32(static_cast<double>(effect.transform.position.z) + Lift);
    return slot;
}

std::optional<std::size_t> ModelEffects::CreateCrywolfMissile(const ModelEffectArguments& arguments)
{
    const auto slot = Begin(arguments);
    if (!slot)
        return std::nullopt;
    auto& effect = m_effects[*slot];
    effect.life = 100;
    effect.transform.scale = 1;
    effect.light = glm::vec3(1);
    effect.siegeTarget = arguments.angles;
    effect.start = arguments.position;
    if (effect.subtype >= 0 && effect.subtype <= 2)
    {
        effect.transform.angles = {40, 0, effect.subtype == 0 ? 90.0f : effect.subtype == 1 ? -90.0f : 0.0f};
        effect.velocity = {0, -80, 0};
    }
    return slot;
}

std::optional<std::size_t> ModelEffects::CreateFireworkEmitter(const ModelEffectArguments& arguments)
{
    const auto slot = Begin(arguments);
    if (!slot)
        return std::nullopt;
    auto& effect = m_effects[*slot];
    effect.transform.angles = {};
    effect.transform.position.z = 100;
    effect.life = 75;
    return slot;
}

std::optional<std::size_t> ModelEffects::CreateFireworkFlare(const ModelEffectArguments& arguments,
                                                             Crt::CrtRandom& random)
{
    const auto slot = Begin(arguments);
    if (!slot)
        return std::nullopt;
    auto& effect = m_effects[*slot];
    effect.light = glm::vec3(1);
    effect.velocity.x = static_cast<float>(random.Next() % 9 - 4);
    effect.velocity.y = static_cast<float>(random.Next() % 9 - 4);
    effect.velocity.z = 26;
    effect.life = 12;
    return slot;
}

std::optional<std::size_t> ModelEffects::CreateCraterEffect(const ModelEffectArguments& arguments)
{
    const auto slot = Begin(arguments);
    if (!slot)
        return std::nullopt;
    ModelEffect& effect = m_effects[*slot];
    if (effect.subtype >= 0 && effect.subtype <= 2)
    {
        constexpr std::array<std::int32_t, 3> Lives{60, 30, 40};
        constexpr std::array<float, 3> Sizes{4.5f, 2.5f, 3.0f};
        const auto subtype = static_cast<std::size_t>(effect.subtype);
        effect.life = Lives[subtype];
        effect.start.x = Sizes[subtype];
        effect.start.y = Sizes[subtype];
    }
    effect.light = glm::vec3(1);
    return slot;
}

std::optional<std::size_t> ModelEffects::CreateCastleShockWave(const ModelEffectArguments& arguments,
                                                               Crt::CrtRandom& random)
{
    const auto slot = Begin(arguments);
    if (!slot)
        return std::nullopt;
    ModelEffect& effect = m_effects[*slot];
    effect.life = 30;
    effect.transform.scale = 20.0f;
    // fild, más la base (0x6b87a8 o 0x6b87f4) y por 0,1f (0x6b8a34), en la x87 hasta el fstp.
    const auto tenths = [&random](int span, double base)
    { return F32((static_cast<double>(random.Next() % span) + base) * StoneTenth); };
    switch (effect.subtype)
    {
    case 1:
    case 2:
        effect.life = 20;
        effect.transform.scale = tenths(10, 10.0);
        break;
    case 3:
        effect.life = 15;
        effect.transform.scale = 1;
        break;
    case 4:
        effect.life = 10;
        effect.transform.scale = tenths(6, 6.0);
        break;
    case 5:
        effect.life = 20;
        effect.transform.scale = 9;
        break;
    case 6:
        effect.life = 50;
        effect.transform.scale = 9;
        break;
    case 7:
        effect.life = 10;
        effect.transform.scale = 1;
        break;
    case 8:
        effect.life = 10;
        effect.transform.scale = F32(static_cast<double>(random.Next() % 10 + 10) * static_cast<double>(0.2f));
        break;
    default:
        break;
    }
    return slot;
}

std::optional<std::size_t> ModelEffects::CreateNoriaEffect(std::int16_t type, const glm::vec3& position,
                                                           const glm::vec3& angles, glm::vec3& sourceLight,
                                                           Crt::CrtRandom& random)
{
    constexpr std::int16_t FirstTowerEffect = 322;
    constexpr std::int16_t LastTowerEffect = 324;
    if (type < FirstTowerEffect || type > LastTowerEffect)
        return std::nullopt;
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        if (m_effects[slot].alive)
            continue;
        ModelEffect& effect = m_effects[slot];
        InitializeCommonEffect(effect, type, position, angles, sourceLight, 0.0f);
        effect.life = 0xffffff;
        effect.objectState.blendMesh = Models::EveryMeshBlends;
        if (type == LastTowerEffect)
        {
            effect.transform.scale = 0.6f;
            return slot;
        }
        // La copia al efecto precede la mutación de la luz prestada (0x51c39b).
        sourceLight = glm::vec3(0.0f);
        effect.transform.scale =
            F32(static_cast<double>(random.Next() % 50) * static_cast<double>(0.01f) + static_cast<double>(1.3f));
        effect.turnDirection = F32(static_cast<double>(random.Next() % 80) * static_cast<double>(0.1f));
        effect.speed =
            F32(static_cast<double>(random.Next() % 100) * static_cast<double>(0.001f) + static_cast<double>(0.01f));
        return slot;
    }
    return std::nullopt;
}

void ModelEffects::Clear()
{
    for (ModelEffect& effect : m_effects)
    {
        effect.alive = false;
    }
}

std::span<ModelEffect> ModelEffects::Effects()
{
    return m_effects;
}

std::span<const ModelEffect> ModelEffects::Effects() const
{
    return m_effects;
}

void StepModelEffect(ModelEffect& effect, const Models::Skeleton* skeleton)
{
    if (!effect.alive)
    {
        return;
    }
    if (effect.type == DragonEffect)
    {
        StepDragon(effect);
    }
    // 0x52cc04..0x52cc7b: la animación a +0xd8. 0x4ec200 devuelve 0 cuando la acción termina o da la
    // vuelta (Render::Models::AdvanceAnimation, true): el frame queda en sus keys y la acción en 0xff.
    if (effect.animation.action != NoAction && skeleton != nullptr &&
        effect.animation.action < skeleton->actions.size())
    {
        const Models::ActionInfo& action = skeleton->actions[effect.animation.action];
        if (Models::AdvanceAnimation(effect.animation, action, effect.speed, Models::AnimationScene::Other))
        {
            effect.animation.frame = static_cast<float>(action.keyCount);
            effect.animation.action = NoAction;
        }
    }
    // 0x51fc80(efecto, 1): la velocidad girada por los ángulos, sumada a la posición.
    effect.previousPosition = effect.transform.position;
    // 52cbea:147 ya hizo su movimiento iterativo y salta directo a52cd94.
    if (effect.type != 0x147)
    {
        const glm::vec3 step =
            Math::OriginalVectorRotate(effect.velocity, Math::OriginalAngleMatrix(effect.transform.angles));
        glm::vec3& position = effect.transform.position;
        position.x = F32(static_cast<double>(step.x) + static_cast<double>(position.x));
        position.y = F32(static_cast<double>(step.y) + static_cast<double>(position.y));
        position.z = F32(static_cast<double>(step.z) + static_cast<double>(position.z));
    }
    --effect.life;
    if (effect.life <= 0)
    {
        effect.alive = false;
    }
}
} // namespace Client::World
