#include "Scenes/HighModelEffects.h"

#include "Core/Expected.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameModelEffects.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/SiegeStoneEffects.h"
#include "World/WorldObjectStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

// Las altas (0x518cf0) y los pasos (0x5208a0) de los tipos altos, cotejados con el PE c32bbd5f: 06, "Los efectos con
// modelo de los tipos altos".
namespace Client::Scenes
{
namespace
{
using Render::Models::CrtRandom;

// El prefijo de los efectos del asedio (0x42d070, 0x42dcf0) y de Crywolf (0x4324c0, 0x4f5b70).
constexpr std::int16_t SiegeAuraEffect = 0x134;     // el aura de la llegada del asedio (0x42d513, 0x42d5af)
constexpr std::int16_t SiegeFlareEffect = 0x13b;    // la llama que suelta el pulso 0x61c y la 0x4fd5f0
constexpr std::int16_t SiegeSpearmanEffect = 0x19f; // el modelo del lancero (Monster78), sin dibujo: suelta los 0x12b/2
constexpr std::int16_t SiegePulseEffect = 0x61c;    // sin dibujo: el pulso del asedio con sus joints
constexpr std::int16_t RadialShardEffect = 0xc1a;   // el fragmento de la descarga radial de Crywolf
// El destello que crece del 0x134 de 0x429110 (0x429202): sin BMD en la tabla de las skills.
constexpr std::int16_t GrowingFlashEffect = 0x141;
constexpr std::uint8_t NoAction = 0xff;

float F32(double value)
{
    return static_cast<float>(value);
}

double D(float value)
{
    return static_cast<double>(value);
}

// 0x5fc8f0 y 0x5fcbd0: el vector girado por los ángulos (en grados) de la matriz del original.
glm::vec3 Rotated(const glm::vec3& vector, const glm::vec3& angles)
{
    return Render::Models::OriginalVectorRotate(vector, Render::Models::OriginalAngleMatrix(angles));
}

// El fld/fadd/fstp de cada eje de una suma de vectores.
glm::vec3 Sum(const glm::vec3& a, const glm::vec3& b)
{
    return {F32(D(a.x) + D(b.x)), F32(D(a.y) + D(b.y)), F32(D(a.z) + D(b.z))};
}

// 0x530860(tipo, punto, punto2, ángulos, subtipo, dueño, escala, clave, 0, 0, −1): el joint, con la dirección del que
// lo pide delante del error de un joint que MuRet no tiene (el lote de los joints de la CL11).
Core::Expected<void, std::string> CreateJoint(WorldState& world, CrtRandom& random, std::int32_t type, glm::vec3 first,
                                              glm::vec3 second, glm::vec3 angles, std::int32_t subtype,
                                              World::ModelEffect* owner, float scale, std::uint32_t address)
{
    constexpr std::int32_t NoKey = -1, NoTarget = -1;
    const auto made = CreateFaunaJoint(
        world.DrawTail().joints,
        World::FishEffectRequest{type, first, second, angles, subtype, owner, scale, NoKey, 0, 0, NoTarget}, random,
        owner != nullptr ? JointOwnerKind::ModelEffect : JointOwnerKind::ByType);
    if (!made.HasValue())
        return Core::Unexpected(std::format("{:#x}: {}", address, made.Error()));
    return {};
}

// 0x53fe70 de una partícula, con el error del pool como texto y la dirección del que la pide.
Core::Expected<std::optional<std::size_t>, std::string>
CreateParticle(WorldState& world, CrtRandom& random, float worldClock, std::int32_t type, const glm::vec3& position,
               const glm::vec3& angles, const glm::vec3& light, std::int32_t subtype, float scale,
               std::uint32_t address)
{
    const auto created =
        world.Particles().CreateChecked(type, position, angles, light, subtype, scale, worldClock, random, 0);
    if (!created.HasValue())
        return Core::Unexpected(std::format("{:#x}: la partícula {:#x}/{} falla con el error {}", address, type,
                                            subtype, static_cast<unsigned>(created.Error())));
    return created.Value();
}

// ---- Las altas ----

// 0x51d714..0x51d73a, el 0x134 (la llegada del asedio, 0x42d513 y 0x42d5af): vida 100, la escala de la clave (+0x8a,
// MOVSX) por 0,01f y +0x64 y +0x68 en 0. Cualquier subtipo; sin rand().
void BeginTheSiegeAura(World::ModelEffect& effect)
{
    constexpr std::int32_t AuraLife = 100;
    constexpr double ScalePerKey = static_cast<double>(std::bit_cast<float>(0x3c23d70au)); // 0x6b86c0, 0,01f
    effect.life = AuraLife;
    effect.transform.scale =
        F32(static_cast<double>(std::bit_cast<std::int16_t>(effect.disturbanceState)) * ScalePerKey);
    effect.objectState.blendMesh = 0;
    effect.objectState.light = 0.0f;
}

// 0x51e4ba..0x51e4f5, el 0x19f con el subtipo 0 (0x42dd9c): vida 20, +0x58 en 3, +0xd8 en 0,33f, 0x4ea030(efecto, 2) y
// los ángulos en 0. Con otro subtipo sale por 0x51db73 con lo del prefijo. 0x4ea030 pone la acción 2 si el modelo
// 0x19f (Monster78) tiene más de dos acciones (+0x26) y la acción no es ya la 2.
void BeginTheSiegeSpearman(World::ModelEffect& effect, const HighEffectContext& context)
{
    if (effect.subtype != 0)
        return;
    constexpr std::int32_t SpearmanLife = 20;
    constexpr std::int16_t SpearmanHidden = 3;
    constexpr float SpearmanSpeed = std::bit_cast<float>(0x3ea8f5c3u); // 0,33f
    constexpr std::uint8_t SpearmanAction = 2;
    effect.life = SpearmanLife;
    effect.objectState.hiddenMesh = SpearmanHidden;
    effect.speed = SpearmanSpeed;
    // 0x4ea030 lee el BMD de la tabla global (0x5769ad8 + tipo · 0xe0): el de FaunaResources, que sigue con los modelos
    // del mapa, y si no el de `models`, como MonsterSkeletonOf. Sin el BMD cargado, la cuenta de acciones del original
    // es 0 y no hace nada.
    const auto model = static_cast<std::uint16_t>(effect.type);
    const auto* skeleton = context.world.FaunaResources().SkeletonOf(model);
    if (skeleton == nullptr && context.models != nullptr)
        skeleton = context.models->SkeletonOf(model);
    if (skeleton != nullptr)
        Render::Models::SetAction(effect.animation, *skeleton, SpearmanAction);
    effect.transform.angles = glm::vec3(0.0f);
}

// 0x51f154..0x51f25d, el 0x61c (0x42dd48 con el subtipo 0, 0x42925a con el 1): vida 20, la velocidad (0, −550, 0) con
// el 0 o (0, −250, 0) con el 1, los ángulos en (−90, 0, z del argumento), +0x28 y +0x2c en 0, +0x30 en rand() % 360 y
// la partida en la posición. El /0, entre el sorteo y la partida, el joint 0x61c/3 en la posición (0x51f1c5). Otro
// subtipo sale por 0x51db73 con lo del prefijo.
Core::Expected<void, std::string> BeginTheSiegePulse(World::ModelEffect& effect,
                                                     const World::ModelEffectArguments& arguments,
                                                     const HighEffectContext& context)
{
    constexpr std::int32_t PulseLife = 20;
    constexpr float FirstRise = -550.0f, SecondRise = -250.0f, Pitch = -90.0f; // 0xc4098000, 0xc37a0000, 0xc2b40000
    constexpr int FullTurn = 360;
    if (effect.subtype != 0 && effect.subtype != 1)
        return {};
    effect.life = PulseLife;
    effect.velocity = {0.0f, effect.subtype == 0 ? FirstRise : SecondRise, 0.0f};
    effect.transform.angles = {Pitch, 0.0f, arguments.angles.z};
    effect.copiedAngles.x = 0.0f;
    effect.copiedAngles.y = 0.0f;
    effect.copiedAngles.z = static_cast<float>(context.random.Next() % FullTurn);
    if (effect.subtype == 0)
    {
        constexpr std::int32_t PulseJoint = 0x61c, PulseJointSubtype = 3;
        constexpr float PulseJointScale = 80.0f; // 0x42a00000
        if (auto joint = CreateJoint(context.world, context.random, PulseJoint, effect.transform.position,
                                     effect.transform.position, effect.transform.angles, PulseJointSubtype, nullptr,
                                     PulseJointScale, 0x51f1c5);
            !joint.HasValue())
            return joint;
    }
    effect.start = arguments.position;
    return {};
}

// 0x51e059..0x51e0ca, el 0x13b: +0xd8 en 0,25f. El /0 (0x502975): vida 15, la escala en 0, la z 100 más, la velocidad
// (0, −10, 0). El /1 (el que suelta el pulso 0x61c, 0x52c39c): vida 5 y escala 3,5. Todos: +0x64 en 0 y +0x68 en 1;
// otro subtipo conserva la vida y la escala de antes. Sin rand().
void BeginTheSiegeFlare(World::ModelEffect& effect)
{
    constexpr float FlareSpeed = 0.25f;
    effect.speed = FlareSpeed;
    if (effect.subtype == 0)
    {
        constexpr std::int32_t FlareLife = 15;
        constexpr double FlareLift = 100.0;
        constexpr float FlareRise = -10.0f;
        effect.life = FlareLife;
        effect.transform.scale = 0.0f;
        effect.transform.position.z = F32(D(effect.transform.position.z) + FlareLift);
        effect.velocity = {0.0f, FlareRise, 0.0f};
    }
    else if (effect.subtype == 1)
    {
        constexpr std::int32_t SparkLife = 5;
        constexpr float SparkScale = 3.5f; // 0x40600000
        effect.life = SparkLife;
        effect.transform.scale = SparkScale;
    }
    effect.objectState.blendMesh = 0;
    effect.objectState.light = 1.0f;
}

// 0x51f85a..0x51f8a7, el 0xc1a (0x4329db y 0x4f6780): vida 30, +0x64 en −2, escala 1, la z 280 más, la x de los
// ángulos 20 más y la velocidad (0, −80, −10). Cualquier subtipo; sin rand().
void BeginTheRadialShard(World::ModelEffect& effect)
{
    constexpr std::int32_t ShardLife = 30;
    constexpr double ShardLift = 280.0, ShardPitch = 20.0;
    effect.life = ShardLife;
    effect.objectState.blendMesh = -2;
    effect.transform.scale = 1.0f;
    effect.transform.position.z = F32(D(effect.transform.position.z) + ShardLift);
    effect.transform.angles.x = F32(D(effect.transform.angles.x) + ShardPitch);
    effect.velocity = {0.0f, -80.0f, -10.0f};
}

// 0x51c410..0x51c437, el 0x141: la z 20 más, vida 20, +0xe8 en 0 y la escala en 0,05f. Sin rand().
void BeginTheGrowingFlash(World::ModelEffect& effect)
{
    constexpr double FlashLift = 20.0; // 0x6b86d0
    constexpr std::int32_t FlashLife = 20;
    constexpr float FlashScale = std::bit_cast<float>(0x3d4ccccdu); // 0,05f
    effect.transform.position.z = F32(D(effect.transform.position.z) + FlashLift);
    effect.life = FlashLife;
    effect.vertexLight = false;
    effect.transform.scale = FlashScale;
}

// ---- Los pasos ----

// 0x52cbf4..0x52cc7b: la acción publicada en el modelo (+0xa0) y 0x4ec200 con +0xd8. Si la acción termina, el frame
// queda en sus keys y la acción en 0xff.
void Animate(World::ModelEffect& effect, const Render::Models::Skeleton* skeleton,
             const World::ModelEffectStepServices& services)
{
    if (services.publishAction)
        services.publishAction(static_cast<std::uint16_t>(effect.type), effect.animation.action);
    if (effect.animation.action == NoAction || skeleton == nullptr ||
        effect.animation.action >= skeleton->actions.size())
        return;
    const Render::Models::ActionInfo& action = skeleton->actions[effect.animation.action];
    if (Render::Models::AdvanceAnimation(effect.animation, action, effect.speed, Render::Models::AnimationScene::Other))
    {
        effect.animation.frame = static_cast<float>(action.keyCount);
        effect.animation.action = NoAction;
    }
}

// 0x51fc80(efecto, 1): la velocidad girada por los ángulos, sumada a la posición; con 0, sin girar.
void Move(World::ModelEffect& effect, bool rotated)
{
    const glm::vec3 step = rotated ? Rotated(effect.velocity, effect.transform.angles) : effect.velocity;
    effect.transform.position = Sum(step, effect.transform.position);
}

// 0x52cb98..0x52cd9d para los tipos de este archivo: la animación de los BMD de [0xad, 0x151) (salvo el 0x116 y el
// 0x147, que saltan a la vida), el movimiento (salvo 0x5e8..0x5ea; el 0x13b anima otra vez antes, 0x52cd41) y una vida
// menos.
Core::Expected<void, std::string> CommonTail(World::ModelEffect& effect, const Render::Models::Skeleton* skeleton,
                                             const World::ModelEffectStepServices& services)
{
    constexpr std::int16_t FirstAnimated = 0xad, PastAnimated = 0x151;
    const std::int16_t type = effect.type;
    const bool still = type == 0x116 || type == 0x147;
    if (!still)
    {
        if (type >= FirstAnimated && type < PastAnimated)
            Animate(effect, skeleton, services);
        if (!(type >= 0x5e8 && type <= 0x5ea))
        {
            if (type == SiegeFlareEffect)
            {
                // 0x52cd41..0x52cd84: publica la acción y llama 0x4ec200 sin mirarla ni su resultado. Con la acción
                // en 0xff lee la acción 255 del modelo, fuera de su tabla.
                if (services.publishAction)
                    services.publishAction(static_cast<std::uint16_t>(type), effect.animation.action);
                if (skeleton != nullptr && !skeleton->actions.empty())
                {
                    if (effect.animation.action >= skeleton->actions.size())
                        return Core::Unexpected(std::format("0x52cd84: la segunda animación del {:#x} lee la acción "
                                                            "{} fuera de las {} del modelo",
                                                            type, effect.animation.action, skeleton->actions.size()));
                    static_cast<void>(
                        Render::Models::AdvanceAnimation(effect.animation, skeleton->actions[effect.animation.action],
                                                         effect.speed, Render::Models::AnimationScene::Other));
                }
            }
            Move(effect, true);
        }
    }
    --effect.life;
    if (effect.life <= 0)
        effect.alive = false;
    return {};
}

// 0x52952d..0x52959f, el 0x134: con el dueño (+0x108, el personaje de la llegada) vivo (+0x0 en 1), +0x58 en −1 con el
// asedio (0x4294b0) o −2 sin él, la vida en 2 y +0x68 en FSIN(T · 0,001f) · 0,1f + 0,2f (PC53); si no, el lugar libre.
// Las dos ramas, +0x6c en T · 0,0005f. T es el reloj del mundo (0x5767b88).
Core::Expected<void, std::string> StepTheSiegeAura(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    constexpr double WaveRate = static_cast<double>(std::bit_cast<float>(0x3a83126fu));   // 0x6b89c8, 0,001f
    constexpr float WaveDepth = std::bit_cast<float>(0x3dcccccdu);                        // 0x6b87b8, 0,1f
    constexpr double WaveBase = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));   // 0x6b8854, 0,2f
    constexpr double ScrollRate = static_cast<double>(std::bit_cast<float>(0x3a03126fu)); // 0x6b8a68, 0,0005f
    constexpr std::int32_t HeldLife = 2;
    const auto* owner = static_cast<const ActorState*>(effect.ownerContext);
    const double clock = D(context.worldClock);
    if (owner != nullptr && owner->alive)
    {
        effect.objectState.hiddenMesh = context.world.Runtime().siegeActive ? std::int16_t{-1} : std::int16_t{-2};
        effect.life = HeldLife;
        const auto wave = Render::Models::Detail::SineProductPC53(clock * WaveRate, WaveDepth);
        if (!wave)
            return Core::Unexpected(std::string("0x529568: la luz del aura 0x134 no cabe en el perfil PC53"));
        effect.objectState.light = F32(*wave + WaveBase);
    }
    else
    {
        effect.alive = false;
    }
    effect.objectState.uv.x = F32(clock * ScrollRate);
    return {};
}

