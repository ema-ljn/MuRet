#include "Scenes/GameScene.h"
#include "Effects/CrywolfDeathSpiritEffects.h"
#include "Effects/ArrowEffects.h"
#include "CastleSiege/CastleWeaponEffects.h"
#include "CastleSiege/SiegeStoneEffects.h"
#include "Terrain/DynamicWorldLight.h"
#include "FrontEnd/SelectionState.h"
#include "Effects/GameParticles.h"
#include "Effects/HandShine.h"
#include "Effects/HighModelEffects.h"
#include "Scenes/GameDrawTail.h"
#include "Joints/FaunaJointCreation.h"
#include "Fauna/FaunaEffects.h"
#include "Effects/CharacterEffectSteps.h"
#include "Items/GameFloorItems.h"
#include "Effects/GameModelEffects.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"
#include "Particles/ParticleOwner.h"

#include "Session/CharacterStepHooks.h"
#include "Session/ModuleStates.h"
#include "Session/ServerSession.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Protocol/Outgoing.h"
#include "Models/Skeleton.h"
#include "Events/Crywolf/CameraTour.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Actors/ActorsState.h"
#include "Actors/CharacterStep.h"
#include "Hud/GamePanels.h"
#include "CastleSiege/CastleWindows.h"
#include "CastleSiege/CastleSiegeReceive.h"
#include "Npc/NpcWindows.h"
#include "Items/InventoryMoves.h"
#include "Pets/DarkSpirit.h"
#include "Actors/Drawing/GameCharacters.h"
#include "Pets/HelperTable.h"
#include "Joints/JointKernels.h"
#include "Scenes/SceneContext.h"
#include "Kernel/SceneId.h"
#include "Actors/SkillHits.h"
#include "Kernel/WorldRuntime.h"
#include "Effects/GroundEffectDrawing.h"
#include "Effects/WizardEffects.h"
#include "Kernel/WorldState.h"
#include "Camera/GameCamera.h"
#include "Terrain/GroundMarks.h"
#include "Terrain/TerrainHeight.h"
#include "Effects/WizardModelEffects.h"
#include "Terrain/WorldTerrain.h"
#include "Audio/SoundId.h"

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
        if (auto stepped = Items::StepFloorItems(floorItems,
                                                 {*terrain.heights, *terrain.attributes, m_context.cloth->random,
                                                  m_context.world->Particles(), worldClock, CameraState().fixedHeight});
            !stepped.HasValue())
            return stepped;
    }
    return {};
}

