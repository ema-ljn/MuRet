#include "World/ModelEffects.h"

#include "Core/Expected.h"

#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
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
constexpr std::int32_t ArrivalLife = 20;
constexpr std::int16_t Construction135 = 0x135;
constexpr std::int16_t Construction142 = 0x142;
constexpr std::int16_t Construction143 = 0x143;
constexpr std::int16_t Construction144 = 0x144;
constexpr std::int32_t Construction135Life = 100;
constexpr std::int32_t PersistentConstructionLife = 0x00ffffff;
constexpr double Construction135Height = 200.0;
constexpr double ConstructionLightAngleStep = 4.0;
constexpr float InitialEffectSpeed = 0.3f;
constexpr glm::vec3 DragonVelocity{0.0f, -35.0f, 0.0f};

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
// 518cf0, los 36 stores de 61f/0: se escribe sobre el lugar, sin limpiar el resto.
void InitializeEffect(ModelEffect& effect, std::int16_t type, float scale, std::uint32_t owner, std::int32_t subtype)
{
    effect.alive = true;
    effect.type = type;
    effect.subtype = subtype;
    effect.vertexLight = true;
    effect.objectState.hiddenMesh = -1;
    effect.hiddenMeshHigh = 0xffff;
    effect.objectState.blendMesh = -1;
    effect.blendMeshHigh = 0xffff;
    effect.objectState.light = 1.0f;
    effect.objectState.uv.x = 0.0f;
    effect.objectState.uv.y = 0.0f;
    effect.animation.frame = 0.0f;
    effect.field16d = 0;
    effect.alpha = 1.0f;
    effect.previousFrame = 0.0f;
    effect.animation.previousKey = 0; // índice derivado de previousFrame para la pose nativa
    effect.transform.scale = scale > 0.0f ? scale : DefaultScale;
    effect.owner = owner;
    effect.speed = InitialEffectSpeed;
    effect.field8a = 0xffff;
    effect.state = 0;
    effect.field89 = 0;
    effect.field74 = 0;
    effect.field100 = 0;
    effect.animation.action = 0;
    effect.field1bd = 0;
}

// Copia por accesos originales: los alias ven los stores anteriores, sin snapshot vectorial.
void CopyEffectVectors(ModelEffect& effect, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light)
{
    effect.light.x = light.x;
    const float lightY = light.y;
    const float lightZ = light.z;
    effect.light.y = lightY;
    effect.light.z = lightZ;
    effect.transform.angles.x = angles.x;
    const float angleY = angles.y;
    const float angleZ = angles.z;
    effect.transform.angles.y = angleY;
    const float positionX = position.x; // se lee antes del store de angles.z en el ASM
    effect.transform.angles.z = angleZ;
    effect.transform.position.x = positionX;
    const float positionY = position.y;
    const float positionZ = position.z;
    effect.transform.position.y = positionY;
    effect.transform.position.z = positionZ;
    effect.velocity.x = 0.0f;
    effect.velocity.y = 0.0f;
    effect.velocity.z = 0.0f;
}

