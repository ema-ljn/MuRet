#include "Scenes/GameScene.h"
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

Core::Expected<void, std::string> GameScene::StepTheCharacters(float worldClock)
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
            .terrain = &StepTerrain(),
            .darkSpiritSkeleton = m_models ? m_models->SkeletonOf(DarkSpiritModel) : nullptr,
            .particles = &m_context.world->Particles(),
            .worldClock = worldClock,
            .counter = m_context.cloth->logicSteps,
            .actionSpeeds = &m_context.world->ActionSpeeds(),
            .fixedHeight = CameraState().fixedHeight,
            .soundRequest = [&](std::int32_t sound) { runtime.lastSoundId = sound; },
            .send = m_session != nullptr
                        ? std::function<void(Protocol::Outgoing)>([session = m_session](Protocol::Outgoing packet)
                                                                  { session->SendChat(std::move(packet)); })
                        : std::function<void(Protocol::Outgoing)>{}});
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
        .sound = [this](std::int32_t id) { m_context.world->Runtime().lastSoundId = id; }};
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
        // 0x52b9a6: el calco 0x61e/1 de la mascota suelta joints 619/1 con el héroe de dueño, fuera de World.
        if (effect.type == World::ColoredDecalEffect && effect.subtype == World::PetSummonSubtype)
        {
            if (auto stepped = StepPetSummon(effect, m_context.world->DrawTail().joints, m_context.cloth->random);
                !stepped.HasValue())
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
                                  &m_context.world->Particles(), worldClock, &m_sprites);
        !stepped.HasValue())
        return stepped;
    // 0x63712d: 0x549090, las marcas del suelo, sin rand(). En la espera también corre, y acá no: la tabla
    // sigue vacía mientras MuRet no tenga quién la llene (08 P-C49).
    if (auto particles = StepGameParticles(
            m_context.world->Particles(), m_context.cloth->random, runtime.map, terrainHeight, worldClock,
            ParticleOwnersAmong(m_context.world->Map().objects), ParticleActorActionsAmong(m_context.world->Actors()),
            ParticleTextureHeightsOf(m_worldModels), &m_context.world->DynamicLighting().Lighting());
        !particles.HasValue())
    {
        return particles;
    }
    World::StepGroundMarks(runtime.groundMarks);
    return {};
}

} // namespace Client::Scenes
