#include "Scenes/GameCharacters.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/TextureSlot.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/ActorAppearance.h"
#include "Scenes/CharacterCloth.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/ChatBubbles.h"
#include "Scenes/FullSetScan.h"
#include "Scenes/GameCharacterEffects.h"
#include "Scenes/GameHanging.h"
#include "Scenes/GameTailDrawingPrimitives.h"
#include "Scenes/GuildMarks.h"
#include "Scenes/GuildState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SocialState.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/Sprites.h"
#include "World/TerrainLighting.h"
#include "World/TerrainWalls.h"
#include "World/WorldTerrain.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <ratio>
#include <span>
#include <string>
#include <string_view>
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

// 0x50c929..0x50cc29, bVar23 y el color de la tela de un jugador 0x20a: la clase 3 o 4 con +0x3e3 en 0 (el monstruo
// 0x37 no llega acá), salvo en los mapas 0x12 a 0x17. Después, en cualquier clase y mapa, la guerra de gremios
// (0x50c972..0x50caaa, CL9 hito 8) y el duelo (0x50cb5c..0x50cc29), que gana: con el duelo en curso (0x78abad8), el
// rival (MOVSX de +0x1f4 igual a 0x78abadc y strcmp de +0x1d9 con 0x78abae0) la lleva en (1; 0,2; 0) y el héroe
// publicado (el WORD de +0x1f4 y strcmp de los nombres), en (0; 0,2; 1).
struct ClothLook
{
    bool wears = false;
    glm::vec3 color = PlainClothColor;
};

constexpr glm::vec3 RedCloth{1.0f, 0.2f, 0.0f};
constexpr glm::vec3 BlueCloth{0.0f, 0.2f, 1.0f};
// 0x50c972..0x50caaa: con +0x1f2 distinto de −1 y la guerra prendida (0x576796d, el 61 y el 62 del tipo 1), el gremio
// del personaje (strcmp de +0xd de su entrada) igual al del héroe publicado (su +0x1f2, sin mirar el −1) prende la tela
// del color del equipo (0x576796e en 0, (1; 0,2; 0); si no, (0; 0,2; 1)), y el del rival (0x5767804), la del otro.
// Después, 0x50caaa..0x50cb5c: con 0x576796f, los nombres de 0x57678d0 y 0x57678d9 la prenden en (1; 0,2; 0) y en
// (0; 0,2; 1); lo prende solo el F3 23 (0x4d9f00, hito 11, 08 P-C284 a), que MuRet no atiende: nunca.
Core::Expected<void, std::string> DressForTheWar(const WorldState& world, const ActorState& actor, ClothLook& look)
{
    const GuildState& guild = world.Runtime().guild;
    const auto index = static_cast<std::int16_t>(actor.word1f2);
    if (index == NoGuildIndex || guild.warType == 0)
        return {};
    const auto own = GuildNameAt(guild, index);
    if (!own)
        return Core::Unexpected(std::format("0x50c9b8: el gremio {} cae fuera de la tabla (0x787aa48)", index));
    const auto heroIndex = static_cast<std::int16_t>(world.Actors()[world.Runtime().hero].word1f2);
    const auto heros = GuildNameAt(guild, heroIndex);
    if (!heros)
        return Core::Unexpected(std::format("0x50c9cf: el gremio {} del héroe cae fuera de la tabla", heroIndex));
    const bool firstTeam = guild.warTeam == 0;
    if (*heros == *own)
        look = {.wears = true, .color = firstTeam ? RedCloth : BlueCloth};
    if (GuildTextOf(guild.rival) == *own)
        look = {.wears = true, .color = firstTeam ? BlueCloth : RedCloth};
    return {};
}