void CreateEffect(ModelEffect& effect, std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                  const glm::vec3& light, float scale, std::uint32_t owner, std::int32_t subtype)
{
    InitializeEffect(effect, type, scale, owner, subtype);
    CopyEffectVectors(effect, position, angles, light);
    if (type == ArrivalEffect)
    {
        effect.life = ArrivalLife;
    }
    else if (type == Construction135)
    {
        effect.life = Construction135Life;
        effect.objectState.light = InitialEffectSpeed;
        effect.start.x = effect.transform.position.x;
        effect.start.y = effect.transform.position.y;
        effect.start.z = effect.transform.position.z;
    }
    else
    {
        effect.life = PersistentConstructionLife;
        effect.objectState.blendMesh = -2;
        if (type == Construction144)
        {
            effect.transform.scale = std::bit_cast<float>(0x3f19999au);
        }
    }
}
// 51c39b..51c405: los tres sorteos suceden después de borrar la entrada, cada store antes del siguiente.
void RandomizeConstruction(ModelEffect& effect, glm::vec3& light, Render::Models::CrtRandom& random)
{
    constexpr float ScaleStep = std::bit_cast<float>(0x3c23d70au);
    constexpr float ScaleBase = std::bit_cast<float>(0x3fa66666u);
    constexpr float DirectionStep = std::bit_cast<float>(0x3dcccccdu);
    constexpr float SpeedStep = std::bit_cast<float>(0x3a83126fu);
    light.x = 0.0f;
    light.y = 0.0f;
    light.z = 0.0f;
    const int scaleDraw = random.Next() % 50;
    effect.transform.scale = F32((static_cast<double>(scaleDraw) * ScaleStep) + ScaleBase);
    const int directionDraw = random.Next() % 80;
    effect.turnDirection = F32(static_cast<double>(directionDraw) * DirectionStep);
    const int speedDraw = random.Next() % 100;
    effect.speed = F32((static_cast<double>(speedDraw) * SpeedStep) + ScaleStep);
}
// 51fc80 modo 1, contrato cerrado: stores XYZ antes de que el caller relea vida.
void MoveModelEffect(ModelEffect& effect)
{
    effect.previousPosition = effect.transform.position;
    const glm::vec3 delta = Render::Models::OriginalVectorRotate(
        effect.velocity, Render::Models::OriginalAngleMatrix(effect.transform.angles));
    glm::vec3& position = effect.transform.position;
    position.x = F32(static_cast<double>(delta.x) + static_cast<double>(position.x));
    position.y = F32(static_cast<double>(delta.y) + static_cast<double>(position.y));
    position.z = F32(static_cast<double>(delta.z) + static_cast<double>(position.z));
}

void DecreaseModelEffectLife(ModelEffect& effect)
{
    effect.life = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(effect.life) - 1u);
    if (effect.life <= 0)
    {
        effect.alive = 0;
    }
}

void StepConstructionBody(ModelEffect& effect, const ModelEffectStepBindings& bindings)
{
    constexpr float Time135 = std::bit_cast<float>(0x3a83126fu);
    constexpr float TimeFirst = std::bit_cast<float>(0x3aded289u);
    constexpr float TimeStored = std::bit_cast<float>(0x3aaa64c3u);
    constexpr float TimeLast = std::bit_cast<float>(0x3a902de0u);
    constexpr float LightAmplitude = std::bit_cast<float>(0x3e4ccccdu);
    constexpr float LightBase = std::bit_cast<float>(0x3c23d70au);
    if (effect.type == Construction135)
    {
        effect.life = Construction135Life;
        if (effect.owner != 0 && effect.alive == 1)
        {
            const double time = static_cast<double>(bindings.readClock()) * Time135;
            effect.transform.position.z = F32(((std::sin(time) + 1.0) * Construction135Height) + effect.start.z);
        }
        return;
    }
    const double first = static_cast<double>(bindings.readClock()) * TimeFirst;
    const float stored = F32(static_cast<double>(bindings.readClock()) * TimeStored);
    const double last = static_cast<double>(bindings.readClock()) * TimeLast;
    effect.light.x = F32((std::sin(last) * LightAmplitude) + LightBase);
    effect.light.y = F32((std::sin(first) * LightAmplitude) + LightBase);
    effect.light.z = F32((std::sin(static_cast<double>(stored)) * LightAmplitude) + LightBase);
    effect.transform.angles.y =
        F32((static_cast<double>(effect.turnDirection) + effect.transform.angles.y) + ConstructionLightAngleStep);
}

Core::Expected<void, ModelEffectStepError> StepConstructionAnimation(ModelEffect& effect,
                                                                     const ModelEffectStepBindings& bindings)
{
    ModelEffectAnimationBinding* model = bindings.animationOf(effect.type);
    if (model == nullptr)
    {
        return Core::Unexpected(ModelEffectStepError::MissingMetadata);
    }
    model->action.get() = effect.animation.action;
    if (effect.animation.action == NoAction)
    {
        return {};
    }
    const float speed = effect.speed;
    if (!bindings.advance(*model, effect, speed))
    {
        // M se conserva, pero su selector y tabla se releen después del servicio.
        if (model->action.get() >= model->actions.size())
        {
            return Core::Unexpected(ModelEffectStepError::InvalidMetadataAction);
        }
        effect.animation.frame = static_cast<float>(model->actions[model->action.get()].keyCount);
        effect.animation.action = NoAction;
    }
    return {};
}

