#include "Scenes/HighModelEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Models/Animation.h"
#include "Math/HalfLifeMath.h"
#include "Models/IModelLibrary.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Actors/CharacterStep.h"
#include "Assets/FaunaModels.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GuildMarkEffects.h"
#include "Scenes/FireBreathEffect.h"
#include "Scenes/HighEffectEmitters.h"
#include "Scenes/JavelinEffect.h"
#include "Scenes/KalimaMonsterEffects.h"
#include "Scenes/KundunEffects.h"
#include "Scenes/KundunFallEffects.h"
#include "Scenes/MeteorEffects.h"
#include "Scenes/RingEffects.h"
#include "Scenes/SkillBurstEffects.h"
#include "Scenes/SkullEffects.h"
#include "Scenes/SkillHits.h"
#include "Scenes/WaveEffects.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "World/GameModelEffects.h"
#include "Terrain/ColoredDecals.h"
#include "Terrain/WorldTerrain.h"
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
#include <functional>
#include <optional>
#include <string>
#include <string_view>

// Las altas (0x518cf0) y los pasos (0x5208a0) de los tipos altos, cotejados con el PE c32bbd5f: 06, "Los efectos con
// modelo de los tipos altos".
namespace Client::Scenes
{
namespace
{
using Crt::CrtRandom;

// El prefijo de los efectos del asedio (0x42d070, 0x42dcf0) y de Crywolf (0x4324c0, 0x4f5b70).
constexpr std::int16_t SiegeAuraEffect = 0x134;     // el aura de la llegada del asedio (0x42d513, 0x42d5af)
constexpr std::int16_t SiegeFlareEffect = 0x13b;    // la llama que suelta el pulso 0x61c y la 0x4fd5f0
constexpr std::int16_t SiegeSpearmanEffect = 0x19f; // el modelo del lancero (Monster78), sin dibujo: suelta los 0x12b/2
constexpr std::int16_t SiegePulseEffect = 0x61c;    // sin dibujo: el pulso del asedio con sus joints
constexpr std::int16_t RadialShardEffect = 0xc1a;   // el fragmento de la descarga radial de Crywolf
// El destello que crece de 0x429110 (0x429202). 0x615700 (los modelos de las skills) no lo carga: lo carga 0x616ec0
// solo en el mapa 0x21 (0x618592 y 0x6185ad, Object34\tree_eff; WorldSpecialModels.cpp), así que fuera de ese mapa
// el dibujo no encuentra su BMD.
constexpr std::int16_t GrowingFlashEffect = 0x141;
constexpr std::uint8_t NoAction = 0xff;
// Los rayos de luz que se dibujan como sprites a lo largo de su velocidad (0x52d941): no se mueven ni tienen paso.
constexpr std::int16_t MovingLightFirst = 0x5e8, MovingLightSecond = 0x5e9, MovingLightLast = 0x5ea;
// Skill\PierPart (la 0x3d y la 0x4a de 0x4fd5f0), Skill\DarkLordSkill (las manos de la 0x40, la 0x4b y la 0x3d) y
// Skill\ManaRune (la 0x4b).
constexpr std::int16_t PierPartEffect = 0x116, DarkLordSkillEffect = 0x117, ManaRuneEffect = 0x138;
// Sin modelo ni dibujo: sueltan joints en su paso. El 0x812, el de la 0x2b y la 0x2f (0x4f97b0); el 0x813, el de la
// mano de la 0x2f.
constexpr std::int16_t SparkEmitterEffect = 0x812, BoltEmitterEffect = 0x813;
// El brillo del tiro penetrante (0x621): el /0 nace y da su paso en World (ModelEffects::CreatePiercingShot y
// StepGameModelEffect); los demás subtipos, aquí.
constexpr std::int16_t PiercingGlowEffect = World::PiercingShotGlowEffect;
// Los que caen sobre el blanco del 0x46 (+0x376) en su paso 5 (0x4f5cb6): sin dibujo, sólo su fuego y sus sprites.
constexpr std::int16_t FallingStarEffect = 0x625;

using Math::D;
using Math::F32;

// 0x5fc8f0 y 0x5fcbd0: el vector girado por los ángulos (en grados) de la matriz del original.
glm::vec3 Rotated(const glm::vec3& vector, const glm::vec3& angles)
{
    return Math::OriginalVectorRotate(vector, Math::OriginalAngleMatrix(angles));
}

// El fld/fadd/fstp de cada eje de una suma de vectores.
glm::vec3 Sum(const glm::vec3& a, const glm::vec3& b)
{
    return {F32(D(a.x) + D(b.x)), F32(D(a.y) + D(b.y)), F32(D(a.z) + D(b.z))};
}

// El BMD de la tabla global (0x5769ad8 + tipo · 0xe0): el de FaunaResources, que sigue con los modelos del mapa, y si
// no el de `models` (los ítems y Player.bmd), como MonsterSkeletonOf.
const Models::Skeleton* ModelSkeletonOf(const Kernel::WorldState& world, const Models::IModelLibrary* models,
                                        std::uint16_t model)
{
    const auto* skeleton = world.FaunaResources().SkeletonOf(model);
    if (skeleton == nullptr && models != nullptr)
        skeleton = models->SkeletonOf(model);
    return skeleton;
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
    const auto* skeleton = ModelSkeletonOf(context.world, context.models, static_cast<std::uint16_t>(effect.type));
    if (skeleton != nullptr)
        Models::SetAction(effect.animation, *skeleton, SpearmanAction);
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
    constexpr int FullTurn = Math::FullTurn;
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
        if (auto joint = CreateHighJoint(context.world, context.random, PulseJoint, effect.transform.position,
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

// 0x51e596..0x51e772, el 0x5e8 (el rayo azul; con el subtipo 1, blanco y de escala 3), el 0x5e9 y el 0x5ea (los
// naranjas): vida 20, la luz, la escala y la velocidad (0, −50, 0) (el 0x5ea, (0, −15, 0)) girada por los ángulos.
// Con el héroe ([0x743a170]) de dueño, cuatro veces: el punto 150 más adelante (girado) y 0x517a20(+0x89, punto, 150,
// +0x8c, +0x8a, 0), los blancos alrededor y el C1 10. Sin rand() propios (los de 0x517a20, sí).
Core::Expected<void, std::string> BeginTheMovingLight(World::ModelEffect& effect, const HighEffectContext& context)
{
    constexpr std::int32_t LightLife = 20;
    constexpr float Orange = std::bit_cast<float>(0x3ecccccdu), Ember = std::bit_cast<float>(0x3e4ccccdu);
    constexpr float BlueGreen = std::bit_cast<float>(0x3f333333u);
    constexpr float WideScale = 16.0f, NarrowScale = 5.0f, WhiteScale = 3.0f; // 0x41800000, 0x40a00000, 0x40400000
    constexpr float Rush = -50.0f, SlowRush = -15.0f;                         // 0xc2480000, 0xc1700000
    constexpr std::int32_t WhiteSubtype = 1;
    effect.life = LightLife;
    glm::vec3 rush{0.0f, Rush, 0.0f};
    if (effect.type == MovingLightLast)
    {
        effect.light = {1.0f, Orange, Ember};
        rush.y = SlowRush;
        effect.transform.scale = NarrowScale;
    }
    else if (effect.type == MovingLightSecond)
    {
        effect.light = {1.0f, Orange, Ember};
        effect.transform.scale = WideScale;
    }
    else
    {
        effect.light = {0.5f, BlueGreen, 1.0f};
        effect.transform.scale = WideScale;
        if (effect.subtype == WhiteSubtype)
        {
            effect.transform.scale = WhiteScale;
            effect.light = glm::vec3(1.0f);
        }
    }
    effect.velocity = Rotated(rush, effect.transform.angles);
    Kernel::WorldState& world = context.world;
    if (effect.ownerContext != &world.Hero())
        return {};
    constexpr float Reach = -150.0f; // 0xc3160000
    constexpr float HitRadius = 150.0f;
    constexpr int Hits = 4;
    const glm::vec3 step = Rotated({0.0f, Reach, 0.0f}, effect.transform.angles);
    glm::vec3 point = effect.transform.position;
    for (int hit = 0; hit < Hits; ++hit)
    {
        point = Sum(step, point);
        const SkillHitsRequest request{.skillIndex = effect.byte89,
                                       .point = point,
                                       .radius = HitRadius,
                                       .volley = static_cast<std::uint8_t>(effect.word8c & 0xff),
                                       .key = effect.disturbanceState,
                                       .extra = 0};
        if (auto hits = HitTheTargetsAround(request, world, Terrain::TerrainOrEmpty(context.terrain), context.random,
                                            context.send);
            !hits.HasValue())
            return Core::Unexpected(std::format("0x51e75b: {}", hits.Error()));
    }
    return {};
}

// 0x51f048..0x51f153, el 0x625 (los veinte que caen sobre el blanco de 0x4f5cb6): vida rand() % 15 + 15, +0xe4 (la
// subida por paso) en −20, +0xd8 en rand() % 360 y la luz en 0. El /1 pone la escala en 0,5f y baja +0xe4 rand() % 20 +
// 10 más (fsubr), y reparte en 300; los demás, la escala en 0 y reparten en 200. La x y la y más rand() % reparto menos
// la mitad (fild, fadd, fsub en la x87), la z 500 más y la x 100 más, y la partida (+0x17c) en la posición. Termina con
// el sonido 0x41ed10(0x42, 0, 0). Cuatro rand() (cinco con el /1): la vida, +0xd8, [+0xe4], la x y la y.
void BeginTheFallingStar(World::ModelEffect& effect, const HighEffectContext& context)
{
    constexpr int LifeSpan = 15, LifeBase = 15, FullTurn = Math::FullTurn;
    constexpr float InitialRise = -20.0f; // 0xc1a00000
    constexpr std::int32_t WideSubtype = 1;
    constexpr int WideSpread = 300, NarrowSpread = 200;    // 0x12c y 0xc8
    constexpr double WideHalf = 150.0, NarrowHalf = 100.0; // 0x96 y 0x64, por fild y fstp
    constexpr float WideScale = 0.5f;                      // 0x3f000000
    constexpr int ExtraFallSpan = 20, ExtraFallBase = 10;  // 0x14 y 0xa
    constexpr double Height = 500.0, Lead = 100.0;         // 0x6b8a98 y 0x6b86b4
    constexpr std::int32_t FallingSound = 0x42;
    CrtRandom& random = context.random;
    effect.life = random.Next() % LifeSpan + LifeBase;
    effect.turnDirection = InitialRise;
    effect.speed = static_cast<float>(random.Next() % FullTurn);
    effect.light = glm::vec3(0.0f);
    int spread = NarrowSpread;
    double half = NarrowHalf;
    if (effect.subtype == WideSubtype)
    {
        spread = WideSpread;
        half = WideHalf;
        effect.transform.scale = WideScale;
        const int extra = random.Next() % ExtraFallSpan + ExtraFallBase;
        effect.turnDirection = F32(D(effect.turnDirection) - static_cast<double>(extra));
    }
    else
    {
        effect.transform.scale = 0.0f;
    }
    glm::vec3& position = effect.transform.position;
    position.x = F32(static_cast<double>(random.Next() % spread) + D(position.x) - half);
    position.y = F32(static_cast<double>(random.Next() % spread) + D(position.y) - half);
    position.z = F32(D(position.z) + Height);
    position.x = F32(D(position.x) + Lead);
    effect.start = position;
    context.world.Runtime().RequestSound(FallingSound);
}

// 0x51ca31..0x51cb46, el 0x116 (Skill\PierPart). El /0 (los tres de la 0x3d, 0x504e40): vida 20, +0xe4 (cuántos /1
// suelta por paso) en 2, +0xd8 (el giro) en 10, +0x58 en 1, la escala 1,2f, la luz en 1, la velocidad (0, −26, 0), la
// partida (+0x17c) en la luz que recibió (el ecx del prefijo: el punto del dueño al que va), +0x28..+0x30 en los
// ángulos y los ángulos en (0, 0, z del argumento). El /1 (lo suelta el paso del /0, con él de dueño): la vida del
// dueño (+0x108 → +0x60), +0x58 en 0, la escala 0,5f, el alpha (20 − vida) · 0,2f, la velocidad en 0 y la llama 0x5da/0
// (0x53fe70 con la escala 1 y el efecto de dueño). El /2 (la 0x4a, 0x504b80): vida 20, +0xd8 en 50, +0x58 en −2, la z
// 20 menos, la velocidad (0, −40, 0), +0x28..+0x30 en los ángulos y los ángulos en (0, 0, z). Otro subtipo, sólo el
// prefijo. Sin rand() (los de la llama, sí).
Core::Expected<void, std::string> BeginThePierPart(World::ModelEffect& effect,
                                                   const World::ModelEffectArguments& arguments,
                                                   const HighEffectContext& context)
{
    constexpr std::int32_t PartLife = 20;
    if (effect.subtype == 0)
    {
        constexpr float Bursts = 2.0f, Turn = 10.0f, Rush = -26.0f; // 0x40000000, 0x41200000 y 0xc1d00000
        constexpr std::int16_t PartHidden = 1;
        constexpr float PartScale = std::bit_cast<float>(0x3f99999au); // 1,2f
        effect.life = PartLife;
        effect.turnDirection = Bursts;
        effect.speed = Turn;
        effect.objectState.hiddenMesh = PartHidden;
        effect.transform.scale = PartScale;
        effect.light = glm::vec3(1.0f);
        effect.velocity = {0.0f, Rush, 0.0f};
        effect.start = arguments.light;
        effect.copiedAngles = effect.transform.angles;
        effect.transform.angles = {0.0f, 0.0f, arguments.angles.z};
        return {};
    }
    if (effect.subtype == 1)
    {
        const auto* parent = static_cast<const World::ModelEffect*>(effect.ownerContext);
        if (parent == nullptr)
            return Core::Unexpected(std::string("0x51cad9: el 0x116/1 lee la vida de un dueño (+0x108) nulo"));
        constexpr float TrailScale = 0.5f;                                                  // 0x3f000000
        constexpr double FadeStep = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)); // 0x6b8cfc, 0,2f
        effect.life = parent->life;
        effect.objectState.hiddenMesh = 0;
        effect.transform.scale = TrailScale;
        effect.alpha = F32(static_cast<double>(PartLife - effect.life) * FadeStep);
        effect.velocity = glm::vec3(0.0f);
        constexpr std::int32_t TrailFire = World::MissileFireParticle, TrailFireSubtype = 0;
        if (auto fire = CreateHighParticle(context.world, context.random, context.worldClock, TrailFire,
                                           effect.transform.position, effect.transform.angles, effect.light,
                                           TrailFireSubtype, 1.0f, 0x51cb38);
            !fire.HasValue())
            return Core::Unexpected(fire.Error());
        return {};
    }
    if (effect.subtype == 2)
    {
        constexpr float Turn = 50.0f, Rush = -40.0f; // 0x42480000 y 0xc2200000
        constexpr std::int16_t PartHidden = -2;
        constexpr double Drop = 20.0; // 0x6b86d0
        effect.life = PartLife;
        effect.speed = Turn;
        effect.objectState.hiddenMesh = PartHidden;
        effect.transform.position.z = F32(D(effect.transform.position.z) - Drop);
        effect.velocity = {0.0f, Rush, 0.0f};
        effect.copiedAngles = effect.transform.angles;
        effect.transform.angles = {0.0f, 0.0f, arguments.angles.z};
    }
    return {};
}

// 0x51cbb2..0x51cc6a, el 0x117 (Skill\DarkLordSkill): vida 10, la escala 0,2f y +0xd8 (lo que crece) en 0,1f. Con el
// subtipo 1 o menos (con signo), los ángulos (45, y, 0): y es subtipo · −90 + 45 si rand() % 2 sale distinto de 0 y,
// si no, 45 con el /0 o −45 con los demás. El /2, vida 12 y +0xd8 en 0,4f, sin rand(). Otro subtipo, nada más.
void BeginTheDarkLordSkill(World::ModelEffect& effect, CrtRandom& random)
{
    constexpr std::int32_t SkillLife = 10, WideLife = 12;
    constexpr float SkillScale = std::bit_cast<float>(0x3e4ccccdu); // 0,2f
    constexpr float Growth = std::bit_cast<float>(0x3dcccccdu);     // 0,1f
    constexpr float WideGrowth = std::bit_cast<float>(0x3ecccccdu); // 0,4f
    constexpr double TiltPerSubtype = -90.0, TiltBase = 45.0;       // 0x6ba1dc y 0x6b8848
    constexpr float Pitch = 45.0f, LeftTilt = -45.0f;               // 0x42340000 y 0x6b8af4
    effect.life = SkillLife;
    effect.transform.scale = SkillScale;
    effect.speed = Growth;
    const std::int32_t subtype = effect.subtype;
    if (subtype <= 1)
    {
        const float tilt = F32(static_cast<double>(subtype) * TiltPerSubtype + TiltBase);
        float yaw = tilt;
        if (random.Next() % 2 == 0)
            yaw = subtype == 0 ? Pitch : LeftTilt;
        effect.transform.angles = {Pitch, yaw, 0.0f};
        return;
    }
    if (subtype == 2)
    {
        effect.life = WideLife;
        effect.speed = WideGrowth;
    }
}

// 0x51da46..0x51daca, el 0x138 (Skill\ManaRune). El /0 (la 0x4b, 0x502ae6): vida 50, la escala en 0, +0xe4 (lo que
// crece) en 0,1f, el alpha en 0,3f, +0x58 en 0, la z 300 más y los ángulos en (0, 0, 45). El /1 (lo suelta el paso del
// /0): vida 10, la escala 1,1f, +0xe4 en 0,1f, +0x58 en −1, +0x64 en −2 y +0x68 en 0,4f. Otro subtipo, sólo el
// prefijo. Sin rand().
void BeginTheManaRune(World::ModelEffect& effect)
{
    constexpr float Growth = std::bit_cast<float>(0x3dcccccdu); // 0,1f
    if (effect.subtype == 0)
    {
        constexpr std::int32_t RuneLife = 50;
        constexpr float RuneAlpha = std::bit_cast<float>(0x3e99999au); // 0,3f
        constexpr double Lift = 300.0;                                 // 0x6b8900
        constexpr float RuneTurn = 45.0f;                              // 0x42340000
        effect.life = RuneLife;
        effect.transform.scale = 0.0f;
        effect.turnDirection = Growth;
        effect.alpha = RuneAlpha;
        effect.objectState.hiddenMesh = 0;
        effect.transform.position.z = F32(D(effect.transform.position.z) + Lift);
        effect.transform.angles = {0.0f, 0.0f, RuneTurn};
        return;
    }
    if (effect.subtype == 1)
    {
        constexpr std::int32_t FlashLife = 10;
        constexpr float FlashScale = std::bit_cast<float>(0x3f8ccccdu); // 1,1f
        constexpr std::int16_t FlashHidden = -1, FlashBlend = -2;
        constexpr float FlashLight = std::bit_cast<float>(0x3ecccccdu); // 0,4f
        effect.life = FlashLife;
        effect.transform.scale = FlashScale;
        effect.turnDirection = Growth;
        effect.objectState.hiddenMesh = FlashHidden;
        effect.objectState.blendMesh = FlashBlend;
        effect.objectState.light = FlashLight;
    }
}

// 0x51e4f6, el 0x812 (y el 0x5c6): vida 10. 0x51f8a8, el 0x813: los ángulos en 0 y vida 5. Sin rand().
void BeginTheEmitter(World::ModelEffect& effect)
{
    constexpr std::int32_t SparkLife = 10, BoltLife = 5;
    if (effect.type == BoltEmitterEffect)
    {
        effect.transform.angles = glm::vec3(0.0f);
        effect.life = BoltLife;
        return;
    }
    effect.life = SparkLife;
}

// 0x51efa5..0x51efe9, el 0x621 salvo el /0: vida 10; el /1 y el /2, vida 20 y la partida (+0x17c) en la posición que
// recibió. Sin rand().
void BeginThePiercingGlow(World::ModelEffect& effect, const World::ModelEffectArguments& arguments)
{
    constexpr std::int32_t GlowLife = 10, HeldLife = 20;
    effect.life = GlowLife;
    if (effect.subtype == 1 || effect.subtype == 2)
    {
        effect.start = arguments.position;
        effect.life = HeldLife;
    }
}

// ---- Los pasos ----

// 0x52cbf4..0x52cc7b: la acción publicada en el modelo (+0xa0) y 0x4ec200 con +0xd8. Si la acción termina, el frame
// queda en sus keys y la acción en 0xff.
void Animate(World::ModelEffect& effect, const Models::Skeleton* skeleton,
             const World::ModelEffectStepServices& services)
{
    if (services.publishAction)
        services.publishAction(static_cast<std::uint16_t>(effect.type), effect.animation.action);
    if (effect.animation.action == NoAction || skeleton == nullptr ||
        effect.animation.action >= skeleton->actions.size())
        return;
    const Models::ActionInfo& action = skeleton->actions[effect.animation.action];
    if (Models::AdvanceAnimation(effect.animation, action, effect.speed, Models::AnimationScene::Other))
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
// 0x147, que saltan a la vida), el movimiento (salvo 0x5e8..0x5ea y el 0x139, que la tabla de 0x52ccb0 manda a la
// vida, 0x52cd94; el 0x13b anima otra vez antes, 0x52cd41) y una vida menos.
Core::Expected<void, std::string> CommonTail(World::ModelEffect& effect, const Models::Skeleton* skeleton,
                                             const World::ModelEffectStepServices& services)
{
    constexpr std::int16_t FirstAnimated = 0xad, PastAnimated = 0x151;
    const std::int16_t type = effect.type;
    const bool still = type == 0x116 || type == 0x147;
    if (!still)
    {
        if (type >= FirstAnimated && type < PastAnimated)
            Animate(effect, skeleton, services);
        if (!(type >= MovingLightFirst && type <= MovingLightLast) && type != JavelinEffect)
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
                    static_cast<void>(Models::AdvanceAnimation(effect.animation,
                                                               skeleton->actions[effect.animation.action], effect.speed,
                                                               Models::AnimationScene::Other));
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
    const auto* owner = static_cast<const Kernel::ActorState*>(effect.ownerContext);
    const double clock = D(context.worldClock);
    if (owner != nullptr && owner->alive)
    {
        effect.objectState.hiddenMesh =
            context.world.Runtime().castleSiege.siegeActive ? std::int16_t{-1} : std::int16_t{-2};
        effect.life = HeldLife;
        const auto wave = Math::Detail::SineProductPC53(clock * WaveRate, WaveDepth);
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
        const auto& hero = context.world.Hero();
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
        return CreateHighJoint(context.world, context.random, 0x632, point, point, effect.transform.angles, 5, &effect,
                               BeamScale, 0x52c266);
    }
    // 0x52c307..0x52c31b: la z de la partida y los 100 en la x87, guardados una vez.
    point.z = F32(D(reach.z) + D(effect.start.z) + 100.0);
    constexpr float BoltScale = 80.0f; // 0x42a00000
    if (auto first = CreateHighJoint(context.world, context.random, 0x615, point, point, effect.transform.angles, 6,
                                     &effect, BoltScale, 0x52c31e);
        !first.HasValue())
        return first;
    return CreateHighJoint(context.world, context.random, 0x615, point, point, effect.transform.angles, 6, &effect,
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
        constexpr double Tenth = Math::TenthAsDouble; // 0x6b8a34
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
            if (auto joint = CreateHighJoint(context.world, context.random, 0x617, point, point, angles, 0, nullptr,
                                             SparkJointScale, 0x52c4a2);
                !joint.HasValue())
                return joint;
            if (auto made = CreateHighParticle(context.world, context.random, context.worldClock, World::FireParticle,
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

// 0x52be78..0x52c195, el 0x625: nada hasta la vida 15. Desde ahí, el /0 tiembla alrededor de la partida (x e y en
// FSIN(rand() % 1.000 · 0,01f) · 10 más la partida) y el /1 se queda en ella; los dos suben +0xe4 (que empieza
// negativo: caen), +0xe4 baja 2 y la x de la partida 10, y sueltan el fuego 0x5db (/7 el /0, /0xb el /1) con su escala
// y la luz local (1, 1, 1), la luz +0xf4..+0xfc 0,1f más (FST) y dos sprites con esa luz y el efecto de dueño: el 0x5fd
// con la rotación rand() % 360 y la escala ((rand() & 3) + 4) · 0,2f, y el 0x5ac de escala 1 con otra rotación. El /1,
// con el terreno (0x5f93c0) por encima de su z, el humo 0x5f2/0xb 50 más arriba con la luz (0,24; 0,28; 0,8) y la
// escala
// ((rand() & 31) + 80) · 0,025f, y una de cinco veces (rand() % 5) el pedazo de hielo 0xca ahí con su luz. Otro
// subtipo, nada.
Core::Expected<void, std::string> StepTheFallingStar(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    constexpr std::int32_t QuietLife = 15;
    constexpr std::int32_t WobblySubtype = 0, StraightSubtype = 1;
    if (effect.life > QuietLife || (effect.subtype != WobblySubtype && effect.subtype != StraightSubtype))
        return {};
    CrtRandom& random = context.random;
    glm::vec3& position = effect.transform.position;
    if (effect.subtype == WobblySubtype)
    {
        constexpr int WobbleSpan = 1000;                                                      // 0x3e8
        constexpr double WobbleRate = static_cast<double>(std::bit_cast<float>(0x3c23d70au)); // 0x6b88d4, 0,01f
        constexpr float WobbleSize = 10.0f;                                                   // 0x6b87a8
        for (int axis = 0; axis < 2; ++axis)
        {
            const int draw = random.Next() % WobbleSpan;
            const auto wobble = Math::Detail::SineProductPC53(static_cast<double>(draw) * WobbleRate, WobbleSize);
            if (!wobble)
                return Core::Unexpected(std::string("0x52bea9: el temblor del 0x625 no cabe en el perfil PC53"));
            position[axis] = F32(*wobble + D(effect.start[axis]));
        }
    }
    else
    {
        position.x = effect.start.x;
        position.y = effect.start.y;
    }
    constexpr double RiseLoss = 2.0, Drift = 10.0; // 0x6b8818 y 0x6b87a8
    position.z = F32(D(position.z) + D(effect.turnDirection));
    effect.turnDirection = F32(D(effect.turnDirection) - RiseLoss);
    effect.start.x = F32(D(effect.start.x) - Drift);
    const bool wobbly = effect.subtype == WobblySubtype;
    constexpr std::int32_t WobblyFire = 7, StraightFire = 0xb;
    const glm::vec3 localLight{1.0f};
    if (auto fire =
            CreateHighParticle(context.world, random, context.worldClock, World::DeathSpiritFireParticle, position,
                               effect.transform.angles, localLight, wobbly ? WobblyFire : StraightFire,
                               effect.transform.scale, wobbly ? 0x52bf29u : 0x52c036u);
        !fire.HasValue())
        return Core::Unexpected(fire.Error());
    constexpr double Glow = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b87b8, 0,1f
    const float glow = F32(D(effect.light.x) + Glow);
    effect.light = glm::vec3(glow);
    constexpr std::uint16_t ShineSprite = 0x5fd, FlareSprite = 0x5ac; // Effect\Shiny02.jpg y Effect\flare01.jpg
    constexpr int FullTurn = Math::FullTurn, ShineSizes = 3, ShineBase = 4;
    constexpr double ShineStep = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)); // 0x6b8854, 0,2f
    const auto shineTurn = static_cast<float>(random.Next() % FullTurn);
    const float shineScale = F32(static_cast<double>((random.Next() & ShineSizes) + ShineBase) * ShineStep);
    if (auto shine = CreateHighSprite(context, ShineSprite, position, shineScale, effect.light, shineTurn,
                                      wobbly ? 0x52bf98u : 0x52c0a5u);
        !shine.HasValue())
        return shine;
    const auto flareTurn = static_cast<float>(random.Next() % FullTurn);
    if (auto flare = CreateHighSprite(context, FlareSprite, position, 1.0f, effect.light, flareTurn,
                                      wobbly ? 0x52bfc6u : 0x52c0d3u);
        !flare.HasValue())
        return flare;
    if (wobbly)
        return {};
    if (!context.services.terrainHeight)
        return Core::Unexpected(std::string("0x52c0df: el 0x625/1 pide la altura del terreno (0x5f93c0)"));
    if (!(context.services.terrainHeight(position.x, position.y) > D(position.z)))
        return {};
    constexpr glm::vec3 SmokeLight{std::bit_cast<float>(0x3e75c28fu), std::bit_cast<float>(0x3e8f5c29u),
                                   std::bit_cast<float>(0x3f4ccccdu)}; // (0,24; 0,28; 0,8)
    constexpr double SmokeLift = 50.0;                                 // 0x6b86b0
    const glm::vec3 point{position.x, position.y, F32(D(position.z) + SmokeLift)};
    constexpr int SmokeSizes = 31, SmokeBase = 0x50;
    constexpr double SmokeStep = static_cast<double>(std::bit_cast<float>(0x3ccccccdu)); // 0x6b8bd0, 0,025f
    const float smokeScale = F32(static_cast<double>((random.Next() & SmokeSizes) + SmokeBase) * SmokeStep);
    if (auto smoke =
            CreateHighParticle(context.world, random, context.worldClock, World::BreathParticle, point,
                               effect.transform.angles, SmokeLight, World::StoneDustSubtype, smokeScale, 0x52c15a);
        !smoke.HasValue())
        return Core::Unexpected(smoke.Error());
    constexpr int ShardOdds = 5;
    if (random.Next() % ShardOdds != 0)
        return {};
    static_cast<void>(context.world.Map().effects.CreateIceShard(point, effect.transform.angles, effect.light, random));
    return {};
}

// La velocidad (+0xcc) girada por los ángulos (0x5fc8f0 y 0x5fcbd0) sumada a la posición, un fstp por eje.
void Advance(World::ModelEffect& effect)
{
    effect.transform.position = Sum(Rotated(effect.velocity, effect.transform.angles), effect.transform.position);
}

// 0x52727b..0x527447, el 0x116/0: con dueño (+0x108, un personaje), el destino es su posición más la partida. Si
// +0xe4 pasa de 1, desde 1 hasta antes de +0xe4: con rand() & 1 en 0, el cabeceo 20 más (por debajo de −90) o 20
// menos; 0x4e9810 gira hacia el destino de a +0xd8; +0xd8 0,4f más (y 0,1f más con menos de 10 de vida, desde la suma
// sin redondear); avanza y suelta el 0x116/1 en su lugar con él de dueño. Después, +0xe4 0,1f más y el sonido 0x1ca.
// Con la distancia del último giro por debajo de 40 y la vida en 5, lee la altura del terreno (0x5f93c0) en una
// variable que nadie lee: MuRet no la pide. Sin dueño, nada.
Core::Expected<void, std::string> StepTheHomingPart(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    const auto* owner = static_cast<const Kernel::ActorState*>(effect.ownerContext);
    if (owner == nullptr)
        return {};
    const glm::vec3 target = Sum(owner->position, effect.start);
    constexpr double Tilt = 20.0, TiltFloor = -90.0;                                        // 0x6b86d0 y 0x6ba1dc
    constexpr double Acceleration = static_cast<double>(std::bit_cast<float>(0x3ecccccdu)); // 0x6b87b4, 0,4f
    constexpr double LateBoost = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));    // 0x6b87b8, 0,1f
    constexpr std::int32_t LateLife = 10;
    constexpr std::int32_t TrailSubtype = 1;
    const HighEffectContext created{
        .world = context.world, .random = context.random, .worldClock = context.worldClock, .terrain = context.terrain};
    for (int count = 1; static_cast<double>(count) < D(effect.turnDirection); ++count)
    {
        if ((context.random.Next() & 1) == 0)
        {
            const double pitch = D(effect.transform.angles.x);
            effect.transform.angles.x = F32(!(pitch >= TiltFloor) ? pitch + Tilt : pitch - Tilt);
        }
        static_cast<void>(Math::MoveToward(effect.transform.position, effect.transform.angles, target, effect.speed));
        const double faster = D(effect.speed) + Acceleration;
        effect.speed = F32(effect.life < LateLife ? faster + LateBoost : faster);
        Advance(effect);
        constexpr std::int16_t PierPart = 0x116;
        if (auto trail = CreateHighModelEffect({.type = PierPart,
                                                .position = effect.transform.position,
                                                .angles = effect.transform.angles,
                                                .light = effect.light,
                                                .subtype = TrailSubtype,
                                                .owner = &effect},
                                               created);
            !trail.HasValue())
            return Core::Unexpected("0x5273c6: " + trail.Error());
    }
    constexpr double MoreBursts = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b87b8, 0,1f
    constexpr std::int32_t PartSound = 0x1ca;
    effect.turnDirection = F32(D(effect.turnDirection) + MoreBursts);
    context.world.Runtime().RequestSound(PartSound);
    return {};
}

// 0x527455..0x527534, el 0x116/2: con dueño, 0x4e9810 gira hacia su posición de a +0xd8, +0xd8 2,4f más, avanza y,
// con la vida múltiplo de 3, el joint 0x61c/2 en su lugar con los ángulos (−90, 0, z) y la escala 150. Sin dueño,
// nada.
Core::Expected<void, std::string> StepTheDivingPart(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    const auto* owner = static_cast<const Kernel::ActorState*>(effect.ownerContext);
    if (owner == nullptr)
        return {};
    constexpr double Acceleration = static_cast<double>(std::bit_cast<float>(0x4019999au)); // 0x6ba238, 2,4f
    static_cast<void>(
        Math::MoveToward(effect.transform.position, effect.transform.angles, owner->position, effect.speed));
    effect.speed = F32(D(effect.speed) + Acceleration);
    Advance(effect);
    constexpr std::int32_t JointEvery = 3;
    if (effect.life % JointEvery != 0)
        return {};
    constexpr std::int32_t DiveJoint = 0x61c, DiveJointSubtype = 2;
    constexpr float DiveJointScale = 150.0f; // 0x43160000
    constexpr float DivePitch = -90.0f;      // 0xc2b40000
    const glm::vec3& position = effect.transform.position;
    return CreateHighJoint(context.world, context.random, DiveJoint, position, position,
                           {DivePitch, 0.0f, effect.transform.angles.z}, DiveJointSubtype, nullptr, DiveJointScale,
                           0x52752c);
}

// 0x52727b..0x527276, el 0x116: el /0 y el /2 buscan a su dueño; el /1, con menos de 5 de vida (edi, 5 desde
// 0x5208df), el alpha por 0,7692308f.
Core::Expected<void, std::string> StepThePierPart(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    if (effect.subtype == 0)
        return StepTheHomingPart(effect, context);
    if (effect.subtype == 2)
        return StepTheDivingPart(effect, context);
    constexpr std::int32_t FadeLife = 5;
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu)); // 0x6ba240
    if (effect.subtype == 1 && effect.life < FadeLife)
        effect.alpha = F32(D(effect.alpha) * Fade);
    return {};
}

// 0x527539..0x5275c8, el 0x117: la escala crece +0xd8. Con el subtipo 1 o menos, +0xd8 0,02f más y, con menos de 7 de
// vida, +0x68 por 0,5555556f. El /2, +0xd8 0,06f menos y, con menos de 8, +0x68 por 0,6666667f. Los demás, +0xd8 0,04f
// más y, con menos de 8, +0x68 por 0,5555556f.
void StepTheDarkLordSkill(World::ModelEffect& effect)
{
    constexpr double SmallGrowth = static_cast<double>(std::bit_cast<float>(0x3ca3d70au)); // 0x6b8b90, 0,02f
    constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x3d75c28fu));      // 0x6ba230, 0,06f
    constexpr double WideGrowth = static_cast<double>(std::bit_cast<float>(0x3d23d70au));  // 0x6b8aec, 0,04f
    constexpr double QuickFade = static_cast<double>(std::bit_cast<float>(0x3f0e38e4u));   // 0x6ba234
    constexpr double SlowFade = static_cast<double>(std::bit_cast<float>(0x3f2aaaabu));    // 0x6b8c64
    constexpr std::int32_t EarlyFadeLife = 7, LateFadeLife = 8;
    effect.transform.scale = F32(D(effect.transform.scale) + D(effect.speed));
    double fade = QuickFade;
    std::int32_t fadeLife = LateFadeLife;
    if (effect.subtype <= 1)
    {
        effect.speed = F32(D(effect.speed) + SmallGrowth);
        fadeLife = EarlyFadeLife;
    }
    else if (effect.subtype == 2)
    {
        effect.speed = F32(D(effect.speed) - Shrink);
        fade = SlowFade;
    }
    else
    {
        effect.speed = F32(D(effect.speed) + WideGrowth);
    }
    if (effect.life < fadeLife)
        effect.objectState.light = F32(D(effect.objectState.light) * fade);
}

// 0x529a4f..0x529b81, el 0x138. El /0: con más de 43 de vida, +0x58 en −2; con más de 40, en 0; con más de 30, la
// escala crece +0xe4 y +0xe4 0,15f más, y al pasar de 1 la escala queda en 1, +0xe4 en 0,01f y suelta el 0x138/1 en su
// lugar con su luz; con menos de 15, la escala achica +0xe4 (con piso 0), +0xe4 0,2f más, el alpha es vida · 0,05f y
// baja 10. El /1: la escala 0,02f menos (comparada sin redondear: por debajo de 1, queda en 1) y el alpha vida · 0,05f.
Core::Expected<void, std::string> StepTheManaRune(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    constexpr double AlphaPerLife = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu)); // 0x6b8ad0, 0,05f
    if (effect.subtype == 1)
    {
        constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x3ca3d70au)); // 0x6b8b90, 0,02f
        const double scale = D(effect.transform.scale) - Shrink;
        effect.transform.scale = F32(scale);
        if (!(scale >= 1.0))
            effect.transform.scale = 1.0f;
        effect.alpha = F32(static_cast<double>(effect.life) * AlphaPerLife);
        return {};
    }
    if (effect.subtype != 0)
        return {};
    constexpr std::int32_t HiddenLife = 43, ShownLife = 40, GrowingLife = 30, FadingLife = 15;
    constexpr std::int16_t Hidden = -2;
    if (effect.life > HiddenLife)
    {
        effect.objectState.hiddenMesh = Hidden;
        return {};
    }
    if (effect.life > ShownLife)
    {
        effect.objectState.hiddenMesh = 0;
        return {};
    }
    if (effect.life > GrowingLife)
    {
        constexpr double Acceleration = static_cast<double>(std::bit_cast<float>(0x3e19999au)); // 0x6b8ae8, 0,15f
        constexpr float SettledGrowth = std::bit_cast<float>(0x3c23d70au);                      // 0,01f
        effect.transform.scale = F32(D(effect.transform.scale) + D(effect.turnDirection));
        effect.turnDirection = F32(D(effect.turnDirection) + Acceleration);
        if (!(effect.transform.scale > 1.0f))
            return {};
        effect.transform.scale = 1.0f;
        effect.turnDirection = SettledGrowth;
        constexpr std::int16_t ManaRune = 0x138;
        constexpr std::int32_t FlashSubtype = 1;
        const HighEffectContext created{.world = context.world,
                                        .random = context.random,
                                        .worldClock = context.worldClock,
                                        .terrain = context.terrain};
        if (auto flash = CreateHighModelEffect({.type = ManaRune,
                                                .position = effect.transform.position,
                                                .angles = effect.transform.angles,
                                                .light = effect.light,
                                                .subtype = FlashSubtype},
                                               created);
            !flash.HasValue())
            return Core::Unexpected("0x529ae2: " + flash.Error());
        return {};
    }
    if (effect.life >= FadingLife)
        return {};
    constexpr double Acceleration = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)); // 0x6b8854, 0,2f
    constexpr double Sink = 10.0;                                                           // 0x6b87a8
    effect.transform.scale = F32(D(effect.transform.scale) - D(effect.turnDirection));
    effect.turnDirection = F32(D(effect.turnDirection) + Acceleration);
    if (!(effect.transform.scale >= 0.0f))
        effect.transform.scale = 0.0f;
    effect.alpha = F32(static_cast<double>(effect.life) * AlphaPerLife);
    effect.transform.position.z = F32(D(effect.transform.position.z) - Sink);
    return {};
}

