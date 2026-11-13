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
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/ActorAppearance.h"
#include "Scenes/CharacterCloth.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/FullSetScan.h"
#include "Scenes/GameCharacterEffects.h"
#include "Scenes/GameHanging.h"
#include "Scenes/MonsterDrawing.h"
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
#include <ratio>
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
constexpr std::int32_t FirstEventMap = 18;
constexpr std::int32_t LastEventMap = 23;
constexpr float ShadowAlphaThreshold = 0.5f;
// +0x88 de un jugador (0x511290 y el F3 03) y las banderas de 0x609640 con que va su Angel en Chaos Castle.
constexpr std::uint8_t PlayerKind = 1;
constexpr std::uint32_t ChaosCastleBodyFlags = 2;
constexpr std::uint32_t ChaosCastleHeroFlags = 6;

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
    character.kind = actor.byte88;
    character.map = static_cast<std::uint8_t>(map);
    character.cloth = slot < pass.cloths.size() ? &pass.cloths[slot] : nullptr;
    character.clothWorld = pass.clothWorld;
    character.sprites = pass.sprites;
    character.originalRandom = pass.clothWorld != nullptr ? &pass.clothWorld->random : nullptr;
    character.pace = pass.pace;
    // 0x511290 le da al personaje su buffer de huesos (+0x11c en 1 en 0x511bf7, +0x120): 0x5ffdf0 posa ahí el
    // cuerpo, que arranca de lo que dejó el dibujo anterior (actor.bodyPose, Q-C318), y no del global.
    if (pass.poseBones)
        character.poseBones = [&actor, global = pass.poseBones](std::uint16_t model)
        {
            return model == Data::Models::PlayerSkeletonModel
                       ? std::span<const Render::Models::BoneTransform>(actor.bodyPose)
                       : global(model);
        };
    return character;
}

// 0x50ba9e..0x50bae0: un 0x20a con un +0x376 de la tabla de 0x510adc va con la tela sobre la malla (0x50bae7); con
// otro, por 0x6001b0 como un jugador (0x50bbe4), con sus casos por +0x376 más adelante.
Core::Expected<void, std::string> CheckTheEarlyBranches(const ActorState& actor)
{
    if (WearsClothOnTheMesh(actor.word376))
        return Core::Unexpected(
            std::format("0x50bae7: la tela sobre la malla del +0x376 {:#x} (0x6a0210, 0x455190 y 0x4fb1a0) no está "
                        "portada",
                        actor.word376));
    return {};
}

Core::Expected<void, std::string> CheckTheBody(const ActorState& actor)
{
    if (actor.subtype >= 209 && actor.subtype <= 211)
        return Core::Unexpected<std::string>(
            "0x50d2dc: las partes por subtipo 209 a 211 de 0x50ba30 no están portadas");
    return {};
}

bool InChaosCastle(std::int32_t map)
{
    return map >= FirstEventMap && map <= LastEventMap;
}

// 0x50d4fb..0x50d569: fuera de Chaos Castle, con un gremio (+0x1f2 desde 0, que sólo escriben los paquetes) y el
// alpha distinto de 0, la marca del gremio: 0x447b30 y, si va, 0x5f16a0 y 0x5091e0. El 0x7cf no pasa por 0x447b30.
Core::Expected<void, std::string> CheckTheGuildMark(const ActorState& actor, std::int32_t map)
{
    if (InChaosCastle(map) || static_cast<std::int16_t>(actor.word1f2) < 0 || actor.alpha == 0.0f)
        return {};
    return Core::Unexpected<std::string>(
        "0x50d533: la marca del gremio (0x447b30, 0x5f16a0 y 0x5091e0) no está portada");
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
                                              const GameCharacterPass& pass, bool hero)
{
    if (auto supported = CheckTheBody(actor); !supported.HasValue())
        return supported;
    // 0x50d363: con +0x3e3, ni las partes ni la marca del gremio; sigue la tela (0x50d58b).
    if (actor.byte3e3 == 0)
    {
        // 0x50d374: en Chaos Castle, un jugador va con el Angel, con el cromo si es el héroe (0x743a170), y salta
        // a 0x50d4fb, que en esos mapas no dibuja la marca del gremio.
        if (actor.byte88 == PlayerKind && InChaosCastle(character.map))
        {
            if (pass.worldModels == nullptr)
                return Core::Unexpected<std::string>(
                    "0x50d3c9: faltan los modelos del mundo, donde 0x618970 carga el Angel (0x1f7)");
            if (auto drawn = DrawChaosCastleBody(batch, models, *pass.worldModels, character, pass.frameClock,
                                                 hero ? ChaosCastleHeroFlags : ChaosCastleBodyFlags);
                !drawn.HasValue())
                return drawn;
        }
        else
        {
            DrawCharacter(batch, models, character, pass.frameClock, CharacterDrawPass::Parts);
        }
        if (auto marked = CheckTheGuildMark(actor, character.map); !marked.HasValue())
            return marked;
    }
    // 0x50c951: la tela propia del +0x376 0x37 (0x50d599), fuera de Chaos Castle.
    if (actor.word376 == 0x37 && !InChaosCastle(character.map))
        return Core::Unexpected<std::string>("0x50d599: la tela del +0x376 0x37 no está portada");
    if (WearsACloth(actor.characterClass, character.map) && character.cloth == nullptr)
        return Core::Unexpected<std::string>("0x50d58b: falta la tela persistente del lugar del personaje");
    DrawCharacter(batch, models, character, pass.frameClock, CharacterDrawPass::Cloth);
    return {};
}

