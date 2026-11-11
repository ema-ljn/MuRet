#include "Scenes/GameCharacterEffects.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/PassColors.h"
#include "Render/SpriteVertex.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/FullSetScan.h"
#include "World/ChaosCastle.h"
#include "World/Ftol.h"
#include "World/Sprites.h"
#include "World/TerrainHeight.h"
#include "World/TerrainLighting.h"
#include "World/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
using Result = Core::Expected<void, std::string>;

// Las texturas de los sprites (0x61ccb0): flare01, Spark03, lightning2 y Shiny02.
constexpr std::uint16_t Flare = 0x5ac;
constexpr std::uint16_t ThirdSpark = 0x5c6;
constexpr std::uint16_t Lightning = 0x5d5;
constexpr std::uint16_t Shiny = 0x5fd;

constexpr std::uint32_t HiddenAttachmentFlag = 0x4000;

// 0x50ea38..0x50ea94: la piel especial del mago, en el hueso 19 (0x390 / 0x30), corrida (−4, 11, 0).
constexpr std::uint8_t MageSkinBone = 19;
constexpr glm::vec3 MageSkinOffset{-4.0f, 11.0f, 0.0f};
constexpr float MageSparkScale = std::bit_cast<float>(0x3f19999au);  // 0,6
constexpr float MageShinyClock = std::bit_cast<float>(0x3a83126fu);  // 0x6b89c8, 0,001f
constexpr float MageShinyFactor = std::bit_cast<float>(0x3ecccccdu); // 0x6b87b4, 0,4f

// 0x50f828: las acciones con efectos y la clase 4 (el Dark Lord).
constexpr std::uint8_t LightningAction = 0x94;
constexpr std::uint8_t FirstEffectAction = 0x51;
constexpr std::uint8_t SecondEffectAction = 0x5b;
constexpr std::uint8_t DarkLordClass = 4;
constexpr float Tenth = std::bit_cast<float>(0x3dcccccdu);               // 0x6b87b8, 0,1f
constexpr float FirstLightningScale = std::bit_cast<float>(0x3e99999au); // 0x6b89dc, 0,3f
constexpr float ThirdLightningScale = 2.5f;                              // 0x6b89a8
constexpr glm::vec3 FirstLightningLight{Tenth, Tenth, 1.0f};
constexpr float FirstWindowStart = std::bit_cast<float>(0x3f99999au); // 0x6b8bb0, 1,2f
constexpr float FirstWindowEnd = std::bit_cast<float>(0x3fcccccdu);   // 0x6b8bfc, 1,6f
constexpr float SecondWindowStart = 7.0f;                             // 0x6b8878
constexpr float SecondWindowEnd = 8.0f;                               // 0x6b86c4

// 0x50fba4: las banderas de +0x78 con sprites y joints.
constexpr std::uint32_t FirstGlowFlag = 0x4;
constexpr std::uint32_t SecondGlowFlag = 0x2000000;

// 0x50fd74: +0x358, cada 100 dibujos con la acción por debajo de 0xd.
constexpr std::uint8_t LastCounterAction = 0xc;
constexpr std::int32_t CounterPeriod = 100;

// 0x50fddb: el conjunto completo.
constexpr float HandFlareScale = std::bit_cast<float>(0x3fa66666u); // 1,3
constexpr float FullSetShineFactor = 0.5f;
constexpr std::array<std::int32_t, 3> HandBoneDrops{0, 6, 7};
constexpr std::int16_t FirstGlowingBoots = 0x182f;
constexpr std::int16_t LastGlowingBoots = 0x1833;
constexpr std::int32_t LastQuietLevel = 9;
constexpr int GlowChance = 20;

// 0x50ff5e..0x510002: la luz de cada bota de 0x182f a 0x1833.
constexpr std::array<glm::vec3, 5> BootsLights{{
    {std::bit_cast<float>(0x3f266666u), std::bit_cast<float>(0x3e99999au), std::bit_cast<float>(0x3dcccccdu)},
    {std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3f666666u)},
    {0.0f, std::bit_cast<float>(0x3ea3d70au), std::bit_cast<float>(0x3e75c28fu)},
    {0.5f, std::bit_cast<float>(0x3e75c28fu), std::bit_cast<float>(0x3f4ccccdu)},
    {std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3ecccccdu), 0.0f},
}};

