#pragma once

#include "Assets/FaunaModels.h"

// What the assets own of the original's globals.
namespace Client::Assets
{
struct AssetsState
{
    // 0x5769ad8: the models a creation loads (0x612d90, 0x612270), which no map change releases.
    FaunaModels fauna;
};
} // namespace Client::Assets
