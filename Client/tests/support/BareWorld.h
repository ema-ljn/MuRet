#pragma once

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Models/Cloth.h"
#include "Scenes/LoginModels.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldState.h"
#include "Session/WorldEntry.h"
#include "Tables/PlayerModels.h"
#include "World/GameCamera.h"

#include "support/PlayerActors.h"
#include "support/RecordingSink.h"

namespace Client::Tests
{
// A world without Data, with the player's skeleton, for what takes the WorldEntryResources. On the heap
// (std::make_unique): a WorldState on the stack overflows MSVC's Debug stack.
struct BareWorld
{
    BareWorld()
    {
        models.Set(Tables::PlayerSkeletonModel, {}, PlayerActionsSkeleton(), {});
    }

    Scenes::WorldState world;
    Scenes::SelectionState selection{world};
    World::GameCamera camera;
    World::GameCameraInput input;
    Models::ClothWorld cloth;
    Scenes::SingleModelLibrary models;
    RecordingSink sink;
    Core::Log log{sink, Core::LogLevel::Debug};
    Scenes::WorldEntryResources resources{.world = world,
                                          .selection = selection,
                                          .dataRoot = {},
                                          .cloth = cloth,
                                          .camera = camera,
                                          .cameraInput = input,
                                          .models = models,
                                          .log = log};
};
} // namespace Client::Tests