// 0x52bb79..0x52bbf1, el 0x621 salvo el /0: con el /1 y el /2, el origen compartido del modelo del dueño (+0x108 →
// +0x2, el +0x6c..+0x74 de su BMD) pasa a ser la partida y la posición es el punto (0, 0, 10) del hueso 33 del dueño
// (0x4ec110 con sus huesos, +0x120 + 0x630). Después, todos, las tres emisiones del /0 (0x52bbf6).
Core::Expected<void, std::string> StepThePiercingGlow(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    if (effect.subtype == 1 || effect.subtype == 2)
    {
        const auto* owner = static_cast<const Kernel::ActorState*>(effect.ownerContext);
        if (owner == nullptr)
            return Core::Unexpected(std::string("0x52bb86: el brillo 0x621 lee el modelo de un dueño (+0x108) nulo"));
        constexpr std::size_t GlowBone = 0x630 / 0x30;
        constexpr glm::vec3 GlowOffset{0.0f, 0.0f, 10.0f}; // 0x41200000
        const auto type = static_cast<std::uint16_t>(owner->type);
        Kernel::SharedModelPlacement placement = context.world.ModelPlacement(type);
        placement.origin = effect.start;
        context.world.PlaceModel(type, placement);
        effect.transform.position = ActorBonePoint(*owner, GlowBone, GlowOffset, context.world);
    }
    return World::EmitPiercingShotGlow(effect, context.world.Particles(), context.random, context.services);
}