// 0x52aa6a..0x52aaff y 0x52725b, el 0x19f: con el subtipo 0 y la vida múltiplo de 5, el fragmento 0x12b/2 en la
// posición corrida rand() % 100 − 50 en x y −(rand() % 30 + 15) en y; con menos de 5 de vida, el alpha por 0,769f.
Core::Expected<void, std::string> StepTheSiegeSpearman(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    constexpr std::int32_t DropEvery = 5;
    if (effect.subtype == 0 && effect.life % DropEvery == 0)
    {
        constexpr int SideSpan = 100, FallSpan = 30;
        constexpr double SideCenter = 50.0, FallBase = 15.0;
        glm::vec3 point = effect.transform.position;
        point.x = F32(static_cast<double>(context.random.Next() % SideSpan) - SideCenter + D(point.x));
        point.y = F32(D(point.y) - (static_cast<double>(context.random.Next() % FallSpan) + FallBase));
        constexpr std::int32_t FragmentSubtype = 2;
        const World::ModelEffectArguments arguments{.type = World::SiegeStoneEffect,
                                                    .position = point,
                                                    .angles = effect.transform.angles,
                                                    .light = effect.light,
                                                    .subtype = FragmentSubtype};
        const auto& hero = context.world.Actors()[context.world.Runtime().hero];
        if (auto made = World::CreateSiegeStone(arguments, context.world.Map().effects, hero.position, context.random);
            !made.HasValue())
            return Core::Unexpected(made.Error());
    }
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu)); // 0x6ba240, 0,769f
    if (effect.life < DropEvery)
        effect.alpha = F32(D(effect.alpha) * Fade);
    return {};
}

