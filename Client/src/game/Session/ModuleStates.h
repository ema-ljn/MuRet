#pragma once

#include "Actors/ActorsState.h"
#include "Assets/AssetsState.h"
#include "Camera/CameraState.h"
#include "Monsters/MonstersState.h"
#include "Terrain/TerrainState.h"

// What each module owns of the original's globals, one member per module, by value (ADR 0029). App::GameClient owns it
// beside Kernel::WorldState, and Session/Route.h hands a routed handler the member it names.
namespace Client::Session
{
struct ModuleStates
{
    Actors::ActorsState actors;
    Assets::AssetsState assets;
    Camera::CameraState camera;
    Monsters::MonstersState monsters;
    Terrain::TerrainState terrain;
};
} // namespace Client::Session
