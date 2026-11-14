#include "Scenes/KalimaSiegeHelpers.h"

#include "Core/Expected.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "World/IFishStepServices.h"
#include "World/KalimaWorld.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/TerrainDecal.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
{
namespace
{
using Render::Models::BoneTransform;
using Result = Core::Expected<void, std::string>;

constexpr std::int32_t FirstKalimaMap = 24;
constexpr std::int32_t LastKalimaMap = 29;
constexpr std::int32_t SeventhKalimaMap = 36;
constexpr std::int32_t CastleMap = 30;
constexpr std::uint8_t DeadAction = 6;
constexpr std::uint16_t NoModel = 0xffff;

// Las texturas de los sprites (0x5491e0).
constexpr std::uint16_t Star = 0x5ac;
constexpr std::uint16_t Flare = 0x5ca;
constexpr std::uint16_t Glow = 0x5fd;
constexpr std::uint16_t Ring = 0x61e;
constexpr std::uint16_t Halo = 0x62b;
constexpr std::uint16_t Pulse = 0x62d;

// Las constantes float de la .data, con los mismos bits que el literal.
constexpr float Tenth = 0.1f;        // 0x6b87b8
constexpr float Fifth = 0.2f;        // 0x6b8854
constexpr float ThreeTenths = 0.3f;  // 0x6b89dc
constexpr float TwoFifths = 0.4f;    // 0x6b87b4
constexpr float Thousandth = 0.001f; // 0x6b89c8

float F32(double value)
{
    return static_cast<float>(value);
}

double D(float value)
{
    return static_cast<double>(value);
}

bool InKalima(std::int32_t map)
{
    return (map >= FirstKalimaMap && map <= LastKalimaMap) || map == SeventhKalimaMap;
}

// FLD T; FMUL tasa; FSIN y FMUL de un float de la .data (`Wave`), FMUL de un double (`WaveDouble`) o FADD (`WavePlus`):
// el seno retenido con un solo R53 (ADR0014). DrawTheTail llama a los helpers después de DrawGameHanging, que corta con
// el reloj fuera de 2^40: el argumento cae siempre en el perfil y el 0 de afuera no se alcanza (como GameHanging).
double Wave(float clock, float rate, float factor)
{
    return Render::Models::Detail::SineProductPC53(D(clock) * D(rate), factor).value_or(0.0);
}

double WaveDouble(float clock, float rate, double factor)
{
    return Render::Models::Detail::SineDoubleProductPC53(D(clock) * D(rate), factor).value_or(0.0);
}

double WavePlus(float clock, float rate, double addend)
{
    return Render::Models::Detail::SineSumPC53(D(clock) * D(rate), addend).value_or(0.0);
}

// rand() % 360 en un float, como FILD/FSTP.
float RandomTurn(Render::Models::CrtRandom& random)
{
    constexpr int Turn = 360;
    return static_cast<float>(random.Next() % Turn);
}

// 0x620a90 y 0x620b10 ponen la mezcla con su perfil entero y no tocan el depth test ni la textura ligada (como las
// marcas del suelo, GroundMarks.cpp).
Render::ModelDrawState Blended(Render::ModelDrawState state, Render::ModelBlend blend)
{
    state.blend = blend;
    state.cullEnabled.reset();
    state.depthWrite.reset();
    state.alphaTest.reset();
    state.texture2d.reset();
    return state;
}

// Los servicios de 0x43c150 y 0x42dfe0 sobre el pase.
class Helper
{
public:
    explicit Helper(const KalimaSiegeHelperPass& pass) : m_pass(pass) {}

    [[nodiscard]] ActorState& Actor() const
    {
        return m_pass.actor;
    }

    [[nodiscard]] float Clock() const
    {
        return m_pass.worldClock;
    }

    [[nodiscard]] Render::Models::CrtRandom& Random() const
    {
        return m_pass.random;
    }

    // 0x4ec110(hueso, offset, salida, 1) con el modelo del personaje: el hueso propio (+0x120) con la escala y la
    // posición que 0x6001b0 le dio al modelo.
    [[nodiscard]] glm::vec3 Point(std::size_t bone, const glm::vec3& offset) const
    {
        const ActorState& actor = m_pass.actor;
        const BoneTransform matrix = bone < actor.bodyPose.size() ? actor.bodyPose[bone] : BoneTransform(0.0f);
        return Render::Models::OriginalTransformPosition(Render::Models::OriginalMatrixOf(matrix), offset, actor.scale,
                                                         actor.position);
    }

    // 0x5491e0(textura, posición, escala, luz, objeto, rotación, mezcla).
    void Sprite(std::uint16_t texture, const glm::vec3& position, float scale, const glm::vec3& light,
                float rotation = 0.0f, int blend = 0) const
    {
        static_cast<void>(m_pass.sprites->Create(texture, position, scale, light, rotation, blend));
    }

    // 0x53fe70(tipo, posición, ángulos, luz, subtipo, escala, 0) en `address`. Llena, éxito sin nada; con lugar, el
    // tipo que MuRet no tiene corta antes de sacar rand().
    [[nodiscard]] Result Particle(std::int32_t type, const glm::vec3& position, const glm::vec3& angles,
                                  const glm::vec3& light, std::int32_t subtype, float scale,
                                  std::string_view address) const
    {
        if (m_pass.particles == nullptr)
            return Core::Unexpected(std::format("{}: faltan las partículas del frame (0x53fe70)", address));
        const auto created = m_pass.particles->CreateChecked(type, position, angles, light, subtype, scale,
                                                             m_pass.worldClock, m_pass.random);
        if (!created.HasValue())
            return Core::Unexpected(std::format("{}: la partícula {:#x}/{} (0x53fe70) no está portada (CL11, error {})",
                                                address, type, subtype, static_cast<int>(created.Error())));
        return {};
    }

    // 0x530860 en `address`: llena, nada; con lugar, el tipo que MuRet no tiene corta.
    [[nodiscard]] Result Joint(const World::FishEffectRequest& request, std::string_view address) const
    {
        if (m_pass.joints.empty())
            return Core::Unexpected(std::format("{}: faltan los joints del frame (0x530860)", address));
        const auto created = CreateFaunaJoint(m_pass.joints, request, m_pass.random);
        if (!created.HasValue())
            return Core::Unexpected(std::format("{}: {} (CL11)", address, created.Error()));
        return {};
    }

    // 0x518cf0 con un tipo que MuRet no crea: con un lugar sin el byte vivo corta en `address`; llena, nada.
    [[nodiscard]] Result UnportedEffect(std::int32_t type, std::int32_t subtype, std::string_view address) const
    {
        if (m_pass.effects == nullptr)
            return Core::Unexpected(std::format("{}: faltan los efectos del frame (0x518cf0)", address));
        const auto effects = m_pass.effects->Effects();
        if (std::ranges::any_of(effects, [](const World::ModelEffect& effect) { return !effect.alive; }))
            return Core::Unexpected(
                std::format("{}: el efecto {:#x}/{} (0x518cf0) no está portado (CL11)", address, type, subtype));
        return {};
    }

    // 0x518cf0(9, posición, ángulos, luz, 0, 0, −1, 0, 0, 0, 0).
    [[nodiscard]] Result KalimaEffect(const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light) const
    {
        if (m_pass.effects == nullptr)
            return Core::Unexpected<std::string>("0x43cfad: faltan los efectos del frame (0x518cf0)");
        static_cast<void>(m_pass.effects->CreateKalimaEffect(position, angles, light, m_pass.random));
        return {};
    }

    // 0x41ed10(id, 0, 0): en WorldRuntime::lastSoundId (CL10), solo en el frame del original.
    void Sound(std::int32_t id) const
    {
        if (m_pass.sound != nullptr)
            *m_pass.sound = id;
    }

    // 0x7d325c8.
    [[nodiscard]] Result Shake(float value, std::string_view address) const
    {
        if (m_pass.cameraShake == nullptr)
            return Core::Unexpected(std::format("{}: falta el temblor de la cámara (0x7d325c8)", address));
        *m_pass.cameraShake = value;
        return {};
    }

    // Las celdas lógicas (+0x420 y después +0x41c, ReadLogicalCell) y 0x437860(x, y, 2, 2000), que sin el mundo de
    // Kalima (0x723c48 en 0) no hace nada.
    [[nodiscard]] Result Disturb() const
    {
        const LogicalCell cell = ReadLogicalCell(m_pass.actor, m_pass.random);
        if (m_pass.kalima == nullptr)
            return {};
        constexpr std::int32_t Radius = 2;
        constexpr std::int32_t Amplitude = 2000;
        return m_pass.kalima->Disturb(cell.x, cell.y, Radius, Amplitude, m_pass.random);
    }

    [[nodiscard]] const KalimaSiegeHelperPass& Pass() const
    {
        return m_pass;
    }

private:
    const KalimaSiegeHelperPass& m_pass;
};

// 0x43c17f..0x43c22a, el 0xc5.
Result DrawTheGate(const Helper& helper)
{
    ActorState& actor = helper.Actor();
    if (actor.animation.action != 1)
        return {};
    // 0x43c18c..0x43c1a6: la posición 20 más arriba va al local [ebp−0x38], que 0x4ec110 pisa con el punto del hueso 8
    // antes de la partícula: la partícula sale del hueso.
    if (actor.dword60 == 0)
    {
        constexpr std::int32_t GateParticle = 0x6ae;
        constexpr float GateScale = 3.0f;
        const glm::vec3 point = helper.Point(8, {});
        if (auto made =
                helper.Particle(GateParticle, point, actor.angles, {1.0f, Tenth, Tenth}, 0, GateScale, "0x43c20d");
            !made.HasValue())
            return made;
    }
    // 0x43c215..0x43c223: inc de 32 bits y, desde 5 (con signo), a 0.
    actor.dword60 = static_cast<std::int32_t>(static_cast<std::uint32_t>(actor.dword60) + 1u);
    constexpr std::int32_t GateCycle = 5;
    if (actor.dword60 >= GateCycle)
        actor.dword60 = 0;
    return {};
}

// 0x43c565..0x43c638: los 0x5ac de los huesos 51..55 y 29..33, también en la acción 6.
void DrawTheSpineStars(const Helper& helper)
{
    const double lum = Wave(helper.Clock(), Thousandth, Fifth) + D(TwoFifths);
    const float stored = F32(lum);
    const glm::vec3 light{F32(lum * D(Tenth)), F32(D(stored) * D(TwoFifths)), stored};
    constexpr std::size_t FirstBone = 29;
    constexpr std::size_t LastBone = 33;
    constexpr std::size_t UpperBones = 22; // 0x420 / 0x30
    for (std::size_t bone = FirstBone; bone <= LastBone; ++bone)
    {
        helper.Sprite(Star, helper.Point(bone + UpperBones, {}), 2.0f, light, helper.Clock());
        helper.Sprite(Star, helper.Point(bone, {}), 1.5f, light, helper.Clock());
    }
}

// 0x43c233..0x43c646, el 0x191.
Result DrawTheFirstKalimaHelper(const Helper& helper)
{
    ActorState& actor = helper.Actor();
    if (actor.animation.action != DeadAction)
    {
        // 0x43c26e..0x43c3dc: cinco vueltas, con la luz que sube 0,25 por vuelta y la escala que baja 0,2.
        float level = 0.0f;
        constexpr std::size_t FirstLowBone = 62;
        constexpr std::size_t HighBones = 12; // 0x240 / 0x30
        constexpr std::size_t Rounds = 5;
        constexpr std::int32_t FlameParticle = 0x607;
        for (std::size_t round = 0; round < Rounds; ++round)
        {
            const double raised = D(level) + D(0.25f);
            level = F32(raised);
            const glm::vec3 light{F32(raised * D(Tenth)), F32(raised * D(TwoFifths)), F32(raised)};
            const float scale = F32(D(1.5f) - static_cast<double>(round) * D(Fifth));
            const std::size_t low = FirstLowBone + round;
            for (const std::size_t bone : {low + HighBones, low})
            {
                const glm::vec3 point = helper.Point(bone, {});
                helper.Sprite(Star, point, scale, light, helper.Clock());
                helper.Sprite(Star, point, scale, light, helper.Clock());
                if ((helper.Random().Next() & 1) != 0)
                    continue;
                const std::string_view address = bone == low ? "0x43c3bf" : "0x43c332";
                if (auto made = helper.Particle(FlameParticle, point, actor.angles, light, 1, 1.0f, address);
                    !made.HasValue())
                    return made;
            }
        }
        // 0x43c3e2..0x43c55d: con la luz de las mallas (+0x68).
        const float lit = actor.blendMeshLight;
        const float dim = F32(D(lit) * D(Tenth));
        const glm::vec3 white{lit, lit, lit};
        const glm::vec3 blue{dim, dim, lit};
        const glm::vec3 eyes = helper.Point(8, {2.0f, 2.0f, 0.0f});
        helper.Sprite(Flare, eyes, Fifth, white);
        helper.Sprite(Glow, eyes, 1.0f, blue);
        for (const std::size_t bone : {std::size_t{22}, std::size_t{15}})
        {
            const glm::vec3 point = helper.Point(bone, {});
            helper.Sprite(Flare, point, Fifth, blue);
            helper.Sprite(Glow, point, 1.0f, blue);
        }
    }
    DrawTheSpineStars(helper);
    return {};
}

// 0x43c7a8..0x43c8e3: el pulso de la acción 5 en +0x8a (un short) y +0x89, y la escala del 0x62d.
float KundunPulse(ActorState& actor)
{
    const float frame = actor.animation.frame;
    constexpr std::int16_t Rest = 10;
    if (frame > 10.0f && actor.word8a > Rest)
        actor.word8a = static_cast<std::int16_t>(actor.word8a - 1);
    else if (frame > 4.0f)
        actor.word8a = static_cast<std::int16_t>(static_cast<std::uint16_t>(actor.word8a) + 1u);
    else
        actor.word8a = Rest;
    constexpr float Step = 0.15f; // 0x6b8ae8
    float scale = F32(static_cast<double>(actor.word8a) * D(Step));
    // El byte sube 3 antes de la mitad de la ventana y baja 3 después (si pasa de 3); fuera de las dos, a 0.
    const auto beat = [&actor, &scale, frame](float middle)
    {
        constexpr std::uint8_t Beat = 3;
        if (!(frame >= middle))
            actor.byte89 = static_cast<std::uint8_t>(actor.byte89 + Beat);
        else if (actor.byte89 > Beat)
            actor.byte89 = static_cast<std::uint8_t>(actor.byte89 - Beat);
        scale = F32(static_cast<double>(actor.byte89) * D(Step) + D(scale));
    };
    if (frame > 4.0f && !(frame >= 6.0f))
        beat(5.0f);
    else if (frame > 8.0f && !(frame >= 10.0f))
        beat(9.0f);
    else
        actor.byte89 = 0;
    return scale;
}

// 0x43ce8b..0x43cfb6: el temblor y, en Kalima, los dos efectos 9 que caen.
Result ShakeForKundun(const Helper& helper)
{
    const ActorState& actor = helper.Actor();
    const float frame = actor.animation.frame;
    if (frame > 3.0f)
    {
        const int jolt = (helper.Random().Next() & 7) - 8;
        if (auto shaken = helper.Shake(F32(static_cast<double>(jolt) * D(Fifth)), "0x43cec4"); !shaken.HasValue())
            return shaken;
    }
    if (!InKalima(helper.Pass().map) || !(frame > 3.0f) || frame >= 14.0f)
        return {};
    const glm::vec3 light{ThreeTenths, 0.8f, 1.0f};
    const glm::vec3 angles{};
    for (int stone = 0; stone < 2; ++stone)
    {
        const float turn = RandomTurn(helper.Random());
        constexpr int Spread = 600;
        constexpr int Near = 200;
        // 0x43cf7d..0x43cf99: FSIN y FCOS del giro (en radianes, sin convertir) por la distancia entera de la x87 (FMUL
        // ST1), con un solo R53 cada uno (ADR0014); el giro, menor que 360, cae siempre en el perfil.
        const auto distance = static_cast<float>(helper.Random().Next() % Spread + Near);
        const auto sine = Render::Models::Detail::SineProductPC53(D(turn), distance);
        const auto cosine = Render::Models::Detail::CosineProductPC53(D(turn), distance);
        if (!sine || !cosine)
            return Core::Unexpected<std::string>("0x43cf83: el giro de la piedra fuera del perfil PC53");
        const glm::vec3 position{F32(*sine + D(actor.position.x)), F32(*cosine + D(actor.position.y)),
                                 F32(D(actor.position.z) + 800.0)};
        if (auto made = helper.KalimaEffect(position, angles, light); !made.HasValue())
            return made;
    }
    return {};
}

// 0x43cfb8..0x43dc81: las acciones 3 y 4 de Kundun.
Result StepKundunAttacks(const Helper& helper)
{
    ActorState& actor = helper.Actor();
    const std::uint8_t action = actor.animation.action;
    const float frame = actor.animation.frame;
    constexpr std::int32_t Armed = 100;
    constexpr std::int32_t Cast = 0x65;
    constexpr std::int32_t Burst = 0x66;
    constexpr std::int32_t Done = 0x67;
    constexpr std::int32_t BigEffect = 0x128;
    constexpr std::size_t HandBone = 49;
    if (action == 3)
    {
        if (!(frame >= 2.0f))
            actor.dword60 = Armed;
        if (actor.dword60 == Armed && frame > 3.0f)
        {
            actor.dword60 = Cast;
            // 0x43cff8..0x43d04e: la x y la y del objeto con la z del hueso 49, para el efecto.
            return helper.UnportedEffect(BigEffect, 0, "0x43d051");
        }
        return {};
    }
    if (action != 4)
        return {};
    if (!(frame >= 2.0f))
        actor.dword60 = Armed;
    if (actor.dword60 == Armed && frame > 5.0f)
    {
        actor.dword60 = Cast;
        // 0x43d09b..0x43d0f6: el hueso 49 a 400 de altura, para el efecto.
        if (auto made = helper.UnportedEffect(BigEffect, 1, "0x43d0fd"); !made.HasValue())
            return made;
    }
    if (actor.dword60 == Cast && frame > 6.0f)
    {
        actor.dword60 = Burst;
        // 0x43d126..0x43d1b3: veinticuatro joints 0x6ee/14 desde el hueso 49 a 400 de altura, de a 30 grados.
        glm::vec3 point = helper.Point(HandBone, {});
        point.z = 400.0f;
        constexpr int FullTurn = 720;
        constexpr int TurnStep = 30;
        constexpr float Width = 100.0f;
        for (int turn = 0; turn < FullTurn; turn += TurnStep)
        {
            glm::vec3 angles{0.0f, 0.0f, static_cast<float>(turn)};
            if (auto made = helper.Joint({0x6ee, point, point, angles, 14, nullptr, Width, 0, 0, 0, -1}, "0x43d19f");
                !made.HasValue())
                return made;
        }
        if (InKalima(helper.Pass().map))
        {
            if (auto disturbed = helper.Disturb(); !disturbed.HasValue())
                return disturbed;
        }
    }
    if (actor.dword60 == Burst && frame > 9.0f)
    {
        actor.dword60 = Done;
        if (InKalima(helper.Pass().map))
        {
            if (auto disturbed = helper.Disturb(); !disturbed.HasValue())
                return disturbed;
        }
    }
    if (frame > 6.0f)
    {
        const int jolt = (helper.Random().Next() & 15) - 16;
        return helper.Shake(F32(static_cast<double>(jolt) * D(Tenth)), "0x43dc7b");
    }
    return {};
}

// 0x43c647..0x43dc89, el 0x192 (Kundun).
Result DrawKundun(const Helper& helper)
{
    ActorState& actor = helper.Actor();
    if (actor.animation.action == DeadAction)
    {
        // 0x43c650..0x43c65f: la mano izquierda sin ítem.
        actor.hands[1].model = NoModel;
        actor.hands[1].level = 0xff;
        return {};
    }
    const float red = F32(Wave(helper.Clock(), 0.003f, Fifth) + D(0.8f));
    const glm::vec3 eyes{red, 0.0f, 0.0f};
    helper.Sprite(Flare, helper.Point(8, {}), TwoFifths, eyes);
    helper.Sprite(Flare, helper.Point(9, {}), TwoFifths, eyes);
    const glm::vec3 white{1.0f, 1.0f, 1.0f};
    constexpr std::size_t StaffBone = 100;
    const glm::vec3 staff = helper.Point(StaffBone, {});
    constexpr std::uint8_t CastAction = 5;
    const double halo = Wave(helper.Clock(), Thousandth, ThreeTenths) + D(1.2f);
    float scale = 1.0f;
    if (actor.animation.action == CastAction)
    {
        scale = KundunPulse(actor);
        helper.Sprite(Halo, staff, F32(halo), white);
        helper.Sprite(Pulse, staff, F32(halo * D(scale)), white);
    }
    else
    {
        actor.word8a = 0;
        helper.Sprite(Halo, staff, F32(halo), white);
    }
    if ((helper.Random().Next() & 1) == 0)
    {
        if (auto made = helper.Particle(0x5f2, staff, actor.angles, white, 13, scale, "0x43c9cb"); !made.HasValue())
            return made;
    }
    if (actor.animation.action == CastAction)
    {
        const float frame = actor.animation.frame;
        constexpr std::int32_t Armed = 100;
        if (!(frame >= 2.0f) && actor.dword60 != Armed)
        {
            actor.dword60 = Armed;
            constexpr std::int32_t RoarSound = 0x1db;
            helper.Sound(RoarSound);
        }
        if (actor.dword60 == Armed && frame > 4.0f)
        {
            constexpr std::int32_t Cast = 0x65;
            actor.dword60 = Cast;
            if (InKalima(helper.Pass().map))
            {
                if (auto disturbed = helper.Disturb(); !disturbed.HasValue())
                    return disturbed;
            }
        }
        return ShakeForKundun(helper);
    }
    return StepKundunAttacks(helper);
}

// 0x43dc8a..0x43e07f, el 0x193.
void DrawTheThirdKalimaHelper(const Helper& helper)
{
    if (helper.Actor().animation.action == DeadAction)
        return;
    const float lum = F32(Wave(helper.Clock(), 0.003f, Fifth) + D(0.8f));
    const float pale = F32(D(lum) * D(0.9f));
    const glm::vec3 core{lum, pale, pale};
    const glm::vec3 fire{lum, F32(D(lum) * D(ThreeTenths)), F32(D(lum) * D(Tenth))};
    const glm::vec3 ember{lum, F32(D(lum) * D(0.5f)), F32(D(lum) * D(Tenth))};
    for (const float side : {-7.0f, 7.0f})
    {
        const glm::vec3 point = helper.Point(6, {20.0f, 30.0f, side});
        helper.Sprite(Flare, point, Fifth, core);
        helper.Sprite(Glow, point, 1.0f, fire);
    }
    for (const auto& [bone, side] : {std::pair{std::size_t{13}, 7.0f}, std::pair{std::size_t{19}, -7.0f}})
    {
        const glm::vec3 point = helper.Point(bone, {0.0f, 0.0f, side});
        helper.Sprite(Flare, point, Tenth, core);
        helper.Sprite(Glow, point, 0.7f, ember);
    }
    const float warm = F32(D(lum) * D(0.8f));
    const glm::vec3 light{lum, warm, warm};
    helper.Sprite(Star, helper.Point(23, {}), 3.0f, light);
    helper.Sprite(Star, helper.Point(25, {-30.0f, -25.0f, 0.0f}), 2.5f, light);
    helper.Sprite(Star, helper.Point(31, {}), 3.0f, light);
    helper.Sprite(Star, helper.Point(33, {30.0f, 25.0f, 0.0f}), 2.5f, light);
}

// 0x43e080..0x43e3f2, el 0x194.
Result DrawTheFourthKalimaHelper(const Helper& helper)
{
    ActorState& actor = helper.Actor();
    if (actor.animation.action == DeadAction)
        return {};
    const float lum = F32(Wave(helper.Clock(), 0.005f, 0.15f) + D(0.85f));
    const glm::vec3 white{lum, lum, lum};
    const glm::vec3 blue{F32(D(lum) * D(ThreeTenths)), F32(D(lum) * D(0.6f)), lum};
    for (const std::size_t bone : {std::size_t{9}, std::size_t{10}})
    {
        const glm::vec3 point = helper.Point(bone, {4.0f, 0.0f, 5.0f});
        helper.Sprite(Flare, point, Tenth, white);
        helper.Sprite(Glow, point, 1.0f, blue);
    }
    const glm::vec3 violet{0.7f, 0.6f, 1.0f};
    constexpr float StarScale = 1.7f; // 0x3fd9999a
    for (const std::size_t bone : {std::size_t{3}, std::size_t{4}})
    {
        const glm::vec3 point = helper.Point(bone, {});
        helper.Sprite(Star, point, StarScale, violet);
        helper.Sprite(Glow, point, 1.0f, violet, RandomTurn(helper.Random()));
    }
    if (actor.animation.action != 3)
        return {};
    // 0x43e306..0x43e3e8: los joints 0x614/7 entre los huesos 10..16 y 31..37 de a pares.
    constexpr float Width = 30.0f;
    struct Chain
    {
        std::size_t first;
        std::size_t last;
        std::string_view address;
    };
    for (const Chain& chain : {Chain{10, 16, "0x43e368"}, Chain{31, 37, "0x43e3da"}})
    {
        for (std::size_t bone = chain.first; bone < chain.last; ++bone)
        {
            glm::vec3 from = helper.Point(bone, {});
            glm::vec3 to = helper.Point(bone + 1, {});
            if (auto made =
                    helper.Joint({0x614, from, to, actor.angles, 7, nullptr, Width, -1, 0, 0, -1}, chain.address);
                !made.HasValue())
                return made;
        }
    }
    return {};
}

// 0x43ebf0..0x43ec0c: las dos manos sin ítem.
void DropTheHands(ActorState& actor)
{
    actor.hands[0].model = NoModel;
    actor.hands[1].model = NoModel;
}

// 0x43e3f3..0x43e605, el 0x195.
Result DrawTheFifthKalimaHelper(const Helper& helper)
{
    ActorState& actor = helper.Actor();
    if (actor.animation.action == DeadAction)
    {
        DropTheHands(actor);
        return {};
    }
    constexpr std::uint16_t RedMonster = 0x91;
    if (actor.word376 == RedMonster)
    {
        const glm::vec3 red{1.0f, 0.0f, 0.0f};
        const glm::vec3 point = helper.Point(0, {0.0f, 0.0f, 30.0f});
        helper.Sprite(Halo, point, F32(Wave(helper.Clock(), Thousandth, ThreeTenths) + 2.0), red);
        if ((helper.Random().Next() & 1) == 0)
        {
            if (auto made = helper.Particle(0x5f2, point, actor.angles, red, 13, 1.0f, "0x43e4c0"); !made.HasValue())
                return made;
        }
    }
    else
    {
        helper.Sprite(Halo, helper.Point(0, {0.0f, 0.0f, 30.0f}),
                      F32(Wave(helper.Clock(), Thousandth, ThreeTenths) + D(1.2f)), {1.0f, 1.0f, 1.0f});
    }
    const float small = F32(Wave(helper.Clock(), Thousandth, Fifth) + D(ThreeTenths));
    for (const std::size_t bone : {std::size_t{28}, std::size_t{29}})
        helper.Sprite(Star, helper.Point(bone, {5.0f, 0.0f, 0.0f}), small, {1.0f, 1.0f, 1.0f});
    return {};
}

// 0x43e606..0x43e923, el 0x196.
Result DrawTheSixthKalimaHelper(const Helper& helper)
{
    ActorState& actor = helper.Actor();
    if (actor.animation.action == DeadAction)
        return {};
    glm::vec3 turned = actor.angles;
    const glm::vec3 white{1.0f, 1.0f, 1.0f};
    const float small = F32(Wave(helper.Clock(), Thousandth, Fifth) + D(ThreeTenths));
    const glm::vec3 knee = helper.Point(9, {5.0f, 0.0f, 0.0f});
    helper.Sprite(Star, knee, small, white);
    const glm::vec3 other = helper.Point(10, {5.0f, 0.0f, 0.0f});
    helper.Sprite(Star, other, small, white);
    // 0x43e6fd..0x43e7f5: la luz del hueso 60, verde en la acción 4.
    const double lum = Wave(helper.Clock(), 0.002f, ThreeTenths) + D(0.6f);
    const float stored = F32(lum);
    const float tenth = F32(lum * D(Tenth));
    const glm::vec3 light = actor.animation.action == 4 ? glm::vec3{tenth, stored, tenth} : glm::vec3(stored);
    glm::vec3 heart = helper.Point(60, {});
    const float scale = F32(D(stored) * D(Fifth) + D(0.5f));
    helper.Sprite(Flare, heart, scale, light, F32(D(helper.Clock()) * D(Tenth)));
    helper.Sprite(Flare, heart, scale, light, F32(D(helper.Clock()) * D(-0.1f)));
    constexpr float HeartScale = 1.1f; // 0x3f8ccccd
    if (auto made = helper.Particle(Star, heart, actor.angles, light, 0, HeartScale, "0x43e7f0"); !made.HasValue())
        return made;
    helper.Sprite(Star, other, 1.0f, {Tenth, TwoFifths, 1.0f});
    // 0x43e843..0x43e915: los huesos 63..67, con rand() % 50 en las acciones 0 y 1 para el joint 0x610/5 hacia el 60.
    const glm::vec3 blue{Tenth, ThreeTenths, 1.0f};
    const float glow = F32(Wave(helper.Clock(), Thousandth, Fifth) + D(0.5f));
    for (std::size_t bone = 63; bone < 68; ++bone)
    {
        glm::vec3 point = helper.Point(bone, {});
        helper.Sprite(Star, point, glow, blue);
        if (actor.animation.action > 1)
            continue;
        constexpr int Rarity = 50;
        if (helper.Random().Next() % Rarity != 0)
            continue;
        turned.x = RandomTurn(helper.Random());
        turned.z = RandomTurn(helper.Random());
        constexpr float Width = 20.0f;
        if (auto made = helper.Joint({0x610, point, heart, turned, 5, nullptr, Width, -1, 0, 0, -1}, "0x43e904");
            !made.HasValue())
            return made;
    }
    return {};
}

// 0x43e924..0x43ebef, el 0x197.
Result DrawTheSeventhKalimaHelper(const Helper& helper)
{
    ActorState& actor = helper.Actor();
    if (actor.animation.action == DeadAction)
    {
        DropTheHands(actor);
        return {};
    }
    const float lum = F32(Wave(helper.Clock(), 0.003f, Fifth) + D(0.8f));
    const glm::vec3 first = helper.Point(33, {});
    const glm::vec3 second = helper.Point(34, {});
    // +0x4: el subtipo 9 rojo; los demás, azules.
    constexpr std::int32_t RedSubtype = 9;
    const bool red = actor.subtype == RedSubtype;
    const float pale = F32(D(lum) * D(0.9f));
    const glm::vec3 core = red ? glm::vec3{lum, pale, pale} : glm::vec3{0.0f, pale, lum};
    helper.Sprite(Flare, first, Tenth, core);
    helper.Sprite(Flare, second, Tenth, core);
    const float third = F32(D(lum) * D(ThreeTenths));
    const float tenth = F32(D(lum) * D(Tenth));
    const glm::vec3 fire = red ? glm::vec3{lum, third, tenth} : glm::vec3{tenth, third, lum};
    helper.Sprite(Glow, first, 0.7f, fire);
    helper.Sprite(Glow, second, 0.7f, fire);
    const glm::vec3 light = red ? glm::vec3{lum, pale, pale} : glm::vec3{pale, pale, lum};
    // 0x43eacc..0x43ebe1: seis vueltas por los huesos 41 y 51, 24 más abajo en y cada una.
    constexpr int Rounds = 6;
    for (int round = 0; round < Rounds; ++round)
    {
        const glm::vec3 offset{0.0f, F32(-40.0 - static_cast<double>(round) * 24.0), 0.0f};
        const glm::vec3 from = helper.Point(41, offset);
        const glm::vec3 to = helper.Point(51, offset);
        const double share = D(7.2f) / static_cast<double>(round / 2 + 6);
        const float scale = red ? F32(share) : F32(share * 0.5);
        const std::int32_t type = red ? 0x5da : 0x5dc;
        const std::int32_t subtype = red ? 1 : 12;
        if (auto made = helper.Particle(type, from, actor.angles, light, subtype, scale, red ? "0x43eb65" : "0x43ebb7");
            !made.HasValue())
            return made;
        if (auto made = helper.Particle(type, to, actor.angles, light, subtype, scale, "0x43ebd2"); !made.HasValue())
            return made;
    }
    return {};
}

// 0x43c150: el switch de 0x43ec18/0x43ec3c sobre el tipo − 0xc5.
Core::Expected<bool, std::string> DrawTheKalimaHelpers(const Helper& helper)
{
    Result drawn;
    switch (helper.Actor().type)
    {
    case 0xc5:
        drawn = DrawTheGate(helper);
        break;
    case 0x191:
        drawn = DrawTheFirstKalimaHelper(helper);
        break;
    case 0x192:
        drawn = DrawKundun(helper);
        break;
    case 0x193:
        DrawTheThirdKalimaHelper(helper);
        break;
    case 0x194:
        drawn = DrawTheFourthKalimaHelper(helper);
        break;
    case 0x195:
        drawn = DrawTheFifthKalimaHelper(helper);
        break;
    case 0x196:
        drawn = DrawTheSixthKalimaHelper(helper);
        break;
    case 0x197:
        drawn = DrawTheSeventhKalimaHelper(helper);
        break;
    default:
        return false;
    }
    if (!drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    return true;
}

// 0x42dfe0: solo en el mapa 30, el switch de 0x42e340/0x42e354 sobre el tipo − 0x19c.
Core::Expected<bool, std::string> DrawTheCastleHelpers(const Helper& helper)
{
    const KalimaSiegeHelperPass& pass = helper.Pass();
    if (pass.map != CastleMap)
        return false;
    ActorState& actor = helper.Actor();
    const glm::vec3 position = actor.position;
    constexpr std::int32_t Additive = 0x40;
    constexpr std::int32_t Subtractive = 0x80;
    constexpr float WideSide = 9.0f;
    constexpr float NarrowSide = 5.5f;
    const glm::vec3 amber{ThreeTenths, Fifth, 0.0f};
    const glm::vec3 gold{0.8f, 0.6f, ThreeTenths};
    Result drawn;
    switch (actor.type)
    {
    case 0x19c:
        if (pass.siegeActive)
        {
            glm::vec3 light = amber;
            drawn = DrawCastleGlow(pass, Additive, position.x, position.y, WideSide, light);
        }
        else
        {
            drawn = DrawCastleGlow(pass, Subtractive, position.x, position.y, NarrowSide, actor.light);
        }
        break;
    case 0x1a8:
        if (actor.byte93 >= 5)
        {
            actor.blendMeshLight = F32(WaveDouble(helper.Clock(), Thousandth, 0.3) + D(ThreeTenths));
            glm::vec3 light = amber;
            drawn = DrawCastleGlow(pass, Additive, position.x, position.y, WideSide, light);
        }
        else
        {
            const glm::vec3 point{position.x, position.y, F32(D(position.z) + 50.0)};
            helper.Sprite(Star, point, 4.0f, gold);
            helper.Sprite(Glow, point, 2.5f, gold, RandomTurn(helper.Random()));
        }
        break;
    case 0x1a1:
        if (pass.siegeActive)
        {
            actor.byte42c = 1;
            glm::vec3 point = helper.Point(1, {});
            point.z = F32(WavePlus(helper.Clock(), Thousandth, 1.0) * 60.0 + D(point.z));
            helper.Sprite(Star, point, 3.0f, gold);
            helper.Sprite(Glow, point, 1.5f, gold, RandomTurn(helper.Random()));
            helper.Sprite(Star, helper.Point(2, {}), 4.0f, gold);
            drawn = DrawCastleGlow(pass, Subtractive, position.x, position.y, NarrowSide, actor.light);
        }
        else
        {
            actor.byte42c = 0;
            actor.dword58 = -2;
        }
        break;
    case 0x1fd:
        if (pass.siegeActive)
        {
            actor.position.z = actor.floatD8;
            actor.byte42c = 1;
        }
        else
        {
            actor.position.z = F32(D(actor.floatD8) - 100.0);
            actor.byte42c = 0;
        }
        actor.blendMeshLight = F32(Wave(helper.Clock(), Thousandth, ThreeTenths) + 0.5);
        break;
    default:
        return false;
    }
    if (!drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    return true;
}
} // namespace

Core::Expected<void, std::string> DrawCastleGlow(const KalimaSiegeHelperPass& pass, std::int32_t mode, float x, float y,
                                                 float side, glm::vec3& light)
{
    if (pass.heights == nullptr)
        return Core::Unexpected<std::string>("0x42a248: faltan las alturas del terreno para 0x5fb2f0");
    const float lum = F32(Wave(pass.worldClock, 0.0015f, ThreeTenths) + D(0.7f));
    constexpr std::int32_t Subtractive = 0x80;
    if (mode == Subtractive)
    {
        // 0x42a1de: 0x620b10, y la luz en (v, v, v).
        pass.batch.SetState(Blended(pass.batch.State(), Render::ModelBlend::Subtractive));
        light = glm::vec3(lum);
    }
    else
    {
        pass.batch.SetState(Blended(pass.batch.State(), Render::ModelBlend::Additive));
        for (int axis = 0; axis < 3; ++axis)
            light[axis] = F32(D(lum) * D(light[axis]));
    }
    constexpr std::int32_t Slot = Ring;
    constexpr float Lift = 5.0f;
    World::TerrainDecal decal{.slot = Slot,
                              .x = x,
                              .y = y,
                              .width = side,
                              .height = side,
                              .color = light,
                              .angle = F32(D(pass.worldClock) * D(0.01f)),
                              .alpha = 1.0f,
                              .lift = Lift};
    if (auto drawn = World::DrawTerrainDecal(pass.batch, *pass.heights, decal); !drawn.HasValue())
        return drawn;
    if (mode == Subtractive)
    {
        const double rest = 1.0 - D(lum);
        for (int axis = 0; axis < 3; ++axis)
            light[axis] = F32(rest * D(light[axis]));
    }
    decal.color = light;
    decal.angle = F32(D(pass.worldClock) * D(-0.01f));
    return World::DrawTerrainDecal(pass.batch, *pass.heights, decal);
}

Core::Expected<bool, std::string> DrawKalimaSiegeHelpers(const KalimaSiegeHelperPass& pass)
{
    // Los que sacan sprites (0x5491e0): del 0x191 al 0x197 y, en el 30, el 0x1a1 y el 0x1a8.
    const std::uint16_t type = pass.actor.type;
    const bool drawsSprites =
        (type >= 0x191 && type <= 0x197) || (pass.map == CastleMap && (type == 0x1a1 || type == 0x1a8));
    if (drawsSprites && pass.sprites == nullptr)
        return Core::Unexpected(
            std::format("0x510a1e: falta la tabla de sprites del frame (0x5491e0) para el {:#x}", type));
    const Helper helper(pass);
    const auto kalima = DrawTheKalimaHelpers(helper);
    if (!kalima.HasValue() || kalima.Value())
        return kalima;
    return DrawTheCastleHelpers(helper);
}

Core::Expected<void, std::string> DrawTheBoneSparks(const KalimaSiegeHelperPass& pass,
                                                    const Render::Models::Skeleton& skeleton)
{
    if (pass.sprites == nullptr)
        return Core::Unexpected<std::string>("0x5108b6: falta la tabla de sprites del frame (0x5491e0)");
    const Helper helper(pass);
    ActorState& actor = pass.actor;
    // 0x5108ce..0x510904: blanca con +0x1d6 en 0 y verde con otro.
    const bool plain = actor.level == 0;
    const glm::vec3 light = plain ? glm::vec3{1.0f, 1.0f, 1.0f} : glm::vec3{Fifth, 0.7f, Tenth};
    constexpr std::size_t FirstSkipped = 15, LastSkipped = 20, FirstHidden = 27, LastHidden = 32;
    for (std::size_t bone = 0; bone < skeleton.bones.size(); ++bone)
    {
        if (skeleton.bones[bone].dummy || (bone >= FirstSkipped && bone <= LastSkipped) ||
            (bone >= FirstHidden && bone <= LastHidden))
            continue;
        const glm::vec3 point = helper.Point(bone, {});
        if (plain)
            helper.Sprite(Glow, point, 2.5f, light, 0.0f, 1);
        else
            helper.Sprite(Ring, point, 0.8f, light);
        if ((helper.Random().Next() & 3) != 0)
            continue;
        const std::uint8_t action = actor.animation.action;
        if (action < 3 || action > 4)
            continue;
        if (auto made = helper.Particle(Flare, point, actor.angles, light, 0, 1.0f, "0x5109f2"); !made.HasValue())
            return made;
    }
    return {};
}
} // namespace Client::Scenes
