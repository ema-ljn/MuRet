#include "World/ModelEffects.h"

#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

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
