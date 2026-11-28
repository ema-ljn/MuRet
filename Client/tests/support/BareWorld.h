#pragma once

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Models/Cloth.h"
#include "Scenes/ActorEffectHooks.h"
#include "Scenes/LoginModels.h"
#include "Scenes/SelectionState.h"
#include "Kernel/WorldState.h"
#include "Session/ModuleStates.h"
#include "Session/WorldEntry.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Tables/PlayerModels.h"
#include "Camera/GameCamera.h"

#include "support/ItemSetFiles.h"
#include "support/LetterMeasure.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace Client::Tests
{
// A world without Data, with the player's skeleton and a measure for the chat, for what takes the WorldEntryResources.
// On the heap (std::make_unique): a WorldState on the stack overflows MSVC's Debug stack.
struct BareWorld
{
    BareWorld()
    {
        models.Set(Tables::PlayerSkeletonModel, {}, PlayerActionsSkeleton(), {});
    }

    // Map 0, flat and in zeros, and item tables without items: a warp needs the map (0x5f93c0), a regen the tables too
    // (0x40c600).
    void ReadyForTransitions()
    {
        world.Runtime().map = 0;
        states.terrain.current.attributes = std::make_unique<Data::Terrain::TerrainAttributes>();
        states.terrain.current.heights = std::make_unique<Data::Terrain::TerrainHeights>();
        items = std::make_unique<Tables::ItemTable>(ItemsMarking({}));
        itemSets = std::make_unique<Tables::ItemSetTables>(
            ItemSetTablesFrom(std::vector<std::byte>(Tables::ItemSetTypeTableSize),
                              std::vector<std::byte>(Tables::ItemSetOptionTableSize)));
        resources.items = items.get();
        resources.itemSetTables = itemSets.get();
    }

    Kernel::WorldState world;
    Session::ModuleStates states;
    Scenes::SelectionState selection{world};
    Camera::GameCamera camera;
    Camera::GameCameraInput input;
    Models::ClothWorld cloth;
    Scenes::SingleModelLibrary models;
    LetterMeasure measure;
    std::unique_ptr<Tables::ItemTable> items;
    std::unique_ptr<Tables::ItemSetTables> itemSets;
    RecordingSink sink;
    Core::Log log{sink, Core::LogLevel::Debug};
    Scenes::ActorEffectHooks effectHooks;
    Scenes::WorldEntryResources resources{.world = world,
                                          .selection = selection,
                                          .dataRoot = {},
                                          .cloth = cloth,
                                          .camera = camera,
                                          .cameraInput = input,
                                          .models = models,
                                          .log = log,
                                          .textMeasure = &measure,
                                          .states = &states,
                                          .effectHooks = &effectHooks};
};
} // namespace Client::Tests
