#include "Scenes/ModelEffects.h"

#include "Core/Expected.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/DynamicWorldLight.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GroundMarks.h"
#include "World/IFishStepServices.h"
#include "World/OriginalFtol.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/WorldObjectStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
{
namespace
{
using Result = Core::Expected<void, std::string>;
// Si el tipo tiene caso en 0x505de0 (true) o va a los helpers de 0x5085db (false).
using CaseResult = Core::Expected<bool, std::string>;

float F32(double value)
{
    return static_cast<float>(value);
}

double D(float value)
{
    return static_cast<double>(value);
}

float Bits(std::uint32_t bits)
{
    return std::bit_cast<float>(bits);
}

// 0x6b87b8: 0,1f.
constexpr float Tenth = 0.1f;
const glm::vec3 White{1.0f, 1.0f, 1.0f};
const glm::vec3 NoOffset{0.0f, 0.0f, 0.0f};

Core::Unexpected<std::string> Cut(const ModelEffectsPass& pass, std::uint32_t address, std::string_view what)
{
    return Core::Unexpected(std::format("{:#x}: {} del modelo {:#x} (lugar {}, mapa {}) no está portado", address, what,
                                        pass.actor.type, pass.slot, pass.map));
}

// 0x4ec110(modelo, +0x120 + hueso · 0x30, offset, salida, 1): los huesos propios con la escala (+0x68) y la posición
// (+0x6c) que 0x505ec6 copió del personaje al modelo. Antes del primer dibujo, una matriz en cero (Q-C318).
glm::vec3 BonePoint(const ActorState& actor, std::size_t bone, const glm::vec3& offset)
{
    const Render::Models::BoneTransform matrix =
        bone < actor.bodyPose.size() ? actor.bodyPose[bone] : Render::Models::BoneTransform(0.0f);
    return Render::Models::OriginalTransformPosition(Render::Models::OriginalMatrixOf(matrix), offset, actor.scale,
                                                     actor.position);
}

// 0x53fe70(tipo, posición, ángulos, luz, subtipo, 1, 0). Con el pool lleno no crea nada ni corta; con una que el pool
// no conoce, corta (CL11).
Result Emit(ModelEffectsPass& pass, std::uint32_t address, std::int32_t type, std::int32_t subtype,
            const glm::vec3& position, const glm::vec3& light, const glm::vec3& angles)
{
    if (pass.particles == nullptr)
        return Core::Unexpected(std::format("{:#x}: faltan las partículas del modelo {:#x} (lugar {})", address,
                                            pass.actor.type, pass.slot));
    const auto created =
        pass.particles->CreateChecked(type, position, angles, light, subtype, 1.0f, pass.worldClock, pass.random, 0);
    if (!created.HasValue())
        return Core::Unexpected(std::format("{:#x}: la partícula {:#x}/{} del modelo {:#x} (lugar {}) no está portada "
                                            "(error {}, CL11)",
                                            address, type, subtype, pass.actor.type, pass.slot,
                                            static_cast<unsigned>(created.Error())));
    return {};
}

// 0x5f9780(x, y, color, radio, 0x7b4ccc4).
Result Light(ModelEffectsPass& pass, std::string_view address, const glm::vec3& color, std::int32_t radius)
{
    return World::AddDynamicLight(&pass.world.DynamicLighting().Lighting(), pass.actor.position, color, radius,
                                  address);
}

// 0x530860(tipo, primero, segundo, ángulos, subtipo, 0, ancho, −1, 0, 0, −1). Con el pool lleno no crea nada.
Result Joint(ModelEffectsPass& pass, std::uint32_t address, std::int32_t type, std::int32_t subtype, glm::vec3 first,
             glm::vec3 second, glm::vec3 angles, float width)
{
    const World::FishEffectRequest request{.type = type,
                                           .firstPosition = first,
                                           .secondPosition = second,
                                           .angles = angles,
                                           .subtype = subtype,
                                           .ownerContext = nullptr,
                                           .scale = width,
                                           .key = -1,
                                           .skill = 0,
                                           .skillSerial = 0,
                                           .target = -1};
    const auto created = CreateFaunaJoint(pass.world.DrawTail().joints, request, pass.random);
    if (!created.HasValue())
        return Core::Unexpected(
            std::format("{:#x}: {} (modelo {:#x}, lugar {})", address, created.Error(), pass.actor.type, pass.slot));
    return {};
}

// 0x41ed10(sonido, 0, 0) (CL10).
void Sound(ModelEffectsPass& pass, std::int32_t sound)
{
    pass.world.Runtime().lastSoundId = sound;
}

// FLD del reloj del mundo, _ftol y IDIV: el resto con signo.
Core::Expected<std::int32_t, std::string> ClockRemainder(const ModelEffectsPass& pass, std::uint32_t address,
                                                         std::int32_t modulus)
{
    const auto clock = World::OriginalFtolLow(D(pass.worldClock));
    if (!clock)
        return Core::Unexpected(std::format("{:#x}: ftol del reloj del mundo fuera de lo acreditado", address));
    return *clock % modulus;
}

// FILD del resto y FMUL de un float, guardado en float.
Core::Expected<float, std::string> ClockScroll(const ModelEffectsPass& pass, std::uint32_t address,
                                               std::int32_t modulus, float factor)
{
    const auto remainder = ClockRemainder(pass, address, modulus);
    if (!remainder.HasValue())
        return Core::Unexpected(remainder.Error());
    return F32(static_cast<double>(remainder.Value()) * D(factor));
}

// rand() % n con el IDIV (rand() no es negativo).
std::int32_t Roll(ModelEffectsPass& pass, std::int32_t modulus)
{
    return pass.random.Next() % modulus;
}

// Tres rand() alrededor de la posición: (rand() & 63) − 32, (rand() & 63) − 32 y (rand() & 31) − 16, cada uno por FILD
// y el FADD de la coordenada, guardado en float (0x50830d..0x508386, 0x5083d7..0x508449, 0x505cb5..0x505d29).
glm::vec3 Scatter(ModelEffectsPass& pass)
{
    const glm::vec3& at = pass.actor.position;
    const float x = F32(static_cast<double>(Roll(pass, 64) - 32) + D(at.x));
    const float y = F32(static_cast<double>(Roll(pass, 64) - 32) + D(at.y));
    const float z = F32(static_cast<double>(Roll(pass, 32) - 16) + D(at.z));
    return {x, y, z};
}

// 0x505bc0(c, modelo, a, b): +0x4c con el hueso a y +0x40 con el b, sin offset.
void MarkTheBones(ModelEffectsPass& pass, std::size_t first, std::size_t second)
{
    pass.actor.bonePoint4c = BonePoint(pass.actor, first, NoOffset);
    pass.actor.effectOrigin = BonePoint(pass.actor, second, NoOffset);
}

// 0x505d50: en la acción 2, el polvo 0x5f3/0 en rand() % 200 + x − 100 y rand() % 200 + y − 100 (FILD, FADD de la
// coordenada y FSUB de 100,0f en la x87), la z del personaje, con la luz propia (+0xf4).
Result RaiseTheDust(ModelEffectsPass& pass)
{
    if (pass.actor.animation.action != 2)
        return {};
    const glm::vec3& at = pass.actor.position;
    constexpr double Spread = 100.0;
    const float x = F32(static_cast<double>(Roll(pass, 200)) + D(at.x) - Spread);
    const float y = F32(static_cast<double>(Roll(pass, 200)) + D(at.y) - Spread);
    return Emit(pass, 0x505dc4, World::DustParticle, 0, {x, y, at.z}, pass.actor.light, pass.actor.angles);
}

// 0x505c50: en la acción 6 con el frame en [8, 9), veinte vueltas: la luz propia en (1, 1, 1), tres rand() alrededor
// de la posición y el polvo 0x5f3/1.
Result BurstTheDust(ModelEffectsPass& pass)
{
    ActorState& actor = pass.actor;
    const float frame = actor.animation.frame;
    if (actor.animation.action != 6 || !(frame >= 8.0f) || !(frame < 9.0f))
        return {};
    constexpr int Bursts = 20;
    for (int burst = 0; burst < Bursts; ++burst)
    {
        actor.light = White;
        const glm::vec3 position = Scatter(pass);
        if (auto emitted = Emit(pass, 0x505d2c, World::DustParticle, 1, position, actor.light, actor.angles);
            !emitted.HasValue())
            return emitted;
    }
    return {};
}

// 0x4eb860 (0x4eb86d..0x4eb94c): la key que deja en +0xa8 del modelo, como PoseKeyOf de MonsterDrawing.
std::optional<std::int16_t> PoseKeyOf(const Render::Models::Skeleton& skeleton,
                                      const Render::Models::AnimationState& animation)
{
    const auto truncated = World::OriginalFtolLow(D(animation.frame));
    if (skeleton.actions.empty() || !truncated)
        return std::nullopt;
    auto key = static_cast<std::int16_t>(*truncated);
    if (key < 0)
        key = 0;
    const std::size_t action = animation.action < skeleton.actions.size() ? animation.action : 0;
    if (key >= skeleton.actions[action].keyCount)
        key = 0;
    return key;
}

// La velocidad de la acción del personaje en su modelo (+0x30 + acción · 0x10 + 4).
Core::Expected<float, std::string> SpeedOf(const ModelEffectsPass& pass, std::uint32_t address)
{
    const std::uint8_t action = pass.actor.animation.action;
    if (action >= pass.skeleton.actions.size())
        return Core::Unexpected(std::format("{:#x}: la acción {} del modelo {:#x} pasa su tabla de acciones (+0x30)",
                                            address, action, pass.actor.type));
    return pass.skeleton.actions[action].speed;
}

// 0x4eb860(modelo, 0x68b181c, frame − velocidad, +0x118, +0x112, ángulos, +0x28, 0, 1): la pose en el buffer global
// (WorldState::PoseCarry), trasladada al mundo, con la key en +0xa8 del modelo. La acción, la escala y la posición del
// modelo son las del personaje (Q-C1102). Devuelve la velocidad.
Core::Expected<float, std::string> PoseTheModel(ModelEffectsPass& pass, std::uint32_t address)
{
    const auto speed = SpeedOf(pass, address);
    if (!speed.HasValue())
        return speed;
    const ActorState& actor = pass.actor;
    const Render::Models::Skeleton& skeleton = pass.skeleton;
    const Render::Models::AnimationState animation{.action = actor.animation.action,
                                                   .frame = F32(D(actor.animation.frame) - D(speed.Value())),
                                                   .previousAction = actor.animation.previousAction,
                                                   .previousKey = actor.animation.previousKey};
    const Render::Models::ObjectTransform transform{
        .position = actor.position, .angles = actor.angles, .scale = actor.scale, .lockedRootHeight = 0.0f};
    FaunaDrawCarry& carry = pass.world.PoseCarry();
    if (carry.bones.size() < skeleton.bones.size())
        carry.bones.resize(skeleton.bones.size(), Render::Models::BoneTransform(0.0f));
    const auto bones = std::span(carry.bones).first(skeleton.bones.size());
    if (!Render::Models::PoseSkeleton(skeleton, animation, transform, actor.headTurn.angles, bones))
        return Core::Unexpected(std::format("{:#x}: la pose del modelo {:#x} falló", address, actor.type));
    FaunaModels& models = pass.world.FaunaResources();
    if (auto published = PublishFaunaPoseRaw(carry, models, actor.type, bones, transform, true, false);
        !published.HasValue())
        return Core::Unexpected(published.Error());
    if (const auto key = PoseKeyOf(skeleton, animation))
        models.RecordPoseKey(actor.type, *key);
    return speed;
}

// La ventana del ataque de 0x431470: el frame desde `first` y antes de la velocidad más `first` (FLD de la velocidad
// guardada en float, FADD y FCOMP con el frame en la x87). NaN no entra.
bool InTheStrike(float frame, float speed, float first)
{
    return frame >= first && D(speed) + D(first) > D(frame);
}

// rand() % la cantidad de huesos del modelo (MOVSX de +0x22).
Core::Expected<std::size_t, std::string> RandomBone(ModelEffectsPass& pass, std::uint32_t address)
{
    const auto count = static_cast<std::int16_t>(pass.skeleton.bones.size());
    if (count == 0)
        return Core::Unexpected(
            std::format("{:#x}: el modelo {:#x} no tiene huesos y el IDIV divide por 0", address, pass.actor.type));
    return static_cast<std::size_t>(pass.random.Next() % count);
}

Result ScrollU(ModelEffectsPass& pass, std::uint32_t address, std::int32_t modulus, float factor)
{
    const auto scroll = ClockScroll(pass, address, modulus, factor);
    if (!scroll.HasValue())
        return Core::Unexpected(scroll.Error());
    pass.actor.textureU = scroll.Value();
    return {};
}

Result ScrollV(ModelEffectsPass& pass, std::uint32_t address, std::int32_t modulus, float factor)
{
    const auto scroll = ClockScroll(pass, address, modulus, factor);
    if (!scroll.HasValue())
        return Core::Unexpected(scroll.Error());
    pass.actor.textureV = scroll.Value();
    return {};
}

// 0x507006..0x5073c1: el 0x185, catorce joints 0x614/7 de ancho 14 entre pares de huesos.
Result StepThe185(ModelEffectsPass& pass)
{
    constexpr std::array<std::pair<std::size_t, std::size_t>, 14> Pairs{{{2, 3},
                                                                         {3, 4},
                                                                         {4, 5},
                                                                         {2, 10},
                                                                         {10, 11},
                                                                         {2, 18},
                                                                         {18, 22},
                                                                         {22, 23},
                                                                         {23, 24},
                                                                         {24, 25},
                                                                         {18, 31},
                                                                         {31, 32},
                                                                         {32, 33},
                                                                         {33, 34}}};
    constexpr float Width = 14.0f;
    for (const auto& [first, second] : Pairs)
    {
        if (auto joined = Joint(pass, 0x50706f, 0x614, 7, BonePoint(pass.actor, first, NoOffset),
                                BonePoint(pass.actor, second, NoOffset), pass.actor.angles, Width);
            !joined.HasValue())
            return joined;
    }
    return {};
}

// 0x507414..0x50748e: el 0x182, con rand() % 5 en 0, la marca 0x5e3 del suelo en (rand() % 21 − 10) · 3,657f más la x
// y lo mismo en la y, con el ángulo x (+0x1c), la luz propia y 0,65f. La z sale de [EBP−0x8], lo que dejó ahí el código
// de antes: 0x549130 no la lee (solo la x y la y).
Result StepThe182(ModelEffectsPass& pass)
{
    if (Roll(pass, 5) != 0)
        return {};
    const ActorState& actor = pass.actor;
    const double step = D(Bits(0x406a0ea1u)); // 3,657f (0x6ba1a8)
    const float x = F32(static_cast<double>(Roll(pass, 21) - 10) * step + D(actor.position.x));
    const float y = F32(static_cast<double>(Roll(pass, 21) - 10) * step + D(actor.position.y));
    const World::GroundMarkRequest request{.type = 0x5e3,
                                           .position = {x, y, 0.0f},
                                           .angle = actor.angles.x,
                                           .color = actor.light,
                                           .size = Bits(0x3f266666u)};
    if (auto marked = World::AddGroundMark(pass.world.Runtime().groundMarks, request, pass.random); !marked.HasValue())
        return Core::Unexpected(std::format("0x507486: {} (modelo {:#x})", marked.Error(), actor.type));
    return {};
}

// 0x5079c0..0x507a1b: el 0x177 sube +0x68 0,1 en la 3 y la 4, con tope 1, y lo baja 0,1 en las demás, con piso 0 (el
// FCOMP mira la suma de la x87, no la guardada; con NaN, la 3 y la 4 la dejan y las demás la ponen en 0).
void StepThe177(ModelEffectsPass& pass)
{
    ActorState& actor = pass.actor;
    const std::uint8_t action = actor.animation.action;
    if (action >= 3 && action <= 4)
    {
        const double raised = D(actor.blendMeshLight) + D(Tenth);
        actor.blendMeshLight = F32(raised);
        if (raised > 1.0)
            actor.blendMeshLight = 1.0f;
        return;
    }
    const double lowered = D(actor.blendMeshLight) - D(Tenth);
    actor.blendMeshLight = F32(lowered);
    if (!(lowered >= 0.0))
        actor.blendMeshLight = 0.0f;
}

// 0x507a20..0x507ab2: el 0x16f, +0x64 en 3, la V del reloj y, con rand() % 2 en 0, el 0x5d9/0 del hueso 2.
Result StepThe16f(ModelEffectsPass& pass)
{
    pass.actor.dword64 = 3;
    if (auto scrolled = ScrollV(pass, 0x507a2d, 1000, Bits(0xba83126fu)); !scrolled.HasValue())
        return scrolled;
    if (Roll(pass, 2) != 0)
        return {};
    return Emit(pass, 0x507aaa, World::FireParticle, 0, BonePoint(pass.actor, 2, NoOffset), White, pass.actor.angles);
}

// 0x507b05..0x507da6: el 0x172. En la 3, la 4, la 8 y la 9, del hueso de la acción el 0x5ca/0 con (0,6; 1; 0,8) y el
// 0x5d9/0 con (1; 0,6; 1); en la 6 antes del frame 12 (o NaN), veinte 0x5d9/0 con (0,1; 0,8; 0,6) en huesos de
// rand() % la cantidad de huesos.
Result StepThe172(ModelEffectsPass& pass)
{
    ActorState& actor = pass.actor;
    struct Strike
    {
        std::uint8_t action;
        std::size_t bone;
        std::uint32_t thunder;
        std::uint32_t fire;
    };
    constexpr std::array<Strike, 4> Strikes{{
        {3, 33, 0x507b71, 0x507ba2},
        {4, 20, 0x507beb, 0x507c1c},
        {8, 41, 0x507c65, 0x507c96},
        {9, 49, 0x507cde, 0x507d0f},
    }};
    const glm::vec3 spark{Bits(0x3f19999au), 1.0f, Bits(0x3f4ccccdu)};
    const glm::vec3 flame{1.0f, Bits(0x3f19999au), 1.0f};
    for (const Strike& strike : Strikes)
    {
        if (actor.animation.action != strike.action)
            continue;
        const glm::vec3 point = BonePoint(actor, strike.bone, NoOffset);
        if (auto emitted = Emit(pass, strike.thunder, World::ThunderParticle, 0, point, spark, actor.angles);
            !emitted.HasValue())
            return emitted;
        if (auto emitted = Emit(pass, strike.fire, World::FireParticle, 0, point, flame, actor.angles);
            !emitted.HasValue())
            return emitted;
    }
    if (actor.animation.action != 6 || actor.animation.frame >= 12.0f)
        return {};
    const glm::vec3 glow{Tenth, Bits(0x3f4ccccdu), Bits(0x3f19999au)};
    constexpr int Flames = 20;
    for (int burst = 0; burst < Flames; ++burst)
    {
        const auto bone = RandomBone(pass, 0x507d69);
        if (!bone.HasValue())
            return Core::Unexpected(bone.Error());
        if (auto emitted = Emit(pass, 0x507d9b, World::FireParticle, 0, BonePoint(actor, bone.Value(), NoOffset), glow,
                                actor.angles);
            !emitted.HasValue())
            return emitted;
    }
    return {};
}

// 0x507dab..0x507e5e: el 0x15d, +0x68 en (rand() % 10) · 0,1f; con el nivel (+0x1d6) en 2, diez 0x5d9/0 en huesos de
// rand() y la luz (s; s · 0,2; 0) de radio 2.
Result StepThe15d(ModelEffectsPass& pass, float brightness)
{
    ActorState& actor = pass.actor;
    actor.blendMeshLight = F32(static_cast<double>(Roll(pass, 10)) * D(Tenth));
    if (actor.level != 2)
        return {};
    constexpr int Flames = 10;
    for (int flame = 0; flame < Flames; ++flame)
    {
        const auto bone = RandomBone(pass, 0x507ded);
        if (!bone.HasValue())
            return Core::Unexpected(bone.Error());
        if (auto emitted = Emit(pass, 0x507e1f, World::FireParticle, 0, BonePoint(actor, bone.Value(), NoOffset), White,
                                actor.angles);
            !emitted.HasValue())
            return emitted;
    }
    return Light(pass, "0x507e56", {brightness, F32(D(brightness) * D(Bits(0x3e4ccccdu))), 0.0f}, 2);
}

// 0x507ee6..0x50808e: el 0x1ce. Con +0x7c & 0x10000000, nada. En la 0 con el frame en [5, 10], el sonido 0xa0; +0x68
// en s, la luz (s; s · 0,4; 0) de radio 3 y +0x64 en 4; en la 0 con el frame en [5, 6], del hueso 17 cuatro joints
// 0x617/0 de ancho 10 con los ángulos (rand() % 60 + 150, 0, rand() % 30) y el 0x5c5/0 blanco.
Result StepThe1ce(ModelEffectsPass& pass, float brightness)
{
    ActorState& actor = pass.actor;
    if ((actor.flags7c & 0x10000000u) != 0)
        return {};
    const bool resting = actor.animation.action == 0;
    const float frame = actor.animation.frame;
    if (resting && frame >= 5.0f && frame <= 10.0f)
        Sound(pass, 0xa0);
    actor.blendMeshLight = brightness;
    if (auto lit = Light(pass, "0x507f74", {brightness, F32(D(brightness) * D(Bits(0x3ecccccdu))), 0.0f}, 3);
        !lit.HasValue())
        return lit;
    actor.dword64 = 4;
    if (!resting || !(frame >= 5.0f) || !(frame <= 6.0f))
        return {};
    const glm::vec3 point = BonePoint(actor, 17, NoOffset);
    constexpr int Sparks = 4;
    for (int spark = 0; spark < Sparks; ++spark)
    {
        const float pitch = static_cast<float>(Roll(pass, 60) + 150);
        const float turn = static_cast<float>(Roll(pass, 30));
        const glm::vec3 angles{pitch, 0.0f, turn};
        if (auto joined = Joint(pass, 0x508060, 0x617, 0, point, point, angles, 10.0f); !joined.HasValue())
            return joined;
        if (auto emitted = Emit(pass, 0x50807f, World::FairySparkParticle, 0, point, White, angles);
            !emitted.HasValue())
            return emitted;
    }
    return {};
}

// 0x508093..0x5081c4: el 0x1f5 en la 0: la luz (s · 0,5; s · 0,3; 0) de radio 3 y, del hueso 37 en (0, 5, 10), cuatro
// joints 0x617/0 de ancho 10 con los ángulos (rand() % 60 + 90, 0, rand() % 30) y, con rand() % 2 en 1, el 0x5c5/0
// con esa luz.
Result StepThe1f5(ModelEffectsPass& pass, float brightness)
{
    ActorState& actor = pass.actor;
    if (actor.animation.action != 0)
        return {};
    const glm::vec3 glow{F32(D(brightness) * D(0.5f)), F32(D(brightness) * D(Bits(0x3e99999au))), 0.0f};
    if (auto lit = Light(pass, "0x5080fd", glow, 3); !lit.HasValue())
        return lit;
    const glm::vec3 point = BonePoint(actor, 37, {0.0f, 5.0f, 10.0f});
    constexpr int Sparks = 4;
    for (int spark = 0; spark < Sparks; ++spark)
    {
        const float pitch = static_cast<float>(Roll(pass, 60) + 90);
        const float turn = static_cast<float>(Roll(pass, 30));
        const glm::vec3 angles{pitch, 0.0f, turn};
        if (auto joined = Joint(pass, 0x508180, 0x617, 0, point, point, angles, 10.0f); !joined.HasValue())
            return joined;
        if (Roll(pass, 2) == 0)
            continue;
        if (auto emitted = Emit(pass, 0x5081b5, World::FairySparkParticle, 0, point, glow, angles); !emitted.HasValue())
            return emitted;
    }
    return {};
}

// 0x50826d..0x50839c y 0x50844c: el 0x155, el 0x158 y el 0x166 (y el 0x154 después de lo suyo). El 0x166, el sprite
// 0x5ac del hueso 7 con (s; s · 0,4; s · 0,2), que queda en la luz; sin morirse (+0x389), con rand() % 4 en 0, tres
// rand() alrededor y, en Devias, el 0x5f2/0 o, si no, el 0x5f3/0 con esa luz.
Result StepThe155(ModelEffectsPass& pass, float brightness)
{
    ActorState& actor = pass.actor;
    glm::vec3 light = White;
    if (actor.type == 0x166)
    {
        const glm::vec3 point = BonePoint(actor, 7, NoOffset);
        light = {brightness, F32(D(brightness) * D(Bits(0x3ecccccdu))), F32(D(brightness) * D(Bits(0x3e4ccccdu)))};
        if (pass.sprites == nullptr)
            return Core::Unexpected(std::format(
                "0x5082dd: faltan los sprites del frame para el modelo {:#x} (lugar {})", actor.type, pass.slot));
        static_cast<void>(pass.sprites->Create(0x5ac, point, 1.0f, light, 0.0f, 0));
    }
    if (actor.dying != 0 || Roll(pass, 4) != 0)
        return {};
    const glm::vec3 position = Scatter(pass);
    constexpr std::int32_t Devias = 2;
    if (pass.map == Devias)
        return Emit(pass, 0x508394, World::BreathParticle, 0, position, light, actor.angles);
    return Emit(pass, 0x508451, World::DustParticle, 0, position, light, actor.angles);
}

// 0x5081c9..0x508265: el 0x154 en la 3 con el frame en 4 o menos (o NaN), el 0x5d9/1 blanco del hueso 7 en (0,
// rand() % 32 + 32, 0); después, lo del 0x155.
Result StepThe154(ModelEffectsPass& pass, float brightness)
{
    ActorState& actor = pass.actor;
    if (actor.animation.action == 3 && !(actor.animation.frame > 4.0f))
    {
        const glm::vec3 offset{0.0f, static_cast<float>(Roll(pass, 32) + 32), 0.0f};
        if (auto emitted =
                Emit(pass, 0x508265, World::FireParticle, 1, BonePoint(actor, 7, offset), White, actor.angles);
            !emitted.HasValue())
            return emitted;
    }
    return StepThe155(pass, brightness);
}

// 0x5083a1..0x508459: el 0x173 sin morirse y con el nivel en 1, con rand() % 4 en 0: tres rand() alrededor y el
// 0x5f3/0 blanco.
Result StepThe173(ModelEffectsPass& pass)
{
    ActorState& actor = pass.actor;
    if (actor.dying != 0 || actor.level != 1 || Roll(pass, 4) != 0)
        return {};
    const glm::vec3 position = Scatter(pass);
    return Emit(pass, 0x508451, World::DustParticle, 0, position, White, actor.angles);
}

// 0x50846c..0x5085d1: el 0x15e y el 0x15f, con rand() % 4 en 0, el 0x5f2/0 del hueso 22 con la luz propia.
Result StepThe15e(ModelEffectsPass& pass)
{
    if (Roll(pass, 4) != 0)
        return {};
    return Emit(pass, 0x5085d1, World::BreathParticle, 0, BonePoint(pass.actor, 22, NoOffset), pass.actor.light,
                pass.actor.angles);
}

// 0x5084b3..0x5085d1: el 0x152 con el frame en [15, 20] de la 0, en [20, 25] de la 1 o en [2, 3] o [5, 6] de la 2: con
// rand() % 2 en 0, el 0x5f2/0 del hueso 24 en (0, −4, 0) con la luz propia.
Result StepThe152(ModelEffectsPass& pass)
{
    const ActorState& actor = pass.actor;
    const float frame = actor.animation.frame;
    bool breath = false;
    switch (actor.animation.action)
    {
    case 0:
        breath = frame >= 15.0f && frame <= 20.0f;
        break;
    case 1:
        breath = frame >= 20.0f && frame <= 25.0f;
        break;
    case 2:
        breath = (frame >= 2.0f && frame <= 3.0f) || (frame >= 5.0f && frame <= 6.0f);
        break;
    default:
        break;
    }
    if (!breath || Roll(pass, 2) != 0)
        return {};
    return Emit(pass, 0x5085d1, World::BreathParticle, 0, BonePoint(actor, 24, {0.0f, -4.0f, 0.0f}), actor.light,
                actor.angles);
}

// 0x5078b5..0x507989: el 0x17c. Con el subtipo 1, el 0x5d9/0 blanco de los huesos 6 y 13 y la luz (−1,3) × 3 de radio
// 3; si no, 0x505d50 y 0x505c50.
Result StepThe17c(ModelEffectsPass& pass)
{
    ActorState& actor = pass.actor;
    MarkTheBones(pass, 24, 25);
    if (actor.subtype != 1)
    {
        if (auto dust = RaiseTheDust(pass); !dust.HasValue())
            return dust;
        return BurstTheDust(pass);
    }
    for (const auto& [bone, address] : {std::pair<std::size_t, std::uint32_t>{6, 0x507907}, {13, 0x507943}})
    {
        if (auto emitted =
                Emit(pass, address, World::FireParticle, 0, BonePoint(actor, bone, NoOffset), White, actor.angles);
            !emitted.HasValue())
            return emitted;
    }
    const float dark = Bits(0xbfa66666u);
    return Light(pass, "0x507970", {dark, dark, dark}, 3);
}

// El caso del tipo − 0x152 (0x506ccc..0x50862f).
CaseResult StepTheTypeCase(ModelEffectsPass& pass, float brightness)
{
    Result stepped{};
    switch (pass.actor.type)
    {
    case 0x185:
        stepped = StepThe185(pass);
        break;
    case 0x186:
        stepped = ScrollU(pass, 0x5073cc, 10000, Bits(0xb9d1b717u));
        break;
    case 0x189:
        stepped = ScrollV(pass, 0x5073f3, 10000, Bits(0x38d1b717u));
        break;
    case 0x182:
        stepped = StepThe182(pass);
        break;
    case 0x17f:
    case 0x1aa:
    case 0x17d:
        MarkTheBones(pass, pass.actor.type == 0x17d ? 11 : 8, pass.actor.type == 0x17d ? 12 : 9);
        stepped = RaiseTheDust(pass);
        break;
    case 0x17e:
        // 0x50749c..0x507897: 0x505bc0(8, 9) y, con +0x376 en 0x3f, el 0x5de de los huesos 22 a 29 (0x50758a); si no,
        // del 55 y el 62 (0x50780d). El 0x5de no lo conoce el pool (CL11), con lo que viene después: las luces, los
        // demás 0x5de y, sin el 0x3f, 0x505d50 y 0x505c50. Con el pool lleno el original sigue; MuRet corta igual.
        MarkTheBones(pass, 8, 9);
        return Cut(pass, pass.actor.word376 == 0x3f ? 0x50758a : 0x50780d, "el 0x5de (0x53fe70) y lo que le sigue");
    case 0x17c:
        stepped = StepThe17c(pass);
        break;
    case 0x17b:
        MarkTheBones(pass, 8, 9);
        stepped = RaiseTheDust(pass);
        if (stepped.HasValue())
            stepped = BurstTheDust(pass);
        break;
    case 0x179:
        MarkTheBones(pass, 28, 27);
        break;
    case 0x177:
        StepThe177(pass);
        break;
    case 0x16f:
        stepped = StepThe16f(pass);
        break;
    case 0x16d:
        stepped = ScrollV(pass, 0x507abd, 1000, Bits(0xba83126fu));
        break;
    case 0x16c:
        stepped = ScrollU(pass, 0x507ae4, 10000, Bits(0xb8d1b717u));
        break;
    case 0x172:
        stepped = StepThe172(pass);
        break;
    case 0x15d:
        stepped = StepThe15d(pass, brightness);
        break;
    case 0x161:
        stepped = ScrollV(pass, 0x507e69, 2000, Bits(0xba03126fu));
        break;
    case 0x1d8:
        // 0x507e8a: rand() & 63 en 0, el sonido 0xa1.
        if (Roll(pass, 64) == 0)
            Sound(pass, 0xa1);
        break;
    case 0x1d3:
        // 0x507eb7: rand() & 255 en 0, el sonido 0xa0.
        if (Roll(pass, 256) == 0)
            Sound(pass, 0xa0);
        break;
    case 0x1ce:
        stepped = StepThe1ce(pass, brightness);
        break;
    case 0x1f5:
        stepped = StepThe1f5(pass, brightness);
        break;
    case 0x154:
        stepped = StepThe154(pass, brightness);
        break;
    case 0x155:
    case 0x158:
    case 0x166:
        stepped = StepThe155(pass, brightness);
        break;
    case 0x173:
        stepped = StepThe173(pass);
        break;
    case 0x157:
        stepped = BurstTheDust(pass);
        break;
    case 0x15e:
    case 0x15f:
        stepped = StepThe15e(pass);
        break;
    case 0x152:
        stepped = StepThe152(pass);
        break;
    case 0x183:
    case 0x184:
    case 0x188:
        // Directo a 0x50862f.
        break;
    default:
        return false;
    }
    if (!stepped.HasValue())
        return Core::Unexpected(stepped.Error());
    return true;
}

// 0x43bdd0(c, modelo), la tabla 0x43c07c sobre 0x43c064 desde el 0xc5. Devuelve 1 con los suyos.
CaseResult StepTheKalimaHelper(ModelEffectsPass& pass)
{
    ActorState& actor = pass.actor;
    const double clock = D(pass.worldClock);
    const double rate = D(Bits(0x3a83126fu)); // 0,001f (0x6b89c8)
    switch (actor.type)
    {
    case 0xc5:
    {
        // 0x43bdfd..0x43bef3: en la 0, dos 0x5f3/0 en rand() % 200 + x − 100 y rand() % 100 + y − 50 con la luz propia
        // y los efectos 0xc8 y 0xc9 de 0x518cf0 (CL11). Si no, +0x64 en 1 y +0x68 en (sen(T · 0,001f) + 1) · 0,5f.
        if (actor.animation.action == 0)
        {
            const float x = F32(static_cast<double>(Roll(pass, 200)) + D(actor.position.x) - 100.0);
            const float y = F32(static_cast<double>(Roll(pass, 100)) + D(actor.position.y) - 50.0);
            const glm::vec3 position{x, y, actor.position.z};
            for (const std::uint32_t address : {0x43be6eu, 0x43be87u})
                if (auto emitted = Emit(pass, address, World::DustParticle, 0, position, actor.light, actor.angles);
                    !emitted.HasValue())
                    return Core::Unexpected(emitted.Error());
            return Cut(pass, 0x43bea5, "el efecto 0xc8 (0x518cf0) de la acción 0");
        }
        actor.dword64 = 1;
        const auto pulse = Render::Models::Detail::SineSumPC53(clock * rate, 1.0);
        if (!pulse)
            return Core::Unexpected(std::string("0x43bee5: el reloj del mundo está fuera del dominio de FSIN"));
        actor.blendMeshLight = F32(*pulse * 0.5);
        return true;
    }
    case 0x191:
    case 0x196:
    {
        // 0x43bf84: +0x68 en sen(T · 0,001f) · 0,7f + 0,3f.
        const auto pulse = Render::Models::Detail::SineProductPC53(clock * rate, Bits(0x3f333333u));
        if (!pulse)
            return Core::Unexpected(std::string("0x43bf92: el reloj del mundo está fuera del dominio de FSIN"));
        actor.blendMeshLight = F32(*pulse + D(Bits(0x3e99999au)));
        return true;
    }
    case 0x192:
    case 0x197:
    {
        // 0x43befb y 0x43bfa8: el 0x197 en la 6, nada; si no, rand() % 2 en 0 pide del hueso 6 (el 0x192) o 31 (el
        // 0x197) en (2, 30, 0) el 0x5f2/0x11, que el pool no conoce.
        if (actor.type == 0x197 && actor.animation.action == 6)
            return true;
        if (Roll(pass, 2) != 0)
            return true;
        const bool tall = actor.type == 0x197;
        const glm::vec3 point = BonePoint(actor, tall ? 31 : 6, {2.0f, 30.0f, 0.0f});
        glm::vec3 light{1.0f, Bits(0x3e4ccccdu), 0.0f};
        if (tall)
            light = actor.subtype == 9 ? glm::vec3{1.0f, 0.0f, 0.0f} : glm::vec3{0.0f, Bits(0x3e99999au), 1.0f};
        if (auto emitted =
                Emit(pass, tall ? 0x43c04b : 0x43bf75, World::BreathParticle, 0x11, point, light, actor.angles);
            !emitted.HasValue())
            return Core::Unexpected(emitted.Error());
        return true;
    }
    case 0x193:
    case 0x194:
    case 0x195:
        // 0x43c053.
        return true;
    default:
        return false;
    }
}

// 0x42de90(c, modelo), solo en el mapa 30 ([0x6c9dd0]), la tabla 0x42df78 sobre 0x42df64 desde el 0x19b.
CaseResult StepTheCastleHelper(ModelEffectsPass& pass)
{
    constexpr std::int32_t CastleMap = 30;
    if (pass.map != CastleMap)
        return false;
    ActorState& actor = pass.actor;
    switch (actor.type)
    {
    case 0x19b:
    case 0x19d:
    case 0x19e:
    case 0x19f:
    case 0x1a0:
        // 0x42df47: el ángulo z (+0x24) en 0.
        actor.angles.z = 0.0f;
        return true;
    case 0x1a1:
        // 0x42df55: en 45.
        actor.angles.z = 45.0f;
        return true;
    case 0x19c:
    {
        // 0x42deeb..0x42df40: en 0 y la luz (v · 0,2; v · 0,7; v) de radio 2, con v = sen(T · 0,0005) · 0,58f + 0,42f
        // (el reloj por el double de 0x6b8aa8 en la x87).
        actor.angles.z = 0.0f;
        constexpr double Rate = 0.0005;
        const auto wave = Render::Models::Detail::SineProductPC53(D(pass.worldClock) * Rate, Bits(0x3f147ae1u));
        if (!wave)
            return Core::Unexpected(std::string("0x42df0e: el reloj del mundo está fuera del dominio de FSIN"));
        const double glow = *wave + D(Bits(0x3ed70a3du));
        const glm::vec3 color{F32(glow * D(Bits(0x3e4ccccdu))), F32(glow * D(Bits(0x3f333333u))), F32(glow)};
        if (auto lit = Light(pass, "0x42df38", color, 2); !lit.HasValue())
            return Core::Unexpected(lit.Error());
        return true;
    }
    case 0x1fc:
    {
        // 0x42dec1..0x42dee4: +0x58 en −2; con el asedio (0x4294b0) y [0x7239e4] en 0x20000, en −1. Devuelve 0.
        actor.dword58 = -2;
        const WorldRuntime& runtime = pass.world.Runtime();
        constexpr std::uint32_t CrownOpen = 0x20000;
        if (runtime.siegeActive && runtime.castleFlags7239e4 == CrownOpen)
            actor.dword58 = -1;
        return false;
    }
    default:
        return false;
    }
}

// 0x42f550(c, modelo), solo en el mapa 35 (0x42ee20). Devuelve 0.
Result StepTheKanturuHelper(ModelEffectsPass& pass)
{
    constexpr std::int32_t KanturuMap = 35;
    if (pass.map != KanturuMap)
        return {};
    const ActorState& actor = pass.actor;
    const float frame = actor.animation.frame;
    switch (actor.type)
    {
    case 0x1b3:
        // 0x42f5a8..0x42f63b: en la 4 con el frame en [4, 6], el efecto 0x115 de 0x518cf0.
        if (actor.animation.action == 4 && frame >= 4.0f && frame <= 6.0f)
            return Cut(pass, 0x42f63b, "el efecto 0x115 (0x518cf0) del ataque");
        return {};
    case 0x1b2:
        // 0x42f656: rand() % 360 y los sprites 0x5ac y 0x5fd de huesos por nombre (0x4292e0).
        static_cast<void>(Roll(pass, 360));
        return Cut(pass, 0x42f6ce, "el hueso por nombre (0x4292e0) y los sprites 0x5ac y 0x5fd");
    case 0x1b1:
        // 0x42f8fa: fuera de la 6, el sprite 0x5ac de un hueso por nombre; en la 6, nada (no es la 2 ni la 0xa).
        if (actor.animation.action != 6)
            return Cut(pass, 0x42f946, "el hueso por nombre (0x4292e0) y el sprite 0x5ac");
        return {};
    default:
        return {};
    }
}

// 0x431470 en la 2 y la 0xa: con rand() % 10 en 0, el 0x5f3/0 en la posición con la luz del caso (0x431e86).
Result KickTheDust(ModelEffectsPass& pass, const glm::vec3* light, std::uint32_t local)
{
    const std::uint8_t action = pass.actor.animation.action;
    if (action != 2 && action != 0xa)
        return {};
    if (Roll(pass, 10) != 0)
        return {};
    if (light == nullptr)
        return Core::Unexpected(std::format("0x431e86: la luz del polvo 0x5f3/0 del modelo {:#x} (lugar {}) es "
                                            "[EBP−{:#x}], que ese camino de 0x431470 no escribe (comportamiento "
                                            "indefinido)",
                                            pass.actor.type, pass.slot, local));
    return Emit(pass, 0x431e86, World::DustParticle, 0, pass.actor.position, *light, pass.actor.angles);
}

// 0x431470(c, c, modelo), la tabla 0x432488 desde el 0x1ab. Devuelve 0 salvo el 0x1b5 después de su efecto.
Result StepTheCrywolfHelper(ModelEffectsPass& pass)
{
    ActorState& actor = pass.actor;
    const std::uint8_t action = actor.animation.action;
    const float frame = actor.animation.frame;
    const glm::vec3 ember{Bits(0x3f666666u), Bits(0x3e4ccccdu), Tenth};
    const std::uint8_t stage = pass.world.Runtime().crywolf.stage;
    constexpr std::uint8_t Siege = 4;
    switch (actor.type)
    {
    case 0x1ab:
    {
        // 0x431868: 0x505bc0(9, 10), la pose y, en la 4 dentro de [7,5; velocidad + 7,5), los efectos 0x636 de
        // 0x518cf0.
        MarkTheBones(pass, 9, 10);
        const auto speed = PoseTheModel(pass, 0x4318bc);
        if (!speed.HasValue())
            return Core::Unexpected(speed.Error());
        if (action == 4)
        {
            if (InTheStrike(frame, speed.Value(), 7.5f))
                return Cut(pass, 0x43195f, "el efecto 0x636 (0x518cf0) del ataque");
            return {};
        }
        return KickTheDust(pass, nullptr, 0x18);
    }
    case 0x1ad:
    case 0x1af:
        // 0x4319f1 y 0x431bdd: la luz (0,9; 0,2; 0,1).
        return KickTheDust(pass, &ember, 0);
    case 0x1ae:
        // 0x431a3c: con la etapa de Crywolf (0x723c05) en 4 y [0x723bec], que escriben los paquetes de Crywolf (CL9.5)
        // y MuRet no modela (en 0), sus efectos. Sin eso, nada.
        return {};
    case 0x1b0:
    {
        // 0x43161d: en la 3 dentro de [6,5; velocidad + 6,5), los efectos 0x636 y las partículas 0x640.
        const auto speed = SpeedOf(pass, 0x431636);
        if (!speed.HasValue())
            return Core::Unexpected(speed.Error());
        if (action == 3)
        {
            if (InTheStrike(frame, speed.Value(), 6.5f))
                return Cut(pass, 0x4316db, "el efecto 0x636 (0x518cf0) del ataque");
            return {};
        }
        return KickTheDust(pass, &White, 0);
    }
    case 0x1b1:
        // 0x431c28: fuera de la 6, el sprite 0x5ac de un hueso por nombre; en la 6, nada.
        if (action != 6)
            return Cut(pass, 0x431d5c, "el hueso por nombre (0x4292e0) y el sprite 0x5ac");
        return {};
    case 0x1b2:
        // 0x431f52: rand() % 360 y los sprites 0x5ac y 0x5fd de huesos por nombre.
        static_cast<void>(Roll(pass, 360));
        return Cut(pass, 0x431fa4, "el hueso por nombre (0x4292e0) y los sprites 0x5ac y 0x5fd");
    case 0x1b3:
        // 0x431ea1: en la 4 con el frame en [4, 6], el efecto 0x115.
        if (action == 4 && frame >= 4.0f && frame <= 6.0f)
            return Cut(pass, 0x431f37, "el efecto 0x115 (0x518cf0) del ataque");
        return {};
    case 0x1b4:
    {
        // 0x4314ad: la pose y, en la 4 dentro de [5,5; velocidad + 5,5), seis efectos 0xc9.
        const auto speed = PoseTheModel(pass, 0x4314f3);
        if (!speed.HasValue())
            return Core::Unexpected(speed.Error());
        if (action == 4)
        {
            if (InTheStrike(frame, speed.Value(), 5.5f))
                return Cut(pass, 0x4315b4, "el efecto 0xc9 (0x518cf0) del ataque");
            return {};
        }
        return KickTheDust(pass, nullptr, 0x24);
    }
    case 0x1b5:
    {
        // 0x4323d2: con la etapa de Crywolf en 4, rand() % 100 en 0 le pone la acción 3 (0x4ea030) y +0x84 en 1; en la
        // 3 con +0x84 en 1 desde el frame 5, el efecto 0xed.
        if (stage != Siege)
            return {};
        if (Roll(pass, 100) == 0)
        {
            Render::Models::SetAction(actor.animation, pass.skeleton, 3);
            actor.movementPhase = 1.0f;
        }
        if (actor.animation.action == 3 && std::bit_cast<std::uint32_t>(actor.movementPhase) == 0x3f800000u &&
            actor.animation.frame >= 5.0f)
            return Cut(pass, 0x432460, "el efecto 0xed (0x518cf0)");
        return {};
    }
    default:
        // 0x4315e0: el 0x1ac y los de fuera de la tabla.
        return {};
    }
}

// 0x426ce0(c, modelo): la luz de radio 3 del 0x1b6, el 0x1b7 y el 0x1bb. Devuelve 0.
Result StepTheLightHelper(ModelEffectsPass& pass)
{
    switch (pass.actor.type)
    {
    case 0x1b6:
        return Light(pass, "0x426d7f", {Bits(0x3f333333u), Tenth, Tenth}, 3);
    case 0x1b7:
        return Light(pass, "0x426d7f", {0.0f, 0.0f, Bits(0x3f333333u)}, 3);
    case 0x1bb:
        return Light(pass, "0x426d28", {1.0f, 0.0f, 0.0f}, 3);
    default:
        return {};
    }
}

// 0x5085db: los seis, hasta el primero que devuelve 1.
Result StepTheHelpers(ModelEffectsPass& pass)
{
    const auto kalima = StepTheKalimaHelper(pass);
    if (!kalima.HasValue())
        return Core::Unexpected(kalima.Error());
    if (kalima.Value())
        return {};
    const auto castle = StepTheCastleHelper(pass);
    if (!castle.HasValue())
        return Core::Unexpected(castle.Error());
    if (castle.Value())
        return {};
    // 0x441210: el 0x1a4, la luz (1; 0,2; 0,1) de radio 3. Devuelve 0.
    if (pass.actor.type == 0x1a4)
        if (auto lit = Light(pass, "0x441249", {1.0f, Bits(0x3e4ccccdu), Tenth}, 3); !lit.HasValue())
            return lit;
    if (auto kanturu = StepTheKanturuHelper(pass); !kanturu.HasValue())
        return kanturu;
    if (auto crywolf = StepTheCrywolfHelper(pass); !crywolf.HasValue())
        return crywolf;
    return StepTheLightHelper(pass);
}
} // namespace

Core::Expected<void, std::string> StepTheModelEffects(ModelEffectsPass& pass)
{
    // 0x506c82..0x506cc6: la luz local en (1, 1, 1), el offset en 0 y s = ((rand() & 7) + 2) · 0,1f.
    const float brightness = F32(static_cast<double>(pass.random.Next() % 8 + 2) * D(Tenth));
    const auto handled = StepTheTypeCase(pass, brightness);
    if (!handled.HasValue())
        return Core::Unexpected(handled.Error());
    if (handled.Value())
        return {};
    return StepTheHelpers(pass);
}
} // namespace Client::Scenes