// 0x52c19a..0x52c34a, lo del 0x61c antes de la parte que comparte con el 0x13b: con la vida por debajo de 11 y par, los
// ángulos en (90, 0, 0), +0x30 72 más y, desde la partida, la velocidad girada por +0x28; con el /0 el joint 0x632/5
// rand() % 400 + 700 más arriba (escala 110) y con el /1 dos 0x615/6 100 más arriba (escala 80).
Core::Expected<void, std::string> StepTheSiegePulseJoints(World::ModelEffect& effect,
                                                          const HighEffectStepContext& context)
{
    constexpr std::int32_t LastJointLife = 10;
    if ((effect.subtype != 0 && effect.subtype != 1) || effect.life > LastJointLife || effect.life % 2 != 0)
        return {};
    constexpr double Spin = 72.0;
    effect.transform.angles = {90.0f, 0.0f, 0.0f};
    effect.copiedAngles.z = F32(D(effect.copiedAngles.z) + Spin);
    const glm::vec3 reach = Rotated(effect.velocity, effect.copiedAngles);
    glm::vec3 point = Sum(reach, effect.start);
    if (effect.subtype == 0)
    {
        constexpr int RiseSpan = 400;
        constexpr double RiseBase = 700.0;
        constexpr float BeamScale = 110.0f; // 0x42dc0000
        point.z = F32(static_cast<double>(context.random.Next() % RiseSpan) + D(point.z) + RiseBase);
        return CreateJoint(context.world, context.random, 0x632, point, point, effect.transform.angles, 5, &effect,
                           BeamScale, 0x52c266);
    }
    // 0x52c307..0x52c31b: la z de la partida y los 100 en la x87, guardados una vez.
    point.z = F32(D(reach.z) + D(effect.start.z) + 100.0);
    constexpr float BoltScale = 80.0f; // 0x42a00000
    if (auto first = CreateJoint(context.world, context.random, 0x615, point, point, effect.transform.angles, 6,
                                 &effect, BoltScale, 0x52c31e);
        !first.HasValue())
        return first;
    return CreateJoint(context.world, context.random, 0x615, point, point, effect.transform.angles, 6, &effect,
                       BoltScale, 0x52c342);
}