// 0x52c981..0x52c9e2, el 0x812: en cada paso, el joint 0x60f/0xc (el /1, escala 100) o 0x60f/4 (el /0, escala 50) en
// su lugar con él de dueño (0x530860(0x60f, posición, posición, ángulos, subtipo, efecto, escala, −1, 0, 0, −1)). Otro
// subtipo, nada.
Core::Expected<void, std::string> StepTheSparkEmitter(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    constexpr std::int32_t SparkJoint = 0x60f;
    constexpr std::int32_t WideSubtype = 0xc, NarrowSubtype = 4;
    constexpr float WideScale = 100.0f, NarrowScale = 50.0f; // 0x42c80000 y 0x42480000
    if (effect.subtype != 0 && effect.subtype != 1)
        return {};
    const bool wide = effect.subtype == 1;
    const glm::vec3& position = effect.transform.position;
    return CreateHighJoint(context.world, context.random, SparkJoint, position, position, effect.transform.angles,
                           wide ? WideSubtype : NarrowSubtype, &effect, wide ? WideScale : NarrowScale,
                           wide ? 0x52c9a7u : 0x52c9dau);
}

// 0x52caa5..0x52cb94, el 0x813: tres veces, los ángulos (rand() % 90, 0, rand() % 360), el punto 100 hacia atrás
// (la posición menos (0, −100, 0) girado por ellos, un fstp por eje) y el joint 0x619/6 del punto a la posición, sin
// dueño y con la escala 5.
Core::Expected<void, std::string> StepTheBoltEmitter(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    constexpr int Bolts = 3, PitchSpan = 90, FullTurn = Math::FullTurn;
    constexpr glm::vec3 Reach{0.0f, -100.0f, 0.0f}; // 0xc2c80000
    constexpr std::int32_t BoltJoint = 0x619, BoltJointSubtype = 6;
    constexpr float BoltJointScale = 5.0f; // 0x40a00000
    const glm::vec3& position = effect.transform.position;
    for (int bolt = 0; bolt < Bolts; ++bolt)
    {
        glm::vec3 angles{0.0f};
        angles.x = static_cast<float>(context.random.Next() % PitchSpan);
        angles.z = static_cast<float>(context.random.Next() % FullTurn);
        const glm::vec3 back = Rotated(Reach, angles);
        const glm::vec3 from{F32(D(position.x) - D(back.x)), F32(D(position.y) - D(back.y)),
                             F32(D(position.z) - D(back.z))};
        if (auto joint = CreateHighJoint(context.world, context.random, BoltJoint, from, position, angles,
                                         BoltJointSubtype, nullptr, BoltJointScale, 0x52cb85);
            !joint.HasValue())
            return joint;
    }
    return {};
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
        const auto made =
            CreateHighParticle(context.world, context.random, context.worldClock, World::ExplosionParticle, blast,
                               effect.transform.angles, glm::vec3(1.0f), 0, 1.0f, 0x52ca4f);
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
    constexpr double Fifth = Math::FifthAsDouble;                                        // 0x6b8854
    constexpr double TwoFifths = static_cast<double>(std::bit_cast<float>(0x3ecccccdu)); // 0x6b87b4
    const glm::vec3 color{F32(D(luminosity) * Fifth), F32(D(luminosity) * TwoFifths), luminosity};
    return World::AddDynamicLight(context.services.lighting, position, color, 2, "0x521246");
}