// 0x510a7a: los mapas sin 0x510ef0 (Blood Castle, Chaos Castle y el asedio).
constexpr std::int32_t FirstBloodCastle = 11;
constexpr std::int32_t LastBloodCastle = 17;
constexpr std::int32_t CastleMap = 30;

// 0x5ffdf0: por debajo de 0,01f (0x6b88d4) no posa ni dibuja.
constexpr float DrawnAlpha = std::bit_cast<float>(0x3c23d70au);
// 0x420e11: con +0x78 & 0x4000 el dueño le pasa 10 a 0x4182f0.
constexpr std::uint32_t RiderFlag = 0x4000;
// 0x418951: 0x418920 sólo hace algo en los estados 5 y 6.
constexpr std::uint8_t FirstOrderState = 5;
constexpr std::uint8_t SecondOrderState = 6;

float F32(double value)
{
    return static_cast<float>(value);
}

double D(float value)
{
    return static_cast<double>(value);
}

// El estado de la cola de 0x50ba30 en un dibujo.
struct Tail
{
    ActorState& actor;
    const GameCharacterEffectsPass& pass;
    // [ebp−0x50]: el corrimiento de 0x4ec110, en 0 desde 0x50ea23. Lo escriben la piel del mago y las
    // partículas de las botas; lo leen las manos de las acciones, de las banderas y del conjunto (Q-C350).
    glm::vec3 offset{};

    // 0x4ec110 con el jugador sobre el hueso `bone` de su pose propia (+0x120), en la llamada `call`.
    [[nodiscard]] Core::Expected<glm::vec3, std::string> BodyPoint(std::int32_t bone, std::string_view call) const
    {
        if (bone < 0 || static_cast<std::size_t>(bone) >= pass.body.size())
            return Core::Unexpected(std::format("{}: el hueso {} del cuerpo no existe", call, bone));
        return Render::Models::OriginalTransformPosition(
            Render::Models::OriginalMatrixOf(pass.body[static_cast<std::size_t>(bone)]), offset, actor.scale,
            actor.position);
    }

    // 0x5491e0: el sprite aditivo; el dueño no se guarda.
    [[nodiscard]] Result Sprite(std::string_view call, std::uint16_t texture, const glm::vec3& point, float scale,
                                const glm::vec3& light, float rotation = 0.0f) const
    {
        if (pass.sprites == nullptr)
            return Core::Unexpected(std::format("{}: falta la tabla de sprites del frame", call));
        static_cast<void>(pass.sprites->Create(texture, point, scale, light, rotation, 0));
        return {};
    }
};

// 0x50ea38..0x50eb08: la piel especial del mago, sin rand().
Result MageSkin(Tail& tail)
{
    const ActorState& actor = tail.actor;
    if ((actor.characterClass & 7u) != 0 || (actor.skin & 0xf8u) != 8u || (actor.flags78 & HiddenAttachmentFlag) != 0)
        return {};
    tail.offset = MageSkinOffset;
    const auto point = tail.BodyPoint(MageSkinBone, "0x50eab0");
    if (!point.HasValue())
        return Core::Unexpected(point.Error());
    if (auto spark = tail.Sprite("0x50eacd", ThirdSpark, point.Value(), MageSparkScale, glm::vec3(1.0f));
        !spark.HasValue())
        return spark;
    const std::optional<double> shine =
        Render::Models::Detail::SineProductPC53(D(tail.pass.worldClock) * D(MageShinyClock), MageShinyFactor);
    if (!shine)
        return Core::Unexpected<std::string>("0x50eae7: el reloj del mundo está fuera del dominio de FSIN");
    return tail.Sprite("0x50eb00", Shiny, point.Value(), F32(*shine), glm::vec3(1.0f));
}

// 0x50f8e8..0x50f95c: −ftol y ftol del reloj por 0,1f.
float ClockRotation(float clock, bool negated)
{
    const std::int32_t whole = World::FtolLow(D(clock));
    const std::int32_t turned =
        negated ? static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(whole)) : whole; // NEG
    return F32(static_cast<double>(turned) * D(Tenth));
}