// 0x52c34e..0x52c537, la parte que comparten el 0x13b y el 0x61c (el 0x61c entra después de sus joints). Con el /0 y
// más de 12 de vida, la escala 0,9f más, la y de la velocidad 2 menos y el 0x13b/1 con el efecto de dueño; con 12 o
// menos, la escala 0,05f menos, +0x68, el alpha y la luz en vida / 18 (0,0555…f), la y de la velocidad 2 menos y, desde
// un punto corrido rand() % 30 − 15 en x y en y y 100 más abajo, cuatro veces el joint 0x617/0 (escala 10) con los
// ángulos (rand() % 60 + 150, 0, z) y la chispa 0x5d9/2 (escala 1,5f); después, la luz dinámica (1; 0,8f; 0,6f) de
// radio 1. Con el /1, +0x68, el alpha y la luz en vida · 0,1f.
Core::Expected<void, std::string> StepTheSiegeFlare(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    if (effect.subtype == 1)
    {
        constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b8a34
        const float light = F32(static_cast<double>(effect.life) * Tenth);
        effect.objectState.light = light;
        effect.alpha = light;
        effect.light = glm::vec3(light);
        return {};
    }
    if (effect.subtype != 0)
        return {};
    constexpr std::int32_t GrowingLife = 12;
    constexpr double Fall = 2.0;
    if (effect.life > GrowingLife)
    {
        constexpr double Growth = static_cast<double>(std::bit_cast<float>(0x3f666666u)); // 0x6b8a1c, 0,9f
        effect.transform.scale = F32(D(effect.transform.scale) + Growth);
        effect.velocity.y = F32(D(effect.velocity.y) - Fall);
        const World::ModelEffectArguments spark{.type = SiegeFlareEffect,
                                                .position = effect.transform.position,
                                                .angles = effect.transform.angles,
                                                .light = effect.light,
                                                .subtype = 1,
                                                .owner = &effect};
        if (auto made = CreateHighModelEffect(
                spark, {.world = context.world, .random = context.random, .worldClock = context.worldClock});
            !made.HasValue())
            return Core::Unexpected(made.Error());
    }
    else
    {
        constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));  // 0x6b8a48, 0,05f
        constexpr double PerLife = static_cast<double>(std::bit_cast<float>(0x3d638e39u)); // 0x6ba204, 1/18
        effect.transform.scale = F32(D(effect.transform.scale) - Shrink);
        const float light = F32(static_cast<double>(effect.life) * PerLife);
        effect.objectState.light = light;
        effect.alpha = light;
        effect.light = glm::vec3(light);
        effect.velocity.y = F32(D(effect.velocity.y) - Fall);
        constexpr int Spread = 30;
        constexpr double SpreadCenter = 15.0, Drop = 100.0;
        glm::vec3 point = effect.transform.position;
        point.x = F32(static_cast<double>(context.random.Next() % Spread) - SpreadCenter + D(point.x));
        point.y = F32(static_cast<double>(context.random.Next() % Spread) - SpreadCenter + D(point.y));
        point.z = F32(D(point.z) - Drop);
        constexpr int Sparks = 4, PitchSpan = 60, PitchBase = 150;
        constexpr float SparkJointScale = 10.0f, SparkScale = 1.5f;
        constexpr std::int32_t SparkSubtype = 2;
        for (int spark = 0; spark < Sparks; ++spark)
        {
            glm::vec3 angles{static_cast<float>(context.random.Next() % PitchSpan + PitchBase), 0.0f,
                             effect.transform.angles.z};
            if (auto joint = CreateJoint(context.world, context.random, 0x617, point, point, angles, 0, nullptr,
                                         SparkJointScale, 0x52c4a2);
                !joint.HasValue())
                return joint;
            if (auto made = CreateParticle(context.world, context.random, context.worldClock, World::FireParticle,
                                           point, angles, effect.light, SparkSubtype, SparkScale, 0x52c4be);
                !made.HasValue())
                return Core::Unexpected(made.Error());
        }
    }
    constexpr glm::vec3 GlowColor{1.0f, std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3f19999au)};
    return World::AddDynamicLight(context.services.lighting, effect.transform.position, GlowColor, 1, "0x52c4f1");
}