// 0x52b9c1..0x52bb24, el 0x61e/6 (el calco que la 0x46 pone sobre el blanco, su dueño, 0x4fe424): el modelo del dueño
// (0x5769ad8 + su tipo · 0xe0) toma la partida del calco como origen (+0x6c); +0x68 en (rand() % 10) · 0,1f; tantas
// vueltas como vida, cada una con un hueso al azar (rand() % sus huesos) que, si no es un dummy, da tres rand() del
// corrimiento (± 50, ± 10, ± 10), su punto (0x4ec110 con los huesos propios del dueño, la escala del modelo y ese
// origen) y la partícula 0x5ac/8 con los ángulos del dueño, luz 1 y escala 1; después, el alpha del dueño (+0x174) en
// vida · 0,0125f.
Core::Expected<void, std::string> StepTheBindingDecal(World::ModelEffect& effect, const HighEffectStepContext& context)
{
    auto* owner = static_cast<Kernel::ActorState*>(effect.ownerContext);
    if (owner == nullptr)
        return Core::Unexpected(std::string("0x52b9c1: el 0x61e/6 lee el modelo de un dueño nulo"));
    Kernel::SharedModelPlacement placement = context.world.ModelPlacement(owner->type);
    placement.origin = effect.start;
    context.world.PlaceModel(owner->type, placement);
    constexpr int GlowSpan = 10;
    constexpr double Tenth = Math::TenthAsDouble; // 0x6b87b8
    effect.objectState.light = F32(static_cast<double>(context.random.Next() % GlowSpan) * Tenth);
    // 0x52ba1d: el BMD de la tabla global por el tipo del dueño, por el mismo camino que el alta del 0x19f.
    const Models::Skeleton* skeleton = ModelSkeletonOf(context.world, context.models, owner->type);
    constexpr int SideSpan = 100, SideCenter = 50, DepthSpan = 20, DepthCenter = 10;
    constexpr std::int32_t SparkSubtype = 8;
    constexpr float SparkScale = 1.0f;
    for (std::int32_t turn = 0; turn < effect.life; ++turn)
    {
        const std::size_t bones = skeleton != nullptr ? skeleton->bones.size() : 0;
        if (bones == 0)
            return Core::Unexpected(std::format("0x52ba47: el modelo {:#x} del dueño del 0x61e/6 no tiene huesos (el "
                                                "rand() % 0 del original)",
                                                owner->type));
        const auto bone = static_cast<std::size_t>(context.random.Next() % static_cast<int>(bones));
        if (skeleton->bones[bone].dummy)
            continue;
        glm::vec3 offset{};
        offset.x = static_cast<float>(context.random.Next() % SideSpan - SideCenter);
        offset.y = static_cast<float>(context.random.Next() % DepthSpan - DepthCenter);
        offset.z = static_cast<float>(context.random.Next() % DepthSpan - DepthCenter);
        // Sin huesos propios (antes del primer dibujo del dueño), la matriz en cero, como HandShinePoint (Q-C318).
        const Math::BoneTransform matrix =
            bone < owner->bodyPose.size() ? owner->bodyPose[bone] : Math::BoneTransform(0.0f);
        const glm::vec3 point =
            Math::OriginalTransformPosition(Math::OriginalMatrixOf(matrix), offset, placement.scale, placement.origin);
        if (auto made = CreateHighParticle(context.world, context.random, context.worldClock, World::SwellLifeParticle,
                                           point, owner->angles, glm::vec3(1.0f), SparkSubtype, SparkScale, 0x52baf5);
            !made.HasValue())
            return Core::Unexpected(made.Error());
    }
    constexpr double FadePerLife = static_cast<double>(std::bit_cast<float>(0x3c4ccccdu)); // 0x6ba208, 0,0125f
    owner->alpha = F32(static_cast<double>(effect.life) * FadePerLife);
    return {};
}