Core::Expected<void, std::string> GameScene::StepTheCharacters(float worldClock, bool controlDown)
{
    auto& runtime = m_context.world->Runtime();
    const Models::Skeleton* player =
        m_models != nullptr ? m_models->SkeletonOf(Tables::PlayerSkeletonModelId) : nullptr;
    const Session::CharacterStepHooks hooks{};
    if (auto stepped = Actors::StepCharacters(Actors::CharacterStepContext{
            .world = *m_context.world,
            .random = m_context.cloth->random,
            .scene = SceneId::Game,
            .playerSkeleton = player,
            .items = m_items,
            .skills = m_skills,
            .models = m_models,
            .terrain = &StepTerrain(),
            .darkSpiritSkeleton = m_models ? m_models->SkeletonOf(Models::ModelId{Pets::DarkSpiritModel}) : nullptr,
            .particles = &m_context.world->Particles(),
            .sprites = &m_sprites,
            .joints = m_context.world->DrawTail().joints,
            .worldClock = worldClock,
            .counter = m_context.cloth->logicSteps,
            .actionSpeeds = &m_context.world->ActionSpeeds(),
            .fixedHeight = CameraState().fixedHeight,
            .soundRequest = [&](Audio::SoundId sound) { runtime.RequestSound(sound); },
            .soundAt = [&](Audio::SoundId sound, const glm::vec3& origin) { runtime.RequestSound(sound, origin); },
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
            },
            .groundMarks = &m_context.states->terrain.groundMarks,
            .trapezoid = &m_context.states->camera.trapezoid,
            .dynamicLighting = &DynamicLight().Lighting(),
            .faunaModels = &LoadedFaunaModels(),
            .reportedCell = &m_context.states->actors.reportedCell,
            .hooks = &hooks,
            .effectHooks = &m_effectHooks});
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
    const Effects::ModelEffectStepServices services{
        .terrainHeight = terrainHeight,
        .sprites = &m_sprites,
        .effects = &m_context.world->Map().effects,
        .lighting = &DynamicLight().Lighting(),
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
        .sound = [this](Audio::SoundId id) { m_context.world->Runtime().RequestSound(id); },
        .joint = [this](const Kernel::JointRequest& request) -> Core::Expected<void, std::string>
        {
            const auto created =
                Joints::CreateFaunaJoint(m_context.world->DrawTail().joints, request, m_context.cloth->random);
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
        .soundAt = [this](Audio::SoundId id, const glm::vec3& origin)
        { m_context.world->Runtime().RequestSound(id, origin); },
        // 0x52ae2a (CL9 hito 4): el chorro de la Flame con el héroe de dueño manda los blancos con 0x517a20(+0x89, la
        // posición, 150, el byte +0x8c, +0x8a, 0), como el choque de las flechas de skill.
        .hero = &m_context.world->Hero(),
        .skillHits = [this](const Effects::ModelEffect& flame, float radius) -> Core::Expected<void, std::string>
        {
            const Skills::SkillHitsRequest request{.skillIndex = flame.byte89,
                                                   .point = flame.transform.position,
                                                   .radius = radius,
                                                   .volley = static_cast<std::uint8_t>(flame.word8c & 0xff),
                                                   .key = flame.disturbanceState,
                                                   .extra = 0};
            const auto send = m_session != nullptr ? std::function<void(Protocol::Outgoing)>(
                                                         [session = m_session](Protocol::Outgoing packet)
                                                         { session->SendChat(std::move(packet)); })
                                                   : std::function<void(Protocol::Outgoing)>{};
            const auto hit =
                Skills::HitTheTargetsAround(request, *m_context.world, MapTerrain(), m_context.cloth->random, send);
            if (!hit.HasValue())
                return Core::Unexpected(hit.Error());
            return {};
        },
        // CL11 (World/BlastModelEffects): el registro propio (0x7681eb4) del 0xf4 que suelta el paso del 0xf3.
        .ownRecordDepth = &m_context.world->Runtime().ownRecordDepth,
        .ownRecordCount = &m_context.world->OwnCharacter().bytef04};
    // CL11, lote magos: lo mismo para los efectos de las magias, con el dueño leído del pool de personajes.
    const Effects::WizardEffectServices wizard = Effects::WizardEffectServicesOf(
        *m_context.world, m_context.cloth->random,
        Effects::WizardSceneParts{.particles = &m_context.world->Particles(),
                                  .worldClock = worldClock,
                                  .sprites = &m_sprites,
                                  .terrain = &StepTerrain(),
                                  .fixedHeight = CameraState().fixedHeight,
                                  .cameraShake = &CameraInputState().shake,
                                  .sound = [this](Audio::SoundId id) { m_context.world->Runtime().RequestSound(id); },
                                  .send = m_session != nullptr ? std::function<void(Protocol::Outgoing)>(
                                                                     [session = m_session](Protocol::Outgoing packet)
                                                                     { session->SendChat(std::move(packet)); })
                                                               : std::function<void(Protocol::Outgoing)>{},
                                  .models = m_models,
                                  .dynamicLighting = &DynamicLight().Lighting(),
                                  .faunaModels = &LoadedFaunaModels()});
    // 52d220: reset44dc40antesdelprimeractivo; el último proyectil seguido vuelveaescribirlo.
    CameraInputState().effectTarget = glm::vec3(0.0f);
    auto effects = m_context.world->Map().effects.Effects();
    for (std::size_t slot = 0; slot < effects.size(); ++slot)
    {
        auto& effect = effects[slot];
        if (!effect.alive)
            continue;
        const auto* skeleton = effect.type == Effects::EarthquakeBurstEffect
                                   ? nullptr
                                   : m_worldModels.SkeletonOf(Models::ModelId{static_cast<std::uint16_t>(effect.type)});
        // 0x52a9ad: el 5ac/3 del conjunto completo lee al personaje dueño y crea joints, fuera de World.
        if (effect.type == Effects::IcarusFlareEffect && effect.subtype == Effects::FullSetFlareSubtype)
        {
            if (auto stepped = Effects::StepFullSetFlare(effect, m_context.world->DrawTail().joints,
                                                         &m_characterDrawHooks, m_context.cloth->random);
                !stepped.HasValue())
                return stepped;
            continue;
        }
        // 0x52a7b7 y 0x52a8a7: los 5ac/1 y /2 de las skills que llegan leen al blanco dueño y crean joints, fuera de
        // World.
        if (effect.type == Effects::IcarusFlareEffect &&
            (effect.subtype == Effects::SwellFlareSubtype || effect.subtype == Effects::GuardFlareSubtype))
        {
            Particles::ParticleOwner particleOwner;
            const auto actors = m_context.world->Actors();
            for (std::size_t actorSlot = 0; actorSlot < actors.size(); ++actorSlot)
            {
                if (&actors[actorSlot] == effect.ownerContext)
                {
                    particleOwner = {Particles::ParticleOwnerKind::Actor, actorSlot};
                    break;
                }
            }
            if (auto stepped =
                    Effects::StepSkillFlare(effect, m_context.world->DrawTail().joints, &m_characterDrawHooks,
                                            m_context.cloth->random, &m_context.world->Particles(), particleOwner);
                !stepped.HasValue())
                return stepped;
            continue;
        }
        // 0x52b9a6: el calco 0x61e/1 de la mascota suelta joints 619/1 con el héroe de dueño, y el /3 de la 0x1c y de
        // las invocaciones, los 619/3 de la CL11, fuera de World.
        if (Effects::StepsAsPetSummon(effect))
        {
            if (auto stepped = Effects::StepPetSummon(effect, m_context.world->DrawTail().joints, &m_characterDrawHooks,
                                                      m_context.cloth->random);
                !stepped.HasValue())
                return stepped;
            continue;
        }
        // 0x5208a0 sobre las flechas de 0x53d9a0 (CL9 hito 3, fase 2e): su choque lee el pool de personajes y el
        // registro propio, fuera de World. CL9 hito 4: la flecha 0xdb del asedio lee la celda y la altura del terreno,
        // y el choque de las de skill pide un sonido (CL10) y manda el C1 10 (0x517a20).
        if (CastleSiege::IsSiegeStoneEffect(effect.type))
        {
            if (m_context.worldEntry == nullptr)
                return Core::Unexpected(
                    std::string("0x527dd2: faltan los recursos de entrada para el arma del castillo"));
            if (auto stepped =
                    CastleSiege::StepSiegeStone(effect, skeleton, m_context.world->Particles(), m_context.cloth->random,
                                                CastleSiege::SiegeStoneServicesOf(*m_context.worldEntry));
                !stepped.HasValue())
                return stepped;
            continue;
        }
        if (Effects::IsArrowEffect(effect.type))
        {
            const Effects::ArrowStepContext arrows = ArrowContext(worldClock, terrainHeight);
            if (auto stepped = Effects::StepArrow(effect, skeleton, arrows); !stepped.HasValue())
                return stepped;
            continue;
        }
        if (effect.type == 0xc2 && effect.subtype == 3)
        {
            if (auto stepped = Fauna::StepFaunaLightning(effect, slot, skeleton, m_context.world->Particles(),
                                                         m_sprites, DynamicLight().Lighting(), *m_context.cloth);
                !stepped.HasValue())
                return stepped;
            // 0x52cde1..0x52cdfb: con vida impar después de la cola, otro paso entero (con su propio rand()).
            if (effect.alive && effect.life > 0 && (effect.life & 1) != 0)
                if (auto again = Fauna::StepFaunaLightning(effect, slot, skeleton, m_context.world->Particles(),
                                                           m_sprites, DynamicLight().Lighting(), *m_context.cloth);
                    !again.HasValue())
                    return again;
            continue;
        }
        // CL11, lote magos: los efectos de las magias (0xbd..0xd9, World/WizardModelEffects).
        if (Effects::IsWizardModelEffect(effect.type))
        {
            if (auto stepped = Effects::StepWizardModelEffect(effect, skeleton, m_context.cloth->random, wizard);
                !stepped.HasValue())
                return stepped;
            continue;
        }
        // 0x525e6e, 0x52a688, 0x52b56d y 0x52c9e7: los tipos altos de la CL11 leen el mundo (el dueño, el héroe, los
        // joints, las partículas y la tabla de efectos), fuera de World.
        if (Effects::StepsAsHighModelEffect(effect))
        {
            const Effects::HighEffectStepContext high{.world = *m_context.world,
                                                      .random = m_context.cloth->random,
                                                      .services = services,
                                                      .worldClock = worldClock,
                                                      .models = m_models,
                                                      .terrain = &MapTerrain(),
                                                      .faunaModels = &LoadedFaunaModels()};
            if (auto stepped = Effects::StepHighModelEffect(effect, skeleton, high); !stepped.HasValue())
                return stepped;
            continue;
        }
        if (effect.type == 0x147)
        {
            const auto before = effect.transform.position;
            // 5208c5: el sorteo común precede también el cuerpo147.
            static_cast<void>(m_context.cloth->random.Next());
            if (auto stepped = Effects::StepCrywolfDeathSpirit(effect, m_context.cloth->random, services);
                !stepped.HasValue())
                return stepped;
            Effects::StepModelEffect(effect, skeleton);
            effect.previousPosition = before;
            continue;
        }
        if (auto stepped =
                Effects::StepGameModelEffect(effect, skeleton, m_context.world->Particles(), worldClock,
                                             m_context.cloth->random, m_context.world->Map().kalima.get(), services);
            !stepped.HasValue())
            return stepped;
    }
    return {};
}

Effects::ArrowStepContext GameScene::ArrowContext(float worldClock,
                                                  const std::function<double(float, float)>& terrainHeight)
{
    return Effects::ArrowStepContext{
        .world = *m_context.world,
        .random = m_context.cloth->random,
        .particles = &m_context.world->Particles(),
        .sprites = &m_sprites,
        .lighting = &DynamicLight().Lighting(),
        .worldClock = worldClock,
        .terrain = &StepTerrain(),
        .terrainHeight = terrainHeight,
        .soundRequest = [this](Audio::SoundId id) { m_context.world->Runtime().RequestSound(id); },
        .send = m_session != nullptr
                    ? std::function<void(Protocol::Outgoing)>([session = m_session](Protocol::Outgoing packet)
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
        { return Terrain::TerrainHeightAt(*terrain.heights, *terrain.attributes, x, y, CameraState().fixedHeight); };
    StepDamage(m_context.world->DrawTail().damage);
    if (auto stepped = StepTheModelEffects(worldClock, terrainHeight); !stepped.HasValue())
        return stepped;
    // 0x53ae6d: el 0x60f/0xb de la flecha 0xf8 con el skill 1 la hace chocar desde el paso de los joints.
    const Joints::JointKernels::EffectHit arrowHit = [&](Effects::ModelEffect& arrow)
    { return Effects::HitTheArrow(arrow, ArrowContext(worldClock, terrainHeight)); };
    // 0x53b548: el 0x61c/0 compara su dueño con el héroe ([0x743a170]).
    const auto actors = m_context.world->Actors();
    const Kernel::ActorState* hero = runtime.hero < actors.size() ? &actors[runtime.hero] : nullptr;
    // 0x539a50..0x539add y 0x53b569..0x53b5b1: el 0x613 de ancho 80 y el 0x61c/0 del héroe leen el asedio, el atributo
    // de 0x5f82c0 y mandan los blancos del héroe.
    const Joints::JointKernels::VolleyServices volley{
        .siegeActive = runtime.castleSiege.siegeActive,
        .attributes = terrain.attributes.get(),
        .skillHits = [this](const Skills::SkillHitsRequest& request) -> Core::Expected<void, std::string>
        {
            const auto send = m_session != nullptr ? std::function<void(Protocol::Outgoing)>(
                                                         [session = m_session](Protocol::Outgoing packet)
                                                         { session->SendChat(std::move(packet)); })
                                                   : std::function<void(Protocol::Outgoing)>{};
            const auto hit =
                Skills::HitTheTargetsAround(request, *m_context.world, MapTerrain(), m_context.cloth->random, send);
            if (!hit.HasValue())
                return Core::Unexpected(hit.Error());
            return {};
        }};
    if (auto stepped = Joints::StepJoints(
            m_context.world->DrawTail().joints, m_context.cloth->random, m_context.cloth, &m_context.world->Particles(),
            worldClock, &m_sprites, &DynamicLight().Lighting(),
            [&](Audio::SoundId sound) { runtime.RequestSound(sound); }, terrainHeight, actors, arrowHit,
            &m_context.world->Map().effects, hero, m_context.world, &volley, m_models);
        !stepped.HasValue())
        return stepped;
    // 0x63712d: 0x549090, las marcas del suelo, sin rand(). En la espera también corre, y acá no: la tabla
    // sigue vacía mientras MuRet no tenga quién la llene (08 P-C49).
    if (auto particles = Effects::StepGameParticles(
            m_context.world->Particles(), m_context.cloth->random, runtime.map, terrainHeight, worldClock,
            Effects::ParticleOwnersAmong(m_context.world->Map().objects),
            Effects::ParticleActorActionsAmong(m_context.world->Actors()),
            Effects::ParticleTextureHeightsOf(m_worldModels), &DynamicLight().Lighting(),
            Effects::ParticleActorHandsAmong(m_context.world->Actors(), runtime.hero),
            Effects::ParticleOwnerBonesOf(*m_context.world), [&](Audio::SoundId sound) { runtime.RequestSound(sound); },
            Effects::ParticleOwnerStatesOf(*m_context.world), &m_sprites);
        !particles.HasValue())
    {
        return particles;
    }
    Terrain::StepGroundMarks(m_context.states->terrain.groundMarks);
    // 0x637132: 0x403590, el recorrido de la cámara, con timeGetTime (0x404c90): el reloj de la escena en ms.
    const auto now = m_context.clock != nullptr
                         ? static_cast<std::uint32_t>(
                               std::chrono::duration_cast<std::chrono::milliseconds>(m_context.clock->Now()).count())
                         : 0U;
    // 0x403390, en cada paso del recorrido: el ítem del cursor vuelve a su lugar (0x5be150) y los paneles se cierran
    // como con Esc (0x657880(0x1b)).
    const Items::InventoryServices inventory = InventoryContext();
    const auto releaseTheUi = [&]() -> Core::Expected<void, std::string>
    {
        Kernel::WorldState& world = *m_context.world;
        if (auto returned = Items::ReturnTheCursorItem(
                Items::InventoryMoveContext{.world = world, .random = m_context.cloth->random, .services = inventory});
            !returned.HasValue())
            return returned;
        Hud::GamePanels panels(world, m_context.cloth->random,
                               Hud::GamePanelTables{.items = m_items, .texts = m_texts});
        CastleSiege::CastleSender send;
        if (m_session != nullptr)
            send = [this](Protocol::Outgoing outgoing) { m_session->SendGamePacket(std::move(outgoing)); };
        CastleSiege::LendTheCastlePanelServices(
            panels, world, m_context.cloth->random, send, m_context.worldEntry,
            {.measure = m_context.textMeasure, .texts = m_texts, .screen = m_screen, .now = now});
        Npc::NpcWindowSender npcSend;
        if (m_session != nullptr)
            npcSend = [this](Protocol::Outgoing packet) { return m_session->SendNpcWindowPacket(std::move(packet)); };
        panels.LendTheNpcWindows(world, m_context.cloth->random, inventory, std::move(npcSend));
        auto closed = panels.Registry().PanelKey(0x1b);
        if (!closed.HasValue())
            return Core::Unexpected(std::move(closed).Error());
        return {};
    };
    return Events::Crywolf::StepTheCameraTour(Events::Crywolf::CameraTourContext{.world = *m_context.world,
                                                                                 .random = m_context.cloth->random,
                                                                                 .now = now,
                                                                                 .resources = m_context.worldEntry,
                                                                                 .height = terrainHeight,
                                                                                 .releaseTheUi = releaseTheUi});
}

} // namespace Client::Scenes