// 0x525f1a..0x525f65, el 0x141: la escala 0,1f más (FST) y, con la suma de la x87 en 1 o más, el alpha 0,07f menos y
// la escala en 1.
void StepTheGrowingFlash(World::ModelEffect& effect)
{
    constexpr double Growth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b87b8, 0,1f
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3d8f5c29u));   // 0x6ba190, 0,07f
    const double grown = D(effect.transform.scale) + Growth;
    effect.transform.scale = F32(grown);
    if (grown < 1.0)
        return;
    effect.alpha = F32(D(effect.alpha) - Fade);
    effect.transform.scale = 1.0f;
}

// 0x52c9fd..0x52caa0 y 0x521216, el 0xc1a: con el terreno (0x5f93c0) por encima de su z, la explosión 0x5ed/0 80 más
// arriba (con su sonido 0x40), seis piedritas 0xc8 o 0xc9 (rand() & 1) con el subtipo 0 y el lugar libre; siempre, la
// luz dinámica (luz · 0,2f; luz · 0,4f; luz) de radio 2 con la luz local del primer rand().
Core::Expected<void, std::string> StepTheRadialShard(World::ModelEffect& effect, int lightDraw,
                                                     const HighEffectStepContext& context)
{
    if (!context.services.terrainHeight)
        return Core::Unexpected(std::string("0x52ca08: el fragmento 0xc1a pide la altura del terreno (0x5f93c0)"));
    const glm::vec3& position = effect.transform.position;
    if (context.services.terrainHeight(position.x, position.y) > D(position.z))
    {
        constexpr double BlastLift = 80.0;
        const glm::vec3 blast{position.x, position.y, F32(D(position.z) + BlastLift)};
        const auto made = CreateParticle(context.world, context.random, context.worldClock, World::ExplosionParticle,
                                         blast, effect.transform.angles, glm::vec3(1.0f), 0, 1.0f, 0x52ca4f);
        if (!made.HasValue())
            return Core::Unexpected(made.Error());
        constexpr std::int32_t BlastSound = 0x40;
        if (made.Value().has_value())
            context.world.Runtime().RequestSound(BlastSound);
        constexpr int Chips = 6;
        for (int chip = 0; chip < Chips; ++chip)
        {
            const auto type = static_cast<std::int16_t>(World::FirstStoneChipEffect + (context.random.Next() & 1));
            static_cast<void>(context.world.Map().effects.CreateStoneChip(
                type, effect.transform.position, effect.transform.angles, effect.light, context.random));
        }
        effect.alive = false;
    }
    const float luminosity = World::StepLuminosity(lightDraw, effect.life);
    constexpr double Fifth = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));     // 0x6b8854
    constexpr double TwoFifths = static_cast<double>(std::bit_cast<float>(0x3ecccccdu)); // 0x6b87b4
    const glm::vec3 color{F32(D(luminosity) * Fifth), F32(D(luminosity) * TwoFifths), luminosity};
    return World::AddDynamicLight(context.services.lighting, position, color, 2, "0x521246");
}
} // namespace

