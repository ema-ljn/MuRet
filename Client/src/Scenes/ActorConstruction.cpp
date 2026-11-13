#include "Scenes/ActorConstruction.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "World/TerrainCulling.h"
#include "World/TerrainHeight.h"
#include "World/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Scenes
{
namespace
{
constexpr std::uint16_t SafeAttribute = 1;
constexpr double PetHeightOffset = 30.0;      // 0x6b8858
constexpr double LargePetHeightOffset = 90.0; // 0x6b8948
constexpr std::int32_t FirstLargeOffsetMap = 8;
constexpr std::int32_t SecondLargeOffsetMap = 10;
constexpr std::int32_t NoMap = -1;
// 0x6b86b4 y 0x6b86b0: el centro de la celda, X · 100 + 50.
constexpr float CellSize = 100.0f;
constexpr float CellCenter = 50.0f;
constexpr float PlayerScale = 0.9f;
constexpr std::uint8_t ConstructedPk = 3;
constexpr std::uint16_t NoWord376 = 0xffff;
constexpr std::uint8_t PlayerStartAction = 1;
constexpr std::uint16_t ConstructedMovementSpeed = 10;
constexpr std::uint8_t RightHandBone = 0x21;
constexpr std::uint8_t LeftHandBone = 0x2a;

// La caja de 0x511290 (+0x124..+0x138), relativa a la posición.
struct ConstructionBox
{
    glm::vec3 min;
    float maxX;
    float maxY;
    float top;
};

// 0x512270..0x5123ba: el piso en (−60, −60, 0) y, por la tabla de bytes de 0x512700 sobre la de saltos de 0x5126e4
// con el tipo − 0x154 (de 0 a 0xb6, sin signo), el resto. Fuera de la tabla, el 0x5123a9.
ConstructionBox BoxOf(std::uint16_t type)
{
    constexpr glm::vec3 Floor{-60.0f, -60.0f, 0.0f};
    switch (type)
    {
    case 0x198:
    case 0x199:
    case 0x19a:
    case Data::Models::PlayerSkeletonModel:
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

void PutTheBox(ActorState& actor, const ConstructionBox& box)
{
    actor.boundsMin = box.min;
    actor.boundsMaxX = box.maxX;
    actor.boundsMaxY = box.maxY;
    actor.boundsTop = box.top;
}

// Los huesos de las manos de un caso: el de la derecha (+0x2bd) y el de la izquierda (+0x2dd), si los escribe.
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

// 0x512490..0x5126a8: la tabla de bytes de 0x512824 sobre la de saltos de 0x5127b8 con el tipo − 0x152 (de 0 a 0x48,
// sin signo). nullopt: el caso es el 0x5126b2.
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
        return RightOnly(23); // 0x51257f: la izquierda queda como estaba
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
        return Both(RightHandBone, LeftHandBone); // 0x5126ce, sin 0x43a250 ni 0x42d7f0
    default:
        return std::nullopt;
    }
}

// 0x43a250: los huesos de los tipos 0x192..0x197 (la tabla de 0x43a2d4); el 0x193 y los demás devuelven 0.
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

// 0x42d7f0: la caja del 0x19b, +0x38f en 0 y 1 (sin los huesos), y la del 0x1f8 con 0 (con los huesos).
bool SetTheSiegeBox(ActorState& actor, std::uint16_t type)
{
    constexpr std::uint16_t SiegeGate = 0x19b;
    constexpr std::uint16_t SiegeStatue = 0x1f8;
    if (type == SiegeGate)
    {
        PutTheBox(actor, {{-140.0f, -140.0f, 0.0f}, 140.0f, 140.0f, 300.0f});
        actor.byte38f = 0;
        return true;
    }
    if (type == SiegeStatue)
        PutTheBox(actor, {{-200.0f, -50.0f, -100.0f}, 200.0f, 50.0f, 100.0f});
    return false;
}

void PutTheHandBones(ActorState& actor, const HandBones& bones)
{
    actor.hands[0].bone = bones.right;
    if (bones.left)
        actor.hands[1].bone = *bones.left;
}

// 0x512490..0x5126d5: los huesos de las manos por el tipo; el 0x5126b2 prueba 0x43a250 y 0x42d7f0 y, si ninguno
// devuelve 1, cuelga las manos del 33 y del 42.
void SetTheHandBones(ActorState& actor, std::uint16_t type)
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

// 0x4ec200 no corre acá: 0x511290 deja las dos acciones en 1 y el frame en 0.
void ResetTheAnimation(ActorState& actor)
{
    const std::uint8_t action = actor.type == Data::Models::PlayerSkeletonModel ? PlayerStartAction : 0;
    actor.animation.action = action;
    actor.animation.previousAction = action;
    actor.animation.frame = 0.0f;
    actor.animation.previousKey = 0;
}

// 0x512455..0x512463 y los lazos de antes: la cabeza y las cinco del cuerpo sin ítem, con nivel y bytes
// en 0; las manos sin ítem con el nivel y el +3 en 0; las alas, la mascota y +0x338 solo el modelo.
void ClearTheParts(ActorState& actor)
{
    actor.head = NoItemModel;
    for (ActorItemPart& part : actor.body)
    {
        part.model = NoItemModel;
        part.level = 0;
        part.flags1b = 0;
        part.flags1c = 0;
    }
    for (ActorItemPart& hand : actor.hands)
    {
        hand.model = NoItemModel;
        hand.level = 0;
        hand.flags1b = 0;
    }
    actor.wings.model = NoItemModel;
    actor.pet.model = NoItemModel;
    actor.previousHad1c15 = false;
}

// Un acceso a una de las dos entradas de la celda con `value` en claro mientras está abierta: lo que deja en la
// variable al cerrar va a `left` (ActorState::cellXLeft y cellYLeft).
void AccessTheCell(std::uint8_t& accesses, std::uint8_t value, std::uint32_t& left, Render::Models::CrtRandom& random)
{
    left = value;
    const Render::Models::LogicalAccess access(accesses, random, Render::Models::SmallLogicalCloseRandoms, left);
}

// La celda: escribe X y después Y, cada una en su acceso (0x412890 al cerrar), y las copias en claro.
void WriteTheCell(ActorState& actor, const ActorPlacement& placement, Render::Models::CrtRandom& random)
{
    AccessTheCell(actor.cellXAccesses, placement.x, actor.cellXLeft, random);
    AccessTheCell(actor.cellYAccesses, placement.y, actor.cellYLeft, random);
    actor.cellX = placement.x;
    actor.cellY = placement.y;
    actor.targetX = placement.x;
    actor.targetY = placement.y;
}

// El contador de pasos (0x7d3a0f0), con la cuenta de accesos que lleva la tela.
std::int32_t ReadTheStepCounter(Render::Models::ClothWorld& cloth)
{
    const std::int32_t steps = cloth.logicSteps;
    if (cloth.openCounterAccesses == 0)
    {
        for (std::uint16_t i = 0; i < Render::Models::SmallLogicalCloseRandoms; ++i)
        {
            static_cast<void>(cloth.random.Next());
        }
    }
    return steps;
}

// Lo que 0x511290 deja fijo en un 0x20a, fuera de la celda, la posición y las partes.
void SetTheStartingState(ActorState& actor, std::uint16_t type)
{
    actor.alive = true;
    actor.type = type;
    actor.subtype = 0;
    actor.scale = PlayerScale;
    PutTheBox(actor, BoxOf(type));
    actor.alphaTarget = 1.0f;
    actor.alpha = 1.0f;
    // 0x511bdf y 0x512499: el alpha de a 0,05 (0x4e9980) y la luz del blendMesh en 1,0.
    actor.alphaMode = 1;
    actor.blendMeshLight = 1.0f;
    if (!IsNewFace(type))
    {
        actor.characterClass = 0;
    }
    ResetTheAnimation(actor);
    actor.seen = false;
    actor.dying = 0;
    actor.byte88 = 1;
    // 0x511d51, con el ESI en 0 de 0x511c36.
    actor.flags78 = 0;
    actor.byte3e3 = 0;
    actor.word376 = NoWord376;
    actor.movement = 0;
    // 0x511bc0/0x511bc7: no escribe progress ni el contenido del camino.
    actor.path.count = 0;
    actor.path.current = 0;
    actor.movementPhase = 0.0f;                         // 0x511c44
    actor.baseMovementSpeed = ConstructedMovementSpeed; // 0x511d48
    actor.byte369 = 0;
    actor.byte370 = 0;
    actor.byte372 = 0; // 0x511ccf
    actor.byte374 = 0; // 0x511c13
    actor.byte42c = 1; // 0x511c92
    actor.word1f2 = NoItemModel;
    actor.byte399 = 0;
    actor.byte80 = 0;
    actor.run = 0;
    actor.controlCode = 0;
    actor.pk = ConstructedPk;
    // 0x511c99, en el mismo bloque que el PK y la escala: el +0x358 que después escribe 0x40d010 en el
    // héroe publicado.
    actor.byte358 = 0;
    // 0x511ca0: +0x35c, con el ESI en 0 de 0x511c36.
    actor.counter35c = 0;
    actor.level = 0;
    actor.guildStatus = 0xff;
    actor.light = {};
    actor.drawnLight = {};
    actor.byte390 = 0; // 0x51186b
    // 0x511be6..0x511d45 y 0x51248d.
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
    // 0x511878: reconstruir el dueño borra el cuervo anterior.
    actor.darkSpirit.reset();
    // 0x511872: +0x364 en 0 sin 0x414340, así que +0x360 queda como estaba.
    actor.layer.reset();
}
} // namespace

bool IsNewFace(std::uint16_t type)
{
    constexpr std::uint16_t FirstNewFace = 0xa8;
    constexpr std::uint16_t LastNewFace = 0xac;
    return type >= FirstNewFace && type <= LastNewFace;
}

bool HeightSkippedIn(World::CullingScene scene)
{
    constexpr std::uint8_t InitialScene = 0;
    constexpr std::uint8_t LoadingScene = 1;
    constexpr std::uint8_t WorldLoadingScene = 3;
    const auto number = static_cast<std::uint8_t>(scene);
    return number == InitialScene || number == LoadingScene || number == WorldLoadingScene;
}

double ConstructionHeight(const ActorConstructionContext& context, float x, float y, bool previousHad1c15, bool safe)
{
    double height =
        HeightSkippedIn(context.scene)
            ? 0.0
            : World::TerrainHeightAt(*context.terrain.heights, *context.terrain.attributes, x, y, context.fixedHeight);
    if (context.map != NoMap && previousHad1c15 && !safe)
    {
        height += context.map == FirstLargeOffsetMap || context.map == SecondLargeOffsetMap ? LargePetHeightOffset
                                                                                            : PetHeightOffset;
    }
    return height;
}

Core::Expected<void, std::string> ConstructActor(ActorState& actor, std::uint16_t type, const ActorPlacement& placement,
                                                 const ActorConstructionContext& context,
                                                 Render::Models::ClothWorld& cloth)
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
    // El FSTP a float va después de la suma del corrimiento.
    actor.position = {x, y, static_cast<float>(ConstructionHeight(context, x, y, previousHad1c15, safe))};
    actor.angles = {0.0f, 0.0f, placement.angle};
    // 0x5123c4..0x5123ff: suelta los huesos propios (+0x120) y pide otros sin iniciar (Q-C318).
    actor.bodyPose.clear();
    ClearTheParts(actor);
    SetTheHandBones(actor, type);
    ++actor.revision;
    return {};
}
} // namespace Client::Scenes
