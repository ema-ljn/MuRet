#include "Scenes/MonsterHelpers.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/ModelBatch.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/WorldRuntime.h"
#include "Render/TextureSlot.h"
#include "Scenes/WorldState.h"
#include "World/OriginalFtol.h"
#include "World/Particles.h"
#include "World/TerrainDecal.h"
#include "World/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
{
namespace
{
using Render::Models::BoneTransform;
using Result = Core::Expected<void, std::string>;
using Handled = Core::Expected<bool, std::string>;

// La luz de los ojos rojos (0x3f666666, 0x3e4ccccd y 0x3dcccccd).
constexpr glm::vec3 RedEyes{0.9f, 0.2f, 0.1f};
// La luz azul de Crywolf (0x3e99999a, 0x3e99999a y 0x3f333333).
constexpr glm::vec3 BlueGlow{0.3f, 0.3f, 0.7f};
constexpr std::uint16_t Flare = 0x5ac;
constexpr std::uint16_t Shine = 0x5fd;
constexpr std::uint16_t Spark = 0x5ad;
constexpr std::uint16_t CrywolfMark = 0x69b;
constexpr std::int32_t CrywolfMap = 0x22;
// Los tipos de 0x53fe70 que World::Particles crea: el rayo 0x5ca/0, el aliento 0x5f2/11 (el polvo de piedra), el
// polvo 0x5f3/0, el fuego 0x5d9/0 y la luz que persigue 0x729/0 y /1.
constexpr std::int32_t Thunder = 0x5ca;
constexpr std::int32_t Breath = 0x5f2;
constexpr std::int32_t StoneDust = 11;
constexpr std::int32_t Dust = 0x5f3;
constexpr std::int32_t Fire = 0x5d9;
constexpr std::int32_t ChaseLight = 0x729;
// Los 0x190 personajes del pool (0x743a16c): el blanco +0x3a4 que se lee como WORD con signo.
constexpr std::int16_t PoolSize = 0x190;

float F32(double value)
{
    return static_cast<float>(value);
}

double D(float value)
{
    return static_cast<double>(value);
}

bool Within(std::uint16_t value, std::uint16_t first, std::uint16_t last)
{
    return value >= first && value <= last;
}

bool Resting(const ActorState& actor)
{
    return actor.animation.action == 0 || actor.animation.action == 1;
}

bool Idling(const ActorState& actor)
{
    return actor.animation.action == 2 || actor.animation.action == 10;
}

// rand() % 2: AND 0x80000001 con su corrección de signo, sobre un rand() que nunca es negativo.
std::int32_t Coin(Render::Models::CrtRandom& random)
{
    return random.Next() & 1;
}

// 0x620a90: la mezcla aditiva con su perfil entero, sin tocar el depth test ni la textura ligada (como las marcas del
// suelo, GroundMarks.cpp).
Render::ModelDrawState Blended(Render::ModelDrawState state, Render::ModelBlend blend)
{
    state.blend = blend;
    state.cullEnabled.reset();
    state.depthWrite.reset();
    state.alphaTest.reset();
    state.texture2d.reset();
    return state;
}

Core::Unexpected<std::string> Cut(std::uint32_t address, std::string_view what, const ActorState& actor)
{
    return Core::Unexpected(std::format("{:#x}: {} (el tipo {:#x} en la acción {:#x}) no está portado", address, what,
                                        actor.type, actor.animation.action));
}

// 0x4010b0(objeto, nombre, punto) y 0x4010d0(objeto, nombre, desplazamiento, punto): el hueso que 0x4016c0 encuentra
// con el objeto y el nombre exacto (la lista de 0x401000), y 0x401550 o 0x4015f0: 0x4ec110 sin trasladar en el buffer
// propio (+0x120), por la escala (+0xc) y más la posición (+0x10), como OriginalTransformPosition. Sin el hueso, el
// original devuelve 0 y deja el punto como estaba en la pila: corta en la llamada `call`.
Core::Expected<glm::vec3, std::string> NamedBonePoint(const WorldState& world, const ActorState& actor,
                                                      std::size_t slot, std::string_view name, const glm::vec3& offset,
                                                      std::uint32_t call)
{
    const auto& bones = world.Runtime().namedBones;
    const auto found = std::ranges::find_if(bones, [slot, name](const NamedBone& bone)
                                            { return bone.actor == slot && bone.name == name; });
    if (found == bones.end())
    {
        return Core::Unexpected(std::format("{:#x}: 0x4016c0 no encuentra el hueso {} del lugar {} (el tipo {:#x}) y "
                                            "el punto queda sin escribir",
                                            call, name, slot, actor.type));
    }
    const BoneTransform matrix =
        found->bone < actor.bodyPose.size() ? actor.bodyPose[found->bone] : BoneTransform(0.0f);
    return Render::Models::OriginalTransformPosition(Render::Models::OriginalMatrixOf(matrix), offset, actor.scale,
                                                     actor.position);
}

Core::Expected<glm::vec3, std::string> PointOf(const MonsterHelperPass& pass, std::string_view name, std::uint32_t call,
                                               const glm::vec3& offset = {})
{
    return NamedBonePoint(pass.world, pass.actor, pass.slot, name, offset, call);
}

// 0x5491e0(textura, punto, escala, luz, objeto, giro, mezcla): el dueño no se guarda en MuRet.
void Sprite(const MonsterHelperPass& pass, std::uint16_t texture, const glm::vec3& point, float scale,
            const glm::vec3& light, float rotation = 0.0f, std::int32_t blend = 0)
{
    static_cast<void>(pass.sprites->Create(texture, point, scale, light, rotation, blend));
}

Result NeedTheSprites(const MonsterHelperPass& pass, std::uint32_t address)
{
    if (pass.sprites != nullptr)
        return {};
    return Core::Unexpected(
        std::format("{:#x}: faltan los sprites del frame (0x5491e0) para el tipo {:#x}", address, pass.actor.type));
}

Core::Unexpected<std::string> NoParticles(const ActorState& actor, std::uint32_t call)
{
    return Core::Unexpected(
        std::format("{:#x}: faltan las partículas del frame (0x53fe70) para el tipo {:#x}", call, actor.type));
}

// 0x53fe70(tipo, posición, ángulos, luz, subtipo, escala, 0) en `call`, con un tipo que World::Particles crea: con la
// tabla llena no crea nada ni saca rand(); con lugar, los rand() de su alta.
Result CreateParticle(World::Particles* particles, const ActorState& actor, std::int32_t type, std::int32_t subtype,
                      const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light, float scale,
                      float worldClock, Render::Models::CrtRandom& random, std::uint32_t call)
{
    if (particles == nullptr)
        return NoParticles(actor, call);
    const auto created = particles->CreateChecked(type, position, angles, light, subtype, scale, worldClock, random);
    if (!created.HasValue())
        return Core::Unexpected(
            std::format("{:#x}: la partícula {:#x}/{} (0x53fe70) del tipo {:#x} falla con el error {}", call, type,
                        subtype, actor.type, static_cast<int>(created.Error())));
    return {};
}

// 0x53fe70 en `call` con un tipo o un subtipo que World::Particles no crea (CL11): con un lugar libre corta antes de
// sus rand(); con la tabla llena el original no crea nada ni saca rand() y sigue. La tabla no se vacía mientras se
// dibuja: las partículas que siguen en el mismo helper tampoco crean nada.
Result UnportedParticle(World::Particles* particles, const ActorState& actor, std::int32_t type, std::int32_t subtype,
                        std::uint32_t call)
{
    if (particles == nullptr)
        return NoParticles(actor, call);
    if (std::ranges::none_of(particles->All(), [](const World::Particle& particle) { return !particle.alive; }))
        return {};
    return Core::Unexpected(std::format("{:#x}: la partícula {:#x}/{} (0x53fe70) del tipo {:#x} en la acción {:#x} no "
                                        "está portada (CL11)",
                                        call, type, subtype, actor.type, actor.animation.action));
}

Result Particle(const MonsterHelperPass& pass, std::int32_t type, std::int32_t subtype, const glm::vec3& position,
                const glm::vec3& angles, const glm::vec3& light, float scale, std::uint32_t call)
{
    return CreateParticle(pass.particles, pass.actor, type, subtype, position, angles, light, scale, pass.worldClock,
                          pass.random, call);
}

Result Unported(const MonsterHelperPass& pass, std::int32_t type, std::int32_t subtype, std::uint32_t call)
{
    return UnportedParticle(pass.particles, pass.actor, type, subtype, call);
}

// FLD T; FMUL tasa; FSIN; FMUL factor (un float de la .data, positivo): el seno retenido por el factor con un solo R53
// (ADR0014). Fuera del perfil (un reloj de más de 2^63 / tasa ms) corta en `call`.
Core::Expected<double, std::string> ClockSine(const MonsterHelperPass& pass, float rate, float factor,
                                              std::uint32_t call)
{
    const auto product = Render::Models::Detail::SineProductPC53(D(pass.worldClock) * D(rate), factor);
    if (!product)
        return Cut(call, "el seno del reloj fuera del perfil PC53", pass.actor);
    return *product;
}

// 0x41ed10(sonido, 0, 0) (CL10): el pedido queda en lastSoundId, solo en un frame del original.
void Sound(const MonsterHelperPass& pass, std::int32_t sound)
{
    if (pass.originalFrame)
        pass.world.Runtime().lastSoundId = sound;
}

// Con +4 en 0: +4 = 1 y `sound`, más rand() % 2 con `pair`.
void SoundOnce(const MonsterHelperPass& pass, std::int32_t sound, bool pair)
{
    if (pass.actor.subtype != 0)
        return;
    pass.actor.subtype = 1;
    Sound(pass, pair ? sound + Coin(pass.random) : sound);
}

struct ActionSound
{
    std::uint8_t action = 0;
    std::int32_t sound = 0;
};

// El patrón de 0x434180 y 0x4271d0: en la 2 y la 10, rand() % 15 en 0 pide `idle` más rand() % 2, sin mirar +4; en
// las acciones de `sounds`, con +4 en 0, +4 = 1 y el suyo.
void PlayTheSounds(const MonsterHelperPass& pass, std::int32_t idle, std::span<const ActionSound> sounds)
{
    const ActorState& actor = pass.actor;
    if (Idling(actor))
    {
        if (pass.random.Next() % 15 == 0)
            Sound(pass, idle + Coin(pass.random));
        return;
    }
    const auto found = std::ranges::find_if(sounds, [&actor](const ActionSound& entry)
                                            { return entry.action == actor.animation.action; });
    if (found != sounds.end())
        SoundOnce(pass, found->sound, false);
}

// La cola común: +4 a 0 en las acciones 0 y 1 (0x442dd2, 0x43458e, 0x428dbd).
void ResetAtRest(ActorState& actor)
{
    if (Resting(actor))
        actor.subtype = 0;
}

// 0x441440..0x44164b: un 0x5ac a 0,5 en cada ojo, el derecho primero; 0x1f5 más rand() % 2 en la 2 y la 10 con
// rand() % 15 en 0, 0x1f7 más rand() % 2 en la 3 y la 4 y 0x1f9 en la 6.
Result DrawThe1a3(MonsterHelperPass& pass)
{
    if (auto ready = NeedTheSprites(pass, 0x4414fb); !ready.HasValue())
        return ready;
    constexpr std::array<std::pair<std::string_view, std::uint32_t>, 2> Eyes{
        {{"Monster81_EyeRight", 0x44149e}, {"Monster81_EyeLeft", 0x441552}}};
    for (const auto& [eye, call] : Eyes)
    {
        const auto point = PointOf(pass, eye, call);
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        Sprite(pass, Flare, point.Value(), 0.5f, RedEyes);
    }
    const std::uint8_t action = pass.actor.animation.action;
    if (Idling(pass.actor) && pass.random.Next() % 15 == 0)
        Sound(pass, 0x1f5 + Coin(pass.random));
    if (action == 3 || action == 4)
        SoundOnce(pass, 0x1f7, true);
    if (action == 6)
        SoundOnce(pass, 0x1f9, false);
    return {};
}

// 0x441669..0x441d91: en la 6, 0x4ee770(modelo, 0, 0x13c, 0, 0, 0) (un rand() por vértice y los efectos 0x518cf0); si
// no, el punto (5, 10, 5) de la mano izquierda y la partícula 0x6ec/3 (CL11). Con la tabla llena siguen, sin crear
// nada, la 0x5f2/21, la 0x6ec/4 de la mano derecha, la 0x5f2/21, la 0x6ec/0 y la 0x5f2/21 del ojo y la 0x5f2/21 de la
// espalda (sus puntos no los lee nadie más); después rand() % 20 en 0 pide 0x205 más rand() % 2 sin mirar +4, y con
// +4 en 0, la 3 con el frame de 8,4 a 10,2 y la 4 de 5 a 5,7 ponen +4 = 1 y cortan en sus efectos.
Result DrawThe1a4(MonsterHelperPass& pass)
{
    ActorState& actor = pass.actor;
    if (actor.animation.action == 6)
        return Cut(0x441696,
                   "0x4ee770 al morir (sus rand() y sus efectos 0x518cf0), y después +0 en 0 y el sonido 0x209", actor);
    if (const auto point = PointOf(pass, "Monster82_LHand", 0x441713, {5.0f, 10.0f, 5.0f}); !point.HasValue())
        return Core::Unexpected(point.Error());
    if (auto made = Unported(pass, 0x6ec, 3, 0x441774); !made.HasValue())
        return made;
    // 0x441a0a..0x441a34.
    if (pass.random.Next() % 20 == 0)
        Sound(pass, 0x205 + Coin(pass.random));
    const std::uint8_t action = actor.animation.action;
    const float frame = actor.animation.frame;
    // 0x441a3c..0x441a8a: 8,4 (0x6b8be4) y 10,2 (0x6b8be0).
    if (action == 3 && frame >= 8.4f && frame <= 10.2f && actor.subtype == 0)
    {
        actor.subtype = 1;
        return Cut(0x441b33, "los efectos 0x518cf0 y el joint 0x530860 de la mano derecha, y el sonido que sigue",
                   actor);
    }
    // 0x441c7d..0x441cd4: 5 (0x6b87f8) y 5,7 (0x6b8bd8).
    if (action == 4 && frame >= 5.0f && frame <= 5.7f && actor.subtype == 0)
    {
        actor.subtype = 1;
        return Cut(0x441d7d, "0x51fe10 desde el punto (80, 40, −25) de la mano izquierda, y el sonido que sigue",
                   actor);
    }
    return {};
}

// 0x441d96..0x4420f2: con +4 en 0, 0x200 más rand() % 2 en la 2 y la 10, 0x202 más rand() % 2 en la 3 y la 4 y 0x1f4
// en la 6. En la 4, tres rand() % 12 − 6 (x, y, z) como desplazamiento del punto de la cola, un 0x5fd a 2,5 aditivo y
// un 0x61e a 0,8 con la luz propia (+0xf4) y dos partículas 0x5ca/0 con (1; 0,3; 0); después doce vueltas de rand() %
// 40 − 20, rand() % 40 − 20 y rand() % 48 − 24 para el punto de la panza, con los mismos dos sprites y una 0x5ca/0.
Result DrawThe1a5(MonsterHelperPass& pass)
{
    ActorState& actor = pass.actor;
    const std::uint8_t action = actor.animation.action;
    if (Idling(actor))
        SoundOnce(pass, 0x200, true);
    if (action == 3 || action == 4)
        SoundOnce(pass, 0x202, true);
    if (action == 6)
        SoundOnce(pass, 0x1f4, false);
    if (action != 4)
        return {};
    if (auto ready = NeedTheSprites(pass, 0x441f4b); !ready.HasValue())
        return ready;
    glm::vec3 offset{};
    for (int axis = 0; axis < 3; ++axis)
        offset[axis] = static_cast<float>(pass.random.Next() % 12 - 6);
    const auto point = PointOf(pass, "Monster83_Tail", 0x441ed5, offset);
    if (!point.HasValue())
        return Core::Unexpected(point.Error());
    Sprite(pass, Shine, point.Value(), 2.5f, actor.light, 0.0f, 1);
    Sprite(pass, 0x61e, point.Value(), 0.8f, actor.light);
    // [ebp−0x30] (0x441f36): 1, 0,3 (0x3e99999a) y 0.
    constexpr glm::vec3 ThunderLight{1.0f, 0.3f, 0.0f};
    for (const std::uint32_t call : {0x441f7fu, 0x441f9cu})
    {
        if (auto made = Particle(pass, Thunder, 0, point.Value(), actor.angles, ThunderLight, 1.0f, call);
            !made.HasValue())
            return made;
    }
    // 0x441fa4..0x4420ec.
    constexpr int Turns = 12;
    for (int turn = 0; turn < Turns; ++turn)
    {
        glm::vec3 belly{};
        belly.x = static_cast<float>(pass.random.Next() % 40 - 20);
        belly.y = static_cast<float>(pass.random.Next() % 40 - 20);
        belly.z = static_cast<float>(pass.random.Next() % 48 - 24);
        const auto glow = PointOf(pass, "Monster83_Belly", 0x442048, belly);
        if (!glow.HasValue())
            return Core::Unexpected(glow.Error());
        Sprite(pass, Shine, glow.Value(), 2.5f, actor.light, 0.0f, 1);
        Sprite(pass, 0x61e, glow.Value(), 0.8f, actor.light);
        if (auto made = Particle(pass, Thunder, 0, glow.Value(), actor.angles, ThunderLight, 1.0f, 0x4420dd);
            !made.HasValue())
            return made;
    }
    return {};
}

// 0x4420f7..0x44281f. timeGetTime() (0x4420f7) sin usar lo que da.
Result DrawThe1a6(MonsterHelperPass& pass)
{
    ActorState& actor = pass.actor;
    const std::uint8_t action = actor.animation.action;
    if (Idling(actor))
        SoundOnce(pass, 0x1fa, true);
    if (action == 6)
        return Cut(0x442165,
                   "0x4ee770 al morir (sus rand() y sus efectos 0x518cf0), y después +0 en 0 y el sonido 0x1ff", actor);
    // [ebp−0x18] (0x442107): la luz de las partículas, (0,4; 1; 0,7).
    constexpr glm::vec3 PoisonLight{0.4f, 1.0f, 0.7f};
    if (action == 3)
    {
        // 0x44217c: con +4 en 0, +4 = 1 y 0x1fe; rand() % 2 en 0 elige con otro rand() % 2 el veneno derecho (0) o el
        // izquierdo y rand() % 32 + 50 por 0,025f (0x6b8bd0) es la escala de su 0x5f2/11; los dos caminos siguen en
        // el efecto 0x12e.
        SoundOnce(pass, 0x1fe, false);
        if (Coin(pass.random) == 0)
        {
            const bool right = Coin(pass.random) == 0;
            const auto point = right ? PointOf(pass, "Monster84_PoisonRight", 0x442240)
                                     : PointOf(pass, "Monster84_PoisonLeft", 0x4422f0);
            if (!point.HasValue())
                return Core::Unexpected(point.Error());
            const float scale = F32(static_cast<double>(pass.random.Next() % 32 + 50) * D(0.025f));
            if (auto made =
                    Particle(pass, Breath, StoneDust, point.Value(), actor.angles, PoisonLight, scale, 0x442370);
                !made.HasValue())
                return made;
        }
        return Cut(0x442397, "el efecto 0x12e (0x518cf0) del ataque", actor);
    }
    // 0x44239f..0x4425b8: en la 4 con el frame de 3,5 a 4,2 (0x6b8bcc), con +4 en 0, +4 = 1 y 0x1fc más rand() % 2;
    // después, en cada mano, el punto con la z del objeto (0x44249d y 0x442579) y rand() % 32 + 50 por 0,05f (0x6b8a48)
    // para su 0x5f2/11. Sigue con los venenos.
    const float frame = actor.animation.frame;
    if (action == 4 && frame >= 3.5f && frame <= 4.2f)
    {
        SoundOnce(pass, 0x1fc, true);
        struct Hand
        {
            std::string_view bone;
            std::uint32_t call;
            std::uint32_t particle;
        };
        constexpr std::array<Hand, 2> Hands{
            {{"Monster84_RightHand", 0x442451, 0x4424d7}, {"Monster84_LeftHand", 0x44252d, 0x4425b3}}};
        for (const Hand& hand : Hands)
        {
            auto point = PointOf(pass, hand.bone, hand.call);
            if (!point.HasValue())
                return Core::Unexpected(point.Error());
            glm::vec3 floor = point.Value();
            floor.z = actor.position.z;
            const float scale = F32(static_cast<double>(pass.random.Next() % 32 + 50) * D(0.05f));
            if (auto made = Particle(pass, Breath, StoneDust, floor, actor.angles, PoisonLight, scale, hand.particle);
                !made.HasValue())
                return made;
        }
    }
    // 0x4425bb..0x442817: tres rand() % 10, cada uno con su veneno 0x5f2/23 (CL11).
    struct Poison
    {
        std::string_view bone;
        std::uint32_t call;
        std::uint32_t particle;
    };
    constexpr std::array<Poison, 3> Poisons{{{"Monster84_PoisonTop", 0x44261e, 0x44267f},
                                             {"Monster84_PoisonRight", 0x4426ea, 0x44274b},
                                             {"Monster84_PoisonLeft", 0x4427b6, 0x442817}}};
    for (const Poison& poison : Poisons)
    {
        if (pass.random.Next() % 10 != 0)
            continue;
        if (const auto point = PointOf(pass, poison.bone, poison.call); !point.HasValue())
            return Core::Unexpected(point.Error());
        if (auto made = Unported(pass, Breath, 0x17, poison.particle); !made.HasValue())
            return made;
    }
    return {};
}

// 0x442824..0x442a5d: un 0x5ac a 0,7 en cada ojo, el izquierdo primero; en la 2 y la 10, rand() % 10 en 0 es la
// partícula 0x5f3/0 en la posición, con los ángulos del objeto y la luz de los ojos, y el sonido 0x1f0 más rand() % 2
// sin mirar +4; con +4 en 0, 0x1f2 más rand() % 2 en la 3 y la 4 y 0x1f4 en la 6.
Result DrawThe1a7(MonsterHelperPass& pass)
{
    if (auto ready = NeedTheSprites(pass, 0x4428e6); !ready.HasValue())
        return ready;
    constexpr std::array<std::pair<std::string_view, std::uint32_t>, 2> Eyes{
        {{"Monster85_LeftEye", 0x442889}, {"Monster85_RightEye", 0x44293c}}};
    for (const auto& [eye, call] : Eyes)
    {
        const auto point = PointOf(pass, eye, call);
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        Sprite(pass, Flare, point.Value(), 0.7f, RedEyes);
    }
    const ActorState& actor = pass.actor;
    const std::uint8_t action = actor.animation.action;
    if (Idling(actor) && pass.random.Next() % 10 == 0)
    {
        if (auto made = Particle(pass, Dust, 0, actor.position, actor.angles, RedEyes, 1.0f, 0x4429d8);
            !made.HasValue())
            return made;
        Sound(pass, 0x1f0 + Coin(pass.random));
    }
    if (action == 3 || action == 4)
        SoundOnce(pass, 0x1f2, true);
    if (action == 6)
        SoundOnce(pass, 0x1f4, false);
    return {};
}

// 0x442a62..0x442dc1: dos 0x5ac a 0,5 en el punto (0, −2, 0) de cada ojo, el izquierdo primero; con +4 en 0, 0x20b
// más rand() % 2 en la 3 y 0x20e en la 6; en la 4, con +4 en 0, +4 = 1 y el efecto 0x13f; después, el punto (0, 0,
// −10) de la mano y la partícula 0x6ec/3 (CL11); con la tabla llena, la 0x5f2/21 de 2 que sigue tampoco crea nada.
Result DrawThe1a9(MonsterHelperPass& pass)
{
    if (auto ready = NeedTheSprites(pass, 0x442b3d); !ready.HasValue())
        return ready;
    ActorState& actor = pass.actor;
    constexpr glm::vec3 EyeOffset{0.0f, -2.0f, 0.0f};
    constexpr std::array<std::pair<std::string_view, std::uint32_t>, 2> Eyes{
        {{"Monster87_LeftEye", 0x442ae0}, {"Monster87_RightEye", 0x442bb1}}};
    for (const auto& [eye, call] : Eyes)
    {
        const auto point = PointOf(pass, eye, call, EyeOffset);
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        Sprite(pass, Flare, point.Value(), 0.5f, RedEyes);
        Sprite(pass, Flare, point.Value(), 0.5f, RedEyes);
    }
    const std::uint8_t action = actor.animation.action;
    if (action == 3)
        SoundOnce(pass, 0x20b, true);
    if (action == 6)
        SoundOnce(pass, 0x20e, false);
    if (action != 4)
        return {};
    if (actor.subtype == 0)
    {
        actor.subtype = 1;
        return Cut(0x442cb9, "el efecto 0x13f (0x518cf0), y después el sonido 0x20d", actor);
    }
    if (const auto point = PointOf(pass, "Monster87_LeftHand", 0x442d4c, {0.0f, 0.0f, -10.0f}); !point.HasValue())
        return Core::Unexpected(point.Error());
    return Unported(pass, 0x6ec, 3, 0x442da1);
}

// 0x5fb2f0 con la luz o el giro que son la pila sin escribir (Q-C1100): MuRet no dibuja ese calco, que no saca rand()
// ni escribe estado; solo liga su textura (0x5fb351, 0x620780), lo único que hace antes de leerlos.
void BindTheMark(Render::ModelBatch& batch)
{
    Render::ModelDrawState state = batch.State();
    state.texture = Render::TextureSlot{CrywolfMark};
    state.rawTextureName.reset();
    batch.SetState(state);
}

// 0x4341d2..0x4343a2, del 0x204 al 0x208. El seno del reloj por 0,002f (0x6b89bc) da b = sin · 0,1f + 0,28f
// ([ebp+0xc]) y g = sin · 0,04f + 0,2f ([ebp−0x20], el giro de la primera marca); después 0x620a90 (aditiva). +0x7c
// tiene a lo sumo uno de los bits (0x4e6380 lo escribe con MOV), pero el código mira cada uno:
// - con 0x40000 (0x43421a..0x43429f), la luz de las marcas es (g; g; 0,05f), el giro de la primera T · 0,01f
//   (0x6b88d4) y el de la segunda T · −0,01f (0x6b8a58, [ebp−0x1c]); la luz propia (+0xf4) pasa a (0,09; 0,09; 0,04)
//   y salen dos partículas 0x729, la /0 y la /1, en la posición, con los ángulos del objeto y esa luz, de escala 1;
// - con 0x100000 o 0x20000, la luz es (b; 0,1; 0,1); con 0x10000 u 0x80000 (después, pisa), (0,15; 0,15; b) y el giro
//   de la primera T · 0,01f.
// Las dos marcas 0x69b son 0x5fb2f0(0x69b, x, y, lado, lado, &luz, giro, 1, 5) con la luz al doble (FADD ST0, ST0, en
// la misma pila): la primera de 2,8 (0x40333333) y la segunda de 3,6 (0x40666666). Sin ningún bit la luz es la pila
// sin escribir, y sin 0x40000 el giro de la segunda también: esa marca no se dibuja (BindTheMark). 0x620980 cierra.
Result DrawTheCrywolfMark(MonsterHelperPass& pass)
{
    ActorState& actor = pass.actor;
    if (pass.batch == nullptr || pass.heights == nullptr)
        return Cut(0x43436c, "las marcas 0x69b del piso sin el lote o las alturas del terreno (0x5fb2f0)", actor);
    constexpr float WaveRate = std::bit_cast<float>(0x3b03126fu);   // 0x6b89bc: 0,002
    constexpr float Tenth = std::bit_cast<float>(0x3dcccccdu);      // 0x6b87b8: 0,1
    constexpr float BrightBase = std::bit_cast<float>(0x3e8f5c29u); // 0x6b8af0: 0,28
    constexpr float SoftSwing = std::bit_cast<float>(0x3d23d70au);  // 0x6b8aec: 0,04
    constexpr float SoftBase = std::bit_cast<float>(0x3e4ccccdu);   // 0x6b8854: 0,2
    constexpr float SpinRate = std::bit_cast<float>(0x3c23d70au);   // 0x6b88d4: 0,01
    constexpr float BackRate = std::bit_cast<float>(0xbc23d70au);   // 0x6b8a58: −0,01
    constexpr float Dim = std::bit_cast<float>(0x3d4ccccdu);        // 0x434260: 0,05
    constexpr float Pale = std::bit_cast<float>(0x3e19999au);       // 0x6b8ae8 y 0x43431b: 0,15
    constexpr float Small = std::bit_cast<float>(0x40333333u);      // 2,8
    constexpr float Large = std::bit_cast<float>(0x40666666u);      // 3,6
    constexpr float Lift = 5.0f;
    const double argument = D(pass.worldClock) * D(WaveRate);
    const auto bright = Render::Models::Detail::SineProductPC53(argument, Tenth);
    const auto soft = Render::Models::Detail::SineProductPC53(argument, SoftSwing);
    if (!bright || !soft)
        return Cut(0x4341de, "el seno del reloj fuera del perfil PC53", actor);
    const float b = F32(*bright + D(BrightBase));
    const float g = F32(*soft + D(SoftBase));
    pass.batch->SetState(Blended(pass.batch->State(), Render::ModelBlend::Additive));
    const std::uint32_t flags = actor.flags7c;
    const auto has = [flags](std::uint32_t bit) { return (flags & bit) == bit; };
    // [ebp−0x18], [ebp−0x14] y [ebp−0x10] (la luz), [ebp−0x20] (el giro de la primera) y [ebp−0x1c] (el de la segunda).
    std::optional<glm::vec3> light;
    float spin = g;
    std::optional<float> backSpin;
    if (has(0x40000))
    {
        light = glm::vec3{g, g, Dim};
        spin = F32(D(pass.worldClock) * D(SpinRate));
        backSpin = F32(D(pass.worldClock) * D(BackRate));
        actor.light = {std::bit_cast<float>(0x3db851ecu), std::bit_cast<float>(0x3db851ecu),
                       std::bit_cast<float>(0x3d23d70au)};
        for (const auto& [subtype, call] : {std::pair{0, 0x434281u}, std::pair{1, 0x43429au}})
        {
            if (auto made = Particle(pass, ChaseLight, subtype, actor.position, actor.angles, actor.light, 1.0f, call);
                !made.HasValue())
                return made;
        }
    }
    // 0x4342a2..0x43432b.
    if (has(0x100000) || has(0x20000))
        light = glm::vec3{b, Tenth, Tenth};
    if (has(0x10000) || has(0x80000))
    {
        light = glm::vec3{Pale, Pale, b};
        spin = F32(D(pass.worldClock) * D(SpinRate));
    }
    World::TerrainDecal mark{.slot = CrywolfMark,
                             .x = actor.position.x,
                             .y = actor.position.y,
                             .width = Small,
                             .height = Small,
                             .color = light ? *light + *light : glm::vec3(0.0f),
                             .angle = spin,
                             .alpha = 1.0f,
                             .lift = Lift};
    // 0x43436c y 0x43439a.
    for (const std::optional<float> turn : {std::optional(spin), backSpin})
    {
        if (light && turn)
        {
            mark.angle = *turn;
            if (auto drawn = World::DrawTerrainDecal(*pass.batch, *pass.heights, mark); !drawn.HasValue())
                return drawn;
        }
        else
            BindTheMark(*pass.batch);
        mark.width = Large;
        mark.height = Large;
    }
    // 0x4343a2: 0x620980.
    pass.batch->SetState(Blended(pass.batch->State(), Render::ModelBlend::Opaque));
    return {};
}

// 0x434699..0x434a70, el 0x1af: a = rand() % 14 · 0,01f, el giro rand() % 360; en zx un 0x699 a 1,5 blanco, tres
// 0x5ac a a · 5 + 3,5 y un 0x69a a a + 0,8 girado, azules; en zx01 un 0x5fd a 0,8 (0,1; 0; 0,6) girado y, con
// rand() % 2 en 0, otro (0,7; 0,7; 1) con 360 menos el giro; y un 0x5ac a 1,5 azul.
Result DrawThe1af(MonsterHelperPass& pass)
{
    if (auto ready = NeedTheSprites(pass, 0x4347ea); !ready.HasValue())
        return ready;
    const float a = F32(static_cast<double>(pass.random.Next() % 14) * D(0.01f));
    const auto spin = static_cast<float>(pass.random.Next() % 360);
    const auto zx = PointOf(pass, "Monster94_zx", 0x434799);
    if (!zx.HasValue())
        return Core::Unexpected(zx.Error());
    Sprite(pass, 0x699, zx.Value(), 1.5f, glm::vec3(1.0f));
    const float size = F32(D(a) * 5.0 + 3.5);
    for (int flare = 0; flare < 3; ++flare)
        Sprite(pass, Flare, zx.Value(), size, BlueGlow);
    Sprite(pass, 0x69a, zx.Value(), F32(D(a) + D(0.8f)), BlueGlow, spin);
    const auto zx01 = PointOf(pass, "Monster94_zx01", 0x434970);
    if (!zx01.HasValue())
        return Core::Unexpected(zx01.Error());
    Sprite(pass, Shine, zx01.Value(), 0.8f, {0.1f, 0.0f, 0.6f}, spin);
    if (Coin(pass.random) == 0)
        Sprite(pass, Shine, zx01.Value(), 0.8f, {0.7f, 0.7f, 1.0f}, F32(360.0 - D(spin)));
    Sprite(pass, Flare, zx01.Value(), 1.5f, BlueGlow);
    return {};
}

// 0x434bf3..0x434d89, el 0x1b1: fuera de la 6, un 0x5ac a 3,5 rojo en la cabeza; en la 2 y la 10, rand() % 10 en 0 es
// la partícula 0x5f3/0 (0x434d84) en la posición, con los ángulos del objeto y la luz de los ojos, de escala 1.
Result DrawThe1b1(MonsterHelperPass& pass)
{
    if (pass.actor.animation.action != 6)
    {
        if (auto ready = NeedTheSprites(pass, 0x434d2b); !ready.HasValue())
            return ready;
        const auto head = PointOf(pass, "Monster95_Head", 0x434cd8);
        if (!head.HasValue())
            return Core::Unexpected(head.Error());
        Sprite(pass, Flare, head.Value(), 3.5f, RedEyes);
    }
    const ActorState& actor = pass.actor;
    if (Idling(actor) && pass.random.Next() % 10 == 0)
        return Particle(pass, Dust, 0, actor.position, actor.angles, RedEyes, 1.0f, 0x434d84);
    return {};
}

// 0x434f8a..0x4353fb, el 0x1b2: el giro rand() % 360 y la luz sin(T · 0,0012f) · 0,8f + 1,3f (0x434faf..0x434fc9, el
// seno retenido de ADR0014); un 0x5ac del tamaño (1, o 0,5 en la 3 y la 4) en el centro, con (0; luz · 0,5; luz · 0,1);
// arriba y abajo, dos 0x5fd de la mitad del tamaño con (0,5; 0,5; 0,5), girados y con 360 menos el giro.
Result DrawThe1b2(MonsterHelperPass& pass)
{
    if (auto ready = NeedTheSprites(pass, 0x435119); !ready.HasValue())
        return ready;
    const ActorState& actor = pass.actor;
    const auto spin = static_cast<float>(pass.random.Next() % 360);
    const auto wave = ClockSine(pass, 0.0012f, 0.8f, 0x434fbb);
    if (!wave.HasValue())
        return Core::Unexpected(wave.Error());
    const float lum = F32(wave.Value() + D(1.3f));
    const std::uint8_t action = actor.animation.action;
    const float size = action == 3 || action == 4 ? 0.5f : 1.0f;
    const auto center = PointOf(pass, "Monster96_Center", 0x43509b);
    if (!center.HasValue())
        return Core::Unexpected(center.Error());
    Sprite(pass, Flare, center.Value(), size, {0.0f, F32(D(lum) * 0.5), F32(D(lum) * D(0.1f))});
    const float half = F32(D(size) * 0.5);
    const float back = F32(360.0 - D(spin));
    constexpr std::array<std::pair<std::string_view, std::uint32_t>, 2> Ends{
        {{"Monster96_Top", 0x4351ff}, {"Monster96_Bottom", 0x435383}}};
    for (const auto& [end, call] : Ends)
    {
        const auto point = PointOf(pass, end, call);
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        Sprite(pass, Shine, point.Value(), half, glm::vec3(0.5f), spin);
        Sprite(pass, Shine, point.Value(), half, glm::vec3(0.5f), back);
    }
    return {};
}

// 0x4343ba..0x435569, por la tabla de bytes 0x43559c sobre la de saltos 0x43556c.
Handled DrawTheCrywolfCase(MonsterHelperPass& pass)
{
    ActorState& actor = pass.actor;
    const std::uint16_t type = actor.type;
    const std::uint8_t action = actor.animation.action;
    if (Within(type, 0x204, 0x208))
    {
        // 0x4343a9: devuelve 0 y la cadena sigue.
        if (auto drawn = DrawTheCrywolfMark(pass); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        return false;
    }
    if (type == 0x1b5)
    {
        // 0x4345a8..0x434600: sin la 2 ni la 10; en la 3 y la 6 sale sin tocar +4, y en las demás lo pone en 0.
        if (action == 3 || action == 4)
            SoundOnce(pass, 0x241, false);
        else if (action == 6)
            SoundOnce(pass, 0x242, false);
        if (action != 3 && action != 6)
            actor.subtype = 0;
        return false;
    }
    if (type == 0x1ab || type == 0x1ae)
    {
        // 0x4343ba..0x434578: los dos con las acciones 8 y 9, y devuelven 0.
        const std::int32_t base = type == 0x1ab ? 0x22e : 0x235;
        const std::array<ActionSound, 5> sounds{
            {{3, base + 2}, {4, base + 3}, {8, base + 5}, {9, base + 6}, {6, base + 4}}};
        PlayTheSounds(pass, base, sounds);
        ResetAtRest(actor);
        return false;
    }
    struct Voice
    {
        std::uint16_t type;
        std::int32_t idle;
    };
    // El sonido de la 2 y la 10; la 3, la 4 y la 6 son los dos, tres y cuatro siguientes.
    constexpr std::array<Voice, 7> Voices{{{0x1ad, 0x229},
                                           {0x1af, 0x23c},
                                           {0x1b0, 0x224},
                                           {0x1b1, 0x210},
                                           {0x1b2, 0x21f},
                                           {0x1b3, 0x21a},
                                           {0x1b4, 0x215}}};
    const auto voice = std::ranges::find_if(Voices, [type](const Voice& entry) { return entry.type == type; });
    if (voice == Voices.end())
        return false; // 0x434595: los demás del 0x1ab al 0x208, sin tocar +4.
    Result drawn{};
    if (type == 0x1b1)
        drawn = DrawThe1b1(pass);
    else if (type == 0x1b2)
        drawn = DrawThe1b2(pass);
    else if (type == 0x1b3 && action == 4 && actor.animation.frame >= 4.0f && actor.animation.frame <= 6.0f)
        drawn = Cut(0x434ecf, "el efecto 0x115 (0x518cf0) del ataque, desde el punto girado de +0xcc", actor);
    if (!drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    const std::array<ActionSound, 3> sounds{{{3, voice->idle + 2}, {4, voice->idle + 3}, {6, voice->idle + 4}}};
    PlayTheSounds(pass, voice->idle, sounds);
    ResetAtRest(actor);
    // 0x1af: sus sprites van después de los sonidos (0x434699).
    if (type == 0x1af)
    {
        if (auto glowing = DrawThe1af(pass); !glowing.HasValue())
            return Core::Unexpected(glowing.Error());
    }
    return true;
}

// El blanco +0x3a4 del personaje como WORD con signo dentro de los 0x190 del pool (0x4279cf..0x4279e3 y
// 0x427fb5..0x427fcd).
bool HasTarget(const ActorState& actor)
{
    const auto target = static_cast<std::int16_t>(actor.word3a4);
    return target >= 0 && target < PoolSize;
}

// 0x42720d..0x427a92, el 0x1b6: r = rand() % 30, v = r · 0,01f + 0,6f; la luz (v + 0,5; v − 0,05f; v + 0,5) para
// cinco 0x5ad (la mano izquierda a 0,8, z02 a 1,1, z03 a 1, z04 a 1,7 y z05 a 0,8) y el punto (0, 0, −65) del pie
// para su partícula 0x5ad/4 (CL11). Con la tabla llena siguen sus sonidos: en la 2 y la 10, rand() % 15 en 0 pide
// 0x261 más rand() % 2; con +4 en 0, +4 = 1 y 0x264 en la 3, 0x9c más rand() % 2 y 0x263 en la 4 y 0x265 en la 6.
// Después, en la 3 y la 4, las partículas 0x5f2/0x1b y 0x5ad del pie (desde el frame 4, 0x6b8a44) y del blanco (desde
// 6, 0x6b87f4), que con la tabla llena no crean nada, y con el blanco +0x3a4 en el pool, en la 4 desde el frame 6, el
// joint 0x614.
Result DrawThe1b6(MonsterHelperPass& pass)
{
    if (auto ready = NeedTheSprites(pass, 0x4272ee); !ready.HasValue())
        return ready;
    ActorState& actor = pass.actor;
    const double v = static_cast<double>(pass.random.Next() % 30) * D(0.01f) + D(0.6f);
    const glm::vec3 light{F32(v + 0.5), F32(v - D(0.05f)), F32(v + 0.5)};
    struct Glow
    {
        std::string_view bone;
        std::uint32_t call;
        float scale;
    };
    constexpr std::array<Glow, 5> Glows{{{"Monster100_L_Hand", 0x427291, 0.8f},
                                         {"Monster100_z02", 0x427344, 1.1f},
                                         {"Monster100_z03", 0x4273f7, 1.0f},
                                         {"Monster100_z04", 0x4274aa, 1.7f},
                                         {"Monster100_z05", 0x42755d, 0.8f}}};
    for (const Glow& glow : Glows)
    {
        const auto point = PointOf(pass, glow.bone, glow.call);
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        Sprite(pass, Spark, point.Value(), glow.scale, light);
    }
    if (const auto foot = PointOf(pass, "Monster100_Footstepst", 0x42763e, {0.0f, 0.0f, -65.0f}); !foot.HasValue())
        return Core::Unexpected(foot.Error());
    if (auto made = Unported(pass, Spark, 4, 0x42769f); !made.HasValue())
        return made;
    // 0x4276a4..0x427764.
    const std::uint8_t action = actor.animation.action;
    if (Idling(actor))
    {
        if (pass.random.Next() % 15 == 0)
            Sound(pass, 0x261 + Coin(pass.random));
    }
    else if (action == 3)
        SoundOnce(pass, 0x264, false);
    else if (action == 4)
    {
        if (actor.subtype == 0)
        {
            actor.subtype = 1;
            Sound(pass, 0x9c + Coin(pass.random));
            Sound(pass, 0x263);
        }
    }
    else if (action == 6)
        SoundOnce(pass, 0x265, false);
    // 0x427767..0x427a8d: 6 (0x6b87f4).
    if (action == 4 && HasTarget(actor) && actor.animation.frame >= 6.0f)
        return Cut(0x427a8d, "el joint 0x614 (0x530860) hacia el blanco +0x3a4", actor);
    return {};
}

// 0x427ab0..0x428036, el 0x1b7: el punto del brazo izquierdo y su partícula 0x6f1/1 (CL11). Con la tabla llena, las
// 0x6f1/1 del brazo derecho y de la cabeza tampoco crean nada; siguen sus sonidos (0x247, y 0x249, 0x24a y 0x24b en la
// 3, la 4 y la 6) y, en la 3, las partículas 0x5f2/0x19 (desde el frame 4) y 0x6eb/3 (de 5 a 5,5), que no crean
// nada, y con el blanco +0x3a4 en el pool, desde el frame 6,9 (0x6b8a3c), el joint 0x618 (0x428036 salta a
// 0x427a8d).
Result DrawThe1b7(MonsterHelperPass& pass)
{
    if (const auto arm = PointOf(pass, "Monster101_L_Arm", 0x427b2a); !arm.HasValue())
        return Core::Unexpected(arm.Error());
    if (auto made = Unported(pass, 0x6f1, 1, 0x427b8b); !made.HasValue())
        return made;
    constexpr std::array<ActionSound, 3> Sounds{{{3, 0x249}, {4, 0x24a}, {6, 0x24b}}};
    PlayTheSounds(pass, 0x247, Sounds);
    const ActorState& actor = pass.actor;
    // 0x427fd3: 6,9 (0x6b8a3c).
    if (actor.animation.action == 3 && HasTarget(actor) && actor.animation.frame >= 6.9f)
        return Cut(0x427a8d, "el joint 0x618 (0x530860) hacia el blanco +0x3a4", actor);
    return {};
}

// 0x42803b..0x428222, el 0x1b8: r = rand() % 10, un 0x5ad a 3 con (0; r · 0,01f + 0,2f; 0) en el punto (60, −30, 0) de
// la cabeza y su partícula 0x5c6/7 (CL11); con la tabla llena, sus sonidos (0x24c, y 0x24e, 0x24f y 0x250 en la 3, la
// 4 y la 6).
Result DrawThe1b8(MonsterHelperPass& pass)
{
    if (auto ready = NeedTheSprites(pass, 0x428146); !ready.HasValue())
        return ready;
    const float green = F32(static_cast<double>(pass.random.Next() % 10) * D(0.01f) + D(0.2f));
    const auto head = PointOf(pass, "Monster102_Head", 0x4280e9, {60.0f, -30.0f, 0.0f});
    if (!head.HasValue())
        return Core::Unexpected(head.Error());
    Sprite(pass, Spark, head.Value(), 3.0f, {0.0f, green, 0.0f});
    if (auto made = Unported(pass, 0x5c6, 7, 0x428164); !made.HasValue())
        return made;
    constexpr std::array<ActionSound, 3> Sounds{{{3, 0x24e}, {4, 0x24f}, {6, 0x250}}};
    PlayTheSounds(pass, 0x24c, Sounds);
    return {};
}

// 0x4282ea..0x4285a9, el 0x1ba: sus sonidos y, en cada cuerno (Monster104_Horn0 a Horn5, el formato de 0x6c72a8),
// r = rand() % 10, un 0x5ad a 0,4 con (0,4; r · 0,01f + 0,8f; 0,5) y la partícula 0x5c6/6 (CL11). Con la tabla
// llena, las seis vueltas y, en la 4, la 0x5f2/0x1a del punto (0, −100, 20) del pie, que tampoco crea nada.
Result DrawThe1ba(MonsterHelperPass& pass)
{
    constexpr std::array<ActionSound, 3> Sounds{{{3, 0x253}, {4, 0x254}, {6, 0x255}}};
    PlayTheSounds(pass, 0x251, Sounds);
    if (auto ready = NeedTheSprites(pass, 0x42847e); !ready.HasValue())
        return ready;
    constexpr int Horns = 6;
    for (int horn = 0; horn < Horns; ++horn)
    {
        const std::string name = std::format("Monster104_Horn{}", horn);
        const float green = F32(static_cast<double>(pass.random.Next() % 10) * D(0.01f) + D(0.8f));
        const auto point = PointOf(pass, name, 0x428421);
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        Sprite(pass, Spark, point.Value(), 0.4f, {0.4f, green, 0.5f});
        if (auto made = Unported(pass, 0x5c6, 6, 0x42849c); !made.HasValue())
            return made;
    }
    return {};
}

// 0x4285b1..0x428daf, el 0x1bb: r = rand() % 8, la luz (r · 0,1f + 0,6f; 0; 0) para cinco 0x5ad (los ojos a 0,3 y
// L_Arm00 a L_Arm02 a 1), sus sonidos y la partícula 0x6ec/6 de la mano izquierda (CL11). Con la tabla llena, la
// 0x6ec/6 de la mano derecha tampoco crea nada, y en la 4 con el frame de 9 (0x6b887c) a 10 (0x6b87a8) sigue el
// efecto 0x5e8.
Result DrawThe1bb(MonsterHelperPass& pass)
{
    if (auto ready = NeedTheSprites(pass, 0x4286b0); !ready.HasValue())
        return ready;
    const float red = F32(static_cast<double>(pass.random.Next() % 8) * D(0.1f) + D(0.6f));
    const glm::vec3 light{red, 0.0f, 0.0f};
    struct Glow
    {
        std::string_view bone;
        std::uint32_t call;
        float scale;
    };
    constexpr std::array<Glow, 5> Glows{{{"Monster105_R_Eye", 0x428653, 0.3f},
                                         {"Monster105_L_Eye", 0x428706, 0.3f},
                                         {"Monster105_L_Arm00", 0x4287b9, 1.0f},
                                         {"Monster105_L_Arm01", 0x42886c, 1.0f},
                                         {"Monster105_L_Arm02", 0x42891f, 1.0f}}};
    for (const Glow& glow : Glows)
    {
        const auto point = PointOf(pass, glow.bone, glow.call);
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        Sprite(pass, Spark, point.Value(), glow.scale, light);
    }
    constexpr std::array<ActionSound, 4> Sounds{{{3, 0x25f}, {4, 0x25e}, {8, 0x25d}, {6, 0x260}}};
    PlayTheSounds(pass, 0x25b, Sounds);
    const ActorState& actor = pass.actor;
    const bool attacking = actor.animation.action == 4;
    if (const auto hand = PointOf(pass, "Monster105_L_Hand", attacking ? 0x428a78 : 0x428bee); !hand.HasValue())
        return Core::Unexpected(hand.Error());
    if (auto made = Unported(pass, 0x6ec, 6, attacking ? 0x428adc : 0x428c52); !made.HasValue())
        return made;
    const float frame = actor.animation.frame;
    if (attacking && frame >= 9.0f && frame <= 10.0f)
        return Cut(0x428da7, "el efecto 0x5e8 (0x518cf0) de la mano izquierda", actor);
    return {};
}

// 0x4eb860 (0x4eb86d..0x4eb94c): la key que deja en +0xa8 del modelo, como PoseKeyOf de MonsterDrawing.
std::optional<std::int16_t> SwingKeyOf(const Render::Models::Skeleton& skeleton,
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

// Lo que comparten las estelas de 0x432bd0: el frame de la primera pose (el del objeto menos la velocidad de la acción
// del modelo, +0x30 + acción · 0x10 + 4) y el paso entre poses (un décimo de esa velocidad), en float.
struct Swing
{
    const Render::Models::Skeleton* skeleton = nullptr;
    float first = 0.0f;
    float step = 0.0f;
};

Core::Expected<Swing, std::string> SwingOf(WorldState& world, const ActorState& actor, std::uint8_t speedAction)
{
    const Render::Models::Skeleton* skeleton = world.FaunaResources().SkeletonOf(actor.type);
    if (skeleton == nullptr || skeleton->actions.size() <= speedAction)
    {
        return Core::Unexpected(std::format("0x432bd0: el modelo {:#x} no tiene la acción {} cuya velocidad lee la "
                                            "estela",
                                            actor.type, speedAction));
    }
    const float speed = skeleton->actions[speedAction].speed;
    return Swing{
        .skeleton = skeleton, .first = F32(D(actor.animation.frame) - D(speed)), .step = F32(D(speed) * D(0.1f))};
}

// 0x4eb860(modelo, 0x68b181c, frame, +0x118, +0x112, ángulos, +0x28, 0, 1): la pose en el buffer global, trasladada al
// mundo, con la key en +0xa8 del modelo. La acción (+0xa0), la escala (+0x68) y la posición (+0x6c) del modelo las
// dejó su último 0x5ffdf0; MuRet usa las del personaje (Q-C1102).
Result PoseTheSwing(WorldState& world, const ActorState& actor, const Swing& swing, float frame,
                    const glm::vec3& angles, std::uint32_t call)
{
    const Render::Models::Skeleton& skeleton = *swing.skeleton;
    const Render::Models::AnimationState animation{.action = actor.animation.action,
                                                   .frame = frame,
                                                   .previousAction = actor.animation.previousAction,
                                                   .previousKey = actor.animation.previousKey};
    const Render::Models::ObjectTransform transform{
        .position = actor.position, .angles = angles, .scale = actor.scale, .lockedRootHeight = 0.0f};
    FaunaDrawCarry& carry = world.PoseCarry();
    if (carry.bones.size() < skeleton.bones.size())
        carry.bones.resize(skeleton.bones.size(), BoneTransform(0.0f));
    const auto bones = std::span(carry.bones).first(skeleton.bones.size());
    if (!Render::Models::PoseSkeleton(skeleton, animation, transform, actor.headTurn.angles, bones))
        return Core::Unexpected(std::format("{:#x}: la pose de la estela del modelo {:#x} falló", call, actor.type));
    FaunaModels& models = world.FaunaResources();
    if (auto published = PublishFaunaPoseRaw(carry, models, actor.type, bones, transform, true, false);
        !published.HasValue())
        return published;
    if (const auto key = SwingKeyOf(skeleton, animation))
        models.RecordPoseKey(actor.type, *key);
    return {};
}

// La primera vuelta de una estela que corta: la pose en `pose` y el corte en `cut`.
Result PoseAndCut(WorldState& world, const ActorState& actor, std::uint8_t speedAction, std::uint32_t pose,
                  std::uint32_t cut, std::string_view what)
{
    const auto swing = SwingOf(world, actor, speedAction);
    if (!swing.HasValue())
        return Core::Unexpected(swing.Error());
    if (auto posed = PoseTheSwing(world, actor, swing.Value(), swing.Value().first, actor.angles, pose);
        !posed.HasValue())
        return posed;
    return Cut(cut, what, actor);
}

constexpr std::string_view Ribbon = "la estela 0x52e470 de los huesos de la pose";

// 0x432c12..0x432db0, el 0x1b0 en la 4: diez poses desde el frame menos la velocidad de la 4, cada una con los ángulos
// del objeto y su z 18 grados menos por vuelta (0x6b886c, que 0x432da1 devuelve al final), el punto (0, 100, −150) del
// hueso 16 (0x4ec110 en 0x68b1b1c, sin trasladar: el buffer ya está en el mundo) para la partícula 0x5d9/0 con el
// frame del objeto en (5, 7), con esos ángulos, luz blanca y escala 1 (0x432d07), el punto (0, −150, 0) girado
// (0x5fc8f0 y 0x5fcbd0), que no lee nadie, y dos rand() (0x432d6f y 0x432d74).
Result SwingThe1b0(WorldState& world, const ActorState& actor, Render::Models::CrtRandom& random,
                   World::Particles* particles, float worldClock)
{
    const auto swing = SwingOf(world, actor, 4);
    if (!swing.HasValue())
        return Core::Unexpected(swing.Error());
    const float frame = actor.animation.frame;
    const bool sparks = frame > 5.0f && frame < 7.0f;
    glm::vec3 angles = actor.angles;
    float pose = swing.Value().first;
    constexpr std::size_t SparkBone = 16;
    constexpr glm::vec3 SparkOffset{0.0f, 100.0f, -150.0f};
    constexpr int Poses = 10;
    for (int turn = 0; turn < Poses; ++turn)
    {
        if (auto posed = PoseTheSwing(world, actor, swing.Value(), pose, angles, 0x432c94); !posed.HasValue())
            return posed;
        if (sparks)
        {
            const auto& bones = world.PoseCarry().bones;
            const BoneTransform matrix = SparkBone < bones.size() ? bones[SparkBone] : BoneTransform(0.0f);
            const glm::vec3 point =
                Render::Models::OriginalVectorTransform(SparkOffset, Render::Models::OriginalMatrixOf(matrix));
            if (auto made = CreateParticle(particles, actor, Fire, 0, point, angles, glm::vec3(1.0f), 1.0f, worldClock,
                                           random, 0x432d07);
                !made.HasValue())
                return made;
        }
        angles.z = F32(D(angles.z) - 18.0);
        static_cast<void>(random.Next());
        static_cast<void>(random.Next());
        pose = F32(D(pose) + D(swing.Value().step));
    }
    return {};
}
} // namespace

Handled DrawTheEffectsOf1a3To1a9(MonsterHelperPass& pass)
{
    ActorState& actor = pass.actor;
    const std::uint16_t type = actor.type;
    // 0x441430: fuera de la tabla sale sin tocar nada; el 0x1a8 va directo a 0x442dd5.
    if (!Within(type, 0x1a3, 0x1a9) || type == 0x1a8)
        return false;
    Result drawn{};
    switch (type)
    {
    case 0x1a3:
        drawn = DrawThe1a3(pass);
        break;
    case 0x1a4:
        drawn = DrawThe1a4(pass);
        break;
    case 0x1a5:
        drawn = DrawThe1a5(pass);
        break;
    case 0x1a6:
        drawn = DrawThe1a6(pass);
        break;
    case 0x1a7:
        drawn = DrawThe1a7(pass);
        break;
    default:
        drawn = DrawThe1a9(pass);
        break;
    }
    if (!drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    ResetAtRest(actor);
    return false;
}

Handled DrawTheCrywolfEffectsOf1abTo208(MonsterHelperPass& pass)
{
    // 0x4303c0: el mapa 34 (Crywolf).
    if (pass.map != CrywolfMap)
        return false;
    return DrawTheCrywolfCase(pass);
}

Handled DrawTheEffectsOf1b6To1bb(MonsterHelperPass& pass)
{
    ActorState& actor = pass.actor;
    const std::uint16_t type = actor.type;
    // 0x427200: fuera de la tabla, 0x428dc0 sin tocar +4.
    if (!Within(type, 0x1b6, 0x1bb))
        return false;
    Result drawn{};
    switch (type)
    {
    case 0x1b6:
        drawn = DrawThe1b6(pass);
        break;
    case 0x1b7:
        drawn = DrawThe1b7(pass);
        break;
    case 0x1b8:
        drawn = DrawThe1b8(pass);
        break;
    case 0x1b9:
    {
        // 0x42822a..0x4282e5: solo sus sonidos.
        constexpr std::array<ActionSound, 3> Sounds{{{3, 0x258}, {4, 0x259}, {6, 0x25a}}};
        PlayTheSounds(pass, 0x256, Sounds);
        break;
    }
    case 0x1ba:
        drawn = DrawThe1ba(pass);
        break;
    default:
        drawn = DrawThe1bb(pass);
        break;
    }
    if (!drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    ResetAtRest(actor);
    return false;
}

Core::Expected<void, std::string> DrawTheMonsterHelpers(MonsterHelperPass& pass)
{
    const auto first = DrawTheEffectsOf1a3To1a9(pass);
    if (!first.HasValue())
        return Core::Unexpected(first.Error());
    if (first.Value())
        return {};
    // 0x510a54: 0x42fa30 devuelve 0 sin hacer nada.
    const auto crywolf = DrawTheCrywolfEffectsOf1abTo208(pass);
    if (!crywolf.HasValue())
        return Core::Unexpected(crywolf.Error());
    if (crywolf.Value())
        return {};
    const auto last = DrawTheEffectsOf1b6To1bb(pass);
    if (!last.HasValue())
        return Core::Unexpected(last.Error());
    return {};
}

Core::Expected<void, std::string> StepTheSwingsOf1abTo1b4(ActorState& actor, std::size_t slot, WorldState& world,
                                                          Render::Models::CrtRandom& random,
                                                          World::Particles* particles, float worldClock)
{
    // 0x432bf1: antes de mirar el tipo.
    static_cast<void>(random.Next());
    const std::uint8_t action = actor.animation.action;
    const float frame = actor.animation.frame;
    switch (actor.type)
    {
    case 0x1ab:
    {
        // 0x432f19: en la 3, la primera pose, con el frame en (3,5; 4,5) la partícula 0x640 (CL11; con la tabla llena
        // no crea nada) y la estela.
        if (action != 3)
            return {};
        const auto swing = SwingOf(world, actor, 3);
        if (!swing.HasValue())
            return Core::Unexpected(swing.Error());
        if (auto posed = PoseTheSwing(world, actor, swing.Value(), swing.Value().first, actor.angles, 0x432f9d);
            !posed.HasValue())
            return posed;
        if (frame > 3.5f && frame < 4.5f)
        {
            if (auto made = UnportedParticle(particles, actor, 0x640, 0, 0x433056); !made.HasValue())
                return made;
        }
        return Cut(0x433086, Ribbon, actor);
    }
    case 0x1ae:
        // 0x4330a9: en la 8, el punto de la mano izquierda y su partícula 0x5f2/0x1b (CL11); con la tabla llena, las
        // dos 0x5ad/2 que siguen tampoco crean nada. En la 4 con el frame en (4,5; 5), el efecto 0x145 antes de posar;
        // en la 3 y la 4, la estela.
        if (action == 8)
        {
            if (const auto hand = NamedBonePoint(world, actor, slot, "Left_Hand", {}, 0x4331de); !hand.HasValue())
                return Core::Unexpected(hand.Error());
            return UnportedParticle(particles, actor, Breath, 0x1b, 0x433248);
        }
        if (action != 3 && action != 4)
            return {};
        if (action == 4 && frame > 4.5f && frame < 5.0f)
            return Cut(0x433339, "el efecto 0x145 (0x518cf0) del ataque", actor);
        return PoseAndCut(world, actor, action, 0x433382, 0x433410, Ribbon);
    case 0x1af:
        // 0x433451: en la 3 y la 4, con la velocidad de la suya.
        if (action != 3 && action != 4)
            return {};
        return PoseAndCut(world, actor, action, 0x4334e0, 0x433557, Ribbon);
    case 0x1b0:
        if (action != 4)
            return {};
        return SwingThe1b0(world, actor, random, particles, worldClock);
    case 0x1b1:
        if (action != 4)
            return {};
        return PoseAndCut(world, actor, 4, 0x433761, 0x4337cf, Ribbon);
    case 0x1b3:
        if (action != 3)
            return {};
        return PoseAndCut(world, actor, 3, 0x4338e5, 0x43397d, Ribbon);
    case 0x1b4:
        if (action != 3)
            return {};
        return PoseAndCut(world, actor, 3, 0x432e35, 0x432ea3, Ribbon);
    default:
        // 0x432c05: el 0x1ac, el 0x1ad, el 0x1b2 y los de fuera de la tabla, nada más.
        return {};
    }
}
} // namespace Client::Scenes
