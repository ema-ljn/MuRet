#include "Scenes/GameScene.h"
#include "Scenes/CrywolfDeathSpiritEffects.h"
#include "Scenes/ArrowEffects.h"
#include "Scenes/CastleWeaponEffects.h"
#include "World/SiegeStoneEffects.h"
#include "Scenes/DynamicWorldLight.h"
#include "Scenes/SelectionState.h"
#include "Scenes/GameParticles.h"
#include "Scenes/HandShine.h"
#include "Scenes/HighModelEffects.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/FaunaEffects.h"
#include "Scenes/GameCharacterEffects.h"
#include "Scenes/GameFloorItems.h"
#include "World/GameModelEffects.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/ParticleOwner.h"

#include "App/ServerSession.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Models/PlayerModels.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/CameraTour.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/GamePanels.h"
#include "Scenes/CastleWindows.h"
#include "Scenes/CastleSiegeReceive.h"
#include "Scenes/NpcWindows.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/GameCharacters.h"
#include "Scenes/HelperTable.h"
#include "Scenes/JointKernels.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SkillHits.h"
#include "Scenes/TerrainPolygonFrame.h"
#include "Scenes/WorldRuntime.h"
#include "World/GroundEffectDrawing.h"
#include "Scenes/WizardEffects.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/GroundMarks.h"
#include "World/TerrainHeight.h"
#include "World/WizardModelEffects.h"
#include "World/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace Client::Scenes
{
namespace
{
// timeGetTime() (0x6b8144) con el reloj de la escena, en ms, que lee el mensaje del nombre (0x4fcd82); sin reloj,
// vacío.
std::function<std::uint32_t()> SceneSystemTime(Core::IClock* clock)
{
    if (clock == nullptr)
        return {};
    return [clock]()
    { return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(clock->Now()).count()); };
}
} // namespace

Core::Expected<void, std::string> GameScene::StepTheFloorItems(float worldClock)
{
    const auto& terrain = StepTerrain();
    auto& floorItems = m_context.world->DrawTail().floorItems;
    if (std::ranges::any_of(floorItems, [](const FloorItemState& item) { return item.alive; }))
    {
        if (!terrain.heights || !terrain.attributes)
            return Core::Unexpected(std::string("0x608880: falta el terreno para los ítems del piso"));
        if (auto stepped =
                StepFloorItems(floorItems, {*terrain.heights, *terrain.attributes, m_context.cloth->random,
                                            m_context.world->Particles(), worldClock, CameraState().fixedHeight});
            !stepped.HasValue())
            return stepped;
    }
    return {};
}

Core::Expected<void, std::string> GameScene::StepTheCharacters(float worldClock, bool controlDown)
{
    auto& runtime = m_context.world->Runtime();
    const Render::Models::Skeleton* player =
        m_models != nullptr ? m_models->SkeletonOf(Data::Models::PlayerSkeletonModel) : nullptr;
    if (auto stepped = StepCharacters(CharacterStepContext{
            .world = *m_context.world,
            .random = m_context.cloth->random,
            .scene = SceneId::Game,
            .playerSkeleton = player,
            .items = m_items,
            .skills = m_skills,
            .models = m_models,
            .terrain = &StepTerrain(),
            .darkSpiritSkeleton = m_models ? m_models->SkeletonOf(DarkSpiritModel) : nullptr,
            .particles = &m_context.world->Particles(),
            .sprites = &m_sprites,
            .joints = m_context.world->DrawTail().joints,
            .worldClock = worldClock,
            .counter = m_context.cloth->logicSteps,
            .actionSpeeds = &m_context.world->ActionSpeeds(),
            .fixedHeight = CameraState().fixedHeight,
            .soundRequest = [&](std::int32_t sound) { runtime.RequestSound(sound); },
            .soundAt = [&](std::int32_t sound, const glm::vec3& origin) { runtime.RequestSound(sound, origin); },
            .send = m_session != nullptr
                        ? std::function<void(Protocol::Outgoing)>([session = m_session](Protocol::Outgoing packet)
                                                                  { session->SendChat(std::move(packet)); })
                        : std::function<void(Protocol::Outgoing)>{},
            .controlDown = controlDown,
            .texts = m_texts,
            .textMeasure = m_context.textMeasure,
            .screen = m_screen,
            .systemTime = SceneSystemTime(m_context.clock),
            // CL9 hito 11: el AF 02 de 0x4fbee8 por la sesión.
            .sendChaosCastleReposition = [this](std::uint8_t x, std::uint8_t y) -> Core::Expected<void, std::string>
            {
                if (m_session == nullptr)
                    return Core::Unexpected(std::string("0x4fbee8: no hay sesión para el AF 02"));
                return m_session->SendChaosCastleReposition(x, y);
            }});
        !stepped.HasValue())
    {
        return stepped;
    }
    return {};
}

