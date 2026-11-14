#include "World/ModelEffects.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
#include "World/OriginalFtol.h"
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

// 0x52096c..0x520b72: los topes de los ángulos (0x6b869c, 0x6ba2a8), el alabeo por paso (0x6b882c), el
// radio de la partida (0x6b8ac8, 0x6ba2a4), el giro (0x6b86cc, 0x6b8b94), la vida que se vuelve a
// llenar y el frame que vuelve a 0 (0x6b8bf4).
constexpr float FullTurn = 360.0f;
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

float F32(double value)
{
    return static_cast<float>(value);
}

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

ModelEffect DragonAt(const glm::vec3& position, const glm::vec3& angles, float scale)
{
    ModelEffect effect;
    effect.alive = true;
    effect.type = DragonEffect;
    effect.transform.scale = CreationScale(scale);
    effect.transform.position = {
        position.x, position.y,
        static_cast<float>(static_cast<double>(position.z) + static_cast<double>(DragonHeight))};
    effect.transform.angles = {angles.x, 0.0f, angles.z};
    effect.life = DragonLife;
    effect.velocity = DragonVelocity;
    effect.speed = 1.0f;
    effect.turnPending = 1.0f;
    effect.turnDirection = 1.0f;
    effect.flagEc = 1.0f;
    effect.light = glm::vec3(1.0f);
    effect.start = effect.transform.position;
    effect.previousPosition = effect.transform.position;
    return effect;
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
    effect.objectState = Render::Models::ObjectState{};
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

// Lo común de 0x518d0a..0x518e3c (la escala, la posición, los ángulos, la luz y +0xd8 en 0,3) y el caso
// 0x61e/0x61f con el subtipo 0 (0x51eebb): 20 pasos de vida y la salida de 0x51db73, sin rand().
ModelEffect ArrivalAt(const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light, float scale)
{
    ModelEffect effect;
    effect.alive = true;
    effect.type = ArrivalEffect;
    effect.transform.scale = CreationScale(scale);
    effect.transform.position = position;
    effect.transform.angles = angles;
    effect.light = light;
    effect.life = ArrivalLife;
    effect.speed = CommonAnimationSpeed;
    effect.previousPosition = position;
    return effect;
}
} // namespace

