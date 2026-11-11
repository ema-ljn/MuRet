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

#include <cstddef>
#include <cstdint>
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

// La celda: escribe X y después Y, cada una en su acceso (0x412890 al cerrar), y las copias en claro.
void WriteTheCell(ActorState& actor, const ActorPlacement& placement, Render::Models::CrtRandom& random)
{
    {
        const Render::Models::LogicalAccess access(actor.cellXAccesses, random,
                                                   Render::Models::SmallLogicalCloseRandoms);
    }
    {
        const Render::Models::LogicalAccess access(actor.cellYAccesses, random,
                                                   Render::Models::SmallLogicalCloseRandoms);
    }
    actor.cellX = placement.x;
    actor.cellY = placement.y;
    actor.targetX = placement.x;
    actor.targetY = placement.y;
}

// Un acceso de lectura a una de las dos entradas de la celda.
void ReadTheCell(std::uint8_t& accesses, Render::Models::CrtRandom& random)
{
    const Render::Models::LogicalAccess access(accesses, random, Render::Models::SmallLogicalCloseRandoms);
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
    // 0x5126b2..0x5126d5: 0x43a250 (0x192..0x197) y 0x42d7f0 (0x19b y 0x1f8) no toman el 0x20a, y las
    // manos cuelgan del 33 y del 42.
    if (type == Data::Models::PlayerSkeletonModel)
    {
        actor.hands[0].bone = RightHandBone;
        actor.hands[1].bone = LeftHandBone;
    }
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
    actor.byte374 = 0; // 0x511c13
    actor.word1f2 = NoItemModel;
    actor.byte399 = 0;
    actor.byte80 = 0;
    actor.run = 0;
    actor.controlCode = 0;
    actor.pk = ConstructedPk;
    // 0x511c99, en el mismo bloque que el PK y la escala: el +0x358 que después escribe 0x40d010 en el
    // héroe publicado.
    actor.byte358 = 0;
    actor.level = 0;
    actor.guildStatus = 0xff;
    actor.light = {};
    actor.drawnLight = {};
    // 0x511878: reconstruir el dueño borra el cuervo anterior.
    actor.darkSpirit.reset();
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
    if (type != Data::Models::PlayerSkeletonModel && !IsNewFace(type))
    {
        return Core::Unexpected("0x511290: el tipo " + std::to_string(type) + " no está leído (0x20a y 0xa8..0xac)");
    }
    const bool previousHad1c15 = actor.previousHad1c15;
    SetTheStartingState(actor, type);
    WriteTheCell(actor, placement, cloth.random);
    ReadTheCell(actor.cellYAccesses, cloth.random);
    ReadTheCell(actor.cellXAccesses, cloth.random);
    if (!context.terrain.attributes)
    {
        return Core::Unexpected(std::string("0x511b9f: faltan los atributos del mundo cargado"));
    }
    const std::size_t cell = Data::Terrain::CellIndex(placement.x, placement.y);
    const bool safe = (context.terrain.attributes->cells[cell] & SafeAttribute) != 0;
    actor.safeZone = static_cast<std::uint8_t>(safe ? 1 : 0);
    ReadTheCell(actor.cellXAccesses, cloth.random);
    ReadTheCell(actor.cellYAccesses, cloth.random);
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
    ClearTheParts(actor);
    ++actor.revision;
    return {};
}
} // namespace Client::Scenes