Core::Expected<void, std::string>
GameScene::StepTheModelEffects(float worldClock, const std::function<double(float, float)>& terrainHeight)
{
    // La mariposa 0xae del 0x3c del 54 sigue viva en la espera de la entrada al juego: su altura y el
    // sprite 0x5ac de la tabla del frame.
    // timeGetTime (0x6b8544), del calco 0x14e de los Fenrir: el reloj de la escena en ms.
    const World::ModelEffectStepServices services{
        .terrainHeight = terrainHeight,
        .sprites = &m_sprites,
        .effects = &m_context.world->Map().effects,
        .lighting = &m_context.world->DynamicLighting().Lighting(),
        .systemTime =
            [this]()
        {
            return m_context.clock != nullptr
                       ? static_cast<std::uint32_t>(
                             std::chrono::duration_cast<std::chrono::milliseconds>(m_context.clock->Now()).count())
                       : 0U;
        },
        // 0x5f8290/0x7d0d348 y 0x6c9dd0: la celda y el mapa que lee el terremoto 0xfa para sus grietas.
        .terrainAttributes = StepTerrain().attributes.get(),
        .map = &m_context.world->Runtime().map,
        // 0x5227ca: la grieta 0xfb escribe el temblor de la cámara del paso lógico.
        .cameraShake = &CameraInputState().shake,
        // 0x41ed10(id, 0, 0): RequestSound lo despacha sin sacar rand().
        .sound = [this](std::int32_t id) { m_context.world->Runtime().RequestSound(id); },
        .joint = [this](const World::FishEffectRequest& request) -> Core::Expected<void, std::string>
        {
            const auto created = CreateFaunaJoint(m_context.world->DrawTail().joints, request, m_context.cloth->random);
            if (!created.HasValue())
                return Core::Unexpected(created.Error());
            return {};
        },
        .publishAction =
            [this](std::uint16_t type, std::uint8_t action)
        {
            auto placement = m_context.world->ModelPlacement(type);
            placement.action = action;
            m_context.world->PlaceModel(type, placement);
        },
        .soundAt = [this](std::int32_t id, const glm::vec3& origin)
        { m_context.world->Runtime().RequestSound(id, origin); },
        // 0x52ae2a (CL9 hito 4): el chorro de la Flame con el héroe de dueño manda los blancos con 0x517a20(+0x89, la
        // posición, 150, el byte +0x8c, +0x8a, 0), como el choque de las flechas de skill.
        .hero = &m_context.world->Actors()[m_context.world->Runtime().hero],
        .skillHits = [this](const World::ModelEffect& flame, float radius) -> Core::Expected<void, std::string>
        {
            const SkillHitsRequest request{.skillIndex = flame.byte89,
                                           .point = flame.transform.position,
                                           .radius = radius,
                                           .volley = static_cast<std::uint8_t>(flame.word8c & 0xff),
                                           .key = flame.disturbanceState,
                                           .extra = 0};
            const auto send = m_session != nullptr ? std::function<void(Protocol::Outgoing)>(
                                                         [session = m_session](Protocol::Outgoing packet)
                                                         { session->SendChat(std::move(packet)); })
                                                   : std::function<void(Protocol::Outgoing)>{};
            const auto hit = HitTheTargetsAround(request, *m_context.world, m_context.cloth->random, send);
            if (!hit.HasValue())
                return Core::Unexpected(hit.Error());
            return {};
        },
        // CL11 (World/BlastModelEffects): el registro propio (0x7681eb4) del 0xf4 que suelta el paso del 0xf3.
        .ownRecordDepth = &m_context.world->Runtime().ownRecordDepth,
        .ownRecordCount = &m_context.world->OwnCharacter().bytef04};
    // CL11, lote magos: lo mismo para los efectos de las magias, con el dueño leído del pool de personajes.
    const World::WizardEffectServices wizard = WizardEffectServicesOf(
        *m_context.world, m_context.cloth->random,
        WizardSceneParts{.particles = &m_context.world->Particles(),
                         .worldClock = worldClock,
                         .sprites = &m_sprites,
                         .terrain = &StepTerrain(),
                         .fixedHeight = CameraState().fixedHeight,
                         .cameraShake = &CameraInputState().shake,
                         .sound = [this](std::int32_t id) { m_context.world->Runtime().RequestSound(id); },
                         .send = m_session != nullptr ? std::function<void(Protocol::Outgoing)>(
                                                            [session = m_session](Protocol::Outgoing packet)
                                                            { session->SendChat(std::move(packet)); })
                                                      : std::function<void(Protocol::Outgoing)>{}});
    // 52d220: reset44dc40antesdelprimeractivo; el último proyectil seguido vuelveaescribirlo.
    CameraInputState().effectTarget = glm::vec3(0.0f);
    auto effects = m_context.world->Map().effects.Effects();
    for (std::size_t slot = 0; slot < effects.size(); ++slot)
    {
        auto& effect = effects[slot];
        if (!effect.alive)
            continue;
        const auto* skeleton = effect.type == World::EarthquakeBurstEffect
                                   ? nullptr
                                   : m_worldModels.SkeletonOf(static_cast<std::uint16_t>(effect.type));
        // 0x52a9ad: el 5ac/3 del conjunto completo lee al personaje dueño y crea joints, fuera de World.
        if (effect.type == World::IcarusFlareEffect && effect.subtype == World::FullSetFlareSubtype)
        {
            if (auto stepped = StepFullSetFlare(effect, m_context.world->DrawTail().joints, m_context.cloth->random);
                !stepped.HasValue())
                return stepped;
            continue;
        }
        // 0x52a7b7 y 0x52a8a7: los 5ac/1 y /2 de las skills que llegan leen al blanco dueño y crean joints, fuera de
        // World.
        if (effect.type == World::IcarusFlareEffect &&
            (effect.subtype == World::SwellFlareSubtype || effect.subtype == World::GuardFlareSubtype))
        {
            World::ParticleOwner particleOwner;
            const auto actors = m_context.world->Actors();
            for (std::size_t actorSlot = 0; actorSlot < actors.size(); ++actorSlot)
            {
                if (&actors[actorSlot] == effect.ownerContext)
                {
                    particleOwner = {World::ParticleOwnerKind::Actor, actorSlot};
                    break;
                }
            }
            if (auto stepped = StepSkillFlare(effect, m_context.world->DrawTail().joints, m_context.cloth->random,
                                              &m_context.world->Particles(), particleOwner);
                !stepped.HasValue())
                return stepped;
            continue;
        }
        // 0x52b9a6: el calco 0x61e/1 de la mascota suelta joints 619/1 con el héroe de dueño, y el /3 de la 0x1c y de
        // las invocaciones, los 619/3 de la CL11, fuera de World.
        if (StepsAsPetSummon(effect))
        {
            if (auto stepped = StepPetSummon(effect, m_context.world->DrawTail().joints, m_context.cloth->random);
                !stepped.HasValue())
                return stepped;
            continue;
        }
        // 0x5208a0 sobre las flechas de 0x53d9a0 (CL9 hito 3, fase 2e): su choque lee el pool de personajes y el
        // registro propio, fuera de World. CL9 hito 4: la flecha 0xdb del asedio lee la celda y la altura del terreno,
        // y el choque de las de skill pide un sonido (CL10) y manda el C1 10 (0x517a20).
        if (World::IsSiegeStoneEffect(effect.type))
        {
            if (m_context.worldEntry == nullptr)
                return Core::Unexpected(
                    std::string("0x527dd2: faltan los recursos de entrada para el arma del castillo"));
            if (auto stepped =
                    World::StepSiegeStone(effect, skeleton, m_context.world->Particles(), m_context.cloth->random,
                                          SiegeStoneServicesOf(*m_context.worldEntry));
                !stepped.HasValue())
                return stepped;
            continue;
        }
        if (IsArrowEffect(effect.type))
        {
            const ArrowStepContext arrows = ArrowContext(worldClock, terrainHeight);
            if (auto stepped = StepArrow(effect, skeleton, arrows); !stepped.HasValue())
                return stepped;
            continue;
        }
        if (effect.type == 0xc2 && effect.subtype == 3)
        {
            if (auto stepped = StepFaunaLightning(effect, slot, skeleton, m_context.world->Particles(), m_sprites,
                                                  m_context.world->DynamicLighting().Lighting(), *m_context.cloth);
                !stepped.HasValue())
                return stepped;
            // 0x52cde1..0x52cdfb: con vida impar después de la cola, otro paso entero (con su propio rand()).
            if (effect.alive && effect.life > 0 && (effect.life & 1) != 0)
                if (auto again = StepFaunaLightning(effect, slot, skeleton, m_context.world->Particles(), m_sprites,
                                                    m_context.world->DynamicLighting().Lighting(), *m_context.cloth);
                    !again.HasValue())
                    return again;
            continue;
        }
        // CL11, lote magos: los efectos de las magias (0xbd..0xd9, World/WizardModelEffects).
        if (World::IsWizardModelEffect(effect.type))
        {
            if (auto stepped = World::StepWizardModelEffect(effect, skeleton, m_context.cloth->random, wizard);
                !stepped.HasValue())
                return stepped;
            continue;
        }
        // 0x525e6e, 0x52a688, 0x52b56d y 0x52c9e7: los tipos altos de la CL11 leen el mundo (el dueño, el héroe, los
        // joints, las partículas y la tabla de efectos), fuera de World.
        if (StepsAsHighModelEffect(effect))
        {
            const HighEffectStepContext high{.world = *m_context.world,
                                             .random = m_context.cloth->random,
                                             .services = services,
                                             .worldClock = worldClock,
                                             .models = m_models};
            if (auto stepped = StepHighModelEffect(effect, skeleton, high); !stepped.HasValue())
                return stepped;
            continue;
        }
        if (effect.type == 0x147)
        {
            const auto before = effect.transform.position;
            // 5208c5: el sorteo común precede también el cuerpo147.
            static_cast<void>(m_context.cloth->random.Next());
            if (auto stepped = StepCrywolfDeathSpirit(effect, m_context.cloth->random, services); !stepped.HasValue())
                return stepped;
            World::StepModelEffect(effect, skeleton);
            effect.previousPosition = before;
            continue;
        }
        if (auto stepped =
                World::StepGameModelEffect(effect, skeleton, m_context.world->Particles(), worldClock,
                                           m_context.cloth->random, m_context.world->Map().kalima.get(), services);
            !stepped.HasValue())
            return stepped;
    }
    return {};
}