// 0x52bb29..0x52bb74, el 0x61e/9: la escala 0,3f más y la luz por 0,909f en cada canal.
void StepTheGrowingDecal(World::ModelEffect& effect)
{
    constexpr double Growth = static_cast<double>(std::bit_cast<float>(0x3e99999au)); // 0x6b89dc, 0,3f
    constexpr double Decay = static_cast<double>(std::bit_cast<float>(0x3f68ba2eu));  // 0x6ba26c
    effect.transform.scale = F32(D(effect.transform.scale) + Growth);
    for (int channel = 0; channel < 3; ++channel)
        effect.light[channel] = F32(D(effect.light[channel]) * Decay);
}
} // namespace

glm::vec3 PointAround(const Kernel::ActorState& actor, const glm::vec3& offset)
{
    return Sum(actor.position, Rotated(offset, actor.angles));
}

HighEffectContext HighContextOf(const Actors::CharacterStepContext& context)
{
    return {.world = context.world,
            .random = context.random,
            .worldClock = context.worldClock,
            .send = context.send,
            .models = context.models,
            .terrain = context.terrain};
}

glm::vec3 ActorBonePoint(const Kernel::ActorState& actor, std::size_t bone, const glm::vec3& offset,
                         const Kernel::WorldState& world)
{
    const Math::BoneTransform matrix = bone < actor.bodyPose.size() ? actor.bodyPose[bone] : Math::BoneTransform(0.0f);
    const auto placement = world.ModelPlacement(actor.type);
    return Math::OriginalTransformPosition(Math::OriginalMatrixOf(matrix), offset, placement.scale, placement.origin);
}

