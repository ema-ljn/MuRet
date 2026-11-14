#include "Scenes/GameScene.h"
#include "Scenes/ArrowEffects.h"
#include "Scenes/DynamicWorldLight.h"
#include "Scenes/SelectionState.h"
#include "Scenes/GameParticles.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GameRibbonCreation.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/FaunaEffects.h"
#include "Scenes/GameCharacterEffects.h"
#include "Scenes/GameFloorItems.h"
#include "World/GameModelEffects.h"
#include "World/ModelEffects.h"

#include "App/ServerSession.h"
#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/GameCharacters.h"
#include "Scenes/HelperTable.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SkillHits.h"
#include "Scenes/TerrainPolygonFrame.h"
#include "Scenes/WorldRuntime.h"
#include "World/GroundEffectDrawing.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/GroundMarks.h"
#include "World/TerrainHeight.h"
#include "World/WorldTerrain.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace Client::Scenes
{
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
            .joints = m_context.world->DrawTail().joints,
            .worldClock = worldClock,
            .counter = m_context.cloth->logicSteps,
            .actionSpeeds = &m_context.world->ActionSpeeds(),
            .fixedHeight = CameraState().fixedHeight,
            .soundRequest = [&](std::int32_t sound) { runtime.lastSoundId = sound; },
            .send = m_session != nullptr
                        ? std::function<void(Protocol::Outgoing)>([session = m_session](Protocol::Outgoing packet)
                                                                  { session->SendChat(std::move(packet)); })
                        : std::function<void(Protocol::Outgoing)>{},
            .controlDown = controlDown});
        !stepped.HasValue())
    {
        return stepped;
    }
    // 5091d0, inmediatamente después del último508ea0 y antes de63711e.
    return StepRibbons(m_context.world->DrawTail().ribbons);
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
        // 0x41ed10(id, 0, 0): MuRet no tiene audio (CL10); como los demás pedidos de sonido del paso, queda anotado en
        // WorldRuntime::lastSoundId y no saca rand().
        .sound = [this](std::int32_t id) { m_context.world->Runtime().lastSoundId = id; },
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
        }};
    auto effects = m_context.world->Map().effects.Effects();
    for (std::size_t slot = 0; slot < effects.size(); ++slot)
    {
        auto& effect = effects[slot];
        const auto* skeleton = m_worldModels.SkeletonOf(static_cast<std::uint16_t>(effect.type));
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
            if (auto stepped = StepSkillFlare(effect, m_context.world->DrawTail().joints, m_context.cloth->random);
                !stepped.HasValue())
                return stepped;
            continue;
        }
        // 0x52b9a6: el calco 0x61e/1 de la mascota suelta joints 619/1 con el héroe de dueño, y el /3 de la 0x1c y de
        // las invocaciones, los 619/3 de la CL11, fuera de World.
        if (effect.type == World::ColoredDecalEffect &&
            (effect.subtype == World::PetSummonSubtype || effect.subtype == World::SummonDecalSubtype))
        {
            if (auto stepped = StepPetSummon(effect, m_context.world->DrawTail().joints, m_context.cloth->random);
                !stepped.HasValue())
                return stepped;
            continue;
        }
        // 0x5208a0 sobre las flechas de 0x53d9a0 (CL9 hito 3, fase 2e): su choque lee el pool de personajes y el
        // registro propio, fuera de World. CL9 hito 4: la flecha 0xdb del asedio lee la celda y la altura del terreno,
        // y el choque de las de skill pide un sonido (CL10, en WorldRuntime::lastSoundId) y manda el C1 10 (0x517a20).
        if (IsArrowEffect(effect.type))
        {
            const ArrowStepContext arrows{
                .world = *m_context.world,
                .random = m_context.cloth->random,
                .particles = &m_context.world->Particles(),
                .sprites = &m_sprites,
                .lighting = &m_context.world->DynamicLighting().Lighting(),
                .worldClock = worldClock,
                .terrain = &StepTerrain(),
                .terrainHeight = terrainHeight,
                .soundRequest = [this](std::int32_t id) { m_context.world->Runtime().lastSoundId = id; },
                .send = m_session != nullptr
                            ? std::function<void(Protocol::Outgoing)>([session = m_session](Protocol::Outgoing packet)
                                                                      { session->SendChat(std::move(packet)); })
                            : std::function<void(Protocol::Outgoing)>{}};
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
    if (auto stepped = StepJoints(m_context.world->DrawTail().joints, m_context.cloth->random, m_context.cloth,
                                  &m_context.world->Particles(), worldClock, &m_sprites,
                                  &m_context.world->DynamicLighting().Lighting(),
                                  [&](std::int32_t sound) { runtime.lastSoundId = sound; });
        !stepped.HasValue())
        return stepped;
    // 0x63712d: 0x549090, las marcas del suelo, sin rand(). En la espera también corre, y acá no: la tabla
    // sigue vacía mientras MuRet no tenga quién la llene (08 P-C49).
    if (auto particles = StepGameParticles(
            m_context.world->Particles(), m_context.cloth->random, runtime.map, terrainHeight, worldClock,
            ParticleOwnersAmong(m_context.world->Map().objects), ParticleActorActionsAmong(m_context.world->Actors()),
            ParticleTextureHeightsOf(m_worldModels), &m_context.world->DynamicLighting().Lighting(),
            ParticleOwnerBonesOf(*m_context.world));
        !particles.HasValue())
    {
        return particles;
    }
    World::StepGroundMarks(runtime.groundMarks);
    return {};
}

} // namespace Client::Scenes
