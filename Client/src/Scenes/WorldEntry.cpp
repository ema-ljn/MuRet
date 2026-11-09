#include "Scenes/WorldEntry.h"

#include "App/ServerSession.h"
#include "Core/Expected.h"
#include "Core/Log.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/WorldLoadIdentity.h"
#include "GameLogic/CharacterAppearance.h"
#include "Protocol/Character/CharacterSelectResult.h"
#include "Protocol/Character/InventoryList.h"
#include "Protocol/World.h"
#include "Protocol/Character/Opcodes.h"
#include "Render/Models/Cloth.h"
#include "Render/ModelBatch.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/ActorEquipment.h"
#include "Scenes/ActorActions.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/GameData.h"
#include "Scenes/GameModels.h"
#include "Scenes/DaviasLeafTextureSet.h"
#include "Scenes/LorenciaLeafTextureSet.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldResources.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/IWorldObjectCreationServices.h"
#include "World/ItemState.h"
#include "World/ModelEffects.h"
#include "World/TerrainCulling.h"
#include "World/TerrainHeight.h"
#include "World/WorldObjects.h"
#include "World/WorldTerrain.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Client::Scenes
{
namespace
{
using Render::Models::LogicalAccess;
using Render::Models::OwnRecordCloseRandoms;
using Render::Models::SmallLogicalCloseRandoms;
constexpr std::uint16_t PlayerModel = 0x20a;
constexpr std::int16_t ArrivalEffect = 0x61f;
constexpr std::uint16_t HeightOffsetPet = 0x1c15;

class CreationServices final : public World::IWorldObjectCreationServices
{
public:
    CreationServices(WorldResources& resources, Render::Models::CrtRandom& random)
        : m_resources(resources), m_random(random)
    {
    }
    bool CreateModelEffect(const World::WorldObjectModelEffectRequest& request,
                           World::WorldObject& source) noexcept override
    {
        if ((request.type != World::DragonEffect && request.type != ArrivalEffect && request.type != 0x135 &&
             request.type != 0x142 && request.type != 0x143 && request.type != 0x144) ||
            request.owner > std::numeric_limits<std::uint32_t>::max())
            return false;
        static_cast<void>(m_resources.effects.Create(request.type, request.position, source.transform.angles,
                                                     source.light, request.scale, m_random,
                                                     static_cast<std::uint32_t>(request.owner)));
        return true; // Pool lleno también ejecutó el binding nativo.
    }

private:
    WorldResources& m_resources;
    Render::Models::CrtRandom& m_random;
};

std::int32_t ReadCell(ActorState& actor, bool x, Render::Models::CrtRandom& random)
{
    const LogicalAccess access(x ? actor.cellXDepth : actor.cellYDepth, random, SmallLogicalCloseRandoms);
    return std::bit_cast<std::int32_t>(x ? actor.cellX : actor.cellY);
}

void ClearActors(WorldState& world)
{
    for (ActorState& actor : world.Actors().first<ActorTraversalCount>())
    {
        if (actor.active && static_cast<std::int16_t>(actor.key) != world.Runtime().ownKey)
            actor.active = false;
        actor.spirit.reset();
        actor.cloth = {};
        actor.horse.reset();
    }
}

Core::Expected<void, std::string> LoadMap(SelectionState& selection, const SceneContext& context,
                                          const GameModels& gameModels)
{
    WorldState& world = selection.World();
    WorldRuntime& state = world.Runtime();
    WorldResources& resources = *world.Resources();
    if (context.modelBatch != nullptr)
    {
        const auto invalidated = context.modelBatch->InvalidateTextureKnowledge();
        if (!invalidated.HasValue())
            return Core::Unexpected("hojas: no se pueden mutar recursos del lote: " + invalidated.Error().message());
    }
    else if (context.leafDrawing != nullptr)
        return Core::Unexpected(std::string("hojas: falta el lote canónico del provider activo"));
    selection.BorrowTerrain(state.map, nullptr);
    resources.Release();
    if (selection.Scene() != World::CullingScene::CharacterSelect)
        ClearActors(world);
    const auto loadedModels = resources.models.Load(context.dataRoot, static_cast<std::uint8_t>(state.map),
                                                    context.textures, context.log, &gameModels.TextureSlots());
    if (!loadedModels.HasValue())
        return loadedModels;
    CreationServices creation(resources, context.cloth->random);
    World::WorldTerrainLoad load{
        .random = &context.cloth->random,
        .objectsBeforeHeights = [&]() -> Core::Expected<void, std::string>
        {
            auto records = World::LoadWorldObjects(context.dataRoot, static_cast<std::uint8_t>(state.map));
            if (!records.HasValue())
                return Core::Unexpected(records.Error());
            resources.objectRecords = std::move(records).Value();
            World::WorldObjectCreationContext input{.map = state.map,
                                                    .scene = World::ObjectScene::Other,
                                                    .registrations = resources.registrations,
                                                    .services = &creation,
                                                    .random = &context.cloth->random,
                                                    .state7239Ec = state.state7239Ec ? &*state.state7239Ec : nullptr};
            auto result = World::CreateWorldObjects(resources.objectRecords, input);
            resources.objects = std::move(result.objects);
            state.map = input.map;
            if (result.failure)
                return Core::Unexpected(std::format("creación de objeto {} no integrada (causa {})",
                                                    result.failure->recordIndex,
                                                    static_cast<int>(result.failure->error)));
            return {};
        }};
    std::vector<std::string> skipped;
    const auto terrain =
        resources.terrain.Load(context.dataRoot, static_cast<std::uint8_t>(state.map), skipped, std::move(load));
    for (const std::string& message : skipped)
        context.log.Warning("{}", message);
    if (!terrain.HasValue())
        return terrain;
    state.map = *resources.terrain.LoadedMap();
    resources.walls.cells = resources.terrain.Terrain()->attributes->cells;
    selection.BorrowTerrain(state.map, resources.terrain.Terrain());
    if (state.map == 0 || state.map == 2)
    {
        // 61a560: después de terreno/luz y antes de447d70; no se crea otro registro de texturas.
        if (context.worldTextureModels == nullptr)
            return Core::Unexpected(
                std::string("hojas: falta el préstamo de GameModels para cargar las hojas del mundo"));
        if (context.worldTextureModels != &gameModels)
            return Core::Unexpected(
                std::string("hojas: el préstamo de GameModels no coincide con el dueño de entrada"));
        // 61a560 no corta por un fallo de textura: conserva el último upload exitoso por slot.
        // La ausencia de una fila requerida se informa al alcanzar el lookup del dibujo.
        const auto& leafTextures = state.map == 0 ? LorenciaLeafTextureSet : DaviasLeafTextureSet;
        static_cast<void>(
            context.worldTextureModels->LoadTextures(context.dataRoot, leafTextures, context.textures, context.log));
    }
    // 447d70 selector0/global0: sin llamadas ni stores. Las familias elegidas necesitan su controller.
    if ((state.map >= 9 && state.map <= 23) && state.map != 10)
        return Core::Unexpected(std::string("falta el controlador de evento seleccionado por 447d70"));
    return {};
}

void ConstructActor(ActorState& actor, std::uint8_t x, std::uint8_t y, std::uint8_t direction, WorldState& world,
                    const SceneContext& context)
{
    Render::Models::ClothWorld& cloth = *context.cloth;
    {
        const LogicalAccess access(actor.cellXDepth, cloth.random, SmallLogicalCloseRandoms);
        actor.cellX = x;
    }
    {
        const LogicalAccess access(actor.cellYDepth, cloth.random, SmallLogicalCloseRandoms);
        actor.cellY = y;
    }
    actor.targetX = x;
    actor.targetY = y;
    const std::int32_t safeY = ReadCell(actor, false, cloth.random);
    const std::int32_t safeX = ReadCell(actor, true, cloth.random);
    const World::WorldTerrain& terrain = *world.Resources()->terrain.Terrain();
    actor.safeZone =
        (terrain.attributes
             ->cells[Data::Terrain::CellIndex(static_cast<std::uint8_t>(safeX), static_cast<std::uint8_t>(safeY))] &
         1u) != 0;
    actor.active = true;
    actor.dying = false;
    actor.kind = 1;
    actor.type = PlayerModel;
    actor.appearance.characterClass = 0;
    actor.hideBody = false;
    actor.alpha = 1.0f;
    actor.targetAlpha = 1.0f;
    actor.animation.body = {.action = 1, .frame = 0.0f, .previousAction = 1, .previousKey = 0};
    actor.position.x = static_cast<float>(ReadCell(actor, true, cloth.random) * 100 + 50);
    actor.position.y = static_cast<float>(ReadCell(actor, false, cloth.random) * 100 + 50);
    std::int32_t stamp = 0;
    {
        const LogicalAccess access(cloth.openCounterAccesses, cloth.random, SmallLogicalCloseRandoms);
        stamp = cloth.logicSteps;
    }
    actor.logicStamp = stamp;
    double height = World::TerrainHeightAt(*terrain.heights, *terrain.attributes, actor.position.x, actor.position.y,
                                           context.camera->fixedHeight);
    if (world.Runtime().map != -1 && actor.pet.model == HeightOffsetPet && !actor.safeZone)
        height += world.Runtime().map == 8 || world.Runtime().map == 10 ? 90.0 : 30.0;
    actor.position.z = static_cast<float>(height);
    actor.angles = {0.0f, 0.0f, static_cast<float>((static_cast<int>(direction) - 1) * 45)};
    for (ActorEquipmentPart& part : actor.hands)
    {
        part.model = 0xffff;
        part.level = 0;
        part.flags1b = 0;
    }
    for (ActorEquipmentPart& part : actor.body)
        part = {.model = 0xffff};
    actor.wings.model = 0xffff;
    actor.pet.model = 0xffff;
    actor.previousHad1c15 = false;
    ++actor.revision;
}

} // namespace

Core::Expected<void, std::string> ApplyWorldEntry(const Protocol::Character::CharacterSelectResult& reply,
                                                  SelectionState& selection, const SceneContext& context,
                                                  const GameData& data, const GameModels& models,
                                                  App::ServerSession& session)
{
    WorldState& world = selection.World();
    if (world.Resources() == nullptr || context.cloth == nullptr || context.camera == nullptr)
        return Core::Unexpected(std::string("faltan owner de mapa, RNG compartido o cámara de entrada"));
    auto& random = context.cloth->random;
    WorldRuntime& state = world.Runtime();
    {
        const LogicalAccess record(state.ownRecordDepth, random, OwnRecordCloseRandoms);
        world.OwnCharacter().ApplyStats(reply);
    }
    context.log.Warning("Q-C144: F3/03 no contiene los WORD34..37; se sustituyen por cero");
    state.map = Data::MapAtWorldLoadStart(reply.map, reply.map);
    if (session.PlayerIndex())
        state.ownKey = *session.PlayerIndex();
    const auto loaded = LoadMap(selection, context, models);
    if (!loaded.HasValue())
        return loaded;
    // 4c41e1: consulta el mapa efectivo después de61a560;447d70 es NoAction para selector/global34.
    if (state.map == 34)
        session.RequestCrywolfState();
    const std::size_t index = static_cast<std::size_t>(random.Next() % static_cast<int>(ActorTraversalCount));
    ActorState& hero = world.Actors()[index];
    state.actorConstructionIndex = index;
    ConstructActor(hero, reply.x, reply.y, reply.direction, world, context);
    hero.key = static_cast<std::uint16_t>(state.ownKey);
    {
        const LogicalAccess record(state.ownRecordDepth, random, OwnRecordCloseRandoms);
        hero.appearance.characterClass = world.OwnCharacter().characterClass;
        hero.appearance.skin = 0;
    }
    hero.pk = reply.pk;
    hero.controlCode = reply.controlCode;
    const auto equipped = ApplyActorEquipment(hero, world, *context.cloth);
    if (!equipped.HasValue())
        return equipped;
    state.hero = index;
    {
        const LogicalAccess record(state.ownRecordDepth, random, OwnRecordCloseRandoms);
        std::ranges::copy_n(world.OwnCharacter().name.begin(), Protocol::Character::NameSize, hero.name.begin());
        world.OwnCharacter().ClearEquipmentForEntry();
    }
    hero.name.back() = std::byte{0};
    static_cast<void>(world.Resources()->effects.Create(ArrivalEffect, hero.position, hero.angles, {}, 0.0f,
                                                        static_cast<std::uint32_t>(index + 1)));
    session.PublishWorldEntry();
    state.enteredWorld = true;
    {
        const LogicalAccess access(state.warpDepth, random, SmallLogicalCloseRandoms);
        state.warpCounter = 30;
    }
    state.mouseUpdateTime = 0;
    state.mouseUpdateTimeMax = 6;
    if (hero.appearance.leftHand && hero.appearance.leftHand->model == 0x1c17 &&
        (hero.appearance.characterClass & 7u) == 4)
    {
        const auto& terrain = *world.Resources()->terrain.Terrain();
        const double ground = World::TerrainHeightAt(*terrain.heights, *terrain.attributes, hero.position.x,
                                                     hero.position.y, context.camera->fixedHeight);
        hero.spirit = CreateDarkSpirit(std::bit_cast<std::int32_t>(hero.cellX), std::bit_cast<std::int32_t>(hero.cellY),
                                       ground, random);
    }
    static_cast<void>(world.Resources()->effects.Create(ArrivalEffect, hero.position, hero.angles, {}, 0.0f,
                                                        static_cast<std::uint32_t>(index + 1)));
    hero.alpha = 0.0f;
    state.targets.fill(-1);
    hero.moving = false;
    return SetActorStop(hero, world, data, models, *context.cloth);
}
Core::Expected<void, std::string> ApplyInventory(const Protocol::Character::InventoryList& reply,
                                                 SelectionState& selection, const SceneContext& context,
                                                 const GameData& data)
{
    WorldState& world = selection.World();
    if (context.cloth == nullptr)
        return Core::Unexpected(std::string("falta el RNG compartido de inventario"));
    auto& cloth = *context.cloth;
    {
        const LogicalAccess access(world.Runtime().ownRecordDepth, cloth.random, OwnRecordCloseRandoms);
        world.OwnCharacter().ClearEquipmentForInventory();
    }
    for (const auto grid :
         {std::span<World::ItemState>(world.Inventory()), std::span<World::ItemState>(world.SecondaryInventory())})
        for (auto& item : grid)
        {
            item.id = 0xffff;
            item.occupied = 0;
            item.flags1b = 0;
        }
    // 443d40/421d60 tienen una ruta vacía acreditada cuando no hay auxiliares vivos.
    // La tabla de diez y su recurso transitivo aún necesitan un owner productivo.
    const auto& initialHero = world.Actors()[world.Runtime().hero];
    if (initialHero.horse || initialHero.spirit)
        return Core::Unexpected(std::string("faltan bindings de auxiliares 443d40/421d60 para F3/10"));
    for (const auto& entry : reply.items)
    {
        const auto b = [&](std::size_t index) { return std::to_integer<std::uint8_t>(entry.item[index]); };
        const auto id = static_cast<std::uint16_t>(b(0) | ((b(3) & 0x80u) << 1) | ((b(5) & 0xf0u) << 5));
        if (id == 0x1fff)
            continue;
        if (!data.items)
            return Core::Unexpected(std::format("falta Item.bmd para el ID de inventario {}", id));
        const auto metadata = data.items->table.Record(id);
        if (!metadata)
            return Core::Unexpected(std::string("falta metadata del ID de inventario"));
        const auto initialize = [&](World::ItemState& item, bool occupied) -> Core::Expected<void, std::string>
        {
            item.id = id;
            item.durability = b(2);
            item.flags1b = b(3);
            item.flags1c = b(4);
            item.occupied = occupied ? 1u : 0u;
            if (id == 0x1a04)
                return Core::Unexpected(std::string("falta fuente runtime y tabla de 4225d0 para ID1a04"));
            if (id == 0x180b && ((b(1) >> 3) & 15u) > 6)
                return Core::Unexpected(std::string("falta B3 completo de 5bc740 para ID180b/L>6"));
            // 54bf40 toma S+e: es el nivel vivo publicado por6342c0, no otro contexto por copiar.
            World::InitializeItemState(item, *metadata, {.b1 = b(1), .b3 = b(3), .b4 = b(4)},
                                       world.OwnCharacter().level, {});
            return {};
        };
        if (entry.slot < OwnEquipmentSlotCount)
        {
            // 5bc740 conserva R abierto durante54bf40,512a30 y los efectos de la entrada.
            const LogicalAccess access(world.Runtime().ownRecordDepth, cloth.random, OwnRecordCloseRandoms);
            const auto result = initialize(world.OwnCharacter().equipment[entry.slot], true);
            if (!result.HasValue())
                return result;
            const auto equipped = ApplyActorEquipment(world.Actors()[world.Runtime().hero], world, cloth);
            if (!equipped.HasValue())
                return equipped;
            if ((entry.slot == 8 && (id == 0x1a00 || id == 0x1a02 || id == 0x1a03 || id == 0x1a04 || id == 0x1a25)) ||
                (entry.slot == 1 && id == 0x1a05) || ((entry.slot == 10 || entry.slot == 11) && id == 0x1a14))
                return Core::Unexpected(std::string("faltan efectos contextuales de equipo de 5bc740"));
        }
        else
        {
            const std::size_t start = entry.slot < 76 ? entry.slot - 12u : entry.slot - 76u;
            const auto grid = entry.slot < 76 ? std::span<World::ItemState>(world.Inventory())
                                              : std::span<World::ItemState>(world.SecondaryInventory());
            for (std::size_t row = 0; row < std::to_integer<std::uint8_t>((*metadata)[0x25]); ++row)
                for (std::size_t column = 0; column < std::to_integer<std::uint8_t>((*metadata)[0x24]); ++column)
                {
                    const std::size_t index = start + row * 8 + column;
                    if (index >= grid.size())
                        return Core::Unexpected(
                            std::string("5bc740 escribe fuera de la grilla tipada; falta política ADR"));
                    auto& item = grid[index];
                    item.gridColumn = static_cast<std::uint8_t>(start % 8);
                    item.gridRow = static_cast<std::uint8_t>(start / 8);
                    const auto result = initialize(item, row == 0 && column == 0);
                    if (!result.HasValue())
                        return result;
                }
        }
    }
    return {};
}
namespace
{
void RefreshHeight(ActorState& actor, WorldState& world, const SceneContext& context)
{
    const auto& terrain = *world.Resources()->terrain.Terrain();
    double height = World::TerrainHeightAt(*terrain.heights, *terrain.attributes, actor.position.x, actor.position.y,
                                           context.camera->fixedHeight);
    if (world.Runtime().map != -1 && actor.pet.model == 0x1c15 && actor.safeZone == 0)
        height += world.Runtime().map == 8 || world.Runtime().map == 10 ? 90.0 : 30.0;
    actor.position.z = static_cast<float>(height);
}
} // namespace

Core::Expected<void, std::string> ApplyTeleport(const Protocol::World::Teleport& reply, SelectionState& selection,
                                                const SceneContext& context, const GameData& data,
                                                const GameModels& models, App::ServerSession& session)
{
    WorldState& world = selection.World();
    if (world.Resources() == nullptr || !world.Resources()->terrain.IsLoaded() || context.cloth == nullptr ||
        context.camera == nullptr)
        return Core::Unexpected(std::string("faltan terreno, RNG o cámara para teleport"));
    auto& runtime = world.Runtime();
    auto& random = context.cloth->random;
    ActorState& retained = world.Actors()[runtime.hero];
    {
        const LogicalAccess access(retained.cellXDepth, random, SmallLogicalCloseRandoms);
        retained.cellX = reply.x;
    }
    {
        const LogicalAccess access(retained.cellYDepth, random, SmallLogicalCloseRandoms);
        retained.cellY = reply.y;
    }
    retained.position.x = static_cast<float>((static_cast<double>(ReadCell(retained, true, random)) + 0.5) * 100.0);
    retained.position.y = static_cast<float>((static_cast<double>(ReadCell(retained, false, random)) + 0.5) * 100.0);
    RefreshHeight(retained, world, context);
    retained.angles.z = static_cast<float>((static_cast<int>(reply.direction) - 1) * 45);
    if (reply.gate == 0)
        return Core::Unexpected(std::string("teleport gate0: falta el efecto53d930 antes de la cola común"));
    world.ReleaseActorResources();
    const auto oldMap = runtime.map;
    if (runtime.map != reply.map)
    {
        runtime.map = Data::MapAtWorldLoadStart(reply.map, reply.map);
        const auto loaded = LoadMap(selection, context, models);
        if (!loaded.HasValue())
            return loaded;
        if (runtime.map == 34)
            session.RequestCrywolfState();
        if ((oldMap >= 18 && oldMap <= 23 && oldMap != runtime.map) || (runtime.map >= 18 && runtime.map <= 23))
        {
            const auto equipped = ApplyActorEquipment(world.Actors()[runtime.hero], world, *context.cloth);
            if (!equipped.HasValue())
                return equipped;
        }
        RefreshHeight(retained, world, context);
    }
    session.ConfirmTeleport();
    if (context.clock != nullptr)
        runtime.lastTeleportTick = static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(context.clock->Now()).count());
    runtime.warpPending = false;
    {
        const LogicalAccess access(runtime.warpDepth, random, SmallLogicalCloseRandoms);
        runtime.warpCounter = 30;
    }
    runtime.mouseUpdateTime = 0;
    runtime.mouseUpdateTimeMax = 6;
    static_cast<void>(world.Resources()->effects.Create(ArrivalEffect, retained.position, retained.angles, {}, 0.0f,
                                                        static_cast<std::uint32_t>(runtime.hero + 1)));
    retained.alpha = 0.0f;
    runtime.targets.fill(-1);
    world.Actors()[runtime.hero].moving = false;
    return SetActorStop(world.Actors()[runtime.hero], world, data, models, *context.cloth);
}