// 0x50f828..0x50fba1: las acciones 0x94, 0x51 y 0x5b. Las escriben los paquetes (CL9): lo que crea
// efectos corta con la condición del ASM.
Result Actions(Tail& tail)
{
    const ActorState& actor = tail.actor;
    const std::uint8_t action = actor.animation.action;
    if (action != LightningAction && action != FirstEffectAction && action != SecondEffectAction)
        return {};
    const float frame = actor.animation.frame;
    if ((actor.characterClass & 7u) != DarkLordClass && action == LightningAction)
    {
        if ((actor.flags78 & HiddenAttachmentFlag) != 0)
            return {};
        const auto point = tail.BodyPoint(actor.hands[0].bone, "0x50f89b");
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        const float size = F32(D(frame) * D(Tenth));
        if (auto first = tail.Sprite("0x50f8e3", Lightning, point.Value(), F32(D(size) * D(FirstLightningScale)),
                                     FirstLightningLight);
            !first.HasValue())
            return first;
        if (auto second = tail.Sprite("0x50f91f", Lightning, point.Value(), size, actor.light,
                                      ClockRotation(tail.pass.worldClock, true));
            !second.HasValue())
            return second;
        return tail.Sprite("0x50f95c", Lightning, point.Value(), F32(D(size) * D(ThirdLightningScale)), actor.light,
                           ClockRotation(tail.pass.worldClock, false));
    }
    // 0x50f96c: con el frame en [1,2; 1,6), el efecto 0x621/2 y el sonido 0x1cc. Por debajo de 2, con la
    // cantidad del grupo (0x78aba48) por encima de 0, 0x59e680 y los joints 0x618: el grupo es de la CL9 y
    // en MuRet vale 0, como al arrancar. En [7, 8), dos 0x117/2 en la mano derecha.
    if (!tail.pass.originalFrame)
        return {};
    if (!(frame < FirstWindowStart) && frame < FirstWindowEnd)
        return Core::Unexpected(
            std::format("0x50f9b3: el efecto 0x621 y el sonido 0x1cc de la acción {:#x} no están portados", action));
    if (!(frame < SecondWindowStart) && frame < SecondWindowEnd)
        return Core::Unexpected(
            std::format("0x50fb64: los efectos 0x117 de la acción {:#x} no están portados", action));
    return {};
}

// 0x50fba4..0x50fd5e: los sprites de las manos con el color al azar y los cinco joints 0x10f/4. +0x78 sólo lo
// escriben los paquetes (CL9): corta con la condición del ASM.
Result Flags(const Tail& tail)
{
    const std::uint32_t flags = tail.actor.flags78;
    const bool glows = (flags & FirstGlowFlag) != 0 || (flags & SecondGlowFlag) != 0;
    if (glows && (flags & HiddenAttachmentFlag) == 0)
        return Core::Unexpected(std::format(
            "0x50fbd5: los sprites y los joints 0x10f de las banderas +0x78 ({:#x}) no están portados", flags));
    return {};
}

// 0x50fd74..0x50fdd5: con +0x358, el contador de +0x35c y, con la acción por debajo de 0xd y 100 o más, el
// efecto 0x5ac/3 en el personaje.
Result Counter(const Tail& tail)
{
    ActorState& actor = tail.actor;
    if (actor.byte358 == 0)
        return {};
    if (actor.animation.action <= LastCounterAction && actor.counter35c >= CounterPeriod)
    {
        if (tail.pass.originalFrame)
            return Core::Unexpected<std::string>(
                "0x50fdc3: el efecto 0x5ac/3 del conjunto completo (+0x358) no está portado");
        actor.counter35c = 0;
    }
    ++actor.counter35c;
    return {};
}

// 0x608e00(botas, alpha, 0,5, color, 0): k = alpha · 0,5 en la pila y cada componente k por la del modelo,
// con un fstp; las que el caso pone en 0, un 0 literal.
glm::vec3 FullSetColor(std::uint16_t boots, float alpha)
{
    const Render::Color shine = Render::Models::ShineColorOf(boots);
    const double k = D(alpha) * D(FullSetShineFactor);
    const auto times = [k](float component) { return component == 0.0f ? 0.0f : F32(k * D(component)); };
    return {times(shine.red), times(shine.green), times(shine.blue)};
}

// La luz propia (+0xf4) cambiada hasta el final del bloque (0x5102ac y 0x5104b0).
class SavedLight
{
public:
    SavedLight(ActorState& actor, const glm::vec3& light) : m_actor(actor), m_saved(actor.light)
    {
        actor.light = light;
    }
    SavedLight(const SavedLight&) = delete;
    SavedLight& operator=(const SavedLight&) = delete;
    ~SavedLight()
    {
        m_actor.light = m_saved;
    }

private:
    ActorState& m_actor;
    glm::vec3 m_saved;
};