ModelEffect CreateDragon(const glm::vec3& position, const glm::vec3& angles, float scale)
{
    ModelEffect effect;
    effect.alive = true;
    effect.type = DragonEffect;
    effect.transform.scale = scale > 0.0f ? scale : DefaultScale;
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
} // namespace

std::optional<std::size_t> ModelEffects::CreateStored(std::int16_t type, const glm::vec3& position,
                                                      const glm::vec3& angles, const glm::vec3& light, float scale,
                                                      std::uint32_t owner, std::int32_t subtype,
                                                      Render::Models::CrtRandom* random, glm::vec3* mutableLight)
{
    const bool randomized = type == Construction142 || type == Construction143;
    if (randomized && (random == nullptr || mutableLight == nullptr))
    {
        return std::nullopt;
    }
    if (!randomized && type != DragonEffect && type != ArrivalEffect && type != Construction135 &&
        type != Construction144)
    {
        return std::nullopt;
    }
    for (std::size_t k = 0; k < m_effects.size(); ++k)
    {
        if (m_effects[k].alive)
        {
            continue;
        }
        if (type != DragonEffect)
        {
            CreateEffect(m_effects[k], type, position, angles, light, scale, owner, subtype);
            if (randomized)
            {
                RandomizeConstruction(m_effects[k], *mutableLight, *random);
            }
            // La cola 142/143 puede borrar position por alias con light; la interpolación arranca al terminarla.
            m_effects[k].previousPosition = m_effects[k].transform.position;
            return k;
        }
        m_effects[k] = CreateDragon(position, angles, scale);
        return k;
    }
    return std::nullopt;
}

std::optional<std::size_t> ModelEffects::Create(std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                                                const glm::vec3& light, float scale, std::uint32_t owner)
{
    return CreateStored(type, position, angles, light, scale, owner, 0, nullptr, nullptr);
}

std::optional<std::size_t> ModelEffects::Create(std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                                                glm::vec3& light, float scale, Render::Models::CrtRandom& random,
                                                std::uint32_t owner, std::int32_t subtype)
{
    return CreateStored(type, position, angles, light, scale, owner, subtype, &random, &light);
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

bool IsConstructedMapEffect(std::int16_t type)
{
    return type == Construction135 || type == Construction142 || type == Construction143 || type == Construction144;
}

Core::Expected<void, ModelEffectStepError> StepConstructedModelEffect(ModelEffect& effect,
                                                                      Render::Models::CrtRandom& random,
                                                                      const ModelEffectStepBindings& bindings)
{
    if (!bindings.readClock || !bindings.animationOf || !bindings.advance)
    {
        return Core::Unexpected(ModelEffectStepError::MissingBinding);
    }
    if (!IsConstructedMapEffect(effect.type))
    {
        return Core::Unexpected(ModelEffectStepError::UnsupportedTypeChange);
    }
    (void)random.Next();
    StepConstructionBody(effect, bindings);
    auto animation = StepConstructionAnimation(effect, bindings);
    if (!animation.HasValue())
    {
        return animation;
    }
    if (!IsConstructedMapEffect(effect.type))
    {
        return Core::Unexpected(ModelEffectStepError::UnsupportedTypeChange);
    }
    MoveModelEffect(effect);
    DecreaseModelEffectLife(effect);
    return {};
}

void StepArrivalModelEffect(ModelEffect& effect, Render::Models::CrtRandom& random)
{
    // El sorteo del prefijo sólo alimenta locales no usados por 61f/0.
    (void)random.Next();
    MoveModelEffect(effect);
    DecreaseModelEffectLife(effect);
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
    MoveModelEffect(effect);
    --effect.life;
    if (effect.life <= 0)
    {
        effect.alive = false;
    }
}
} // namespace Client::World