Core::Expected<void, std::string> ApplyRegen(const Protocol::World::Regen& reply, SelectionState& selection,
                                             const SceneContext& context, const GameData& data,
                                             const GameModels& models)
{
    WorldState& world = selection.World();
    if (world.Resources() == nullptr || !world.Resources()->terrain.IsLoaded() || context.cloth == nullptr ||
        context.camera == nullptr)
        return Core::Unexpected(std::string("faltan terreno, RNG o cámara para revivir"));
    auto& runtime = world.Runtime();
    auto& random = context.cloth->random;
    world.Actors()[runtime.hero].active = false;
    {
        const LogicalAccess access(runtime.ownRecordDepth, random, OwnRecordCloseRandoms);
        auto& own = world.OwnCharacter();
        own.life = reply.life;
        own.mana = reply.mana;
        own.bp = reply.bp;
        own.experience = reply.experience;
        own.zen = reply.zen;
    }
    for (auto& actor : world.Actors().first<ActorTraversalCount>())
        actor.active = false;
    if (runtime.actorConstructionIndex >= ActorWindowCount)
        return Core::Unexpected(std::string("índice de reconstrucción fuera del pool canónico"));
    ActorState& reconstructed = world.Actors()[runtime.actorConstructionIndex];
    const std::uint8_t pk = world.Actors()[runtime.hero].pk;
    ConstructActor(reconstructed, reply.x, reply.y, reply.direction, world, context);
    reconstructed.key = static_cast<std::uint16_t>(runtime.ownKey);
    {
        const LogicalAccess access(runtime.ownRecordDepth, random, OwnRecordCloseRandoms);
        reconstructed.appearance.skin = 0;
        reconstructed.appearance.characterClass = world.OwnCharacter().characterClass;
    }
    reconstructed.pk = pk;
    reconstructed.kind = 1;
    reconstructed.safeZone = 1;
    auto result = ApplyActorEquipment(reconstructed, world, *context.cloth);
    if (!result.HasValue())
        return result;
    result = SetActorStop(reconstructed, world, data, models, *context.cloth);
    if (!result.HasValue())
        return result;
    static_cast<void>(
        world.Resources()->effects.Create(ArrivalEffect, reconstructed.position, reconstructed.angles, {}, 0.0f,
                                          static_cast<std::uint32_t>(runtime.actorConstructionIndex + 1)));
    world.ReleaseActorResources();
    const auto oldMap = runtime.map;
    if (runtime.map != reply.map)
    {
        runtime.map = Data::MapAtWorldLoadStart(reply.map, reply.map);
        const auto loaded = LoadMap(selection, context, models);
        if (!loaded.HasValue())
            return loaded;
        if ((oldMap >= 18 && oldMap <= 23 && oldMap != runtime.map) || (runtime.map >= 18 && runtime.map <= 23))
        {
            const auto equipped = ApplyActorEquipment(world.Actors()[runtime.hero], world, *context.cloth);
            if (!equipped.HasValue())
                return equipped;
        }
        RefreshHeight(reconstructed, world, context);
    }
    // 4c4b80 no escribe W30 ni mandaF3/12. S yHero siguen siendo identidades separadas.
    return {};
}

} // namespace Client::Scenes