ArrowStepContext GameScene::ArrowContext(float worldClock, const std::function<double(float, float)>& terrainHeight)
{
    return ArrowStepContext{.world = *m_context.world,
                            .random = m_context.cloth->random,
                            .particles = &m_context.world->Particles(),
                            .sprites = &m_sprites,
                            .lighting = &m_context.world->DynamicLighting().Lighting(),
                            .worldClock = worldClock,
                            .terrain = &StepTerrain(),
                            .terrainHeight = terrainHeight,
                            .soundRequest = [this](std::int32_t id) { m_context.world->Runtime().RequestSound(id); },
                            .send = m_session != nullptr ? std::function<void(Protocol::Outgoing)>(
                                                               [session = m_session](Protocol::Outgoing packet)
                                                               { session->SendChat(std::move(packet)); })
                                                         : std::function<void(Protocol::Outgoing)>{}};
}

Core::Expected<void, std::string> GameScene::StepTheEffects(float worldClock)
{
    auto& runtime = m_context.world->Runtime();
    const auto& terrain = StepTerrain();
    std::function<double(float, float)> terrainHeight;
    if (terrain.heights && terrain.attributes)
        terrainHeight = [&](float x, float y)
        { return World::TerrainHeightAt(*terrain.heights, *terrain.attributes, x, y, CameraState().fixedHeight); };
    StepDamage(m_context.world->DrawTail().damage);
    if (auto stepped = StepTheModelEffects(worldClock, terrainHeight); !stepped.HasValue())
        return stepped;
    // 0x53ae6d: el 0x60f/0xb de la flecha 0xf8 con el skill 1 la hace chocar desde el paso de los joints.
    const JointKernels::EffectHit arrowHit = [&](World::ModelEffect& arrow)
    { return HitTheArrow(arrow, ArrowContext(worldClock, terrainHeight)); };
    if (auto stepped = StepJoints(
            m_context.world->DrawTail().joints, m_context.cloth->random, m_context.cloth, &m_context.world->Particles(),
            worldClock, &m_sprites, &m_context.world->DynamicLighting().Lighting(),
            [&](std::int32_t sound) { runtime.RequestSound(sound); }, terrainHeight, m_context.world->Actors(),
            arrowHit, &m_context.world->Map().effects);
        !stepped.HasValue())
        return stepped;
    // 0x63712d: 0x549090, las marcas del suelo, sin rand(). En la espera también corre, y acá no: la tabla
    // sigue vacía mientras MuRet no tenga quién la llene (08 P-C49).
    if (auto particles = StepGameParticles(
            m_context.world->Particles(), m_context.cloth->random, runtime.map, terrainHeight, worldClock,
            ParticleOwnersAmong(m_context.world->Map().objects), ParticleActorActionsAmong(m_context.world->Actors()),
            ParticleTextureHeightsOf(m_worldModels), &m_context.world->DynamicLighting().Lighting(),
            ParticleActorHandsAmong(m_context.world->Actors(), runtime.hero), ParticleOwnerBonesOf(*m_context.world),
            [&](std::int32_t sound) { runtime.RequestSound(sound); }, ParticleOwnerStatesOf(*m_context.world),
            &m_sprites);
        !particles.HasValue())
    {
        return particles;
    }
    World::StepGroundMarks(runtime.groundMarks);
    // 0x637132: 0x403590, el recorrido de la cámara, con timeGetTime (0x404c90): el reloj de la escena en ms.
    const auto now = m_context.clock != nullptr
                         ? static_cast<std::uint32_t>(
                               std::chrono::duration_cast<std::chrono::milliseconds>(m_context.clock->Now()).count())
                         : 0U;
    // 0x403390, en cada paso del recorrido: el ítem del cursor vuelve a su lugar (0x5be150) y los paneles se cierran
    // como con Esc (0x657880(0x1b)).
    const InventoryServices inventory = InventoryContext();
    const auto releaseTheUi = [&]() -> Core::Expected<void, std::string>
    {
        WorldState& world = *m_context.world;
        if (auto returned = ReturnTheCursorItem(
                InventoryMoveContext{.world = world, .random = m_context.cloth->random, .services = inventory});
            !returned.HasValue())
            return returned;
        GamePanels panels(world, m_context.cloth->random, GamePanelTables{.items = m_items, .texts = m_texts});
        CastleSender send;
        if (m_session != nullptr)
            send = [this](Protocol::Outgoing outgoing) { m_session->SendGamePacket(std::move(outgoing)); };
        LendTheCastlePanelServices(
            panels, world, m_context.cloth->random, send, m_context.worldEntry,
            {.measure = m_context.textMeasure, .texts = m_texts, .screen = m_screen, .now = now});
        NpcWindowSender npcSend;
        if (m_session != nullptr)
            npcSend = [this](Protocol::Outgoing packet) { return m_session->SendNpcWindowPacket(std::move(packet)); };
        panels.LendTheNpcWindows(world, m_context.cloth->random, inventory, std::move(npcSend));
        auto closed = panels.Registry().PanelKey(0x1b);
        if (!closed.HasValue())
            return Core::Unexpected(std::move(closed).Error());
        return {};
    };
    return StepTheCameraTour(CameraTourContext{.world = *m_context.world,
                                               .random = m_context.cloth->random,
                                               .now = now,
                                               .resources = m_context.worldEntry,
                                               .height = terrainHeight,
                                               .releaseTheUi = releaseTheUi});
}

} // namespace Client::Scenes
