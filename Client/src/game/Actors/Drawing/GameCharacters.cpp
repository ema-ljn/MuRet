#include "Actors/Drawing/GameCharacters.h"
#include "Actors/ActorName.h"
#include "Maps/MapId.h"
#include "Models/IModelLibrary.h"
#include "Models/ObjectState.h"
#include "CastleSiege/CastleSiegeBlessing.h"

#include "Crt/CrtRandom.h"
#include "Actors/ActorPickingBox.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Tables/PlayerModels.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Kernel/CharacterAppearance.h"
#include "Kernel/CharacterClass.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/TextureSlot.h"
#include "Models/Animation.h"
#include "Models/Cloth.h"
#include "Math/HalfLifeMath.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalRotation.h"
#include "Math/X87.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Actors/ActorAppearance.h"
#include "Actors/Drawing/CharacterCloth.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Assets/FaunaModels.h"
#include "Actors/FullSetScan.h"
#include "Actors/Drawing/GameCharacterEffects.h"
#include "Actors/Drawing/GameHanging.h"
#include "Actors/Drawing/CharacterDrawHooks.h"
#include "Scenes/GameTailDrawingPrimitives.h"
#include "Guild/GuildMarks.h"
#include "Guild/GuildState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Party/PartyActors.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/GameContext.h"
#include "Kernel/WorldState.h"
#include "Particles/Sprites.h"
#include "Terrain/TerrainHeight.h"
#include "Terrain/TerrainLighting.h"
#include "Pathfinding/TerrainWalls.h"
#include "Terrain/WorldTerrain.h"
#include "CastleSiege/CastleSiegeState.h"
#include "Events/EventGlobals.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
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

