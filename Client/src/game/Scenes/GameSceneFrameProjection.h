#pragma once

#include "Crt/CrtRandom.h"
#include "Models/Cloth.h"
#include "Scenes/GameDrawTail.h"
#include "Actors/ActorPickingBox.h"
#include "Actors/Drawing/GameItemRenderState.h"
#include "Kernel/WorldRuntime.h"
#include "Ui/OldWindows.h"
#include "MapScripts/Kalima/KalimaWorld.h"
#include "Effects/ModelEffects.h"
#include "WorldObjects/ObjectModelStepState.h"
#include "Particles/Particles.h"
#include "Terrain/TerrainLighting.h"
#include "WorldObjects/WorldObjects.h"
#include "Audio/SoundId.h"
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
    Terrain::TerrainLighting lighting{};
    WorldObjects::ObjectModelStepState modelState{};
    Actors::Drawing::GameItemRenderState items{};
    Models::ClothWorld cloth{.random = Crt::CrtRandom{}};
    Effects::ModelEffects effects{};
    Particles::Particles particles{};
    std::vector<WorldObjects::WorldObject> objects{};
    std::vector<Actors::ActorPickingBox> actorPickingBoxes{};
    std::unique_ptr<MapScripts::Kalima::KalimaWorld> kalima{};
    std::uint8_t heroXDepth = 0;
    std::uint8_t heroYDepth = 0;
    std::uint8_t ownRecordDepth = 0;
    // 0x7d325c8: el temblor de la cámara que 0x405e00 escribe en el dibujo.
    float cameraShake = 0.0f;
    // Lo que el HUD escribe y dura (0x5a7320): sus globales, el humo de Crywolf (0x576353c) y el sonido que pide.
    Kernel::HudState hud{};
    std::uint8_t crywolfSandOverlay = 0;
    // Lo que escribe la interfaz de Crywolf (0x4365c0): las cuentas del cartel, la barra, el reloj y el consejo.
    Kernel::CrywolfInterface crywolfInterface{};
    std::optional<Audio::SoundId> sound{};
    // 0x77f36d4 + 0x12 · 4 y 0x78abb38: el latch de Alt y los nombres del piso, que invierte el hover (0x5bbbc0).
    std::int32_t altLatch = 0;
    std::uint8_t altNames = 0;
    // Lo que el dibujo de las ventanas viejas escribe (0x490390 y 0x637680): +0x40, los botones de una caja arrastrada
    // y el renglón y el parpadeo de la lista de ventanas.
    Ui::OldWindowManager oldWindows{};
};
} // namespace Client::Scenes