bool IsHighModelEffect(std::int16_t type)
{
    return type == SiegeAuraEffect || type == SiegeFlareEffect || type == SiegeSpearmanEffect ||
           type == SiegePulseEffect || type == RadialShardEffect || type == GrowingFlashEffect;
}

Core::Expected<std::optional<std::size_t>, std::string>
CreateHighModelEffect(const World::ModelEffectArguments& arguments, const HighEffectContext& context)
{
    if (!IsHighModelEffect(arguments.type))
        return Core::Unexpected(std::format("0x518cf0: el alta del efecto {:#x} no es de este lote", arguments.type));
    const auto slot = context.world.Map().effects.Begin(arguments);
    if (!slot.has_value())
        return slot;
    World::ModelEffect& effect = context.world.Map().effects.Effects()[*slot];
    Core::Expected<void, std::string> begun;
    switch (arguments.type)
    {
    case SiegeAuraEffect:
        BeginTheSiegeAura(effect);
        break;
    case SiegeFlareEffect:
        BeginTheSiegeFlare(effect);
        break;
    case SiegeSpearmanEffect:
        BeginTheSiegeSpearman(effect, context);
        break;
    case SiegePulseEffect:
        begun = BeginTheSiegePulse(effect, arguments, context);
        break;
    case RadialShardEffect:
        BeginTheRadialShard(effect);
        break;
    case GrowingFlashEffect:
        BeginTheGrowingFlash(effect);
        break;
    default:
        break;
    }
    if (!begun.HasValue())
        return Core::Unexpected(begun.Error());
    return slot;
}

