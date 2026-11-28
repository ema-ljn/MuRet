#pragma once

#include "Crt/CrtRandom.h"
#include "Models/Cloth.h"
#include "Kernel/DrawTailState.h"
#include "Actors/ActorPickingBox.h"
#include "Actors/Drawing/GameItemRenderState.h"
#include "Kernel/WorldRuntime.h"
#include "Ui/OldWindows.h"
#include "Kernel/KalimaWater.h"
#include "Effects/ModelEffects.h"
#include "Kernel/WorldObject.h"
#include "Particles/Particles.h"
#include "Terrain/TerrainLighting.h"
#include "Audio/SoundId.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Client::Scenes
{
// One heap owner per intermediate draw: all its services lend these same copies.
struct GameSceneFrameProjection
{
    Kernel::GameDrawTailState tail{};
    Terrain::TerrainLighting lighting{};
    Kernel::ObjectModelStepState modelState{};
    Actors::Drawing::GameItemRenderState items{};
    Models::ClothWorld cloth{.random = Crt::CrtRandom{}};
    Effects::ModelEffects effects{};
    Particles::Particles particles{};
    std::vector<Kernel::WorldObject> objects{};
    std::vector<Actors::ActorPickingBox> actorPickingBoxes{};
    std::unique_ptr<Kernel::KalimaWater> kalima{};
    std::uint8_t heroXDepth = 0;
    std::uint8_t heroYDepth = 0;
    std::uint8_t ownRecordDepth = 0;
    // 0x7d325c8: the camera shake that 0x405e00 writes in the draw.
    float cameraShake = 0.0f;
    // What the HUD (0x5a7320) writes that lasts: its globals, the Crywolf smoke (0x576353c) and the sound it asks for.
    Kernel::HudState hud{};
    std::uint8_t crywolfSandOverlay = 0;
    // Written by the Crywolf interface (0x4365c0): the banner's counts, the bar, the clock and the tip.
    Kernel::CrywolfInterface crywolfInterface{};
    std::optional<Audio::SoundId> sound{};
    // 0x77f36d4 + 0x12 · 4 and 0x78abb38: the Alt latch and the floor names, which the hover (0x5bbbc0) toggles.
    std::int32_t altLatch = 0;
    std::uint8_t altNames = 0;
    // What the old windows' draw (0x490390 and 0x637680) writes: +0x40, the buttons of a dragged box and the window
    // list's row and blink.
    Ui::OldWindowManager oldWindows{};
};
} // namespace Client::Scenes