Core::Expected<ClothLook, std::string> LookOfTheCloth(const WorldState& world, const ActorState& actor,
                                                      std::int32_t map)
{
    constexpr std::uint8_t MagicGladiator = 3;
    constexpr std::uint8_t DarkLord = 4;
    constexpr std::int32_t FirstClothlessMap = 0x12;
    constexpr std::int32_t LastClothlessMap = 0x17;
    ClothLook look;
    const auto base = static_cast<std::uint8_t>(actor.characterClass & 7u);
    look.wears = (base == MagicGladiator || base == DarkLord) && actor.byte3e3 == 0;
    if (map >= FirstClothlessMap && map <= LastClothlessMap)
        look.wears = false;
    if (auto war = DressForTheWar(world, actor, look); !war.HasValue())
        return Core::Unexpected(std::move(war).Error());
    const DuelState& duel = world.Runtime().social.duel;
    if (duel.active == 0)
        return look;
    const std::string name = ChatNameOf(actor);
    const auto rivalEnd = std::ranges::find(duel.rivalName, std::byte{0});
    const std::string_view rival(reinterpret_cast<const char*>(duel.rivalName.data()),
                                 static_cast<std::size_t>(rivalEnd - duel.rivalName.begin()));
    if (static_cast<std::int32_t>(static_cast<std::int16_t>(actor.key)) == duel.rivalKey && name == rival)
        look = {.wears = true, .color = RedCloth};
    const ActorState& hero = world.Actors()[world.Runtime().hero];
    if (actor.key == hero.key && name == ChatNameOf(hero))
        look = {.wears = true, .color = BlueCloth};
    return look;
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

// 0x5091e0: el hueso 26 (+0x4e0 de los huesos propios, +0x120), el giro del personaje (+0x1c) más (80, 45, 135) y la
// traslación (20, −5, −10), con z en −18 si la armadura (+0x238) es la 0x1226.
constexpr std::size_t GuildMarkBone = 0x4e0 / 0x30;
constexpr float MarkTurnX = 80.0f;
constexpr float MarkTurnY = 45.0f;
constexpr float MarkTurnZ = 135.0f;
constexpr float MarkOffsetX = 20.0f;
constexpr float MarkOffsetY = -5.0f;
constexpr float MarkOffsetZ = -10.0f;
constexpr float LowMarkOffsetZ = -18.0f;
constexpr std::uint16_t LowMarkArmor = 0x1226;
// 0x5092a7..0x5092b6: 0x621320(5, 7, matriz).
constexpr float MarkHalfWidth = 5.0f;
constexpr float MarkHalfHeight = 7.0f;
// 0x50d55d: la bandera de +0x78 que esconde la marca.
constexpr std::uint32_t NoGuildMarkFlag = 0x4000;

float AddX87(float value, float offset)
{
    return static_cast<float>(static_cast<double>(value) + static_cast<double>(offset));
}

// 0x5091e0(personaje, +0x238), después de 0x5f16a0, en el lote del cuerpo: 0x620a00(1), el culling (0x6208d0),
// glColor3f(1, 1, 1), el slot de la marca, y el quad de 0x621320 en el mundo: la posición (+0x10, glTranslatef) más
// R_ConcatTransforms(hueso 26, AngleMatrix(giro) con la traslación) por cada esquina; el culling se apaga (0x6208f0).
// Los huesos son los del cuerpo de este dibujo, que 0x5ffdf0 ya dejó en +0x120. Sin la escala del cuerpo.
Core::Expected<void, std::string> DrawTheMarkQuad(Render::ModelBatch& batch, const IModelLibrary& models,
                                                  const ActorState& actor, const DrawnCharacter& character,
                                                  Render::TextureSlot slot)
{
    const std::vector<Render::Models::BoneTransform> body = PoseCharacterBody(models, character);
    if (body.size() <= GuildMarkBone)
        return Core::Unexpected<std::string>("0x50928d: la marca del gremio sin el hueso 26 del cuerpo (+0x120)");
    TailDrawing::Blend(batch, Render::ModelBlend::Alpha);
    Render::ModelDrawState state = batch.State();
    state.cullEnabled = true;
    batch.ApplyOperation(state, Render::ModelStateOperation::Cull);
    state.texture = slot;
    state.rawTextureName.reset();
    state.space = Render::ModelCoordinateSpace::World;
    batch.ApplyOperation(state, Render::ModelStateOperation::Binding);
    const glm::vec3 turn{AddX87(actor.angles.x, MarkTurnX), AddX87(actor.angles.y, MarkTurnY),
                         AddX87(actor.angles.z, MarkTurnZ)};
    Render::Models::OriginalMatrix local = Render::Models::OriginalAngleMatrix(turn);
    constexpr std::size_t Row = 4;
    constexpr std::size_t Translation = 3;
    local[Translation] = MarkOffsetX;
    local[Row + Translation] = MarkOffsetY;
    local[2 * Row + Translation] =
        actor.body[static_cast<std::size_t>(GameLogic::BodyPart::Armor)].model == LowMarkArmor ? LowMarkOffsetZ
                                                                                               : MarkOffsetZ;
    const Render::Models::OriginalMatrix matrix =
        Render::Models::OriginalConcatTransforms(Render::Models::OriginalMatrixOf(body[GuildMarkBone]), local);
    const float a = MarkHalfWidth;
    const float b = MarkHalfHeight;
    const std::array<glm::vec3, 4> corners{{{-a, -a, -b}, {a, a, -b}, {a, a, b}, {-a, -a, b}}};
    const std::array<glm::vec2, 4> uv{{{0, 1}, {1, 1}, {1, 0}, {0, 0}}};
    std::array<Render::ModelVertex, 4> vertices{};
    for (std::size_t i = 0; i < vertices.size(); ++i)
        vertices[i] = {
            actor.position + Render::Models::OriginalVectorTransform(corners[i], matrix), uv[i], {1, 1, 1, 1}};
    TailDrawing::Quad(batch, vertices);
    state = batch.State();
    state.cullEnabled = false;
    batch.ApplyOperation(state, Render::ModelStateOperation::Cull);
    return {};
}

// 0x50d4fb..0x50d588: fuera de Chaos Castle, con un gremio (+0x1f2 desde 0, que escriben el 65 y el 5D), el 0x20a,
// el alpha distinto de 0 y, salvo el 0x7cf (el gremio que se crea), la clave de su entrada en la tabla (0x447b30), sin
// la bandera 0x4000 de +0x78: la marca de 0x5f16a0(+0x1f2, 1) en el quad de 0x5091e0.
Core::Expected<void, std::string> DrawTheGuildMark(Render::ModelBatch& batch, const IModelLibrary& models,
                                                   const ActorState& actor, const DrawnCharacter& character,
                                                   const GameCharacterPass& pass, const GuildState& guild)
{
    const auto index = static_cast<std::int16_t>(actor.word1f2);
    if (InChaosCastle(character.map) || index < 0 || actor.type != Data::Models::PlayerSkeletonModel ||
        actor.alpha == 0.0f)
        return {};
    if (index != CreatingGuildIndex)
    {
        if (static_cast<std::size_t>(index) >= GuildTableSize)
            return Core::Unexpected(std::format("0x50d543: el gremio {} cae fuera de la tabla", index));
        const auto found = FindGuild(guild, guild.table[static_cast<std::size_t>(index)].key);
        if (!found)
            return Core::Unexpected<std::string>("0x447b00: la búsqueda pasa de las 2.000 entradas de la tabla");
        if (*found == -1)
            return {};
    }
    if ((actor.flags78 & NoGuildMarkFlag) == NoGuildMarkFlag)
        return {};
    if (pass.guildMarks == nullptr)
        return Core::Unexpected<std::string>("0x50d575: la marca del gremio (0x5f16a0) sin la tabla de las marcas");
    auto slot = pass.guildMarks->Upload(guild, index, true);
    if (!slot.HasValue())
        return Core::Unexpected(std::move(slot).Error());
    return DrawTheMarkQuad(batch, models, actor, character, slot.Value());
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
                                              const GameCharacterPass& pass, bool hero, const GuildState& guild)
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
        if (auto marked = DrawTheGuildMark(batch, models, actor, character, pass, guild); !marked.HasValue())
            return marked;
    }
    if (character.wearsCloth.value_or(WearsACloth(actor.characterClass, character.map)) && character.cloth == nullptr)
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
    const Render::Models::Skeleton* skeleton = models.SkeletonOf(actor.type);
    if (skeleton == nullptr || skeleton->actions.empty())
        return {};
    if (auto supported = CheckTheEarlyBranches(actor); !supported.HasValue())
        return supported;
    const std::int32_t map = world.Runtime().map;
    DrawnCharacter character = CharacterToDraw(actor, slot, pass, map);
    character.advanceCloth = originalFrame;
    const auto look = LookOfTheCloth(world, actor, map);
    if (!look.HasValue())
        return Core::Unexpected(look.Error());
    character.wearsCloth = look.Value().wears;
    character.clothColor = look.Value().color;
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
    if (pass.clothWorld == nullptr)
        return Core::Unexpected<std::string>("0x50c91e: falta el flujo global de rand del scan de los personajes");
    const FullSetScan scan = ScanTheActor(world, actor, pass.clothWorld->random, originalFrame);
    const auto terrainLight = LightTheBody(world, slot, pass, originalFrame);
    if (!terrainLight.HasValue())
        return Core::Unexpected(terrainLight.Error());
    character.light = actor.drawnLight;
    if (auto supported =
            DrawTheBody(batch, models, actor, character, pass, slot == world.Runtime().hero, world.Runtime().guild);
        !supported.HasValue())
        return supported;
    // 0x50db94: la luz de lo que cuelga (+0x3b4), la del terreno o el rojo de los PK.
    actor.drawnLight = actor.pk >= RedPk ? glm::vec3{1.0f, 0.1f, 0.1f} : terrainLight.Value() + actor.light;
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