// 0x50ff37..0x5102bd: las partículas 0x6f1/3 de las botas de 0x182f a 0x1833, en el hueso 0, con su luz.
Result GlowingBoots(const Tail& tail, std::int16_t boots, std::int32_t level)
{
    ActorState& actor = tail.actor;
    const SavedLight light(actor, BootsLights[static_cast<std::size_t>(boots - FirstGlowingBoots)]);
    Render::Models::CrtRandom& random = tail.pass.random;
    std::string_view call;
    switch (level)
    {
    case 10:
        call = (random.Next() & 3) == 0 ? "0x510158" : "";
        break;
    case 11:
        call = random.Next() % 3 == 0 ? "0x51008d" : "";
        break;
    case 12:
        call = (random.Next() & 1) == 0 ? "0x510158" : "";
        break;
    case 13:
        call = "0x51020c";
        break;
    default:
        break;
    }
    if (call.empty() || !tail.pass.originalFrame)
        return {};
    return Core::Unexpected(std::format("{}: la partícula 0x6f1/3 de las botas {:#x} con +{} no está portada", call,
                                        static_cast<std::uint16_t>(boots), level));
}

// 0x5102c3..0x5104cb: con el nivel por encima de 9 y rand() % 20 en 0, la luz en 1 y por el nivel la
// partícula 0x60f/0 y los joints 0x60f y 0x610 en el personaje.
Result Sparkles(const Tail& tail, std::int32_t level)
{
    if (tail.pass.random.Next() % GlowChance != 0)
        return {};
    ActorState& actor = tail.actor;
    const SavedLight light(actor, glm::vec3(1.0f));
    const std::int32_t steps = tail.pass.logicSteps;
    std::string_view call;
    switch (level)
    {
    case 10:
        call = "0x510339";
        break;
    case 11:
        call = (tail.pass.random.Next() & 7) != 0 ? "0x510339" : "0x51037c";
        break;
    case 12:
        call = steps % 6 == 0 ? "0x5103be" : (steps % 3 == 0 ? "0x510339" : "");
        break;
    case 13:
        call = steps % 6 == 0 ? "0x510435" : (steps % 4 == 0 ? "0x51048b" : "");
        break;
    default:
        break;
    }
    if (call.empty() || !tail.pass.originalFrame)
        return {};
    return Core::Unexpected(std::format(
        "{}: la partícula 0x60f/0 y los joints 0x60f y 0x610 del conjunto con +{} no están portados", call, level));
}

// 0x50fddb..0x5104cb: el conjunto completo de 0x505820.
Result FullSet(const Tail& tail)
{
    const ActorState& actor = tail.actor;
    const std::uint16_t bootsModel = actor.body.back().model;
    const glm::vec3 color = FullSetColor(bootsModel, actor.alpha);
    if ((actor.flags78 & HiddenAttachmentFlag) == 0)
    {
        for (const ActorItemPart& hand : actor.hands)
        {
            for (const std::int32_t drop : HandBoneDrops)
            {
                const auto point = tail.BodyPoint(static_cast<std::int32_t>(hand.bone) - drop, "0x50fe4c");
                if (!point.HasValue())
                    return Core::Unexpected(point.Error());
                if (auto sprite = tail.Sprite("0x50fe68", Flare, point.Value(), HandFlareScale, color);
                    !sprite.HasValue())
                    return sprite;
            }
        }
    }
    const std::int32_t level = tail.pass.scan.globals.dword743a178;
    if (level <= LastQuietLevel)
        return {};
    const auto boots = static_cast<std::int16_t>(bootsModel);
    if (boots >= FirstGlowingBoots && boots <= LastGlowingBoots)
    {
        if (auto glow = GlowingBoots(tail, boots, level); !glow.HasValue())
            return glow;
    }
    return Sparkles(tail, level);
}