bool IsHighModelEffect(std::int16_t type)
{
    return type == SiegeAuraEffect || type == SiegeFlareEffect || type == SiegeSpearmanEffect ||
           type == SiegePulseEffect || type == RadialShardEffect || type == GrowingFlashEffect ||
           type == World::ColoredDecalEffect || (type >= MovingLightFirst && type <= MovingLightLast) ||
           type == FallingStarEffect || type == PierPartEffect || type == DarkLordSkillEffect ||
           type == ManaRuneEffect || type == SparkEmitterEffect || type == BoltEmitterEffect ||
           type == PiercingGlowEffect || type == SkullEffect || type == SeamanEffect || type == FireBreathEffect ||
           type == JavelinEffect || type == ThunderEffect || type == WaveEffect || type == PiercingEffect ||
           type == RingEffect || type == MeteorEffect || type == MeteorShellEffect || type == MeteorCallEffect ||
           type == GuildMarkEffect || type == KundunSpiritEffect || type == KundunPhoenixEffect ||
           type == KundunCallEffect || (type >= FirstKundunPieceEffect && type <= LastKundunPieceEffect) ||
           type == KundunEchoEffect || type == SweepEmitterEffect || type == EnergyBallEffect ||
           type == FlameBurstEffect;
}

Core::Expected<std::optional<std::size_t>, std::string>
CreateHighModelEffect(const World::ModelEffectArguments& arguments, const HighEffectContext& context)
{
    if (!IsHighModelEffect(arguments.type))
        return Core::Unexpected(std::format("0x518cf0: el alta del efecto {:#x} no es de este lote", arguments.type));
    // 0x51efea: el /0, el de World.
    if (arguments.type == PiercingGlowEffect && arguments.subtype == 0)
        return context.world.Map().effects.CreatePiercingShot(arguments);
    // 0x51c2b3..0x51c36d: las ondas 0x114/2..6 y el 0x115 (0x51c8e8), sin rand() ni nada de afuera: los de World.
    constexpr std::int32_t FirstWorldWave = 2, LastWorldWave = 6;
    if (arguments.type == WaveEffect && arguments.subtype >= FirstWorldWave && arguments.subtype <= LastWorldWave)
        return context.world.Map().effects.CreateWaveEffect(arguments);
    if (arguments.type == PiercingEffect)
        return context.world.Map().effects.CreatePiercingEffect(arguments);
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
    case World::ColoredDecalEffect:
        Terrain::BeginColoredDecal(effect, context.random);
        break;
    case MovingLightFirst:
    case MovingLightSecond:
    case MovingLightLast:
        begun = BeginTheMovingLight(effect, context);
        break;
    case FallingStarEffect:
        BeginTheFallingStar(effect, context);
        break;
    case PierPartEffect:
        begun = BeginThePierPart(effect, arguments, context);
        break;
    case DarkLordSkillEffect:
        BeginTheDarkLordSkill(effect, context.random);
        break;
    case ManaRuneEffect:
        BeginTheManaRune(effect);
        break;
    case SparkEmitterEffect:
    case BoltEmitterEffect:
        BeginTheEmitter(effect);
        break;
    case PiercingGlowEffect:
        BeginThePiercingGlow(effect, arguments);
        break;
    case SkullEffect:
        BeginTheSkull(effect);
        break;
    case SeamanEffect:
        BeginTheSeaman(effect);
        break;
    case FireBreathEffect:
        begun = BeginTheFireBreath(effect, context);
        break;
    case JavelinEffect:
        begun = BeginTheJavelin(effect, context);
        break;
    case ThunderEffect:
        BeginTheThunder(effect);
        break;
    case WaveEffect:
        begun = BeginTheWave(effect, arguments, context);
        break;
    case RingEffect:
        begun = BeginTheRing(effect, context);
        break;
    case MeteorEffect:
    case MeteorShellEffect:
    case MeteorCallEffect:
        BeginTheMeteor(effect);
        break;
    case KundunSpiritEffect:
        begun = BeginTheKundunSpirit(effect, context);
        break;
    case KundunPhoenixEffect:
        begun = BeginTheKundunPhoenix(effect, context);
        break;
    case KundunCallEffect:
        BeginTheKundunCall(context.world.Map().effects, *slot);
        break;
    case KundunEchoEffect:
        BeginTheKundunEcho(effect);
        break;
    case SweepEmitterEffect:
        BeginTheSweepEmitter(effect);
        break;
    case EnergyBallEffect:
        BeginTheEnergyBall(effect);
        break;
    case FlameBurstEffect:
        BeginTheFlameBurst(effect);
        break;
    case GuildMarkEffect:
    {
        // 0x51e0d8: el modelo y los huesos propios del dueño (+0x108 → +0x2 y +0x120).
        const auto* owner = static_cast<const Kernel::ActorState*>(arguments.owner);
        if (owner == nullptr)
        {
            begun = Core::Unexpected(std::string("0x51e0d8: el 0x140 lee el modelo de un dueño (+0x108) nulo"));
            break;
        }
        begun = BeginTheGuildMark(effect, *slot, *owner,
                                  ActorBonePoint(*owner, GuildMarkBone, glm::vec3(0.0f), context.world),
                                  context.world.Particles(), context.random, context.worldClock);
        break;
    }
    default:
        // 0x51e234: los pedazos de la caída de Kundun; sin la altura del terreno (la tienen los que llama
        // CreateTheKundunPiece), el /2, el /3 y el /4 cortan.
        if (arguments.type >= FirstKundunPieceEffect && arguments.type <= LastKundunPieceEffect)
            begun = BeginTheKundunPiece(effect, context.random, {});
        break;
    }
    if (!begun.HasValue())
        return Core::Unexpected(begun.Error());
    return slot;
}