bool StepsAsHighModelEffect(const World::ModelEffect& effect)
{
    return IsHighModelEffect(effect.type);
}

Core::Expected<void, std::string> StepHighModelEffect(World::ModelEffect& effect,
                                                      const Render::Models::Skeleton* skeleton,
                                                      const HighEffectStepContext& context)
{
    if (!effect.alive)
        return {};
    if (!StepsAsHighModelEffect(effect))
        return Core::Unexpected(std::format("0x5208a0: el paso del efecto {:#x} no es de este lote", effect.type));
    // 0x5208c5: el primer rand() de todo efecto vivo, la luz local.
    const int lightDraw = context.random.Next();
    const glm::vec3 before = effect.transform.position;
    Core::Expected<void, std::string> stepped;
    switch (effect.type)
    {
    case SiegeAuraEffect:
        stepped = StepTheSiegeAura(effect, context);
        break;
    case SiegeSpearmanEffect:
        stepped = StepTheSiegeSpearman(effect, context);
        break;
    case SiegePulseEffect:
        stepped = StepTheSiegePulseJoints(effect, context);
        if (stepped.HasValue())
            stepped = StepTheSiegeFlare(effect, context);
        break;
    case SiegeFlareEffect:
        stepped = StepTheSiegeFlare(effect, context);
        break;
    case RadialShardEffect:
        stepped = StepTheRadialShard(effect, lightDraw, context);
        break;
    case GrowingFlashEffect:
        StepTheGrowingFlash(effect);
        break;
    default:
        break;
    }
    if (!stepped.HasValue())
        return stepped;
    auto tail = CommonTail(effect, skeleton, context.services);
    // D-C4: el dibujo interpola desde antes del paso entero.
    effect.previousPosition = before;
    return tail;
}
} // namespace Client::Scenes