// 0x510a7a..0x510ab6 y 0x510ef0: en la escena 5 (el único caller), con +0x88 en 1 y fuera de los mapas 11 a 23 y
// 30, +0x369 en 1 crea el efecto 0x140 si +0x36c está en 0 o pasaron más de 5000 ms. +0x369 sólo lo escriben los
// paquetes (CL9) y +0x36c sólo este efecto: en MuRet, siempre 0 (Q-C352).
Result GuildMark(const Tail& tail)
{
    const ActorState& actor = tail.actor;
    const std::int32_t map = tail.pass.map;
    if (actor.byte88 != 1 || (map >= FirstBloodCastle && map <= LastBloodCastle) || World::IsChaosCastleMap(map) ||
        map == CastleMap)
        return {};
    if (actor.byte369 == 1 && tail.pass.originalFrame)
        return Core::Unexpected<std::string>("0x510f4d: el efecto 0x140 de +0x369 no está portado");
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawGameCharacterEffects(ActorState& actor, const GameCharacterEffectsPass& pass)
{
    Tail tail{.actor = actor, .pass = pass};
    if (auto drawn = MageSkin(tail); !drawn.HasValue())
        return drawn;
    if (auto drawn = Actions(tail); !drawn.HasValue())
        return drawn;
    if (auto drawn = Flags(tail); !drawn.HasValue())
        return drawn;
    // 0x50fd61: Chaos Castle salta al final, sin el contador ni el conjunto.
    if (!World::IsChaosCastleMap(pass.map))
    {
        if (auto counted = Counter(tail); !counted.HasValue())
            return counted;
        if (pass.scan.complete)
        {
            if (auto drawn = FullSet(tail); !drawn.HasValue())
                return drawn;
        }
    }
    return GuildMark(tail);
}

Core::Expected<void, std::string> DrawGameRaven(ActorState& owner, const GameRavenPass& pass)
{
    if (!owner.darkSpirit)
        return {};
    DarkSpirit& spirit = *owner.darkSpirit;
    if (!DarkSpiritSeen(spirit, pass.polygon))
    {
        if (pass.originalFrame)
            MissTheView(spirit);
        return {};
    }
    // 0x5ffdff..0x5ffe3b: en los mapas 18 a 23, con +0x1a1 del héroe, el alpha baja 0,15f y queda guardado.
    if (pass.originalFrame)
        spirit.alpha = World::ChaosCastlePoseAlpha(pass.map, pass.heroByte1a1, spirit.alpha);
    if (spirit.alpha >= DrawnAlpha)
    {
        if (pass.lighting == nullptr)
            return Core::Unexpected<std::string>(
                "0x5ffce0: falta el buffer dinámico de luz del terreno para el cuervo");
        if (!pass.terrain.heights || !pass.terrain.attributes)
            return Core::Unexpected<std::string>("0x600a0b: faltan las alturas del terreno para la sombra del cuervo");
        const Render::Models::Skeleton* skeleton = pass.models.SkeletonOf(DarkSpiritModel);
        const glm::vec3 light = World::LightAt(*pass.lighting, spirit.position.x, spirit.position.y);
        // Los personajes del juego se dibujan como los dejó el paso: el cuervo también (`stepAlpha` en 0).
        DrawnObject drawn = DrawnDarkSpirit(spirit, skeleton, light, 0.0f);
        if (pass.carry != nullptr)
        {
            if (auto posed = PublishFaunaPose(*pass.carry, pass.models, drawn, true); !posed.HasValue())
                return posed;
            drawn.retainedMeshes = pass.carry->meshes;
        }
        if ((owner.flags78 & RiderFlag) != 0)
            return Core::Unexpected<std::string>("0x60033c: con +0x78 & 0x4000 del dueño, 0x420df0 dibuja el cuervo "
                                                 "con 10, que 0x6002e0 lleva por otro camino que MuRet no tiene");
        const float ground =
            F32(World::TerrainHeightAt(*pass.terrain.heights, *pass.terrain.attributes, drawn.transform.position.x,
                                       drawn.transform.position.y, pass.fixedHeight));
        DrawDarkSpirit(pass.batch, pass.models, drawn, ground, pass.frameClock, pass.map);
        if (pass.carry != nullptr)
            pass.carry->models[drawn.model].position = {drawn.transform.position.x, drawn.transform.position.y, ground};
        if (pass.originalFrame)
            spirit.pose = PosedWorldBones(pass.models, drawn);
    }
    // 0x41835a: 0x418920, con las órdenes del A7 (CL9).
    if (spirit.state == FirstOrderState || spirit.state == SecondOrderState)
        return Core::Unexpected(
            std::format("0x418957: el estado {} del cuervo (las órdenes del A7) no está portado", spirit.state));
    return {};
}
} // namespace Client::Scenes