bool StepsAsHighModelEffect(const World::ModelEffect& effect)
{
    // 0x52b984: los 0x61e que no van a 0x518900 (los /0, /1, /3 y los demás que sí van son de StepPetSummon).
    if (effect.type == World::ColoredDecalEffect)
        return effect.subtype == Terrain::PulsingDecalSubtype || effect.subtype == Terrain::BindingDecalSubtype ||
               effect.subtype == Terrain::BindingChildSubtype || effect.subtype == Terrain::SpawningDecalSubtype ||
               effect.subtype == Terrain::GrowingDecalSubtype || effect.subtype == Terrain::CoolPulsingDecalSubtype;
    // El 0x621/0 da su paso en World (StepGameModelEffect).
    if (effect.type == PiercingGlowEffect)
        return effect.subtype != 0;
    // El 0x114 y el 0x115 nacen aquí y dan su paso en World (0x526dd0 y 0x526f9d).
    if (effect.type == WaveEffect || effect.type == PiercingEffect)
        return false;
    // El 0x13a nace en Scenes/ArrowEffects (CreateTheBlast) y da su paso aquí (0x529dee).
    if (effect.type == ArrowBlastEffect)
        return true;
    return IsHighModelEffect(effect.type);
}

Core::Expected<void, std::string> StepHighModelEffect(World::ModelEffect& effect, const Models::Skeleton* skeleton,
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
    case World::ColoredDecalEffect:
        if (effect.subtype == Terrain::BindingDecalSubtype)
            stepped = StepTheBindingDecal(effect, context);
        else if (effect.subtype == Terrain::GrowingDecalSubtype)
            StepTheGrowingDecal(effect);
        break;
    case FallingStarEffect:
        stepped = StepTheFallingStar(effect, context);
        break;
    case PierPartEffect:
        stepped = StepThePierPart(effect, context);
        break;
    case DarkLordSkillEffect:
        StepTheDarkLordSkill(effect);
        break;
    case ManaRuneEffect:
        stepped = StepTheManaRune(effect, context);
        break;
    case SparkEmitterEffect:
        stepped = StepTheSparkEmitter(effect, context);
        break;
    case BoltEmitterEffect:
        stepped = StepTheBoltEmitter(effect, context);
        break;
    case PiercingGlowEffect:
        stepped = StepThePiercingGlow(effect, context);
        break;
    case SkullEffect:
        stepped = StepTheSkull(effect, context);
        break;
    case SeamanEffect:
        stepped = StepTheSeaman(effect, World::StepLuminosity(lightDraw, effect.life), context);
        break;
    case FireBreathEffect:
        stepped = StepTheFireBreath(effect, World::StepLuminosity(lightDraw, effect.life), context);
        break;
    case JavelinEffect:
        stepped = StepTheJavelin(effect, context);
        break;
    case ThunderEffect:
        stepped = StepTheThunder(effect, context);
        break;
    case RingEffect:
        stepped = StepTheRing(effect, World::StepLuminosity(lightDraw, effect.life), context);
        break;
    case MeteorEffect:
        stepped = StepTheMeteor(effect, skeleton, context);
        break;
    case MeteorCallEffect:
        stepped = StepTheMeteorCall(effect, context);
        break;
    case GuildMarkEffect:
        stepped = StepTheGuildMark(effect, context);
        break;
    case KundunSpiritEffect:
        stepped = StepTheKundunSpirit(effect, context);
        break;
    case KundunPhoenixEffect:
        StepTheKundunPhoenix(effect);
        break;
    case KundunCallEffect:
        stepped = StepTheKundunCall(effect, context);
        break;
    case KundunEchoEffect:
        stepped = StepTheKundunEcho(effect, context);
        break;
    case SweepEmitterEffect:
        stepped = StepTheSweepEmitter(effect, context);
        break;
    case EnergyBallEffect:
        stepped = StepTheEnergyBall(effect, context);
        break;
    case FlameBurstEffect:
        stepped = StepTheFlameBurst(effect, World::StepLuminosity(lightDraw, effect.life), context);
        break;
    case ArrowBlastEffect:
        stepped = StepTheArrowBlast(effect, context);
        break;
    default:
        if (effect.type >= FirstKundunPieceEffect && effect.type <= LastKundunPieceEffect)
            stepped = StepTheKundunPiece(effect, context);
        break;
    }
    if (!stepped.HasValue())
        return stepped;
    auto tail = CommonTail(effect, skeleton, context.services);
    // D-C4: el dibujo interpola desde antes del paso entero.
    effect.previousPosition = before;
    return tail;
}

std::function<std::optional<glm::vec3>(const void*)> ActorPositionOf(const Kernel::WorldState& world)
{
    return [&world](const void* owner) -> std::optional<glm::vec3>
    {
        for (const Kernel::ActorState& actor : world.Actors())
        {
            if (&actor == owner)
                return actor.position;
        }
        return std::nullopt;
    };
}
} // namespace Client::Scenes