std::optional<std::size_t> ModelEffects::Begin(const ModelEffectArguments& arguments)
{
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
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
                                                            const glm::vec3& light, Render::Models::CrtRandom& random)
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
                                                               Render::Models::CrtRandom& random)
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
                                                         const glm::vec3& light, void* owner)
{
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeOwnerlessEffect(effect, ColoredDecalEffect, position, angles, light);
        effect.subtype = PetSummonSubtype;
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
                                                          std::uint8_t& ownRecordCount,
                                                          Render::Models::CrtRandom& random)
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
        const Render::Models::LogicalAccess access(ownRecordDepth, random, Render::Models::OwnRecordCloseRandoms);
        ++ownRecordCount;
        return slot;
    }
    return std::nullopt;
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
                                                               const glm::vec3& light,
                                                               Render::Models::CrtRandom& random)
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
    const std::optional<std::int32_t> x = OriginalFtolLow(static_cast<double>(position.x) * CellsPerUnit);
    const std::optional<std::int32_t> y = OriginalFtolLow(static_cast<double>(position.y) * CellsPerUnit);
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
                                Render::Models::CrtRandom& random)
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
                                                      Render::Models::CrtRandom& random)
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
        effect.velocity =
            Render::Models::OriginalVectorRotate(push, Render::Models::OriginalAngleMatrix(effect.transform.angles));
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
                                                            const glm::vec3& light, Render::Models::CrtRandom& random)
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
        AddRotated(
            effect.transform.position,
            Render::Models::OriginalVectorRotate(offset, Render::Models::OriginalAngleMatrix(effect.transform.angles)));
        effect.previousPosition = effect.transform.position;
        return slot;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::CreateFlameJet(const glm::vec3& position, const glm::vec3& angles,
                                                        const glm::vec3& light, std::uint8_t& ownRecordDepth,
                                                        Render::Models::CrtRandom& random)
{
    constexpr std::int32_t FlameJetLife = 40;
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeOwnerlessEffect(effect, FlameJetEffect, position, angles, light);
        effect.life = FlameJetLife;
        const Render::Models::LogicalAccess access(ownRecordDepth, random, Render::Models::OwnRecordCloseRandoms);
        return slot;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::CreateStoneChip(std::int16_t type, const glm::vec3& position,
                                                         const glm::vec3& angles, const glm::vec3& light,
                                                         Render::Models::CrtRandom& random)
{
    if (type != FirstStoneChipEffect && type != SecondStoneChipEffect)
        return std::nullopt;
    constexpr int PushMask = 255, PushBase = 64, LifeMask = 15, LifeBase = 32, ScaleMask = 3, ScaleBase = 8;
    constexpr int Degrees = 360, FallMask = 15, FallBase = 8;
    for (std::size_t slot = 0; slot < m_effects.size(); ++slot)
    {
        ModelEffect& effect = m_effects[slot];
        if (effect.alive)
            continue;
        InitializeOwnerlessEffect(effect, type, position, angles, light);
        const glm::vec3 push{0.0f, F32(static_cast<double>((random.Next() & PushMask) + PushBase) * StoneTenth), 0.0f};
        effect.life = (random.Next() & LifeMask) + LifeBase;
        effect.transform.scale = F32(static_cast<double>((random.Next() & ScaleMask) + ScaleBase) * StoneTenth);
        effect.transform.angles.z = static_cast<float>(random.Next() % Degrees);
        effect.velocity =
            Render::Models::OriginalVectorRotate(push, Render::Models::OriginalAngleMatrix(effect.transform.angles));
        effect.turnDirection = static_cast<float>((random.Next() & FallMask) + FallBase);
        return slot;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::CreateGroundStoneChip(std::int16_t type, const glm::vec3& position,
                                                               const glm::vec3& angles, const glm::vec3& light,
                                                               Render::Models::CrtRandom& random)
{
    if (type != FirstStoneChipEffect && type != SecondStoneChipEffect)
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
        effect.velocity =
            Render::Models::OriginalVectorRotate(push, Render::Models::OriginalAngleMatrix(effect.transform.angles));
        effect.turnDirection = static_cast<float>((random.Next() & FallMask) + FallBase);
        return slot;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::CreateIceShard(const glm::vec3& position, const glm::vec3& angles,
                                                        const glm::vec3& light, Render::Models::CrtRandom& random)
{
    constexpr int PushMask = 255, PushBase = 64, LifeMask = 15, LifeBase = 32, ScaleMask = 3, ScaleBase = 8;
    constexpr int Degrees = 360, FallMask = 15, FallBase = 8;
    constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b87b8: 0,1f.
    constexpr double Rise = 50.0;                                                    // 0x6b86b0
    constexpr std::int16_t ShardBlendMesh = 0;                                       // 0x51b6e0: ESI, en 0 (0x518d08)
    constexpr float ShardBlendLight = std::bit_cast<float>(0x3e99999au);             // 0x51b6e3: 0,3f
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
        effect.velocity =
            Render::Models::OriginalVectorRotate(push, Render::Models::OriginalAngleMatrix(effect.transform.angles));
        effect.turnDirection = static_cast<float>((random.Next() & FallMask) + FallBase);
        return slot;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::CreateIcarusEffect(std::int16_t type, const glm::vec3& position,
                                                            const glm::vec3& angles, const glm::vec3& light,
                                                            Render::Models::CrtRandom& random)
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
        constexpr double Half = 0.5;
        // 0x6ba1d0 (70° en radianes, de float a double) y 0x6ba1c8 (30°).
        constexpr double Steep = std::bit_cast<double>(0x3ff38c3540000000ull);
        constexpr double Side = std::bit_cast<double>(0x3fe0c15240000000ull);
        constexpr float FlareScale = 3.0f;
        constexpr std::int32_t FlareLife = 400;
        effect.light = glm::vec3(FlareLight);
        const double speed = static_cast<double>(random.Next() % SpeedSpan + SpeedBase) * Half;
        const double cosSteep = std::cos(Steep);
        effect.velocity.x = F32((std::sin(Side) * cosSteep) * speed);
        effect.velocity.y = F32((std::cos(Side) * cosSteep) * speed);
        effect.velocity.z = F32(std::sin(Steep) * speed);
        effect.transform.scale = FlareScale;
        effect.life = FlareLife;
        return slot;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::CreateAidaWisp(const glm::vec3& position, const glm::vec3& angles,
                                                        const glm::vec3& light, Render::Models::CrtRandom& random)
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
                              Render::Models::CrtRandom& random, const std::function<glm::vec3(std::size_t)>& bonePoint,
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
    if (type != DragonEffect && type != ArrivalEffect && type != TeleportEffect)
    {
        return std::nullopt;
    }
    for (std::size_t k = 0; k < m_effects.size(); ++k)
    {
        if (m_effects[k].alive)
        {
            continue;
        }
        if (type == TeleportEffect)
        {
            InitializeCommonEffect(m_effects[k], type, position, angles, light, scale);
            m_effects[k].life = 10; // 0x51e4f6; sin rand().
            return k;
        }
        const bool seen = m_effects[k].seen;
        m_effects[k] =
            type == DragonEffect ? DragonAt(position, angles, scale) : ArrivalAt(position, angles, light, scale);
        // 0x518cf0 no escribe +0x16c: el consumidor del suelo lee el último dibujo de este lugar.
        m_effects[k].seen = seen;
        return k;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::CreateNoriaEffect(std::int16_t type, const glm::vec3& position,
                                                           const glm::vec3& angles, glm::vec3& sourceLight,
                                                           Render::Models::CrtRandom& random)
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
        effect.objectState.blendMesh = Render::Models::EveryMeshBlends;
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

void StepModelEffect(ModelEffect& effect, const Render::Models::Skeleton* skeleton)
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
        const Render::Models::ActionInfo& action = skeleton->actions[effect.animation.action];
        if (Render::Models::AdvanceAnimation(effect.animation, action, effect.speed,
                                             Render::Models::AnimationScene::Other))
        {
            effect.animation.frame = static_cast<float>(action.keyCount);
            effect.animation.action = NoAction;
        }
    }
    // 0x51fc80(efecto, 1): la velocidad girada por los ángulos, sumada a la posición.
    effect.previousPosition = effect.transform.position;
    const glm::vec3 step = Render::Models::OriginalVectorRotate(
        effect.velocity, Render::Models::OriginalAngleMatrix(effect.transform.angles));
    glm::vec3& position = effect.transform.position;
    position.x = F32(static_cast<double>(step.x) + static_cast<double>(position.x));
    position.y = F32(static_cast<double>(step.y) + static_cast<double>(position.y));
    position.z = F32(static_cast<double>(step.z) + static_cast<double>(position.z));
    --effect.life;
    if (effect.life <= 0)
    {
        effect.alive = false;
    }
}
} // namespace Client::World