namespace Client::Actors::Drawing
{
namespace
{
// 0x510e57..0x510e6d: 0x447a0000 in +0x13c and 0x3f800000 in the other three.
constexpr float UndrawnHeroMark = 1000.0f;
constexpr std::size_t DrawnActorCount = Kernel::ActorPoolSize - 1;
constexpr std::uint32_t HiddenCharacterFlag = 0x10000000;
using Maps::IsChaosCastle;
constexpr float ShadowAlphaThreshold = 0.5f;
// +0x88 of a player (0x511290 and the F3 03) and the flags of 0x609640 its Angel goes with in Chaos Castle.
constexpr std::uint8_t PlayerKind = 1;
// +0x88 at 4: an NPC (0x50d2b6).
constexpr std::uint8_t NpcKind = 4;
constexpr std::uint8_t RedPk = 6;
using Maps::IsBloodCastle;
using Math::D;
using Math::F32;

bool Within(std::uint16_t value, std::uint16_t first, std::uint16_t last)
{
    return value >= first && value <= last;
}

template <std::size_t N> bool OneOf(std::uint16_t value, const std::array<std::uint16_t, N>& values)
{
    return std::ranges::find(values, value) != values.end();
}

// The terrain height (0x5f93c0) of the pass's terrain or, without it, of an empty one.
std::optional<double> GroundHeight(const GameCharacterPass& pass, float x, float y)
{
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(pass.terrain);
    if (!terrain.heights || !terrain.attributes)
        return std::nullopt;
    return Terrain::TerrainHeightAt(*terrain.heights, *terrain.attributes, x, y, pass.fixedHeight);
}
constexpr std::uint32_t ChaosCastleBodyFlags = 2;
constexpr std::uint32_t ChaosCastleHeroFlags = 6;

bool SuppressTheHero(Kernel::ActorState& actor, bool hero, std::span<ActorPickingBox> pickingBoxes, std::size_t slot)
{
    if (!hero || (actor.controlCode & UndrawnHeroControlBit) == 0)
    {
        return false;
    }
    if (slot < pickingBoxes.size())
    {
        pickingBoxes[slot].origin.x = UndrawnHeroMark;
        pickingBoxes[slot].axisX.x = 1.0f;
        pickingBoxes[slot].axisY.y = 1.0f;
        pickingBoxes[slot].axisZ.z = 1.0f;
    }
    else
    {
        actor.field13c = UndrawnHeroMark;
        actor.field148 = 1.0f;
        actor.field158 = 1.0f;
        actor.field168 = 1.0f;
    }
    return true;
}

// 0x50cf86..0x50d01d: the float stores between each bit matter when they combine.
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

glm::vec3 HeroBodyColor(Kernel::WorldState& world, Crt::CrtRandom& random, bool originalFrame)
{
    if (!originalFrame)
        return HeroColor(world.OwnCharacter().speedAdjustBits);
    // 0x50ccb5..0x50d219: one access, even with all the bits off.
    const Models::LogicalAccess access(world.Runtime().ownRecordDepth, random, Models::OwnRecordCloseRandoms);
    return HeroColor(world.OwnCharacter().speedAdjustBits);
}

FullSetScan ScanTheActor(Kernel::WorldState& world, const Kernel::ActorState& actor, Crt::CrtRandom& random,
                         bool originalFrame)
{
    // 0x50c913: with +0x3e4 (the cards' portrait), without 0x505820 or a set: the globals stay the last one's and the
    // tail does not read them without the set.
    if (actor.byte3e4 != 0)
        return FullSetScan{};
    if (originalFrame)
    {
        FullSetScan scan =
            ScanFullSet(ScannedCharacterOf(actor), world.OwnCharacter(), world.Runtime().ownRecordDepth, random);
        world.Runtime().fullSetScan = scan.globals;
        return scan;
    }
    // A draw between frames does not consume or publish the original's scan again.
    std::uint8_t depth = 1;
    Crt::CrtRandom copy = random;
    return ScanFullSet(ScannedCharacterOf(actor), world.OwnCharacter(), depth, copy);
}

DrawnCharacter CharacterToDraw(Kernel::ActorState& actor, std::size_t slot, CharacterCloth* cloth,
                               const GameCharacterPass& pass, std::int32_t map)
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
    character.cloth = cloth;
    character.monster = actor.word376;
    character.clothWorld = pass.clothWorld;
    character.sprites = pass.sprites;
    character.originalRandom = pass.clothWorld != nullptr ? &pass.clothWorld->random : nullptr;
    character.pace = pass.pace;
    character.drawBuffers = pass.drawBuffers;
    character.onPartBox = [&actor, slot, boxes = pass.pickingBoxes](const glm::vec3& position)
    {
        const glm::vec3 max{actor.boundsMaxX, actor.boundsMaxY, actor.boundsTop};
        const ActorPickingBox box = PickingBoxFor(position, actor.boundsMin, max);
        if (slot < boxes.size())
            boxes[slot] = box;
        else
            PublishPickingBox(actor, position, actor.boundsMin, max);
    };
    // 0x511290 gives the character its bone buffer (+0x11c at 1 at 0x511bf7, +0x120): 0x5ffdf0 poses the body there,
    // starting from what the previous draw left (actor.bodyPose, Q-C318), not from the global one.
    if (pass.poseBones)
        character.poseBones = [&actor, global = pass.poseBones](std::uint16_t model) {
            return model == Tables::PlayerSkeletonModel ? std::span<const Math::BoneTransform>(actor.bodyPose)
                                                        : global(model);
        };
    return character;
}

// 0x50c929..0x50cc29, bVar23 and the cloth color of a 0x20a player: class 3 or 4 with +0x3e3 at 0, or +0x376 at 0x37
// (0x50c951, monster 55, which is a 0x20a), except in maps 0x12 to 0x17. Then, in any class and map, the guild war
// (0x50c972..0x50caaa) and the duel (0x50cb5c..0x50cc29), which wins: with the duel on (0x78abad8), the rival (MOVSX of
// +0x1f4 equal to 0x78abadc and strcmp of +0x1d9 with 0x78abae0) wears it in (1, 0.2, 0) and the published hero (the
// WORD of +0x1f4 and strcmp of the names), in (0, 0.2, 1).
struct ClothLook
{
    bool wears = false;
    glm::vec3 color = PlainClothColor;
};

constexpr glm::vec3 RedCloth{1.0f, 0.2f, 0.0f};
constexpr glm::vec3 BlueCloth{0.0f, 0.2f, 1.0f};
// 0x50c972..0x50caaa: with +0x1f2 other than −1 and the war on (0x576796d, the 61 and the 62 of type 1), the
// character's guild (strcmp of +0xd of its entry) equal to the published hero's (its +0x1f2, without looking at −1)
// turns on the cloth of the team's color (0x576796e at 0, (1, 0.2, 0); otherwise (0, 0.2, 1)), and the rival's
// (0x5767804), the other one's.
Core::Expected<void, std::string> DressForTheWar(const Kernel::WorldState& world, const Kernel::ActorState& actor,
                                                 ClothLook& look)
{
    const Guild::GuildState& guild = world.Runtime().guild;
    const auto index = static_cast<std::int16_t>(actor.word1f2);
    if (index == Guild::NoGuildIndex || guild.warType == 0)
        return {};
    const auto own = Guild::GuildNameAt(guild, index);
    if (!own)
        return Core::Unexpected(std::format("0x50c9b8: el gremio {} cae fuera de la tabla (0x787aa48)", index));
    const auto heroIndex = static_cast<std::int16_t>(world.Hero().word1f2);
    const auto heros = Guild::GuildNameAt(guild, heroIndex);
    if (!heros)
        return Core::Unexpected(std::format("0x50c9cf: el gremio {} del héroe cae fuera de la tabla", heroIndex));
    const bool firstTeam = guild.warTeam == 0;
    if (*heros == *own)
        look = {.wears = true, .color = firstTeam ? RedCloth : BlueCloth};
    if (Guild::GuildTextOf(guild.rival) == *own)
        look = {.wears = true, .color = firstTeam ? BlueCloth : RedCloth};
    return {};
}

// 0x50caaa..0x50cb5c: Soccer reads the current name again; the two comparisons are independent.
Core::Expected<void, std::string> DressForSoccer(const Kernel::WorldState& world, const Kernel::ActorState& actor,
                                                 ClothLook& look)
{
    const auto& soccer = world.Runtime().events.soccer;
    if (!soccer.active)
        return {};
    const auto index = static_cast<std::int16_t>(actor.word1f2);
    const auto name = Guild::GuildNameAt(world.Runtime().guild, index);
    if (!name)
        return Core::Unexpected(std::format("0x50cac6: el gremio {} cae fuera de la tabla", index));
    if (*name == Guild::GuildTextOf(soccer.firstName))
        look = {.wears = true, .color = RedCloth};
    if (*name == Guild::GuildTextOf(soccer.secondName))
        look = {.wears = true, .color = BlueCloth};
    return {};
}

Core::Expected<ClothLook, std::string> LookOfTheCloth(const Kernel::WorldState& world, const Kernel::ActorState& actor,
                                                      std::int32_t map)
{
    constexpr std::int32_t FirstClothlessMap = 0x12;
    constexpr std::int32_t LastClothlessMap = 0x17;
    ClothLook look;
    const Kernel::CharacterClass base = Kernel::BaseClassOf(actor.characterClass);
    constexpr std::uint16_t ClothMonster = 0x37;
    look.wears = ((base == Kernel::CharacterClass::MagicGladiator || base == Kernel::CharacterClass::DarkLord) &&
                  actor.byte3e3 == 0) ||
                 actor.word376 == ClothMonster;
    if (map >= FirstClothlessMap && map <= LastClothlessMap)
        look.wears = false;
    if (auto war = DressForTheWar(world, actor, look); !war.HasValue())
        return Core::Unexpected(std::move(war).Error());
    if (auto soccer = DressForSoccer(world, actor, look); !soccer.HasValue())
        return Core::Unexpected(std::move(soccer).Error());
    const Social::DuelState& duel = world.Runtime().social.duel;
    if (duel.active == 0)
        return look;
    const std::string name = ChatNameOf(actor);
    const auto rivalEnd = std::ranges::find(duel.rivalName, std::byte{0});
    const std::string_view rival(reinterpret_cast<const char*>(duel.rivalName.data()),
                                 static_cast<std::size_t>(rivalEnd - duel.rivalName.begin()));
    if (static_cast<std::int32_t>(static_cast<std::int16_t>(actor.key)) == duel.rivalKey && name == rival)
        look = {.wears = true, .color = RedCloth};
    const Kernel::ActorState& hero = world.Hero();
    if (actor.key == hero.key && name == ChatNameOf(hero))
        look = {.wears = true, .color = BlueCloth};
    return look;
}

// 0x50ba9e..0x50bae0: a 0x20a with a +0x376 from the table of 0x510adc goes with the cloth on the mesh (0x50bae7),
// through 0x5ffdf0 and 0x6002e0 and with [ebp+0xf] at 0: the player's shadow (0x50bc33) and then the monsters'. No
// addition creates it (those of that table are the 0x190): it stops. With another, through 0x6001b0 like a player
// (0x50bbe4), with its cases by +0x376 further on.
Core::Expected<void, std::string> CheckTheEarlyBranches(const Kernel::ActorState& actor)
{
    if (WearsClothOnTheMesh(actor.word376))
        return Core::Unexpected(
            std::format("0x50bae7: el 0x20a con la tela sobre la malla del +0x376 {:#x} (0x5ffdf0 y 0x6002e0 en vez "
                        "de 0x6001b0) no está portado",
                        actor.word376));
    return {};
}

// 0x50d2cb..0x50d2da and 0x50d31e..0x50d32d: the subtypes with a skeleton's whole body (Skill\Skeleton01..03).
constexpr std::int32_t FirstSkeletonBody = 0xd1;
constexpr std::int32_t LastSkeletonBody = 0xd3;
// 0x50d2b9..0x50d2bd: the branch with the level, +0x88 at 4 (an NPC) and the map [0x6c9dd0] at 0.
constexpr std::int32_t SkeletonNpcMap = 0;

bool WearsTheSkeletonBody(const Kernel::ActorState& actor)
{
    return actor.subtype >= FirstSkeletonBody && actor.subtype <= LastSkeletonBody;
}

// 0x50d2aa..0x50d35e, before looking at +0x3e3: 0x60cf70 with the model subtype (DrawSkeletonBody) and it jumps to the
// cloth (0x50d58b), without the parts or the guild mark. With +0x88 at 4 and map 0, the level is the word +0x1d6 · 8,
// which 0x60b3d0 reads with >> 3 and & 0xf (0x60b3de); otherwise 0. The object is the character itself: +0x64, +0x68,
// +0x6c, +0x70 and the outline's hidden mesh +0x58.
Core::Expected<void, std::string> DrawTheSkeletonBody(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                      const Kernel::ActorState& actor, const DrawnCharacter& character,
                                                      const GameCharacterPass& pass)
{
    const bool npcOfLorencia = actor.byte88 == NpcKind && character.map == SkeletonNpcMap;
    const std::uint8_t level = npcOfLorencia ? static_cast<std::uint8_t>(actor.level & 15) : std::uint8_t{0};
    const Models::ObjectState state{.blendMesh = static_cast<std::int16_t>(actor.dword64),
                                    .light = actor.blendMeshLight,
                                    .uv = {actor.textureU, actor.textureV},
                                    .hiddenMesh = static_cast<std::int16_t>(actor.dword58)};
    const ItemDrawContext context = pass.itemContext != nullptr ? *pass.itemContext : ItemDrawContext{};
    return DrawSkeletonBody(batch, models, character, pass.frameClock, static_cast<std::uint16_t>(actor.subtype), level,
                            state, actor.byteE8 != 0, context);
}

// 0x5091e0: bone 26 (+0x4e0 of the own bones, +0x120), the character's turn (+0x1c) plus (80, 45, 135) and the
// translation (20, −5, −10), with z at −18 if the armor (+0x238) is 0x1226.
constexpr std::size_t GuildMarkBone = 0x4e0 / 0x30;
constexpr float MarkTurnX = 80.0f;
constexpr float MarkTurnY = 45.0f;
constexpr float MarkTurnZ = 135.0f;
constexpr float MarkOffsetX = 20.0f;
constexpr float MarkOffsetY = -5.0f;
constexpr float MarkOffsetZ = -10.0f;
constexpr float LowMarkOffsetZ = -18.0f;
constexpr std::uint16_t LowMarkArmor = 0x1226;
// 0x5092a7..0x5092b6: 0x621320(5, 7, matrix).
constexpr float MarkHalfWidth = 5.0f;
constexpr float MarkHalfHeight = 7.0f;
// 0x50d55d: the +0x78 flag that hides the mark.
constexpr std::uint32_t NoGuildMarkFlag = 0x4000;

float AddX87(float value, float offset)
{
    return static_cast<float>(static_cast<double>(value) + static_cast<double>(offset));
}

// 0x5091e0(character, +0x238), after 0x5f16a0, in the body's batch: 0x620a00(1), the culling (0x6208d0), glColor3f(1,
// 1, 1), the mark's slot, and the quad of 0x621320 in the world: the position (+0x10, glTranslatef) plus
// R_ConcatTransforms(bone 26, AngleMatrix(turn) with the translation) for each corner; the culling goes off (0x6208f0).
// The bones are this draw's body's, which 0x5ffdf0 already left in +0x120. Without the body's scale.
Core::Expected<void, std::string> DrawTheMarkQuad(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                  const Kernel::ActorState& actor, const DrawnCharacter& character,
                                                  Render::TextureSlot slot)
{
    const std::vector<Math::BoneTransform> body = PoseCharacterBody(models, character);
    if (body.size() <= GuildMarkBone)
        return Core::Unexpected<std::string>("0x50928d: la marca del gremio sin el hueso 26 del cuerpo (+0x120)");
    Scenes::TailDrawing::Blend(batch, Render::ModelBlend::Alpha);
    Render::ModelDrawState state = batch.State();
    state.cullEnabled = true;
    batch.ApplyOperation(state, Render::ModelStateOperation::Cull);
    state.texture = slot;
    state.rawTextureName.reset();
    state.space = Render::ModelCoordinateSpace::World;
    batch.ApplyOperation(state, Render::ModelStateOperation::Binding);
    const glm::vec3 turn{AddX87(actor.angles.x, MarkTurnX), AddX87(actor.angles.y, MarkTurnY),
                         AddX87(actor.angles.z, MarkTurnZ)};
    Math::OriginalMatrix local = Math::OriginalAngleMatrix(turn);
    constexpr std::size_t Row = 4;
    constexpr std::size_t Translation = 3;
    local[Translation] = MarkOffsetX;
    local[Row + Translation] = MarkOffsetY;
    local[2 * Row + Translation] = actor.body[static_cast<std::size_t>(Kernel::BodyPart::Armor)].model == LowMarkArmor
                                       ? LowMarkOffsetZ
                                       : MarkOffsetZ;
    const Math::OriginalMatrix matrix =
        Math::OriginalConcatTransforms(Math::OriginalMatrixOf(body[GuildMarkBone]), local);
    const float a = MarkHalfWidth;
    const float b = MarkHalfHeight;
    const std::array<glm::vec3, 4> corners{{{-a, -a, -b}, {a, a, -b}, {a, a, b}, {-a, -a, b}}};
    const std::array<glm::vec2, 4> uv{{{0, 1}, {1, 1}, {1, 0}, {0, 0}}};
    std::array<Render::ModelVertex, 4> vertices{};
    for (std::size_t i = 0; i < vertices.size(); ++i)
        vertices[i] = {actor.position + Math::OriginalVectorTransform(corners[i], matrix), uv[i], {1, 1, 1, 1}};
    Scenes::TailDrawing::Quad(batch, vertices);
    state = batch.State();
    state.cullEnabled = false;
    batch.ApplyOperation(state, Render::ModelStateOperation::Cull);
    return {};
}

// 0x50d4fb..0x50d588: outside Chaos Castle, with a guild (+0x1f2 from 0, which the 65 and the 5D write), the 0x20a, the
// alpha other than 0 and, except 0x7cf (the guild being created), the key of its entry in the table (0x447b30), without
// the 0x4000 flag of +0x78: the mark of 0x5f16a0(+0x1f2, 1) in the quad of 0x5091e0.
Core::Expected<void, std::string> DrawTheGuildMark(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                   const Kernel::ActorState& actor, const DrawnCharacter& character,
                                                   const GameCharacterPass& pass, const Guild::GuildState& guild)
{
    const auto index = static_cast<std::int16_t>(actor.word1f2);
    if (IsChaosCastle(character.map) || index < 0 || actor.type != Tables::PlayerSkeletonModel || actor.alpha == 0.0f)
        return {};
    if (index != Guild::CreatingGuildIndex)
    {
        if (static_cast<std::size_t>(index) >= Guild::GuildTableSize)
            return Core::Unexpected(std::format("0x50d543: el gremio {} cae fuera de la tabla", index));
        const auto found = Guild::FindGuild(guild, guild.table[static_cast<std::size_t>(index)].key);
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

Core::Expected<glm::vec3, std::string> LightTheBody(Kernel::WorldState& world, Kernel::ActorState& actor,
                                                    std::size_t slot, const GameCharacterPass& pass, bool originalFrame)
{
    if (pass.dynamicLighting == nullptr)
        return Core::Unexpected<std::string>("0x50cc55: falta el buffer dinámico de luz del terreno para 0x50ba30");
    const glm::vec3 terrainLight = Terrain::LightAt(*pass.dynamicLighting, actor.position.x, actor.position.y);
    actor.drawnLight = terrainLight + actor.light;
    if (slot == world.Runtime().hero)
        actor.drawnLight = HeroBodyColor(world, pass.clothWorld->random, originalFrame);
    return terrainLight;
}

Core::Expected<void, std::string> DrawTheBody(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                              const Kernel::ActorState& actor, const DrawnCharacter& character,
                                              const GameCharacterPass& pass, bool hero, const Guild::GuildState& guild)
{
    if (WearsTheSkeletonBody(actor))
    {
        if (auto drawn = DrawTheSkeletonBody(batch, models, actor, character, pass); !drawn.HasValue())
            return drawn;
    }
    // 0x50d363: with +0x3e3, neither the parts nor the guild mark; the cloth follows (0x50d58b).
    else if (actor.byte3e3 == 0)
    {
        // 0x50d374: in Chaos Castle, a player goes with the Angel, with the chrome if it is the hero (0x743a170), and
        // jumps to 0x50d4fb, which in those maps does not draw the guild mark.
        if (actor.byte88 == PlayerKind && IsChaosCastle(character.map))
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
    // 0x50c929..0x50c96e: bVar23, class 3 or 4 or +0x376 at 0x37 (monster 55, which is a 0x20a), outside Chaos Castle;
    // with the duel, LookOfTheCloth (DrawnCharacter::wearsCloth).
    const bool wears = character.wearsCloth.value_or(WearsACloth(actor.characterClass, character.map) ||
                                                     WearsTheMonsterCloth(actor.word376, character.map));
    if (wears && character.cloth == nullptr)
        return Core::Unexpected<std::string>("0x50d58b: falta la tela persistente del lugar del personaje");
    DrawCharacter(batch, models, character, pass.frameClock, CharacterDrawPass::Cloth);
    return {};
}

// 0x50db57..0x510ac2 on the body already posed: what hangs, the effects, in the game (`gameScene`) 0x510ef0, and the
// raven. Between original frames, on a copy of the character and of the rand(): what hangs and the counters stay as the
// last frame left them. `modelOrigin` is the position 0x6001b0 copied to the 0x20a's record (0x600247..0x600256, at
// 0x50bbf7), before the guards and the shadow's floor (0x50bcaf): nothing writes it again inside 0x50ba30 and the tail
// reads it with 0x4ec110 on [ebp−0x14] (0x50ba7f).
Core::Expected<void, std::string> DrawTheTail(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                              Kernel::WorldState& world, Kernel::ActorState& actor, std::size_t slot,
                                              const DrawnCharacter& character, const GameCharacterPass& pass,
                                              const FullSetScan& scan, const glm::vec3& modelOrigin, bool originalFrame,
                                              bool gameScene)
{
    const std::vector<Math::BoneTransform> body = PoseCharacterBody(models, character);
    // 0x509340 poses in the own bones (+0x120), which the next step reads (0x506d25, the Atlans bubble). Before that,
    // 0x6001b0 copies the scale and the position to Player.bmd's shared model, with the alpha from 0.01; the raven
    // reads them in the safe zone (Q-C412).
    if (originalFrame)
    {
        actor.bodyPose = body;
        if (actor.alpha >= Kernel::PlayerModelAlpha)
            world.PlacePlayerModel(actor, modelOrigin);
    }
    std::optional<Kernel::ActorState> projectedActor;
    std::optional<Crt::CrtRandom> projectedRandom;
    Kernel::ActorState& drawn = originalFrame ? actor : projectedActor.emplace(actor);
    Crt::CrtRandom& random = originalFrame ? pass.clothWorld->random : projectedRandom.emplace(pass.clothWorld->random);
    const std::int32_t map = world.Runtime().map;
    const std::uint8_t heroByte1a1 = world.Hero().byte1a1;
    const float worldClock = static_cast<float>(std::chrono::duration<double, std::milli>(pass.frameClock).count());
    const GameHangingPass hangingPass{.batch = batch,
                                      .models = models,
                                      .body = body,
                                      .random = random,
                                      .carry = pass.poseCarry,
                                      .sprites = pass.sprites,
                                      .joints = pass.joints,
                                      .jointHooks = pass.hooks,
                                      .particles = pass.particles,
                                      .itemContext = pass.itemContext,
                                      .frameClock = pass.frameClock,
                                      .worldClock = worldClock,
                                      .map = map,
                                      .heroByte1a1 = heroByte1a1,
                                      .siegeActive = world.Runtime().castleSiege.siegeActive,
                                      .logicSteps = pass.clothWorld->logicSteps,
                                      .originalFrame = originalFrame,
                                      .guild = &world.Runtime().guild,
                                      .modelOrigin = modelOrigin};
    if (auto hung = DrawGameHanging(drawn, hangingPass); !hung.HasValue())
        return hung;
    const GameCharacterEffectsPass effectsPass{.body = body,
                                               .random = random,
                                               .sprites = pass.sprites,
                                               .particles = pass.particles,
                                               .joints = pass.joints,
                                               .jointHooks = pass.hooks,
                                               .effects = pass.effects,
                                               .effectHooks = pass.effectHooks,
                                               .world = &world,
                                               .slot = slot,
                                               .scan = scan,
                                               .party = Party::PartyMembers(world.Runtime()),
                                               .actors = world.Actors(),
                                               .worldClock = worldClock,
                                               .map = map,
                                               .logicSteps = pass.clothWorld->logicSteps,
                                               .gameScene = gameScene,
                                               .originalFrame = originalFrame,
                                               .modelOrigin = modelOrigin};
    if (auto effects = DrawGameCharacterEffects(drawn, effectsPass); !effects.HasValue())
        return effects;
    // 0x510abd: 0x420df0, the raven, in every scene. Outside the game, without the trapezoid, the caller draws it (the
    // login).
    if (!drawn.darkSpirit || (!gameScene && pass.polygon == nullptr))
        return {};
    if (pass.polygon == nullptr)
        return Core::Unexpected<std::string>("0x418320: falta el trapecio del dibujo para el cuervo");
    return DrawGameRaven(drawn, GameRavenPass{.batch = batch,
                                              .models = models,
                                              .polygon = *pass.polygon,
                                              .lighting = pass.dynamicLighting,
                                              .terrain = Terrain::TerrainOrEmpty(pass.terrain),
                                              .fixedHeight = pass.fixedHeight,
                                              .carry = pass.poseCarry,
                                              .frameClock = pass.frameClock,
                                              .map = map,
                                              .heroByte1a1 = heroByte1a1,
                                              .originalFrame = originalFrame,
                                              .random = &random,
                                              .particles = pass.particles,
                                              .worldClock = worldClock,
                                              .world = &world,
                                              .jointHooks = pass.hooks,
                                              .effectHooks = pass.effectHooks});
}

// Between original frames, the z of the shadow's floor (0x50bc77..0x50bcbd) is the draw's, but it does not stay on the
// character: the next step reads the one the last original frame left (ADR 0005). It gives it back on leaving the draw.
class GroundBetweenFrames
{
public:
    GroundBetweenFrames(Kernel::ActorState& actor, float z) : m_actor(actor), m_stepped(actor.position.z)
    {
        actor.position.z = z;
    }
    ~GroundBetweenFrames()
    {
        m_actor.position.z = m_stepped;
    }
    GroundBetweenFrames(const GroundBetweenFrames&) = delete;
    GroundBetweenFrames& operator=(const GroundBetweenFrames&) = delete;
    GroundBetweenFrames(GroundBetweenFrames&&) = delete;
    GroundBetweenFrames& operator=(GroundBetweenFrames&&) = delete;

private:
    Kernel::ActorState& m_actor;
    float m_stepped;
};

// `slot` is its place in the pool, or ActorPoolSize outside it; `cloth`, its cloth.
Core::Expected<void, std::string> DrawOne(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                          Kernel::WorldState& world, Kernel::ActorState& actor, std::size_t slot,
                                          CharacterCloth* cloth, const GameCharacterPass& pass, bool originalFrame)
{
    if ((actor.flags7c & HiddenCharacterFlag) != 0)
        return {};
    // 0x50ba65..0x50ba87: with no actions in the type's model (+0x26), nothing. Player.bmd for the 0x20a; for monsters
    // and NPCs, the one the addition loaded (GameCharacterPass::faunaModels, with the map's library behind it).
    const bool player = actor.type == Tables::PlayerSkeletonModel;
    const Models::Skeleton* skeleton =
        player ? models.SkeletonOf(Models::ModelId{actor.type})
               : Assets::FaunaModelsOrEmpty(pass.faunaModels).SkeletonOf(Models::ModelId{actor.type});
    if (skeleton == nullptr || skeleton->actions.empty())
        return {};
    if (!player)
    {
        if (pass.hooks == nullptr)
            return Core::Unexpected<std::string>("0x50ba30: no draw hooks for a monster or an NPC");
        return pass.hooks->DrawTheMonster(batch, models, world, slot, pass, originalFrame);
    }
    if (auto supported = CheckTheEarlyBranches(actor); !supported.HasValue())
        return supported;
    const std::int32_t map = world.Runtime().map;
    DrawnCharacter character = CharacterToDraw(actor, slot, cloth, pass, map);
    character.advanceCloth = originalFrame;
    const auto look = LookOfTheCloth(world, actor, map);
    if (!look.HasValue())
        return Core::Unexpected(look.Error());
    character.wearsCloth = look.Value().wears;
    character.clothColor = look.Value().color;
    // Only what hangs goes to the global buffer (0x68b181c) (0x509340); the body stays in its own (DrawTheTail).
    if ((originalFrame || pass.publishProjectedPose) && pass.onPose)
        character.onPose = [global = pass.onPose](std::uint16_t model, std::span<const Math::BoneTransform> bones,
                                                  const Models::ObjectTransform& transform, bool translated)
        {
            if (model != Tables::PlayerSkeletonModel && model != ChaosCastleBodyModel)
                global(model, bones, transform, translated);
        };
    // 0x50bbf7: 0x6001b0 copies the position to the 0x20a's record (0x600247..0x600256) before the shadow's guards and
    // its floor; the tail reads it from there (DrawTheTail). Between original frames, the step's, before
    // GroundBetweenFrames borrows it.
    const glm::vec3 modelOrigin = actor.position;
    // 0x50bc77..0x50bcbd: with the shadow, in Blood Castle the one that dies pushed drops to the terrain height before
    // 0x60cf70; the shadow, the body and what hangs go there, and the points of the tail's bones, from the earlier one.
    // The write stays (0x50bcaf) only in an original frame.
    std::optional<GroundBetweenFrames> projectedGround;
    if (DrawsTheShadow(character))
    {
        const auto ground = ShadowGroundOf(pass, actor, map, 0x50bc9d);
        if (!ground.HasValue())
            return Core::Unexpected(ground.Error());
        character.object.position.z = ground.Value();
        if (originalFrame)
            actor.position.z = ground.Value();
        else
            projectedGround.emplace(actor, ground.Value());
    }
    DrawCharacter(batch, models, character, pass.frameClock, CharacterDrawPass::Shadow);
    // 0x50bcec: 0x60cf70 with the 0x20b copies the character's scale and position to it, already on the floor (the
    // 0x632/0 and /1 read it on the floor, 0x53c1eb). Only in an original frame.
    if (originalFrame && DrawsTheShadow(character))
        world.PlaceShadowModel(actor);
    if (auto decorated = CheckMonsterDecorations(actor.word376); !decorated.HasValue())
        return decorated;
    if (pass.clothWorld == nullptr)
        return Core::Unexpected<std::string>("0x50c91e: falta el flujo global de rand del scan de los personajes");
    const FullSetScan scan = ScanTheActor(world, actor, pass.clothWorld->random, originalFrame);
    const auto terrainLight = LightTheBody(world, actor, slot, pass, originalFrame);
    if (!terrainLight.HasValue())
        return Core::Unexpected(terrainLight.Error());
    character.light = actor.drawnLight;
    if (auto supported =
            DrawTheBody(batch, models, actor, character, pass, slot == world.Runtime().hero, world.Runtime().guild);
        !supported.HasValue())
        return supported;
    // 0x50db94: the light of what hangs (+0x3b4), the terrain's, the PKs' red or Blood Castle's.
    actor.drawnLight = HangingLightOf(actor, terrainLight.Value(), map);
    return DrawTheTail(batch, models, world, actor, slot, character, pass, scan, modelOrigin, originalFrame, true);
}

Core::Expected<void, std::string> DrawThePool(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                              Kernel::WorldState& world, const GameCharacterPass& pass,
                                              bool originalFrame)
{
    const std::size_t hero = world.Runtime().hero;
    for (std::size_t slot = 0; slot < DrawnActorCount; ++slot)
    {
        Kernel::ActorState& actor = world.Actors()[slot];
        if (SuppressTheHero(actor, slot == hero, pass.pickingBoxes, slot) || !actor.alive || !actor.seen)
            continue;
        CharacterCloth* cloth = slot < pass.cloths.size() ? &pass.cloths[slot] : nullptr;
        if (auto drawn = DrawOne(batch, models, world, actor, slot, cloth, pass, originalFrame); !drawn.HasValue())
            return Core::Unexpected(std::format("{}, en el personaje {}", drawn.Error(), slot));
        // 0x510ea9..0x510eb3: with the 0x20a, 0x42afe0, which only in map 30, with the siege and with +0x16c calls
        // 0x42ddb0(character, 380); without the siege it does nothing.
        if (world.Runtime().map == 30 && actor.type == Tables::PlayerSkeletonModel &&
            world.Runtime().castleSiege.siegeActive && actor.seen && originalFrame)
        {
            if (pass.clothWorld == nullptr || pass.particles == nullptr)
                return Core::Unexpected(std::string("0x42de7f: faltan partículas o CRT para la bendición del asedio"));
            if (auto blessed = CastleSiege::DrawTheCastleBlessing(
                    actor, world.Actors()[hero].word1f2, world.Runtime().castleSiege.castleMarkX,
                    world.Runtime().castleSiege.castleMarkY, pass.clothWorld->logicSteps, *pass.particles,
                    pass.clothWorld->random);
                !blessed.HasValue())
                return blessed;
        }
    }
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(pass.terrain);
    if (!terrain.attributes)
        return Core::Unexpected<std::string>("0x42da60: falta el TerrainWall que abre las puertas");
    FinishTheCastleFrame(&world.Runtime(), terrain.attributes->cells);
    return {};
}
} // namespace

bool CastsShadow(const Kernel::ActorState& actor, std::int32_t map)
{
    constexpr std::int32_t NoShadowMap = 10;
    using Maps::FirstKalimaMap;
    using Maps::LastKalimaMap;
    using Maps::SeventhKalimaMap;
    // 0x50bc16: with +0x3e4 (the cards' portrait), no shadow.
    return actor.alpha >= ShadowAlphaThreshold && actor.byte3e4 == 0 && map != NoShadowMap &&
           !(map >= FirstKalimaMap && map <= LastKalimaMap) && map != SeventhKalimaMap;
}

void FinishTheCastleFrame(Kernel::WorldRuntime* runtime, Data::Terrain::TerrainLayer<std::uint16_t>& walls)
{
    // 0x42da60..0x42da79: 0x42da40 opens the gates (0x42a110(0, 0, 0, 1)) only with 0x7239d9 at 0.
    if (runtime == nullptr)
    {
        Pathfinding::OpenTheGates(walls);
        return;
    }
    runtime->castleSiege.castleMarkX = 0.0f;
    runtime->castleSiege.castleMarkY = 0.0f;
    if (runtime->castleSiege.castleGatesClosed == 0)
        Pathfinding::OpenTheGates(walls);
    runtime->castleSiege.castleGatesClosed = 0;
}

Core::Expected<void, std::string> DrawGameCharacters(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                     Kernel::WorldState& world, const GameCharacterPass& pass)
{
    if (pass.originalFrames < 0 || pass.originalFrames > Models::MaxOriginalFramesPerDraw)
        return Core::Unexpected<std::string>(
            "0x510d00: cantidad de frames del original fuera del presupuesto del dibujo");
    const int frames = std::max(pass.originalFrames, 1);
    std::optional<Particles::FrameSprites> discardedSprites;
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
            // Each frame starts from the world's prefix. Only a completed frame can discard its transient output: the
            // stop keeps the shadow, parts and cloth from before the branch.
            *discardedSprites = *pass.sprites;
            current.sprites = &*discardedSprites;
        }
        if (pass.originalFrames > 0)
            current.pace = Models::OriginalFramePace::OncePerDraw;
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

Core::Expected<void, std::string> DrawCharacterOutsideThePool(Render::ModelBatch& batch,
                                                              const Models::IModelLibrary& models,
                                                              Kernel::WorldState& world, Kernel::ActorState& actor,
                                                              CharacterCloth& cloth, const GameCharacterPass& pass,
                                                              bool originalFrame)
{
    return DrawOne(batch, models, world, actor, Kernel::ActorPoolSize, &cloth, pass, originalFrame);
}

Core::Expected<void, std::string> DrawCharacterTail(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                    Kernel::WorldState& world, Kernel::ActorState& actor,
                                                    std::size_t slot, const DrawnCharacter& character,
                                                    const GameCharacterPass& pass, const FullSetScan& scan,
                                                    bool originalFrame)
{
    if (pass.clothWorld == nullptr)
        return Core::Unexpected<std::string>("0x50db57: falta el flujo global de rand de lo que cuelga");
    // The login and the selection are not in Blood Castle: the shadow's floor does not move z and the record has the
    // character's position.
    return DrawTheTail(batch, models, world, actor, slot, character, pass, scan, actor.position, originalFrame, false);
}

Core::Expected<void, std::string> DrawGameCharacters(std::span<Kernel::ActorState> actors, std::size_t hero,
                                                     Data::Terrain::TerrainLayer<std::uint16_t>& walls,
                                                     Kernel::WorldRuntime* runtime,
                                                     std::span<ActorPickingBox> pickingBoxes)
{
    for (std::size_t slot = 0; slot < std::min(actors.size(), DrawnActorCount); ++slot)
    {
        Kernel::ActorState& actor = actors[slot];
        // 0x510e40: the hero with +0x1d8 & 4 in scene 5.
        if (SuppressTheHero(actor, slot == hero, pickingBoxes, slot))
        {
            continue;
        }
        // 0x510e75 and 0x510e7a.
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

bool WearsClothOnTheMesh(std::uint16_t monster)
{
    constexpr std::array<std::uint16_t, 7> Cloths{0x59, 0x5f, 0x70, 0x76, 0x7c, 0x82, 0x8f};
    return OneOf(monster, Cloths);
}

Core::Expected<void, std::string> CheckMonsterDecorations(std::uint16_t monster)
{
    const auto cut = [monster](std::string_view address, std::string_view what) {
        return Core::Unexpected(
            std::format("{}: {} del +0x376 {:#x} (0x50bf45) no está portado", address, what, monster));
    };
    constexpr std::array<std::uint16_t, 5> Glowing{0x26, 0x2b, 0x34, 0x3b, 0x43};
    if (OneOf(monster, Glowing) || Within(monster, 0x4e, 0x53))
        return cut("0x50c671", "el brillo de 0x60b130");
    if (monster == 0x45)
        return cut("0x50bf98", "los sprites 0x5ac de la tabla 0x6c88a0 y las partículas 0x5c6");
    if (monster == 0x46)
        return cut("0x50c0fb", "el sprite 0x5ac");
    if (monster == 0x47 || monster == 0x4a || monster == 0x12d)
        return cut("0x50c59f", "la tela 0x64 de 0x4548b0");
    if (monster == 0x49 || monster == 0x4b)
        return cut("0x50c36b", "los sprites, los joints 0x614 y la partícula 0x5d9");
    if (monster == 0x4d)
        return cut("0x50c177", "el modelo, la tela y los huesos");
    if (monster == 0x35 || monster == 0x36)
        return cut("0x50c71b", "el brillo 0x48 con la textura 0x5fd");
    if (monster == 0x2a)
        return cut("0x50c762", "las armas 0xef y 0xf0 colgadas con 0x509340");
    if (Within(monster, 0x84, 0x86))
        return cut("0x50c83b", "el arma 0xc1c, 0x225 o 0xa24 colgada con 0x509340");
    if (monster == 0x87)
        return cut("0x50c8ce", "el brillo 0x140");
    return {};
}

glm::vec3 HangingLightOf(const Kernel::ActorState& actor, const glm::vec3& terrainLight, std::int32_t map)
{
    const float faint = std::bit_cast<float>(0x3dcccccdu);
    if (actor.pk >= RedPk)
        return {1.0f, faint, faint};
    glm::vec3 light{F32(D(terrainLight.x) + D(actor.light.x)), F32(D(terrainLight.y) + D(actor.light.y)),
                    F32(D(terrainLight.z) + D(actor.light.z))};
    const std::int32_t level = map - 9;
    if (level <= 0 || level > 8)
        return light;
    const std::uint16_t monster = actor.word376;
    const bool keeper = Within(monster, 0x56, 0x59) || Within(monster, 0x5c, 0x5f) || Within(monster, 0x73, 0x76) ||
                        Within(monster, 0x79, 0x7c) || Within(monster, 0x7f, 0x82) || Within(monster, 0x8a, 0x8f);
    // 0x50dc2a: the quotient by 3 with IMUL 0x55555556 (signed; level is positive).
    const std::int32_t third = level / 3;
    if (!keeper || third == 0)
        return light;
    return {F32(static_cast<double>(third) * 0.5), faint, faint};
}

Core::Expected<float, std::string> ShadowGroundOf(const GameCharacterPass& pass, const Kernel::ActorState& actor,
                                                  std::int32_t map, std::uint32_t address)
{
    if (!IsBloodCastle(map) || actor.byte1a1 == 0 || actor.dying == 0)
        return actor.position.z;
    const auto ground = GroundHeight(pass, actor.position.x, actor.position.y);
    if (!ground)
        return Core::Unexpected(std::format("{:#x}: falta el terreno de 0x5f93c0", address));
    // fcom [z] and `test ah, 1`: with the terrain at the z or above (C0 at 0, ordered) it does not write.
    if (*ground >= D(actor.position.z))
        return actor.position.z;
    return F32(*ground);
}
} // namespace Client::Actors::Drawing
