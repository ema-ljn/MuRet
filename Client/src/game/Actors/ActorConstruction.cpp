#include "Actors/ActorConstruction.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerBones.h"
#include "Tables/PlayerModels.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/TerrainHeight.h"
#include "Terrain/WorldTerrain.h"
#include "Terrain/TerrainDefines.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Actors
{
namespace
{
constexpr std::uint16_t SafeAttribute = 1;
constexpr double PetHeightOffset = 30.0;      // 0x6b8858
constexpr double LargePetHeightOffset = 90.0; // 0x6b8948
constexpr std::int32_t FirstLargeOffsetMap = 8;
constexpr std::int32_t SecondLargeOffsetMap = 10;
constexpr std::int32_t NoMap = -1;
// 0x6b86b4 and 0x6b86b0: the center of the cell, X · 100 + 50.
using Terrain::CellSize;
constexpr float CellCenter = 50.0f;
constexpr float PlayerScale = 0.9f;
constexpr std::uint8_t ConstructedPk = 3;
// 0x511c70: +0x93 at 0xa.
constexpr std::uint16_t NoWord376 = 0xffff;
constexpr std::uint8_t PlayerStartAction = 1;
constexpr std::uint16_t ConstructedMovementSpeed = 10;
constexpr std::uint8_t RightHandBone = static_cast<std::uint8_t>(Tables::PlayerRightHandBone);
constexpr std::uint8_t LeftHandBone = static_cast<std::uint8_t>(Tables::PlayerLeftHandBone);

// The box of 0x511290 (+0x124..+0x138), relative to the position.
struct ConstructionBox
{
    glm::vec3 min;
    float maxX;
    float maxY;
    float top;
};

// 0x512270..0x5123ba: the floor at (−60, −60, 0) and, through the byte table of 0x512700 over the jump table of
// 0x5126e4 with type − 0x154 (0 to 0xb6, unsigned), the rest. Outside the table, 0x5123a9.
ConstructionBox BoxOf(std::uint16_t type)
{
    constexpr glm::vec3 Floor{-60.0f, -60.0f, 0.0f};
    switch (type)
    {
    case 0x198:
    case 0x199:
    case 0x19a:
    case Tables::PlayerSkeletonModel:
        return {Floor, 40.0f, 40.0f, 120.0f}; // 0x5122d4
    case 0x154:
    case 0x158:
    case 0x15b:
    case 0x163:
    case 0x165:
    case 0x166:
        return {Floor, 50.0f, 50.0f, 80.0f}; // 0x5122f4
    case 0x15d:
    case 0x171:
    case 0x179:
    case 0x17c:
    case 0x17e:
        return {Floor, 70.0f, 70.0f, 250.0f}; // 0x512314
    case 0x177:
        return {Floor, 100.0f, 100.0f, 150.0f}; // 0x512334
    case 0x18f:
        return {{-120.0f, -120.0f, 0.0f}, 100.0f, 100.0f, 300.0f}; // 0x51233b
    case 0x18e:
        return {{-90.0f, -50.0f, 0.0f}, 90.0f, 50.0f, 200.0f}; // 0x51236f
    default:
        return {Floor, 50.0f, 50.0f, 150.0f}; // 0x5123a9
    }
}

void PutTheBox(Kernel::ActorState& actor, const ConstructionBox& box)
{
    actor.boundsMin = box.min;
    actor.boundsMaxX = box.maxX;
    actor.boundsMaxY = box.maxY;
    actor.boundsTop = box.top;
}

// A case's hand bones: the right one (+0x2bd) and the left one (+0x2dd), if it writes them.
struct HandBones
{
    std::uint8_t right;
    std::optional<std::uint8_t> left;
};

constexpr HandBones Both(std::uint8_t right, std::uint8_t left)
{
    return {right, left};
}

constexpr HandBones RightOnly(std::uint8_t right)
{
    return {right, std::nullopt};
}

// 0x512490..0x5126a8: the byte table of 0x512824 over the jump table of 0x5127b8 with type − 0x152 (0 to 0x48,
// unsigned). nullopt: the case is 0x5126b2.
std::optional<HandBones> TableHandBonesOf(std::uint16_t type)
{
    switch (type)
    {
    case 0x152:
    case 0x170:
        return Both(42, 33); // 0x51264a
    case 0x153:
        return Both(19, 14); // 0x512673
    case 0x155:
        return Both(26, 36); // 0x512635
    case 0x156:
    case 0x157:
    case 0x15c:
        return Both(41, 32); // 0x51265f
    case 0x15a:
        return Both(29, 38); // 0x512688
    case 0x15d:
    case 0x162:
    case 0x16f:
    case 0x174:
    case 0x175:
    case 0x187:
    case 0x18b:
        return Both(30, 39); // 0x5124b5
    case 0x164:
        return Both(26, 35); // 0x512620
    case 0x165:
        return Both(31, 22); // 0x51260b
    case 0x167:
        return Both(24, 19); // 0x5125f6
    case 0x168:
        return Both(25, 16); // 0x5125e1
    case 0x16a:
        return Both(39, 30); // 0x5125cc
    case 0x16c:
        return Both(16, 25); // 0x5125a2
    case 0x16d:
        return Both(17, 28); // 0x5125b7
    case 0x176:
        return Both(52, 65); // 0x51258d
    case 0x17a:
        return Both(20, 33); // 0x51269d
    case 0x17b:
        return RightOnly(23); // 0x51257f: the left one stays as it was
    case 0x17c:
        return RightOnly(43); // 0x512571
    case 0x17e:
        return Both(55, 70); // 0x51255c
    case 0x180:
        return Both(39, 39); // 0x51251e
    case 0x181:
        return Both(27, 38); // 0x512533
    case 0x182:
        return Both(32, 43); // 0x512548
    case 0x186:
        return Both(36, 45); // 0x512509
    case 0x189:
        return Both(27, 18); // 0x5124f4
    case 0x18d:
        return Both(33, 20); // 0x5124ca
    case 0x18e:
        return Both(1, 1); // 0x5124df
    case 0x198:
    case 0x199:
    case 0x19a:
        return Both(RightHandBone, LeftHandBone); // 0x5126ce, without 0x43a250 or 0x42d7f0
    default:
        return std::nullopt;
    }
}

// 0x43a250: the bones of types 0x192..0x197 (the table of 0x43a2d4); 0x193 and the rest return 0.
std::optional<HandBones> EventHandBonesOf(std::uint16_t type)
{
    switch (type)
    {
    case 0x192:
        return Both(0x1d, 0x31);
    case 0x194:
        return Both(0xd, 0xe);
    case 0x195:
        return Both(0x38, 0x2a);
    case 0x196:
        return Both(0x3c, 0x3c);
    case 0x197:
        return Both(0x29, 0x33);
    default:
        return std::nullopt;
    }
}

// 0x42d7f0: the box of 0x19b, +0x38f at 0 and 1 (without the bones), and that of 0x1f8 with 0 (with the bones).
bool SetTheSiegeBox(Kernel::ActorState& actor, std::uint16_t type)
{
    constexpr std::uint16_t SiegeGate = 0x19b;
    constexpr std::uint16_t SiegeStatue = 0x1f8;
    if (type == SiegeGate)
    {
        PutTheBox(actor, {{-140.0f, -140.0f, 0.0f}, 140.0f, 140.0f, 300.0f});
        actor.attackCount = 0;
        return true;
    }
    if (type == SiegeStatue)
        PutTheBox(actor, {{-200.0f, -50.0f, -100.0f}, 200.0f, 50.0f, 100.0f});
    return false;
}

void PutTheHandBones(Kernel::ActorState& actor, const HandBones& bones)
{
    actor.hands[0].bone = bones.right;
    if (bones.left)
        actor.hands[1].bone = *bones.left;
}

// 0x512490..0x5126d5: the hand bones by type; 0x5126b2 tries 0x43a250 and 0x42d7f0 and, if neither returns 1, hangs
// the hands from 33 and 42.
void SetTheHandBones(Kernel::ActorState& actor, std::uint16_t type)
{
    if (const auto bones = TableHandBonesOf(type))
    {
        PutTheHandBones(actor, *bones);
        return;
    }
    if (const auto bones = EventHandBonesOf(type))
    {
        PutTheHandBones(actor, *bones);
        return;
    }
    if (SetTheSiegeBox(actor, type))
        return;
    PutTheHandBones(actor, {RightHandBone, LeftHandBone});
}

// 0x4ec200 does not run here: 0x511290 leaves both actions at 1 and the frame at 0.
void ResetTheAnimation(Kernel::ActorState& actor)
{
    const std::uint8_t action = actor.type == Tables::PlayerSkeletonModel ? PlayerStartAction : 0;
    actor.animation.action = action;
    actor.animation.previousAction = action;
    actor.animation.frame = 0.0f;
    actor.animation.previousKey = 0;
}

// 0x512455..0x512463 and the loops before it: the head and the five body parts without an item, with level and bytes
// at 0; the hands without an item with the level and the +3 at 0; the wings, the pet and +0x338 only the model.
void ClearTheParts(Kernel::ActorState& actor)
{
    actor.head = Kernel::NoItemModel;
    for (Kernel::ActorItemPart& part : actor.body)
    {
        part.model = Kernel::NoItemModel;
        part.level = 0;
        part.flags1b = 0;
        part.flags1c = 0;
    }
    for (Kernel::ActorItemPart& hand : actor.hands)
    {
        hand.model = Kernel::NoItemModel;
        hand.level = 0;
        hand.flags1b = 0;
    }
    actor.wings.model = Kernel::NoItemModel;
    actor.pet.model = Kernel::NoItemModel;
    actor.previousHad1c15 = false;
}

// An access to one of the two cell entries with `value` in the clear while it is open: what it leaves in the
// variable when it closes goes to `left` (ActorState::cellXLeft and cellYLeft).
void AccessTheCell(std::uint8_t& accesses, std::uint8_t value, std::uint32_t& left, Crt::CrtRandom& random)
{
    left = value;
    const Models::LogicalAccess access(accesses, random, Models::SmallLogicalCloseRandoms, left);
}

// The cell: writes X and then Y, each in its own access (0x412890 when it closes), and the copies in the clear.
void WriteTheCell(Kernel::ActorState& actor, const ActorPlacement& placement, Crt::CrtRandom& random)
{
    AccessTheCell(actor.cellXAccesses, placement.x, actor.cellXLeft, random);
    AccessTheCell(actor.cellYAccesses, placement.y, actor.cellYLeft, random);
    actor.cellX = placement.x;
    actor.cellY = placement.y;
    actor.targetX = placement.x;
    actor.targetY = placement.y;
}

// The step counter (0x7d3a0f0), with the access count the cloth keeps.
std::int32_t ReadTheStepCounter(Models::ClothWorld& cloth)
{
    const std::int32_t steps = cloth.logicSteps;
    if (cloth.openCounterAccesses == 0)
    {
        for (std::uint16_t i = 0; i < Models::SmallLogicalCloseRandoms; ++i)
        {
            static_cast<void>(cloth.random.Next());
        }
    }
    return steps;
}

// What 0x511290 fixes on a 0x20a, outside the cell, the position and the parts.
void SetTheStartingState(Kernel::ActorState& actor, std::uint16_t type)
{
    actor.alive = true;
    actor.type = type;
    actor.subtype = 0;
    actor.scale = PlayerScale;
    PutTheBox(actor, BoxOf(type));
    actor.alphaTarget = 1.0f;
    actor.alpha = 1.0f;
    // 0x511bdf and 0x512499: the alpha in steps of 0.05 (0x4e9980) and the blendMesh light at 1.0.
    actor.alphaMode = 1;
    actor.blendMeshLight = 1.0f;
    if (!IsNewFace(type))
    {
        actor.characterClass = 0;
    }
    ResetTheAnimation(actor);
    actor.seen = false;
    actor.dying = 0;
    actor.dyingTime = 0.0f; // 0x51247a, with the ESI at 0 from 0x5121f6
    actor.byte88 = 1;
    // 0x511d51, with the ESI at 0 from 0x511c36.
    actor.flags78 = 0;
    actor.byte3e3 = 0;
    actor.word376 = NoWord376;
    actor.movement = 0;
    // 0x511bc0/0x511bc7: writes neither progress nor the path's contents.
    actor.path.count = 0;
    actor.path.current = 0;
    actor.movementPhase = 0.0f;                         // 0x511c44
    actor.baseMovementSpeed = ConstructedMovementSpeed; // 0x511d48
    actor.byte369 = 0;
    actor.byte370 = 0;
    actor.byte371 = 0x43; // 0x511cc1
    actor.byte373 = 0xff; // 0x511cc8
    actor.byte372 = 0;    // 0x511ccf
    actor.byte374 = 0;    // 0x511c13
    actor.byte42c = 1;    // 0x511c92
    actor.byte110 = 0;    // 0x511c69
    actor.byte1a0 = 0;    // 0x511c62
    actor.byte1bc = 0;    // 0x511ce4
    actor.word1f2 = Kernel::NoItemModel;
    actor.byte399 = 0;
    actor.byte80 = 0;
    actor.run = 0;
    actor.controlCode = 0;
    actor.pk = ConstructedPk;
    // 0x511c99, in the same block as the PK and the scale: the +0x358 that 0x40d010 later writes on the
    // published hero.
    actor.byte358 = 0;
    // 0x511ca0: +0x35c, with the ESI at 0 from 0x511c36.
    actor.counter35c = 0;
    actor.level = 0;
    actor.guildStatus = 0xff;
    actor.light = {};
    actor.drawnLight = {};
    actor.byte390 = 0; // 0x51186b
    // 0x511be6..0x511d45 and 0x51248d.
    actor.dword58 = -1;
    actor.dword64 = -1;
    actor.byte92 = 1;
    actor.byte93 = 0xa;
    actor.float94 = 1.2f;
    actor.byte98 = 0;
    actor.byteE8 = 1;
    actor.floatD8 = 0.0f;
    actor.byte38a = 0;
    actor.dword394 = 0;
    actor.dword428 = -0x80;
    // 0x511d7b and 0x511d7e: the U (+0x6c) and the V (+0x70).
    actor.textureU = 0.0f;
    actor.textureV = 0.0f;
    // 0x511878: rebuilding the owner erases the previous raven.
    actor.darkSpirit.reset();
    // 0x511872: +0x364 at 0 without 0x414340, so +0x360 stays as it was.
    actor.layer.reset();
}
} // namespace

bool IsNewFace(std::uint16_t type)
{
    constexpr std::uint16_t FirstNewFace = 0xa8;
    constexpr std::uint16_t LastNewFace = 0xac;
    return type >= FirstNewFace && type <= LastNewFace;
}

bool HeightSkippedIn(Terrain::CullingScene scene)
{
    constexpr std::uint8_t InitialScene = 0;
    constexpr std::uint8_t LoadingScene = 1;
    constexpr std::uint8_t WorldLoadingScene = 3;
    const auto number = static_cast<std::uint8_t>(scene);
    return number == InitialScene || number == LoadingScene || number == WorldLoadingScene;
}

double ConstructionHeight(const ActorConstructionContext& context, float x, float y, bool previousHad1c15, bool safe)
{
    double height = HeightSkippedIn(context.scene)
                        ? 0.0
                        : Terrain::TerrainHeightAt(*context.terrain.heights, *context.terrain.attributes, x, y,
                                                   context.fixedHeight);
    if (context.map != NoMap && previousHad1c15 && !safe)
    {
        height += context.map == FirstLargeOffsetMap || context.map == SecondLargeOffsetMap ? LargePetHeightOffset
                                                                                            : PetHeightOffset;
    }
    return height;
}

Core::Expected<void, std::string> ConstructActor(Kernel::ActorState& actor, std::uint16_t type,
                                                 const ActorPlacement& placement,
                                                 const ActorConstructionContext& context, Models::ClothWorld& cloth)
{
    const bool previousHad1c15 = actor.previousHad1c15;
    SetTheStartingState(actor, type);
    WriteTheCell(actor, placement, cloth.random);
    AccessTheCell(actor.cellYAccesses, actor.cellY, actor.cellYLeft, cloth.random);
    AccessTheCell(actor.cellXAccesses, actor.cellX, actor.cellXLeft, cloth.random);
    if (!context.terrain.attributes)
    {
        return Core::Unexpected(std::string("0x511b9f: faltan los atributos del mundo cargado"));
    }
    const std::size_t cell = Data::Terrain::CellIndex(placement.x, placement.y);
    const bool safe = (context.terrain.attributes->cells[cell] & SafeAttribute) != 0;
    actor.safeZone = static_cast<std::uint8_t>(safe ? 1 : 0);
    AccessTheCell(actor.cellXAccesses, actor.cellX, actor.cellXLeft, cloth.random);
    AccessTheCell(actor.cellYAccesses, actor.cellY, actor.cellYLeft, cloth.random);
    const float x = static_cast<float>(placement.x) * CellSize + CellCenter;
    const float y = static_cast<float>(placement.y) * CellSize + CellCenter;
    actor.stepStamp = ReadTheStepCounter(cloth);
    if (!HeightSkippedIn(context.scene) && !context.terrain.heights)
    {
        return Core::Unexpected(std::string("0x51225f: faltan las alturas del mundo cargado"));
    }
    // The FSTP to float comes after adding the offset.
    actor.position = {x, y, static_cast<float>(ConstructionHeight(context, x, y, previousHad1c15, safe))};
    actor.angles = {0.0f, 0.0f, placement.angle};
    // 0x5123c4..0x5123ff: frees its own bones (+0x120) and asks for others, uninitialized (Q-C318).
    actor.bodyPose.clear();
    ClearTheParts(actor);
    SetTheHandBones(actor, type);
    ++actor.revision;
    return {};
}
} // namespace Client::Actors
