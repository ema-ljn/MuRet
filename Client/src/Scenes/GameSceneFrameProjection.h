#pragma once

#include "Render/Models/Cloth.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GameItemRenderState.h"
#include "Scenes/WorldRuntime.h"
#include "World/KalimaWorld.h"
#include "World/ModelEffects.h"
#include "World/ObjectModelStepState.h"
#include "World/Particles.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjects.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Client::Scenes
{
// Un único dueño en heap por dibujo intermedio: todos sus servicios prestan estas mismas copias.
struct GameSceneFrameProjection
{
    GameDrawTailState tail{};
    World::TerrainLighting lighting{};
    World::ObjectModelStepState modelState{};
    GameItemRenderState items{};
    Render::Models::ClothWorld cloth{.random = Render::Models::CrtRandom{}};
    World::ModelEffects effects{};
    World::Particles particles{};
    std::vector<World::WorldObject> objects{};
    std::unique_ptr<World::KalimaWorld> kalima{};
    std::uint8_t heroXDepth = 0;
    std::uint8_t heroYDepth = 0;
    std::uint8_t ownRecordDepth = 0;
    // 0x7d325c8: el temblor de la cámara que 0x405e00 escribe en el dibujo.
    float cameraShake = 0.0f;
    // Lo que el HUD escribe y dura (0x5a7320): sus globales, el humo de Crywolf (0x576353c) y el sonido que pide.
    HudState hud{};
    std::uint8_t crywolfSandOverlay = 0;
    std::optional<std::int32_t> sound{};
};
} // namespace Client::Scenes
