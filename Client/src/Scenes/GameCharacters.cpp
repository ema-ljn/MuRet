#include "Scenes/GameCharacters.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/ActorAppearance.h"
#include "Scenes/CharacterCloth.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FullSetScan.h"
#include "Scenes/GameHanging.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/Sprites.h"
#include "World/TerrainLighting.h"
#include "World/TerrainWalls.h"
#include "World/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Client::Scenes
{
namespace
{
// 0x510e57..0x510e6d: 0x447a0000 en +0x13c y 0x3f800000 en los otros tres.
constexpr float UndrawnHeroMark = 1000.0f;
constexpr std::size_t DrawnActorCount = ActorPoolSize - 1;
constexpr std::uint32_t HiddenCharacterFlag = 0x10000000;
constexpr std::uint16_t NoMonster = 0xffff;
constexpr std::uint8_t RedPk = 6;
constexpr std::int32_t FirstEventMap = 18;
constexpr std::int32_t LastEventMap = 23;
constexpr float ShadowAlphaThreshold = 0.5f;

bool SuppressTheHero(ActorState& actor, bool hero)
{
    if (!hero || (actor.controlCode & UndrawnHeroControlBit) == 0)
    {
        return false;
    }
    actor.field13c = UndrawnHeroMark;
    actor.field148 = 1.0f;
    actor.field158 = 1.0f;
    actor.field168 = 1.0f;
    return true;
}

// 0x50cf86..0x50d01d: los stores float entre cada bit importan cuando se combinan.
glm::vec3 HeroColor(std::uint8_t bits)
{
    glm::vec3 color = (bits & 1u) != 0 ? glm::vec3{0.9f, 0.5f, 0.5f} : glm::vec3{1.0f};
    if ((bits & 2u) != 0)
        color *= glm::vec3{0.5f, 0.9f, 0.5f};
    if ((bits & 4u) != 0)
        color *= glm::vec3{0.9f, 0.5f, 0.5f};
    if ((bits & 8u) != 0)
        color *= glm::vec3{0.9f, 0.5f, 0.5f};
    return color;
}

glm::vec3 HeroBodyColor(WorldState& world, Render::Models::CrtRandom& random, bool originalFrame)
{
    if (!originalFrame)
        return HeroColor(world.OwnCharacter().speedAdjustBits);
    // 0x50ccb5..0x50d219: un acceso, aun con todos los bits apagados.
    const Render::Models::LogicalAccess access(world.Runtime().ownRecordDepth, random,
                                               Render::Models::OwnRecordCloseRandoms);
    return HeroColor(world.OwnCharacter().speedAdjustBits);
}

FullSetScan ScanTheActor(WorldState& world, const ActorState& actor, Render::Models::CrtRandom& random,
                         bool originalFrame)
{
    if (originalFrame)
    {
        FullSetScan scan =
            ScanFullSet(ScannedCharacterOf(actor), world.OwnCharacter(), world.Runtime().ownRecordDepth, random);
        world.Runtime().fullSetScan = scan.globals;
        return scan;
    }
    // Un dibujo entre frames no vuelve a consumir ni a publicar el scan del original.
    std::uint8_t depth = 1;
    Render::Models::CrtRandom copy = random;
    return ScanFullSet(ScannedCharacterOf(actor), world.OwnCharacter(), depth, copy);
}

bool CastsShadow(const ActorState& actor, std::int32_t map)
{
    constexpr std::int32_t NoShadowMap = 10;
    constexpr std::int32_t FirstKalimaMap = 24;
    constexpr std::int32_t LastKalimaMap = 29;
    constexpr std::int32_t SeventhKalimaMap = 36;
    return actor.alpha >= ShadowAlphaThreshold && map != NoShadowMap &&
           !(map >= FirstKalimaMap && map <= LastKalimaMap) && map != SeventhKalimaMap;
}

DrawnCharacter CharacterToDraw(ActorState& actor, std::size_t slot, const GameCharacterPass& pass, std::int32_t map)
{
    DrawnCharacter character;
    character.appearance = AppearanceOfActor(actor);
    character.body = actor.animation;
    character.object = {.position = actor.position, .angles = actor.angles, .scale = actor.scale};
    character.headAngles = actor.headTurn.angles;
    character.light = actor.drawnLight;
    character.alpha = actor.alpha;
    character.castsShadow = CastsShadow(actor, map);
    character.safeZone = actor.safeZone != 0;
    character.objectFlags = actor.flags78;
    character.outlined =
        static_cast<std::int32_t>(slot) == pass.hovered || static_cast<std::int32_t>(slot) == pass.targeted;
    character.map = static_cast<std::uint8_t>(map);
    character.cloth = slot < pass.cloths.size() ? &pass.cloths[slot] : nullptr;
    character.clothWorld = pass.clothWorld;
    character.sprites = pass.sprites;
    character.originalRandom = pass.clothWorld != nullptr ? &pass.clothWorld->random : nullptr;
    character.pace = pass.pace;
    character.poseBones = pass.poseBones;
    return character;
}

Core::Expected<void, std::string> CheckTheEarlyBranches(const ActorState& actor)
{
    if (actor.word376 != NoMonster)
    {
        constexpr std::uint16_t FirstMonsterCase = 0x59;
        constexpr std::uint16_t LastMonsterCase = 0x8f;
        const bool table = actor.word376 >= FirstMonsterCase && actor.word376 <= LastMonsterCase;
        return Core::Unexpected(std::format("{}: los casos por monstruo de 0x50ba30 no están portados en el juego",
                                            table ? "0x50bad8" : "0x50ba9e"));
    }
    if (actor.type != Data::Models::PlayerSkeletonModel)
        return Core::Unexpected<std::string>(
            "0x50bbe4: el dibujo de personajes que no son un jugador 0x20a no está portado");
    return {};
}

Core::Expected<void, std::string> CheckTheBody(const ActorState& actor, std::int32_t map)
{
    if (actor.subtype >= 209 && actor.subtype <= 211)
        return Core::Unexpected<std::string>(
            "0x50d2dc: las partes por subtipo 209 a 211 de 0x50ba30 no están portadas");
    if (actor.byte3e3 != 0 || (map >= FirstEventMap && map <= LastEventMap))
        return Core::Unexpected<std::string>("0x50d363: la ropa especial y la de los mapas 18 a 23 no están portadas");
    return {};
}

Core::Expected<glm::vec3, std::string> LightTheBody(WorldState& world, std::size_t slot, const GameCharacterPass& pass,
                                                    bool originalFrame)
{
    if (pass.dynamicLighting == nullptr)
        return Core::Unexpected<std::string>("0x50cc55: falta el buffer dinámico de luz del terreno para 0x50ba30");
    ActorState& actor = world.Actors()[slot];
    const glm::vec3 terrainLight = World::LightAt(*pass.dynamicLighting, actor.position.x, actor.position.y);
    actor.drawnLight = terrainLight + actor.light;
    if (slot == world.Runtime().hero)
        actor.drawnLight = HeroBodyColor(world, pass.clothWorld->random, originalFrame);
    return terrainLight;
}

Core::Expected<void, std::string> DrawTheBody(Render::ModelBatch& batch, const IModelLibrary& models,
                                              const ActorState& actor, const DrawnCharacter& character,
                                              Core::Duration frameClock)
{
    if (auto supported = CheckTheBody(actor, character.map); !supported.HasValue())
        return supported;
    DrawCharacter(batch, models, character, frameClock, CharacterDrawPass::Parts);
    if (WearsACloth(actor.characterClass, character.map) && character.cloth == nullptr)
        return Core::Unexpected<std::string>("0x50d58b: falta la tela persistente del lugar del personaje");
    DrawCharacter(batch, models, character, frameClock, CharacterDrawPass::Cloth);
    return {};
}

Core::Expected<void, std::string> CheckTheEffects(const ActorState& actor, const FullSetScan& scan)
{
    if ((actor.characterClass & 7u) == 0 && (actor.skin & 0xf8u) == 8u)
        return Core::Unexpected<std::string>("0x50ea55: los sprites de la piel especial del mago no están portados");
    if (actor.animation.action == 0x94 || actor.animation.action == 0x51 || actor.animation.action == 0x5b)
        return Core::Unexpected<std::string>("0x50f83e: los efectos de la acción del personaje no están portados");
    if (actor.flags78 != 0)
        return Core::Unexpected<std::string>(
            "0x50fba4: los efectos de las banderas +0x78 del personaje no están portados");
    if (actor.byte358 != 0)
        return Core::Unexpected<std::string>(
            "0x50fd74: el contador y el efecto de +0x358 del personaje no están portados");
    if (scan.complete)
        return Core::Unexpected<std::string>(
            "0x50fddb: los efectos del conjunto completo de 0x505820 no están portados");
    if (actor.darkSpirit.has_value())
        return Core::Unexpected<std::string>("0x420df0: el cuervo del personaje del juego no está portado");
    return {};
}

// 0x50db57..0x50e9fa sobre el cuerpo ya posado. Entre frames del original, sobre una copia del personaje
// y del rand(): lo que cuelga queda como lo dejó el último frame.
Core::Expected<void, std::string> HangTheItems(Render::ModelBatch& batch, const IModelLibrary& models,
                                               ActorState& actor, const DrawnCharacter& character,
                                               const GameCharacterPass& pass, std::int32_t map, bool originalFrame)
{
    const std::vector<Render::Models::BoneTransform> body = PoseCharacterBody(models, character);
    std::optional<ActorState> projectedActor;
    std::optional<Render::Models::CrtRandom> projectedRandom;
    ActorState& hanging = originalFrame ? actor : projectedActor.emplace(actor);
    Render::Models::CrtRandom& random =
        originalFrame ? pass.clothWorld->random : projectedRandom.emplace(pass.clothWorld->random);
    const GameHangingPass hangingPass{
        .batch = batch,
        .models = models,
        .body = body,
        .random = random,
        .carry = pass.poseCarry,
        .sprites = pass.sprites,
        .itemContext = pass.itemContext,
        .frameClock = pass.frameClock,
        .worldClock = static_cast<float>(std::chrono::duration<double, std::milli>(pass.frameClock).count()),
        .map = map,
        .logicSteps = pass.clothWorld->logicSteps,
        .originalFrame = originalFrame};
    return DrawGameHanging(hanging, hangingPass);
}

Core::Expected<void, std::string> DrawOne(Render::ModelBatch& batch, const IModelLibrary& models, WorldState& world,
                                          std::size_t slot, const GameCharacterPass& pass, bool originalFrame)
{
    ActorState& actor = world.Actors()[slot];
    if ((actor.flags7c & HiddenCharacterFlag) != 0)
        return {};
    const Render::Models::Skeleton* skeleton = models.SkeletonOf(actor.type);
    if (skeleton == nullptr || skeleton->actions.empty())
        return {};
    if (auto supported = CheckTheEarlyBranches(actor); !supported.HasValue())
        return supported;
    const std::int32_t map = world.Runtime().map;
    DrawnCharacter character = CharacterToDraw(actor, slot, pass, map);
    character.advanceCloth = originalFrame;
    if (originalFrame || pass.publishProjectedPose)
        character.onPose = pass.onPose;
    DrawCharacter(batch, models, character, pass.frameClock, CharacterDrawPass::Shadow);
    if (pass.clothWorld == nullptr)
        return Core::Unexpected<std::string>("0x50c91e: falta el flujo global de rand del scan de los personajes");
    const FullSetScan scan = ScanTheActor(world, actor, pass.clothWorld->random, originalFrame);
    const auto terrainLight = LightTheBody(world, slot, pass, originalFrame);
    if (!terrainLight.HasValue())
        return Core::Unexpected(terrainLight.Error());
    character.light = actor.drawnLight;
    if (auto supported = DrawTheBody(batch, models, actor, character, pass.frameClock); !supported.HasValue())
        return supported;
    // 0x50db94: la luz de lo que cuelga (+0x3b4), la del terreno o el rojo de los PK.
    actor.drawnLight = actor.pk >= RedPk ? glm::vec3{1.0f, 0.1f, 0.1f} : terrainLight.Value() + actor.light;
    if (auto hung = HangTheItems(batch, models, actor, character, pass, map, originalFrame); !hung.HasValue())
        return hung;
    return CheckTheEffects(actor, scan);
}

Core::Expected<void, std::string> DrawThePool(Render::ModelBatch& batch, const IModelLibrary& models, WorldState& world,
                                              const GameCharacterPass& pass, bool originalFrame)
{
    const std::size_t hero = world.Runtime().hero;
    for (std::size_t slot = 0; slot < DrawnActorCount; ++slot)
    {
        ActorState& actor = world.Actors()[slot];
        if (SuppressTheHero(actor, slot == hero) || !actor.alive || !actor.seen)
            continue;
        if (auto drawn = DrawOne(batch, models, world, slot, pass, originalFrame); !drawn.HasValue())
            return Core::Unexpected(std::format("{}, en el personaje {}", drawn.Error(), slot));
        if (world.Runtime().map == 30 && actor.type == Data::Models::PlayerSkeletonModel)
            return Core::Unexpected<std::string>("0x510eb3: el dibujo del asedio 0x42afe0 no está portado");
    }
    if (!world.Map().terrain.attributes)
        return Core::Unexpected<std::string>("0x42da60: falta el TerrainWall que abre las puertas");
    World::OpenTheGates(world.Map().terrain.attributes->cells);
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawGameCharacters(Render::ModelBatch& batch, const IModelLibrary& models,
                                                     WorldState& world, const GameCharacterPass& pass)
{
    if (pass.originalFrames < 0 || pass.originalFrames > Render::Models::MaxOriginalFramesPerDraw)
        return Core::Unexpected<std::string>(
            "0x510d00: cantidad de frames del original fuera del presupuesto del dibujo");
    const int frames = std::max(pass.originalFrames, 1);
    std::optional<World::FrameSprites> discardedSprites;
    if (frames > 1 && pass.sprites != nullptr)
        discardedSprites.emplace();
    for (int frame = 0; frame < frames; ++frame)
    {
        const bool lastFrame = frame + 1 == frames;
        Render::ModelBatch discarded;
        if (!lastFrame)
            discarded = batch;
        Render::ModelBatch& destination = lastFrame ? batch : discarded;
        GameCharacterPass current = pass;
        if (!lastFrame && discardedSprites.has_value())
        {
            // Cada frame parte del prefijo del mundo. Solo un frame completado puede descartar
            // su salida transitoria: el corte conserva sombra/partes/tela anteriores a la rama.
            *discardedSprites = *pass.sprites;
            current.sprites = &*discardedSprites;
        }
        if (pass.originalFrames > 0)
            current.pace = Render::Models::OriginalFramePace::OncePerDraw;
        if (auto drawn = DrawThePool(destination, models, world, current, pass.originalFrames > 0); !drawn.HasValue())
        {
            if (!lastFrame)
            {
                batch = std::move(discarded);
                if (discardedSprites.has_value())
                    *pass.sprites = std::move(*discardedSprites);
            }
            return drawn;
        }
    }
    return {};
}

Core::Expected<void, std::string> DrawGameCharacters(std::span<ActorState> actors, std::size_t hero,
                                                     Data::Terrain::TerrainLayer<std::uint16_t>& walls)
{
    for (std::size_t slot = 0; slot < std::min(actors.size(), DrawnActorCount); ++slot)
    {
        ActorState& actor = actors[slot];
        // 0x510e40: el héroe con +0x1d8 & 4 en la escena 5.
        if (SuppressTheHero(actor, slot == hero))
        {
            continue;
        }
        // 0x510e75 y 0x510e7a.
        if (!actor.alive || !actor.seen)
        {
            continue;
        }
        return Core::Unexpected(std::format("0x510ea1: el personaje {} está vivo y visto, y 0x50ba30 necesita los "
                                            "recursos del dibujo del juego (modelos, luz dinámica y rand global)",
                                            slot));
    }
    World::OpenTheGates(walls);
    return {};
}
} // namespace Client::Scenes