// 0x50db57..0x510ac2 sobre el cuerpo ya posado: lo que cuelga, los efectos, en el juego (`gameScene`) 0x510ef0, y el
// cuervo. Entre frames del original, sobre una copia del personaje y del rand(): lo que cuelga y los contadores quedan
// como los dejó el último frame.
Core::Expected<void, std::string> DrawTheTail(Render::ModelBatch& batch, const IModelLibrary& models, WorldState& world,
                                              ActorState& actor, std::size_t slot, const DrawnCharacter& character,
                                              const GameCharacterPass& pass, const FullSetScan& scan,
                                              bool originalFrame, bool gameScene)
{
    const std::vector<Render::Models::BoneTransform> body = PoseCharacterBody(models, character);
    // 0x509340 posa en los huesos propios (+0x120), que lee el paso siguiente (0x506d25, la burbuja de Atlans).
    // 0x6001b0 copia antes la escala y la posición al modelo compartido de Player.bmd, con el alpha desde 0,01;
    // las lee el cuervo en la zona segura (Q-C412).
    if (originalFrame)
    {
        actor.bodyPose = body;
        if (actor.alpha >= PlayerModelAlpha)
            world.PlacePlayerModel(actor);
    }
    std::optional<ActorState> projectedActor;
    std::optional<Render::Models::CrtRandom> projectedRandom;
    ActorState& drawn = originalFrame ? actor : projectedActor.emplace(actor);
    Render::Models::CrtRandom& random =
        originalFrame ? pass.clothWorld->random : projectedRandom.emplace(pass.clothWorld->random);
    const std::int32_t map = world.Runtime().map;
    const std::uint8_t heroByte1a1 = world.Actors()[world.Runtime().hero].byte1a1;
    const float worldClock = static_cast<float>(std::chrono::duration<double, std::milli>(pass.frameClock).count());
    const GameHangingPass hangingPass{.batch = batch,
                                      .models = models,
                                      .body = body,
                                      .random = random,
                                      .carry = pass.poseCarry,
                                      .sprites = pass.sprites,
                                      .joints = pass.joints,
                                      .particles = pass.particles,
                                      .itemContext = pass.itemContext,
                                      .frameClock = pass.frameClock,
                                      .worldClock = worldClock,
                                      .map = map,
                                      .heroByte1a1 = heroByte1a1,
                                      .siegeActive = world.Runtime().siegeActive,
                                      .logicSteps = pass.clothWorld->logicSteps,
                                      .originalFrame = originalFrame};
    if (auto hung = DrawGameHanging(drawn, hangingPass); !hung.HasValue())
        return hung;
    const GameCharacterEffectsPass effectsPass{.body = body,
                                               .random = random,
                                               .sprites = pass.sprites,
                                               .particles = pass.particles,
                                               .joints = pass.joints,
                                               .effects = pass.effects,
                                               .slot = slot,
                                               .scan = scan,
                                               .worldClock = worldClock,
                                               .map = map,
                                               .logicSteps = pass.clothWorld->logicSteps,
                                               .gameScene = gameScene,
                                               .originalFrame = originalFrame};
    if (auto effects = DrawGameCharacterEffects(drawn, effectsPass); !effects.HasValue())
        return effects;
    // 0x510abd: 0x420df0, el cuervo, en todas las escenas. Fuera del juego, sin el trapecio, lo dibuja el caller
    // (el login).
    if (!drawn.darkSpirit || (!gameScene && pass.polygon == nullptr))
        return {};
    if (pass.polygon == nullptr)
        return Core::Unexpected<std::string>("0x418320: falta el trapecio del dibujo para el cuervo");
    return DrawGameRaven(drawn, GameRavenPass{.batch = batch,
                                              .models = models,
                                              .polygon = *pass.polygon,
                                              .lighting = pass.dynamicLighting,
                                              .terrain = pass.terrain != nullptr ? *pass.terrain : world.Map().terrain,
                                              .fixedHeight = pass.fixedHeight,
                                              .carry = pass.poseCarry,
                                              .frameClock = pass.frameClock,
                                              .map = map,
                                              .heroByte1a1 = heroByte1a1,
                                              .originalFrame = originalFrame});
}

Core::Expected<void, std::string> DrawOne(Render::ModelBatch& batch, const IModelLibrary& models, WorldState& world,
                                          std::size_t slot, const GameCharacterPass& pass, bool originalFrame)
{
    ActorState& actor = world.Actors()[slot];
    if ((actor.flags7c & HiddenCharacterFlag) != 0)
        return {};
    // 0x50ba65..0x50ba87: sin acciones en el modelo del tipo (+0x26), nada. Player.bmd para el 0x20a; el de los
    // monstruos y los NPC, el que cargó el alta (WorldState::FaunaResources, con la biblioteca del mapa detrás).
    const bool player = actor.type == Data::Models::PlayerSkeletonModel;
    const Render::Models::Skeleton* skeleton =
        player ? models.SkeletonOf(actor.type) : world.FaunaResources().SkeletonOf(actor.type);
    if (skeleton == nullptr || skeleton->actions.empty())
        return {};
    if (!player)
        return DrawGameMonster(batch, models, world, slot, pass, originalFrame);
    if (auto supported = CheckTheEarlyBranches(actor); !supported.HasValue())
        return supported;
    const std::int32_t map = world.Runtime().map;
    DrawnCharacter character = CharacterToDraw(actor, slot, pass, map);
    character.advanceCloth = originalFrame;
    // Al buffer global (0x68b181c) sólo va lo que cuelga (0x509340); el cuerpo queda en el propio (DrawTheTail).
    if ((originalFrame || pass.publishProjectedPose) && pass.onPose)
        character.onPose = [global = pass.onPose](std::uint16_t model,
                                                  std::span<const Render::Models::BoneTransform> bones,
                                                  const Render::Models::ObjectTransform& transform, bool translated)
        {
            if (model != Data::Models::PlayerSkeletonModel && model != ChaosCastleBodyModel)
                global(model, bones, transform, translated);
        };
    DrawCharacter(batch, models, character, pass.frameClock, CharacterDrawPass::Shadow);
    if (auto decorated = CheckMonsterDecorations(actor.word376); !decorated.HasValue())
        return decorated;
    if (pass.clothWorld == nullptr)
        return Core::Unexpected<std::string>("0x50c91e: falta el flujo global de rand del scan de los personajes");
    const FullSetScan scan = ScanTheActor(world, actor, pass.clothWorld->random, originalFrame);
    const auto terrainLight = LightTheBody(world, slot, pass, originalFrame);
    if (!terrainLight.HasValue())
        return Core::Unexpected(terrainLight.Error());
    character.light = actor.drawnLight;
    if (auto supported = DrawTheBody(batch, models, actor, character, pass, slot == world.Runtime().hero);
        !supported.HasValue())
        return supported;
    // 0x50db94: la luz de lo que cuelga (+0x3b4), la del terreno, el rojo de los PK o la de Blood Castle.
    actor.drawnLight = HangingLightOf(actor, terrainLight.Value(), map);
    return DrawTheTail(batch, models, world, actor, slot, character, pass, scan, originalFrame, true);
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
        // 0x510ea9..0x510eb3: con el 0x20a, 0x42afe0, que solo en el 30, con el asedio y con +0x16c llama a
        // 0x42ddb0(personaje, 380); sin el asedio no hace nada.
        if (world.Runtime().map == 30 && actor.type == Data::Models::PlayerSkeletonModel &&
            world.Runtime().siegeActive && actor.seen)
            return Core::Unexpected<std::string>("0x42b00b: 0x42ddb0 (el dibujo del asedio sobre el personaje) no "
                                                 "está portado");
    }
    if (!world.Map().terrain.attributes)
        return Core::Unexpected<std::string>("0x42da60: falta el TerrainWall que abre las puertas");
    FinishTheCastleFrame(&world.Runtime(), world.Map().terrain.attributes->cells);
    return {};
}
} // namespace

void FinishTheCastleFrame(WorldRuntime* runtime, Data::Terrain::TerrainLayer<std::uint16_t>& walls)
{
    // 0x42da60..0x42da79: 0x42da40 abre las puertas (0x42a110(0, 0, 0, 1)) solo con 0x7239d9 en 0.
    if (runtime == nullptr)
    {
        World::OpenTheGates(walls);
        return;
    }
    runtime->castleMarkX = 0.0f;
    runtime->castleMarkY = 0.0f;
    if (runtime->castleGatesClosed == 0)
        World::OpenTheGates(walls);
    runtime->castleGatesClosed = 0;
}

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

Core::Expected<void, std::string> DrawCharacterTail(Render::ModelBatch& batch, const IModelLibrary& models,
                                                    WorldState& world, ActorState& actor, std::size_t slot,
                                                    const DrawnCharacter& character, const GameCharacterPass& pass,
                                                    const FullSetScan& scan, bool originalFrame)
{
    if (pass.clothWorld == nullptr)
        return Core::Unexpected<std::string>("0x50db57: falta el flujo global de rand de lo que cuelga");
    return DrawTheTail(batch, models, world, actor, slot, character, pass, scan, originalFrame, false);
}

Core::Expected<void, std::string> DrawGameCharacters(std::span<ActorState> actors, std::size_t hero,
                                                     Data::Terrain::TerrainLayer<std::uint16_t>& walls,
                                                     WorldRuntime* runtime)
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
    FinishTheCastleFrame(runtime, walls);
    return {};
}
} // namespace Client::Scenes
